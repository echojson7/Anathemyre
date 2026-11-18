#!/usr/bin/env python3
"""Человек MakeHuman (CC0) → наш .mesh с весами костей и морфами и .skel (docs/mesh.md, docs/people.md).

    tools/humanconv/fetch.sh            (скачать исходники и пересобрать; зовёт make meshes)
    python3 -I tools/humanconv/humanconv.py <папка исходников> <data> <meshpack>

Папка исходников (fetch.sh): base.obj, default.mhskel, default_weights.mhw, rig.game_engine.json, weights.game_engine.json, rig.mixamo.json,
weights.mixamo.json (MPFB2, CC0) и targets.npz (морфы из пакета makehuman на PyPI, CC0).

Что делает: морфы сложения → поза покоя MakeHuman (A-поза) → рост 1.80 м, низ на y = 0 → оси игры (x восток,
y вверх, z север; человек смотрит на север) → материалы граней (кожа, бельё, глаза) → веса двух скелетов на
≤ 4 кости → сдвиги морфов → сырой .mesh одним LOD → meshpack (5 LOD, кластеры, вершины по LOD префиксами).
Скелеты — data/people/<тело>.<скелет>.skel. Только numpy. Байты зависят только от исходников, версии numpy
не должны их менять: всё считается в float64 и округляется перед записью (CI сравнивает с git).
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I не кладёт папку скрипта в путь
import decimate  # noqa: E402

HEIGHT = 1.80  # все тела этого роста, в игре — масштаб экземпляра
# LOD 0..4 — треугольников тела: упрощение беты (decimate.py), лицо дороже; LOD k+1 — продолжение того же
# упрощения после LOD k, meshpack берёт их готовыми (кусок LODI). У беты LOD 0 — 16 000 треугольников тела
LOD0_TRIS = 16000
LOD_TRIS = (9000, 2400, 800, 500)
SKELETONS = ("game_engine", "mixamo")  # оба пишутся, выбор — этап 8 (docs/people.md)
# Материалы граней. Бельё у мужчин и женщин разное на одной сетке: грань знает, чем она будет у мужчины и чем у
# женщины (PAIR), вид skin выбирает по биту женщины в зерне экземпляра (src/view/appear.h, materials/skin.glsl)
MATERIALS = ("human_skin", "human_linen", "human_band", "human_eye", "human_iris", "human_bra", "human_bra_band",
             "human_boxer", "human_boxer_band", "human_lb", "human_bl", "human_lip", "human_areola", "human_nipple")
SKIN, LINEN, BAND, EYE, IRIS = range(5)
# (у мужчины, у женщины) → материал: S кожа, L холст, B резинка, P губы, A ореол, T сосок
PAIR = {("S", "S"): "human_skin", ("L", "L"): "human_linen", ("B", "B"): "human_band", ("S", "L"): "human_bra",
        ("S", "B"): "human_bra_band", ("L", "S"): "human_boxer", ("B", "S"): "human_boxer_band", ("L", "B"): "human_lb",
        ("B", "L"): "human_bl", ("P", "P"): "human_lip", ("A", "L"): "human_areola", ("T", "L"): "human_nipple"}
CUP = "breast/female-young-averagemuscle-averageweight-maxcup-averagefirmness"  # область груди — по этой цели
LIPS = ("mouth/mouth-upperlip-volume-incr", "mouth/mouth-lowerlip-volume-incr")  # губы — где их цели двигают вершины

# Тело стенда: мужчина средних лет, сложение чуть крепче среднего (макро-морфы MakeHuman)
_HEAD_ARMS = {"head/head-scale-vert-decr": 0.4, "head/head-scale-horiz-decr": 0.25, "head/head-scale-depth-decr": 0.3,
              "head/head-back-scale-depth-decr": 0.5,
              "measure/measure-upperarm-length-incr": 0.6, "measure/measure-lowerarm-length-decr": 0.4}
# Стопы чуть меньше, как в бете: родные ступни MakeHuman бросаются в глаза
FEET = {"armslegs/%s-foot-scale-%s-decr" % (sd, a): w for sd in "lr" for a, w in (("depth", 0.38), ("horiz", 0.3))}
# Голова мужчины беты (mh_male, HEAD_SMALLER): у MakeHuman голова крупная и затылок раздут за ухом
_HEAD_BETA = {"head/head-scale-vert-decr": 0.5, "head/head-scale-horiz-decr": 0.3, "head/head-scale-depth-decr": 0.4,
              "head/head-back-scale-depth-decr": 0.5}
PEOPLE = [
    # Мужчина беты 1 в 1 (mh_male, 18.11): мышцы 0.8, голова, руки, грудные, талия, стопы
    dict(name="human_m", gender=1.0, age=0.0, muscle=0.8, weight=0.5,
         mods=dict(_HEAD_BETA, **{k: w for k, w in _HEAD_ARMS.items() if k.startswith("measure/")}, **FEET,
                   **{"torso/torso-muscle-pectoral-incr": 0.5, "measure/measure-waist-circ-incr": 0.3})),
]
# Скелет бинда — от тела до правок 2026-11-17 (стопы, грудь): суставы те же, .skel, клипы и дорожки шага мира прежние
RIG = dict(name="human_m", gender=1.0, age=0.0, muscle=0.6, weight=0.5, mods=dict(_HEAD_ARMS))
# Морфы внешности (docs/people.md, «Тело»): сдвиги вершин от тела выше, вид смешивает их весами человека
# (скиннинг). Морф — сумма «доля × тело с такими-то параметрами»; каждое тело перед вычитанием приводится к
# росту HEIGHT, поэтому морф роста не меняет (рост — масштаб экземпляра), а суставы сдвигает (кусок MRPJ)
BASE = PEOPLE[0]


def P(**kw):
    # Параметры тела: BASE с заменами; mods — добавка к правкам BASE, replace_mods — вместо них
    q = dict(BASE)
    mods = dict(kw.pop("replace_mods")) if "replace_mods" in kw else dict(BASE.get("mods", {}))
    for k, x in kw.pop("mods", {}).items():
        mods[k] = mods.get(k, 0.0) + x
    q.update(kw)
    q["mods"] = mods
    return q


def one(**kw):
    return ((1.0, P(**kw)), (-1.0, BASE))


def pair(incr, decr):
    # Морф в обе стороны: вес +1 — тело incr, −1 — тело decr
    return ((0.5, P(mods=incr)), (-0.5, P(mods=decr)))


def both(names, w=1.0):
    # Правки MakeHuman парой incr / decr; имена с «l-» — на обе стороны
    out = ({}, {})
    for n in names:
        for side in (("l-", "r-") if n.startswith("l-") else ("",)):
            m = side + n[2:] if side else n
            out[0][m + "-incr"], out[1][m + "-decr"] = w, w
    return pair(*out)


# Женщина (числа беты, docs/legacy.md): талия уже бёдер без «галифе», плечи уже мужских, колени и икры суше
FEMALE = dict(gender=0.0, muscle=0.5, weight=0.55, cup=0.65, firm=0.7, replace_mods={
    "head/head-scale-vert-decr": 0.3, "head/head-scale-horiz-decr": 0.35, "head/head-scale-depth-decr": 0.3,
    "head/head-back-scale-depth-decr": 0.5, "bodyshapes/bodyshapes-elvs-fem-neat-hourglass": 0.5,
    "hip/hip-scale-horiz-decr": 0.4, "measure/measure-hips-circ-decr": 0.15, "measure/measure-thigh-circ-decr": 0.15,
    "armslegs/l-upperleg-fat-decr": 0.2, "armslegs/r-upperleg-fat-decr": 0.2, "measure/measure-knee-circ-decr": 0.4,
    "measure/measure-calf-circ-decr": 0.3, "buttocks/buttocks-volume-decr": 1.0,
    "measure/measure-shoulder-dist-decr": 0.6, "measure/measure-upperarm-length-incr": 0.5,
    "measure/measure-lowerarm-length-incr": 0.2, "breast/breast-dist-decr": 0.15, "pelvis/bulge-decr": 1.0, **FEET})


def cross(**kw):
    # Поправка к сумме двух морфов: «женщина и полная» — не «женщина» плюс «полный мужчина»
    f = dict(FEMALE)
    f.update(kw)
    return ((1.0, P(**f)), (-1.0, P(**FEMALE)), (-1.0, P(**kw)), (1.0, BASE))


def kin(name):
    r = {"african": 0.0, "asian": 0.0, "caucasian": 0.0}
    r[name] = 1.0
    return one(race=r)


_eyes = lambda d: {"eyes/%s-%s" % (s, k): w for s in "lr" for k, w in d.items()}
MORPHS = (
    # Сложение
    ("female", ((1.0, P(**FEMALE)), (-1.0, BASE))),
    ("full", one(weight=1.0)),
    ("thin", one(weight=0.0)),
    ("strong", one(muscle=1.0)),
    ("old", one(age=1.0)),
    ("belly", one(mods={"stomach/stomach-pregnant-incr": 0.55, "stomach/stomach-tone-decr": 0.6,
                        "measure/measure-waist-circ-incr": 0.5})),
    ("female_full", cross(weight=1.0)),
    ("female_thin", cross(weight=0.0)),
    ("female_strong", cross(muscle=1.0)),
    # Пропорции: в обе стороны, суставы идут за ними
    ("shoulders", both(["measure/measure-shoulder-dist"])),
    ("hips", both(["hip/hip-scale-horiz"])),
    ("legs", both(["measure/measure-upperleg-height", "measure/measure-lowerleg-height"])),
    ("torso", both(["torso/torso-scale-vert"])),
    ("arms", both(["measure/measure-upperarm-length", "measure/measure-lowerarm-length"])),
    ("neck", both(["measure/measure-neck-height"])),
    ("head", both(["head/head-scale-vert", "head/head-scale-horiz", "head/head-scale-depth"], 0.6)),
    # Род: черты лица и черепа трёх наборов MakeHuman (третий — оба веса по −0.5)
    ("kin_a", kin("african")),
    ("kin_b", kin("asian")),
    # Лицо — местные правки
    ("nose", both(["nose/nose-volume", "nose/nose-scale-horiz", "nose/nose-scale-depth"], 0.6)),
    ("nose_hump", pair({"nose/nose-hump-incr": 0.9, "nose/nose-point-down": 0.5},
                       {"nose/nose-hump-decr": 0.9, "nose/nose-point-up": 0.5})),
    ("jaw", pair({"chin/chin-width-incr": 0.7, "chin/chin-bones-incr": 0.6, "head/head-square": 0.6},
                 {"chin/chin-width-decr": 0.7, "chin/chin-bones-decr": 0.6, "head/head-oval": 0.6})),
    ("chin", pair({"chin/chin-prominent-incr": 0.8, "chin/chin-height-incr": 0.6},
                  {"chin/chin-prominent-decr": 0.8, "chin/chin-height-decr": 0.6})),
    ("lips", pair({"mouth/mouth-upperlip-volume-incr": 0.8, "mouth/mouth-lowerlip-volume-incr": 0.8,
                   "mouth/mouth-scale-horiz-incr": 0.25},
                  {"mouth/mouth-upperlip-volume-decr": 0.8, "mouth/mouth-lowerlip-volume-decr": 0.8,
                   "mouth/mouth-scale-horiz-decr": 0.25})),
    ("eyes", pair(_eyes({"eye-height2-decr": 0.7, "eye-height1-decr": 0.3, "eye-corner2-up": 0.6}),
                  _eyes({"eye-height2-incr": 0.7, "eye-height1-incr": 0.3, "eye-corner2-down": 0.6}))),
    ("cheeks", both(["cheek/l-cheek-bones", "cheek/l-cheek-volume"], 0.8)),
    ("brow", pair({"eyebrows/eyebrows-trans-forward": 0.8, "eyebrows/eyebrows-trans-down": 0.5,
                   "forehead/forehead-nubian-incr": 0.4},
                  {"eyebrows/eyebrows-trans-backward": 0.8, "eyebrows/eyebrows-trans-up": 0.5,
                   "forehead/forehead-nubian-decr": 0.4})),
    ("ears", both(["ears/l-ear-wing", "ears/l-ear-scale"], 0.7)),
)

# Блендер → игра: исходники MPFB в осях Блендера (x, −z_obj, y_obj). Игра — x восток, y вверх, z север, и это
# левая тройка (вид: «вверх × вперёд» — восток справа): MakeHuman (правая тройка, лицо к +z) зеркалится по x,
# иначе левая рука встала бы справа. Кадр кости в игре — M · F_blender · D: ось Y кости та же, ось X —
# обратная (D), чтобы кадр остался поворотом. Местный поворот кости из Блендера в игре — D · R · D
OBJ_TO_GAME = np.array([-1.0, 1.0, 1.0])
M_B2G = np.array([[-1.0, 0, 0], [0, 0, 1.0], [0, -1.0, 0]])
D_LOCAL = np.diag([-1.0, 1.0, 1.0])


def load_obj(path):
    v, faces, g = [], {}, None
    for line in open(path):
        if line.startswith("v "):
            v.append([float(x) for x in line.split()[1:4]])
        elif line.startswith("g "):
            g = line.split()[1]
        elif line.startswith("f "):
            faces.setdefault(g, []).append([int(t.split("/")[0]) - 1 for t in line.split()[1:]])
    return np.array(v, dtype=np.float64), {k: np.array(f) for k, f in faces.items()}


def three(x):
    # Макро-морф: вес соседних категорий (min, average, max) по линейной интерполяции
    lo, hi = max(0.0, 1 - 2 * x), max(0.0, 2 * x - 1)
    return {"min": lo, "average": 1 - lo - hi, "max": hi}


def morph(base, Z, p):
    v = base.copy()

    def add(name, w):
        if w <= 1e-6:
            return
        i = Z["targets/" + name + ".index"]
        if len(i):
            v[i] += w * Z["targets/" + name + ".vector"].astype(np.float64) * 1e-3
    gen = {"female": 1 - p["gender"], "male": p["gender"]}
    age = {"young": 1 - p["age"], "old": p["age"]}
    race = p.get("race", {"african": 1 / 3, "asian": 1 / 3, "caucasian": 1 / 3})
    for gn, gw in gen.items():
        for an, aw in age.items():
            for mn, mw in three(p["muscle"]).items():
                for wn, ww in three(p["weight"]).items():
                    add("macrodetails/universal-%s-%s-%smuscle-%sweight" % (gn, an, mn, wn), gw * aw * mw * ww)
            for rn, rw in race.items():
                add("macrodetails/%s-%s-%s" % (rn, gn, an), gw * aw * rw)
    # Размер и упругость груди (у MakeHuman только для женщин)
    gw = 1 - p["gender"]
    for mn, mw in three(p["muscle"]).items():
        for wn, ww in three(p["weight"]).items():
            for cn, cw in three(p.get("cup", 0.5)).items():
                for fn, fw in three(p.get("firm", 0.5)).items():
                    if cn != "average" or fn != "average":
                        add("breast/female-young-%smuscle-%sweight-%scup-%sfirmness" % (mn, wn, cn, fn),
                            gw * mw * ww * cw * fw)
    for name, w in sorted(p.get("mods", {}).items()):
        if "targets/" + name + ".index" not in Z:
            sys.exit("humanconv: нет правки MakeHuman %s" % name)
        add(name, w)
    return v


def grid(x, bits):
    # Округление к кратному 2^-bits: такое число точно и во float64, и во float32 (|x| < 2^(24 - bits)), поэтому
    # байты не зависят от последних битов вычислений разных версий numpy
    return np.rint(np.asarray(x, dtype=np.float64) * float(1 << bits)) / float(1 << bits) + 0.0  # без −0


def tri(q):
    # Четырёхугольники → треугольники (0 1 2, 0 2 3)
    return np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]])


def normals(v, t):
    n = np.zeros_like(v)
    fn = np.cross(v[t[:, 1]] - v[t[:, 0]], v[t[:, 2]] - v[t[:, 0]])
    for k in range(3):
        np.add.at(n, t[:, k], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)


def joint(v, groups, spec):
    # Голова или хвост кости MPFB: середина кубика сустава или среднее заданных вершин
    if spec["strategy"] == "CUBE":
        return v[np.unique(groups[spec["cube_name"]])].mean(0)
    if spec["strategy"] == "MEAN":
        return v[np.array(spec["vertex_indices"])].mean(0)
    sys.exit("humanconv: неизвестный способ сустава %s" % spec["strategy"])


def vec_roll_to_mat3(nor, roll):
    # Кадр кости Блендера (vec_roll_to_mat3_normalized в armature.cc): столбец Y — вдоль кости, потом крен
    x, y, z = nor
    theta, theta_alt = 1.0 + y, x * x + z * z
    b = np.eye(3)
    if theta > 6.1e-3 or theta_alt > 2.5e-4 ** 2:
        if theta <= 6.1e-3:
            theta = theta_alt * 0.5 + theta_alt * theta_alt * 0.125
        # Матрица по столбцам, как в Блендере: b[:, j] — столбец j
        b = np.array([[1 - x * x / theta, x, -x * z / theta],
                      [-x, y, -z],
                      [-x * z / theta, z, 1 - z * z / theta]])
    else:
        b = np.diag([-1.0, -1.0, 1.0])
    k = nor
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    r = np.eye(3) + np.sin(roll) * K + (1 - np.cos(roll)) * K @ K
    return r @ b


def quat(m):
    # Поворот 3×3 → кватернион (x, y, z, w), w ≥ 0
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    q = np.array(q)
    q /= np.linalg.norm(q)
    return -q if q[3] < 0 else q


def bone_order(bones):
    # Родители раньше детей: обход в глубину от корня, дети по имени (порядок не зависит от файла)
    kids = {}
    for n in sorted(bones):
        kids.setdefault(bones[n]["parent"], []).append(n)
    out, stack = [], list(reversed(kids.get("", [])))
    while stack:
        n = stack.pop()
        out.append(n)
        stack += list(reversed(kids.get(n, [])))
    if len(out) != len(bones):
        sys.exit("humanconv: у скелета кости без пути к корню")
    return out


def skeleton(v_obj, groups, rig, to_game):
    # Кости в игре: имя, родитель, длина, бинд-поза (положение, поворот), обратная бинд-матрица 3×4
    order = bone_order(rig)
    out = []
    for n in order:
        b = rig[n]
        h, t = joint(v_obj, groups, b["head"]), joint(v_obj, groups, b["tail"])
        d = t - h
        nb = np.array([d[0], -d[2], d[1]])  # в осях Блендера
        nb /= np.linalg.norm(nb)
        F = M_B2G @ vec_roll_to_mat3(nb, float(b["roll"])) @ D_LOCAL
        hg, tg = to_game(h), to_game(t)
        inv = np.concatenate([F.T, (-F.T @ hg)[:, None]], 1)
        parent = order.index(b["parent"]) if b["parent"] else -1
        out.append(dict(name=n, parent=parent, length=float(np.linalg.norm(tg - hg)), pos=hg, rot=F, q=quat(F), inv=inv))
    return out


# ---- Поза привязки — стойка тел беты (stage0/test-room, tools/humanconv/humanconv.py: repose, fingers)
# Слово пользователя 18.11: «облик старой модели, а технически — v2». Тело стоит, как стояло у беты: руки вниз,
# ноги наполовину к отвесу, кисти в свободном хвате. Стойку даёт скелет MakeHuman по умолчанию (163 кости,
# дельтовидная кость на полповорота плеча, свои веса) — сгибы плеч, паха и ягодиц беты. Скелеты игры (.skel) —
# прежние, в A-позе (клипы и шаг мира не меняются); стойка беты — только обратные бинд-матрицы (rest_skeleton)
# и вершины сеток людей (тело, одежда, волосы)


def mh_load(srcdir):
    # Скелет MakeHuman по умолчанию: суставы — средние вершин, веса — списки (вершина, вес) на кость
    sk = json.load(open(os.path.join(srcdir, "default.mhskel")))
    W = json.load(open(os.path.join(srcdir, "default_weights.mhw")))["weights"]
    joints = {k: np.array(ix, np.int64) for k, ix in sorted(sk["joints"].items())}
    ws = []
    for n in sorted(W):
        a = np.array(W[n], np.float64)
        ws.append((n, a[:, 0].astype(np.int64), a[:, 1]))
    return dict(joints=joints, weights=ws)


def rot_to(a, b):
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    c = np.cross(a, b)
    s = np.linalg.norm(c)
    d = float(np.dot(a, b))
    if s < 1e-9:
        return np.eye(3)
    k = c / s
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + s * K + (1 - d) * K @ K


def rot_axis(k, a):
    k = k / np.linalg.norm(k)
    K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + np.sin(a) * K + (1 - np.cos(a)) * K @ K


def half(R):
    # Половина поворота
    ang = np.arccos(np.clip((np.trace(R) - 1) / 2, -1, 1))
    if ang < 1e-6:
        return np.eye(3)
    ax = np.array([R[2, 1] - R[1, 2], R[0, 2] - R[2, 0], R[1, 0] - R[0, 1]]) / (2 * np.sin(ang))
    return rot_axis(ax, ang / 2)


# Свободный хват беты: сгиб фаланг, градусы (большой палец, затем указательный..мизинец)
CURL = ((10, 20, 20), (40, 50, 30), (45, 55, 30), (50, 55, 30), (55, 55, 30))


def rest_xf(J):
    """Стойка беты в осях MakeHuman (до зеркала): кость MH → (R, точка до, точка после), x → R (x − до) + после.
    J — суставы скелета MH этого тела. Цифры направлений — беты (руки по костям её игры, ноги наполовину к отвесу)"""
    xf = {}
    for sd, sg in (("L", 1), ("R", -1)):
        j = lambda n, e="head": J["%s.%s____%s" % (n, sd, e)]
        S0, E0, W0 = j("upperarm01"), j("lowerarm01"), j("wrist")
        R_ua = rot_to(E0 - S0, np.array([0.03 * sg, -0.28, 0.0]))
        E1 = S0 + R_ua @ (E0 - S0)
        R_fa = rot_to(R_ua @ (W0 - E0), np.array([0.01 * sg, -0.21, 0.01])) @ R_ua
        W1 = E1 + R_fa @ (W0 - E0)
        H0, K0, A0 = j("upperleg01"), j("lowerleg01"), j("foot")
        # Ноги сводятся наполовину: отвесно бёдра у паха входят друг в друга
        R_th = half(rot_to(K0 - H0, np.array([0.0, -1.0, 0.0])))
        K1 = H0 + R_th @ (K0 - H0)
        R_sh = rot_to(R_th @ (A0 - K0), R_th @ (K0 - H0)) @ R_th
        A1 = K1 + R_sh @ (A0 - K0)
        # Пальцы к ладони; ладонь после стойки смотрит к бедру
        lat = j("finger5-1") - j("finger2-1")
        curl = {}
        for f, ang in enumerate(CURL, 1):
            d = j("finger%d-3" % f, "tail") - j("finger%d-1" % f)
            d = d / np.linalg.norm(d)
            nrm = np.cross(d, lat)
            nrm /= np.linalg.norm(nrm)
            if (R_fa @ nrm)[0] * sg > 0:
                nrm = -nrm
            if f == 1:  # большой — поперёк ладони, к указательному
                nrm = j("finger2-1") - j("finger1-2")
                nrm = nrm - d * (nrm @ d)
                nrm /= np.linalg.norm(nrm)
            k = np.cross(d, nrm)
            R, t = np.eye(3), np.zeros(3)
            for s, a in enumerate(ang, 1):
                Rs = rot_axis(k, np.radians(a))
                h = R @ j("finger%d-%d" % (f, s)) + t
                R, t = Rs @ R, Rs @ (t - h) + h
                curl["finger%d-%d" % (f, s)] = (R, t)
        hand_t = W1 - R_fa @ W0
        xf[sd] = dict(shoulder=(half(R_ua), S0, S0), upperarm=(R_ua, S0, S0), lowerarm=(R_fa, E0, E1),
                      hand=(R_fa, W0, W1), upperleg=(R_th, H0, H0), lowerleg=(R_sh, K0, K1), foot=(np.eye(3), A0, A1),
                      curl={b: (R_fa @ R, np.zeros(3), R_fa @ t + hand_t) for b, (R, t) in curl.items()})
    return xf


def rest_of(xf, n):
    # Преобразование стойки для кости MH n (None — кость не движется)
    if n[-2:] not in (".L", ".R"):
        return None
    x, b = xf[n[-1]], n[:-2]
    if b in x["curl"]:
        return x["curl"][b]
    for pre, key in (("shoulder", "shoulder"), ("upperarm", "upperarm"), ("lowerarm", "lowerarm"),
                     (("wrist", "finger", "metacarpal"), "hand"), ("upperleg", "upperleg"), ("lowerleg", "lowerleg"),
                     (("foot", "toe"), "foot")):
        if b.startswith(pre):
            return x[key]
    return None


def rest(vo, mh):
    """Вершины тела в стойке беты (оси MakeHuman): смесь преобразований костей MH по их весам"""
    J = {k: vo[ix].mean(0) for k, ix in mh["joints"].items()}
    xf = rest_xf(J)
    out = np.zeros_like(vo)
    wsum = np.zeros(len(vo))
    for n, i, w in mh["weights"]:
        t = rest_of(xf, n)
        out[i] += w[:, None] * (vo[i] if t is None else (vo[i] - t[1]) @ t[0].T + t[2])
        wsum[i] += w
    free = wsum < 1e-6
    out[~free] /= wsum[~free, None]
    out[free] = vo[free]
    return out


def rest_frames(vo, mh):
    # Повороты (Ri) и точки до и после (на кость MH) — для поворота нормалей и для скелета
    J = {k: vo[ix].mean(0) for k, ix in mh["joints"].items()}
    return rest_xf(J)


def rest_skeleton(bones, r_obj, groups, rig, wlist, mh, to_game):
    """Обратные бинд-матрицы скелета игры в стойке беты. Кость игры поворачивается, как кость MH, с которой у
    неё больше всего общего веса (без дельтовидной — она на полповорота); головы — прямой кинематикой от A-позы
    (как pose_world), поэтому вид, повернув кости к этим поворотам, получит палитру ровно единичную"""
    xf = rest_frames(r_obj, mh)
    nv = len(r_obj)
    mh_names = [n for n, _, _ in mh["weights"] if not n.startswith("shoulder")]
    MW = np.zeros((nv, len(mh_names)))
    for k, n in enumerate(mh_names):
        _, i, w = next(x for x in mh["weights"] if x[0] == n)
        np.add.at(MW[:, k], i, w)
    Dm = np.diag([-1.0, 1.0, 1.0])
    out = []
    Rw, Pw = [], []
    for b in bones:
        lst = wlist.get(b["name"], [])
        Rg = np.eye(3)
        if lst:
            a = np.array(lst, np.float64)
            ov = (MW[a[:, 0].astype(np.int64)] * a[:, 1:2]).sum(0)
            t = rest_of(xf, mh_names[int(np.argmax(ov))])
            if t is not None:
                Rg = Dm @ t[0] @ Dm  # зеркало осей MakeHuman → игра
        R = Rg @ b["rot"]
        if b["parent"] < 0:
            p = b["pos"]
        else:
            pb = bones[b["parent"]]
            p = Pw[b["parent"]] + Rw[b["parent"]] @ (pb["rot"].T @ (b["pos"] - pb["pos"]))
        Rw.append(R)
        Pw.append(p)
        out.append(dict(b, inv=np.concatenate([R.T, (-R.T @ p)[:, None]], 1)))
    return out


def skin_dense(W, order, nv):
    # Плотные веса (вершины × кости) из списков (вершина, вес) на кость
    acc = np.zeros((nv, len(order)))
    for k, n in enumerate(order):
        lst = W.get(n, [])
        if lst:
            a = np.array(lst)
            np.add.at(acc[:, k], a[:, 0].astype(int), a[:, 1])
    return acc


def skin_weights(W, order, nv):
    # До 4 костей на вершину: самые тяжёлые (при равенстве — меньший номер), сумма весов ровно 255
    return skin_quant(skin_dense(W, order, nv))


def skin_quant(acc):
    # Плотные веса (вершины × кости) → 4 кости u8 и веса u8 с суммой 255 (одежда — смесью весов вершин тела)
    top = np.argsort(-acc, axis=1, kind="stable")[:, :4]
    w = np.take_along_axis(acc, top, 1)
    s = w.sum(1, keepdims=True)
    if (s <= 1e-9).any():
        sys.exit("humanconv: вершина без весов")
    w = w / s
    w[w < 0.01] = 0
    w = w / w.sum(1, keepdims=True)
    q = np.floor(w * 255 + 1e-9).astype(np.int64)
    rest = 255 - q.sum(1)
    frac = w * 255 - q
    for r in range(4):  # остаток — по одному тем, у кого больше дробная часть
        need = rest > 0
        if not need.any():
            break
        fm = np.where(q < 255, frac, -1.0)
        j = np.argmax(fm, axis=1)
        idx = np.where(need)[0]
        q[idx, j[idx]] += 1
        frac[idx, j[idx]] = -1.0
        rest[idx] -= 1
    top = np.where(q > 0, top, top[:, :1])  # пустые места — первая кость с весом 0
    return top.astype(np.uint8), q.astype(np.uint8)


def oct_encode(n):
    # Единичная нормаль → октаэдр с осью y, два snorm16 (как oct_encode в tools/meshgen.py)
    s = np.abs(n).sum(1)
    px, pz = n[:, 0] / s, n[:, 2] / s
    neg = n[:, 1] < 0
    fx = (1 - np.abs(pz)) * np.where(px >= 0, 1.0, -1.0)
    fz = (1 - np.abs(px)) * np.where(pz >= 0, 1.0, -1.0)
    px, pz = np.where(neg, fx, px), np.where(neg, fz, pz)
    q = lambda a: (np.clip(np.round(a * 32767), -32767, 32767).astype(np.int64) & 0xFFFF).astype(np.uint16)
    return q(px), q(pz)


def chunk(tag, body):
    return tag + struct.pack("<IQ", 1, len(body)) + body


def name_bytes(s):
    b = s.encode()
    return struct.pack("<B", len(b)) + b


def ray_hit(V, T, o, d):
    # Первое пересечение лучей o + d·t с треугольниками V[T] (Мёллер — Трумбор); нет пересечения — nan
    A, B, C = V[T[:, 0]], V[T[:, 1]], V[T[:, 2]]
    e1, e2 = B - A, C - A
    out = np.full((len(o), 3), np.nan)
    for k in range(len(o)):
        pv = np.cross(d[k], e2)
        det = (e1 * pv).sum(1)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = o[k] - A
        u = (tv * pv).sum(1) * inv
        qv = np.cross(tv, e1)
        w = (qv @ d[k]) * inv
        t = (e2 * qv).sum(1) * inv
        t = np.where(ok & (u >= 0) & (w >= 0) & (u + w <= 1) & (t > 0), t, np.inf)
        if np.isfinite(t.min()):
            out[k] = o[k] + d[k] * t.min()
    return out


def seg_side(P, N, Q):
    # До ломаной Q от точек P (нормали N): расстояние и сдвиг вбок по коже (со знаком — по какую сторону линии)
    best, side = np.full(len(P), np.inf), np.zeros(len(P))
    for a, b in zip(Q[:-1], Q[1:]):
        ab = b - a
        t = np.clip(((P - a) @ ab) / max(float(ab @ ab), 1e-12), 0.0, 1.0)
        c = a + t[:, None] * ab
        d = np.linalg.norm(P - c, axis=1)
        sd = np.cross(ab / np.linalg.norm(ab), N)
        near = d < best
        best = np.where(near, d, best)
        side = np.where(near, ((P - c) * sd).sum(1), side)
    return best, side


def nipples(v, Z):
    # Соски: вершина, которую цель груди MakeHuman выдвигает дальше всех, по обе стороны (по топологии)
    i = Z["targets/" + CUP + ".index"]
    d = np.linalg.norm(Z["targets/" + CUP + ".vector"].astype(np.float64), axis=1)
    return [int(i[np.where(v[i, 0] * sg > 0, d, -1.0).argmax()]) for sg in (1.0, -1.0)]


def fields(v, J, vf, Jf, T, Z):
    # Поля областей на вершинах MakeHuman (> 0 — внутри, ~ метры до края). Грани режутся по нулю каждого поля (cut):
    # край ровный, а не лесенкой по граням. Каждое поле — одна граница (почти линейное): тонкая полоса из двух
    # границ одним полем min() потерялась бы между вершинами ребра. Мужское бельё и соски — по телу мужчины v
    # (суставы J), женское — по телу женщины vf (Jf): одни и те же вершины, разные места. Числа — бельё беты
    y, ax = v[:, 1], np.abs(v[:, 0])
    hip = (J["thigh_l"][1] + J["thigh_r"][1]) * 0.5
    knee = (J["calf_l"][1] + J["calf_r"][1]) * 0.5
    top = J["pelvis"][1] + 0.075
    leg = hip - (hip - knee) * 0.30
    out = dict(top=top - y, leg=y - leg, band=y - (top - 0.025), legband=(leg + 0.007) - y)
    # Соски мужчины: ореол 11 мм, сосок 4 мм (как в бете)
    nip = nipples(v, Z)
    dn = np.minimum(*[np.linalg.norm(v - v[k], axis=1) for k in nip])
    out["areola"], out["tip"] = 0.011 - dn, 0.004 - dn
    # Губы — где цели объёма губ двигают вершины заметно
    lip = np.zeros(len(v))
    for name in LIPS:
        i = Z["targets/" + name + ".index"]
        lip[i] = np.maximum(lip[i], np.linalg.norm(Z["targets/" + name + ".vector"].astype(np.float64), axis=1))
    out["lip"] = (lip / lip.max() - 0.25) * 0.01

    # Женщина: слипы с низкой посадкой — вырез по бедру спереди дугой (от узкой ластовицы круто вверх и полого к боку),
    # сзади наискось через ягодицу; резинка сверху 1.7 см и по вырезу 7 мм
    yf, axf, zf = vf[:, 1], np.abs(vf[:, 0]), vf[:, 2]
    hipf = (Jf["thigh_l"][1] + Jf["thigh_r"][1]) * 0.5
    body = np.zeros(len(v), bool)
    body[np.unique(T)] = True  # без вспомогательной геометрии MakeHuman (юбка, колготки)
    mid = body & (axf < 0.012) & (yf > 0.6) & (yf < hipf)
    crotch = yf[mid].min()
    front = np.clip((zf - zf[mid].mean()) / 0.08 + 0.5, 0.0, 1.0)
    t = np.clip((axf - 0.018) / 0.15, 0.0, 1.0)
    topf = hipf + 0.065
    legf = crotch + 0.012 + front * 0.155 * (1.0 - (1.0 - t) ** 2) + (1.0 - front) * 0.165 * t
    out.update(ftop=topf - yf, fleg=yf - legf, fband=yf - (topf - 0.017), flegband=(legf + 0.007) - yf)
    # Лиф: чашки спереди до соска + 4 см (грудь беты, cup 0.65) (ореол мужчины на теле женщины крупнее), сзади лента 4.5 см, между чашками край плавно спускается к узкой перемычке;
    # снизу резинка 1.2 см; лямки 14 мм через плечо, на спине наискось к другому боку
    nf = vf[nip[0]] if vf[nip[0], 0] > 0 else vf[nip[1]]
    lo = nf[1] - 0.075
    k = np.clip((zf - nf[2] + 0.14) / 0.08, 0.0, 1.0)
    k = k * k * (3.0 - 2.0 * k)
    gore = np.clip(1.0 - axf / 0.055, 0.0, 1.0)
    cup = lo + 0.045 + (nf[1] + 0.040 - lo - 0.045) * k
    hi = cup + (lo + 0.05 - cup) * k * gore * gore * (3.0 - 2.0 * gore)
    out.update(blo=yf - lo, bhi=hi - yf, bband=(lo + 0.012) - yf)
    # Лямка — две границы (по обе стороны линии): узкая полоса одним полем терялась бы между вершинами
    nrm_f = normals(vf, T)
    s_a, s_b, s_d = np.full(len(v), -0.02), np.full(len(v), -0.02), np.full(len(v), np.inf)
    for sg in (1.0, -1.0):
        x0 = abs(nf[0])
        xs = sg * (x0 + 0.012)
        top_s = ray_hit(vf, T, np.array([[xs, 2.5, 0.0]]), np.array([[0.0, -1.0, 0.0]]))[0]
        fy = np.linspace(nf[1] + 0.03, top_s[1] - 0.035, 8)  # от края чашки
        fx = sg * x0 + (xs - sg * x0) * (fy - fy[0]) / (fy[-1] - fy[0])
        fr = ray_hit(vf, T, np.stack([fx, fy, np.full(8, 1.0)], 1), np.tile([0.0, 0.0, -1.0], (8, 1)))
        bt = np.linspace(0.0, 1.0, 10)
        bk = ray_hit(vf, T, np.stack([xs + (-sg * 0.06 - xs) * bt, top_s[1] - 0.035 + (lo + 0.035 - top_s[1] + 0.035) * bt,
                                      np.full(10, -1.0)], 1), np.tile([0.0, 0.0, 1.0], (10, 1)))
        path = np.concatenate([fr, [top_s], bk])
        path = path[np.isfinite(path).all(1)]
        for _ in range(3):
            path[1:-1] = 0.25 * path[:-2] + 0.5 * path[1:-1] + 0.25 * path[2:]
        dist, side = seg_side(vf, nrm_f, path)
        # На спине лямки скрещены: ближняя. В точке скрещения расстояния равны, и шум последнего бита (numpy 1.26
        # и 2.x считают по-разному) выбирал бы разные лямки: вторая берёт вершину, только если ближе на 1 мкм
        near = (dist < 0.03) & (yf > lo) & (dist < s_d - 1e-6)
        s_a = np.where(near, 0.007 - side, s_a)
        s_b = np.where(near, 0.007 + side, s_b)
        s_d = np.where(near, dist, s_d)
    # Округление до 1 нм снимает разницу последнего бита между версиями numpy (cross, matmul)
    out["strap_a"], out["strap_b"] = np.round(s_a, 9), np.round(s_b, 9)
    trunk = ax < np.abs(J["thigh_l"][0]) + 0.13  # кисти в A-позе висят сбоку, не задевать
    chest = (axf < np.abs(Jf["upperarm_l"][0]) - 0.01) & (yf < Jf["spine_03"][1] + 0.25) & (yf > lo - 0.02)
    face = (v[:, 2] > J["head"][2]) & (y > J["neck_01"][1])
    return out, dict(trunk=trunk, chest=chest, face=face)


def ext_eval(X, EI, EW):
    # Значение на вершинах сетки: вершина — смесь до 8 вершин MakeHuman (EI — номера, EW — доли)
    w = EW.reshape(EW.shape + (1,) * (X.ndim - 1))
    return (X[EI] * w).sum(1)


def cut(T, f, EI, EW):
    # Треугольники T (вершины сетки) режутся по нулю поля f (на вершинах сетки): новые вершины — точки рёбер, общие
    # для соседних треугольников. Возвращает T, поле, EI, EW с новыми вершинами в конце
    f = np.where(np.abs(f) < 1e-7, 1e-7, f)
    ins = f > 0
    k = ins[T].sum(1)
    made, nI, nW, nf, out = {}, [], [], [], []
    nv = len(f)

    def pt(a, b):
        key = (min(a, b), max(a, b))
        if key not in made:
            p, q = key
            t = float(grid(f[p] / (f[p] - f[q]), 20))
            idx = np.concatenate([EI[p], EI[q]])
            w = np.concatenate([EW[p] * (1 - t), EW[q] * t])
            acc = {}
            for x, y in zip(idx.tolist(), w.tolist()):
                if y > 0:
                    acc[x] = acc.get(x, 0.0) + y
            top = sorted(acc.items(), key=lambda e: (-e[1], e[0]))[:EI.shape[1]]
            ii = np.zeros(EI.shape[1], np.int64)
            ww = np.zeros(EI.shape[1])
            for j, (x, y) in enumerate(top):
                ii[j], ww[j] = x, y
            ww /= ww.sum()
            made[key] = nv + len(nI)
            nI.append(ii)
            nW.append(ww)
            nf.append(0.0)
        return made[key]

    for tri_, kk in zip(T.tolist(), k.tolist()):
        if kk in (0, 3):
            out.append(tri_)
            continue
        # Одинокая вершина по свою сторону: у неё треугольник, у другой стороны четырёхугольник (два треугольника)
        r = [ins[x] for x in tri_].index(kk == 1)
        a, b, c = tri_[r], tri_[(r + 1) % 3], tri_[(r + 2) % 3]
        p1, p2 = pt(a, b), pt(a, c)
        out += [[a, p1, p2], [p1, b, c], [p1, c, p2]]
    if nI:
        EI, EW = np.concatenate([EI, np.array(nI)]), np.concatenate([EW, np.array(nW)])
        f = np.concatenate([f, np.array(nf)])
    return np.array(out, np.int64), f, EI, EW


def build(p, base, groups, Z, rigs, weights, mh, data_dir, meshpack, regions):
    body_q, eye_q = groups["body"], np.concatenate([groups["helper-l-eye"], groups["helper-r-eye"]])
    used = np.unique(body_q)

    def place(vo, fit):
        # Оси игры, рост HEIGHT, низ на нуле; fit — (низ, масштаб) тела стенда, общий для морфов
        g = vo * OBJ_TO_GAME
        return (g - np.array([0.0, fit[0], 0.0])) * fit[1]

    v_obj = morph(base, Z, p)
    y0 = v_obj[used, 1].min()
    fit = (y0, HEIGHT / (v_obj[used, 1].max() - y0))
    # Вершины — в стойке беты (rest); поля белья размечаются в A-позе (числа полей — от суставов скелета игры)
    v_a = place(v_obj, fit)
    v = place(rest(v_obj, mh), fit)
    # Скелет — от тела RIG (суставы прежние, см. RIG); его рост и низ — свои
    r_obj = morph(base, Z, RIG)
    r0 = r_obj[used, 1].min()
    rfit = (r0, HEIGHT / (r_obj[used, 1].max() - r0))
    r_game = lambda x: place(x[None, :], rfit)[0]
    skels = {s: skeleton(r_obj, groups, rigs[s], r_game) for s in SKELETONS}
    skels = {s: rest_skeleton(b, r_obj, groups, rigs[s], weights[s], mh, r_game) for s, b in skels.items()}
    J = {b["name"]: b["pos"] for b in skels["game_engine"]}
    # Тело женщины: на нём размечается женское бельё (те же вершины, места — её)
    f_obj = morph(base, Z, P(**FEMALE))
    f0 = f_obj[used, 1].min()
    ffit = (f0, HEIGHT / (f_obj[used, 1].max() - f0))
    vf = place(f_obj, ffit)
    Jf = {b["name"]: b["pos"] for b in skeleton(f_obj, groups, rigs["game_engine"], lambda x: place(x[None, :], ffit)[0])}

    # Треугольники; зеркало по x меняет обход — вершины граней в обратном порядке (лицом наружу против часовой)
    quads = np.concatenate([body_q, eye_q])[:, ::-1]
    nbody = len(body_q)
    n = normals(v, tri(quads[:nbody]))
    # Глаза: нормали шаром от середины глазного яблока, радужка — грани, смотрящие вперёд
    for gname in ("helper-l-eye", "helper-r-eye"):
        ix = np.unique(groups[gname])
        d = v[ix] - (v[ix].min(0) + v[ix].max(0)) / 2
        n[ix] = d / np.linalg.norm(d, axis=1, keepdims=True)
    eye_c = n[quads[nbody:]].mean(1)
    eye_mat = np.where(eye_c[:, 2] > 0.85, IRIS, EYE)

    # Тело режется по полям белья; вершина сетки — смесь вершин MakeHuman (на ребре — двух, на пересечении — больше)
    EI = np.zeros((len(base), 8), np.int64)
    EI[:, 0] = np.arange(len(base))
    EW = np.zeros((len(base), 8))
    EW[:, 0] = 1.0
    Tb = tri(quads[:nbody]).astype(np.int64)
    F, M = fields(v_a, J, vf, Jf, Tb, Z)
    for name in sorted(F):
        Tb, _, EI, EW = cut(Tb, ext_eval(F[name], EI, EW), EI, EW)
    fe = {k: ext_eval(f, EI, EW)[Tb].mean(1) > 0 for k, f in F.items()}
    me = {k: ext_eval(m.astype(np.float64), EI, EW)[Tb].mean(1) > 0.5 for k, m in M.items()}
    # Чем грань будет у мужчины (cm) и у женщины (cf)
    cm = np.full(len(Tb), "S")
    shorts = me["trunk"] & fe["top"] & fe["leg"]
    cm[shorts] = "L"
    cm[shorts & (fe["band"] | fe["legband"])] = "B"
    cm[(cm == "S") & me["chest"] & fe["areola"]] = "A"
    cm[(cm == "A") & fe["tip"]] = "T"
    cf = np.full(len(Tb), "S")
    slip = me["trunk"] & fe["ftop"] & fe["fleg"]
    cf[slip] = "L"
    cf[slip & (fe["fband"] | fe["flegband"])] = "B"
    bra = me["chest"] & fe["blo"] & fe["bhi"]
    cf[(cf == "S") & (bra | (me["chest"] & fe["strap_a"] & fe["strap_b"]))] = "L"
    cf[bra & fe["bband"]] = "B"
    lips = me["face"] & fe["lip"]
    cm[lips], cf[lips] = "P", "P"
    bad = [(a, b) for a, b in set(zip(cm.tolist(), cf.tolist())) if (a, b) not in PAIR]
    if bad:
        for a, b in bad:
            sel = (cm == a) & (cf == b)
            print("humanconv: грани %s/%s: %d, середина у женщины %s" % (a, b, sel.sum(), ext_eval(vf, EI, EW)[Tb[sel]].mean((0, 1))))
        sys.exit("humanconv: нет материала для граней %s" % bad)
    bmat = np.array([MATERIALS.index(PAIR[(a, b)]) for a, b in zip(cm.tolist(), cf.tolist())])
    print("humanconv: бельё — вершин на срезах %d" % (len(EI) - len(base)))
    Te = tri(quads[nbody:]).astype(np.int64)
    T = np.concatenate([Tb, Te])
    tm = np.concatenate([bmat, np.concatenate([eye_mat, eye_mat])])

    # Область тела треугольника (скрытие под одеждой, wear_core.REGIONS): у большинства его вершин MakeHuman
    dom3 = EI[T, np.argmax(EW[T], 2)]
    r3 = regions[dom3]
    treg = np.where(r3[:, 1] == r3[:, 2], r3[:, 1], r3[:, 0])
    treg[len(Tb):] = 0
    # LOD 0 — упрощение беты (decimate.py): лицо дороже (голова ×40, шея ×8), швы материалов и областей держатся.
    # Нормали — от упрощённых граней в номерах вершин среза (до деления по материалу): шва нормалей нет
    nb_ = len(Tb)
    ge = [b["name"] for b in skels["game_engine"]]
    gdom = np.array(ge)[np.argmax(skin_dense(weights["game_engine"], ge, len(base)), 1)]
    wmh = np.where(gdom == "head", 40.0, np.where(gdom == "neck_01", 8.0, 1.0))
    wv = wmh[EI[np.arange(len(EI)), np.argmax(EW, 1)]]
    Vc = grid(ext_eval(v, EI, EW), 17)
    res = decimate.decimate(Vc.tolist(), Tb.tolist(), (tm[:nb_] * 32 + treg[:nb_]).tolist(),
                            [LOD0_TRIS] + list(LOD_TRIS), wv.tolist())
    keep, Tk, _ = res[0]
    keep = np.array(keep, np.int64)
    bm_, br_ = tm[:nb_], treg[:nb_]
    T = np.concatenate([np.array(Tk, np.int64).reshape(-1, 3), Te])
    tm = np.concatenate([tm[:nb_][keep], tm[nb_:]])
    treg = np.concatenate([treg[:nb_][keep], treg[nb_:]])
    Td = T[:len(keep)]
    eye_v = np.unique(eye_q)

    def cut_normals(X):
        # Нормали в номерах вершин среза по упрощённым граням; у глаз — свои (шаром), как в X без среза
        N = normals(X, Td)
        N[eye_v] = n[eye_v]
        return N
    Nc = cut_normals(ext_eval(v, EI, EW))
    head_tris = int((gdom[EI[Td, 0]] == "head").all(1).sum())
    print("humanconv: LOD 0 — треугольников %d (голова %d), было %d" % (len(Td), head_tris, nb_))
    # Вершины делятся по материалу и области граней: край белья резкий, область скрывается целыми треугольниками.
    # Вершина сетки — (вершина среза, материал, область); область — в байте доли смеси (у тела m0 == m1, docs/mesh.md)
    key = (T * len(MATERIALS) + tm[:, None]) * 32 + treg[:, None]
    uk, inv = np.unique(key.ravel(), return_inverse=True)
    uk_all = uk.copy()
    vreg = uk % 32
    uk = uk // 32
    src, vmat = uk // len(MATERIALS), uk % len(MATERIALS)
    T = inv.reshape(-1, 3)
    # LOD 1… — дальше то же упрощение (лицо дороже), глаза в каждом; вершины — из вершин LOD 0 (decimate их не
    # создаёт, вершина уходит вдоль шва), meshpack берёт их готовыми (кусок LODI)
    lodi = struct.pack("<I", len(res) - 1)
    for kl, Tl, el in res[1:]:
        kl = np.array(kl, np.int64)
        Tl = np.concatenate([np.array(Tl, np.int64).reshape(-1, 3), Te])
        ml = np.concatenate([bm_[kl], tm[len(keep):]])
        rl = np.concatenate([br_[kl], treg[len(keep):]])
        kk = ((Tl * len(MATERIALS) + ml[:, None]) * 32 + rl[:, None]).ravel()
        j = np.searchsorted(uk_all, kk)
        if (j >= len(uk_all)).any() or (uk_all[np.minimum(j, len(uk_all) - 1)] != kk).any():
            sys.exit("humanconv: вершина LOD вне вершин LOD 0")
        lodi += struct.pack("<If", len(kk), el) + j.astype("<u4").tobytes()
        print("humanconv: LOD — треугольников %d, ошибка %.1f мм" % (len(Tl), el * 1000))
    SI, SW = EI[src], EW[src]
    dom = SI[np.arange(len(src)), np.argmax(SW, 1)]  # веса костей — от главной вершины смеси
    at = lambda X: ext_eval(X, SI, SW)
    unit = lambda X: X / np.linalg.norm(X, axis=1, keepdims=True)
    n_s = unit(Nc[src])

    pos = grid(at(v), 17)
    nx, nz = oct_encode(n_s)
    vert = np.zeros(len(src), dtype=[("p", "<f4", 3), ("nx", "<u2"), ("nz", "<u2"), ("m0", "u1"), ("m1", "u1"),
                                       ("blend", "u1"), ("ao", "u1")])
    vert["p"], vert["nx"], vert["nz"] = pos, nx, nz
    vert["m0"] = vert["m1"] = vmat
    vert["blend"], vert["ao"] = vreg, 255

    out = b"ANAMESH1"
    mn, mx = pos.min(0), pos.max(0)
    out += chunk(b"HEAD", struct.pack("<II6f", len(src), T.size, *mn, *mx))
    out += chunk(b"MATS", struct.pack("<I", len(MATERIALS)) + b"".join(name_bytes(m) for m in MATERIALS))
    out += chunk(b"VERT", vert.tobytes())
    out += chunk(b"INDX", T.astype("<u4").tobytes())
    out += chunk(b"LODI", lodi)
    for s in SKELETONS:
        order = [b["name"] for b in skels[s]]
        bones, wq = skin_weights(weights[s], order, len(base))
        body = name_bytes(s) + struct.pack("<I", len(order)) + b"".join(name_bytes(nm) for nm in order)
        rec = np.zeros(len(src), dtype=[("b", "u1", 4), ("w", "u1", 4)])
        rec["b"], rec["w"] = bones[dom], wq[dom]
        out += chunk(b"SKIN", body + rec.tobytes())
    # Морфы: сумма тел, каждое приведено к росту HEIGHT; сдвиг положения int16 × шаг морфа, нормали int8 / 127.
    # Суставы тех же тел — сдвиги голов костей и длин (кусок MRPJ): вид строит по ним скелет человека
    shapes = {}

    def shape(q):
        key = json.dumps(q, sort_keys=True)
        if key not in shapes:
            vo = morph(base, Z, q)
            lo = vo[used, 1].min()
            f = (lo, HEIGHT / (vo[used, 1].max() - lo))
            vk = place(rest(vo, mh), f)
            jk = {}
            for sn in SKELETONS:
                h = np.array([place(joint(vo, groups, rigs[sn][b["name"]]["head"])[None, :], f)[0] for b in skels[sn]])
                t = np.array([place(joint(vo, groups, rigs[sn][b["name"]]["tail"])[None, :], f)[0] for b in skels[sn]])
                jk[sn] = np.concatenate([h, np.linalg.norm(t - h, axis=1)[:, None]], 1)
            shapes[key] = (grid(at(vk), 17), unit(cut_normals(ext_eval(vk, EI, EW))[src]), jk)
        return shapes[key]

    head = struct.pack("<I", len(MORPHS))
    recs, jrec = [], {sn: [] for sn in SKELETONS}
    for name, terms in MORPHS:
        dp, dn = np.zeros_like(pos), np.zeros((len(src), 3))  # сумма тел морфа (доли в сумме ноль)
        dj = {sn: np.zeros((len(skels[sn]), 4)) for sn in SKELETONS}
        for coef, q in terms:
            pk, nk, jk = shape(q)
            dp += coef * pk
            dn += coef * nk
            for sn in SKELETONS:
                dj[sn] += coef * jk[sn]
        if abs(sum(c for c, _ in terms)) > 1e-9:
            sys.exit("humanconv: морф %s — доли тел в сумме не ноль" % name)
        step = float(2.0 ** np.ceil(np.log2(max(float(np.abs(dp).max()), 1e-6) / 32767.0)))  # степень двойки
        ip = np.clip(np.round(dp / step), -32767, 32767).astype("<i2")
        inn = np.clip(np.round(dn * 127), -127, 127).astype("i1")
        head += name_bytes(name) + struct.pack("<f", step)
        recs.append((ip, inn))
        for sn in SKELETONS:
            jrec[sn].append(dj[sn])
        print("humanconv: морф %-13s сдвиг вершин до %4.1f см, суставов до %4.1f см" % (
            name, float(np.abs(dp).max()) * 100, float(np.linalg.norm(dj["game_engine"][:, :3], axis=1).max()) * 100))
    rec = np.zeros(len(src), dtype=[("m", [("p", "<i2", 3), ("n", "i1", 3)], len(MORPHS))])
    for k, (ip, inn) in enumerate(recs):
        rec["m"]["p"][:, k], rec["m"]["n"][:, k] = ip, inn
    out += chunk(b"MRPH", head + rec.tobytes())
    for sn in SKELETONS:
        body = name_bytes(sn) + struct.pack("<II", len(skels[sn]), len(MORPHS))
        for (name, _), dj in zip(MORPHS, jrec[sn]):
            body += name_bytes(name) + grid(dj, 20).astype("<f4").tobytes()
        out += chunk(b"MRPJ", body)

    with tempfile.TemporaryDirectory() as tmp:
        raw = os.path.join(tmp, p["name"] + ".raw.mesh")
        open(raw, "wb").write(out)
        lods = ",".join(str(t) for t in LOD_TRIS)
        subprocess.run([meshpack, "--lods", lods, raw, os.path.join(data_dir, "meshes", p["name"] + ".mesh")], check=True)

    for s in SKELETONS:
        bones = skels[s]
        b = struct.pack("<I", len(bones))
        for x in bones:
            b += name_bytes(x["name"]) + struct.pack("<if", x["parent"], grid(x["length"], 20))
            b += struct.pack("<3f", *grid(x["pos"], 20)) + struct.pack("<4f", *grid(x["q"], 22))
            b += struct.pack("<12f", *grid(x["inv"].ravel(), 20))
        path = os.path.join(data_dir, "people", "%s.%s.skel" % (p["name"], s))
        open(path, "wb").write(b"ANASKEL1" + chunk(b"BONE", b))
        print("humanconv: %s — костей %d" % (path, len(bones)))


def main():
    if len(sys.argv) != 4:
        sys.exit("использование: humanconv.py <исходники> <data> <meshpack>")
    srcdir, data_dir, meshpack = sys.argv[1:]
    os.makedirs(os.path.join(data_dir, "people"), exist_ok=True)
    base, groups = load_obj(os.path.join(srcdir, "base.obj"))
    Z = np.load(os.path.join(srcdir, "targets.npz"), allow_pickle=False)
    rigs, weights = {}, {}
    for s in SKELETONS:
        r = json.load(open(os.path.join(srcdir, "rig.%s.json" % s)))
        rigs[s] = r["bones"] if "bones" in r else r
        weights[s] = json.load(open(os.path.join(srcdir, "weights.%s.json" % s)))["weights"]
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I не кладёт папку скрипта в путь
    import wear  # одежда над этим телом (docs/wear.md)
    mh = mh_load(srcdir)
    body = wear.W.Body(base, groups, Z, rigs, weights, mh)
    for p in PEOPLE:
        build(p, base, groups, Z, rigs, weights, mh, data_dir, meshpack, body.regions())
    wear.build_all(body, data_dir, meshpack)
    import hair  # волосы над этим телом (docs/hair.md)
    hair.build_all(body, data_dir, meshpack)


if __name__ == "__main__":
    main()
