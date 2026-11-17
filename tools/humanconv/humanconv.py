#!/usr/bin/env python3
"""Человек MakeHuman (CC0) → наш .mesh с весами костей и морфами и .skel (docs/mesh.md, docs/people.md).

    tools/humanconv/fetch.sh            (скачать исходники и пересобрать; зовёт make meshes)
    python3 -I tools/humanconv/humanconv.py <папка исходников> <data> <meshpack>

Папка исходников (fetch.sh): base.obj, rig.game_engine.json, weights.game_engine.json, rig.mixamo.json,
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

HEIGHT = 1.80  # все тела этого роста, в игре — масштаб экземпляра
# LOD 1..4 — треугольников (LOD 0 — вся сетка MakeHuman). Каждый — упрощение предыдущего (meshpack)
LOD_TRIS = (9000, 2400, 700, 300)
SKELETONS = ("game_engine", "mixamo")  # оба пишутся, выбор — этап 8 (docs/people.md)
# human_bra — лиф: у женщин холст, у мужчин кожа (вид skin, бит женщины в зерне экземпляра, src/view/appear.h)
MATERIALS = ("human_skin", "human_linen", "human_band", "human_eye", "human_iris", "human_bra")
SKIN, LINEN, BAND, EYE, IRIS, BRA = range(6)
CUP = "breast/female-young-averagemuscle-averageweight-maxcup-averagefirmness"  # область груди — по этой цели

# Тело стенда: мужчина средних лет, сложение чуть крепче среднего (макро-морфы MakeHuman)
PEOPLE = [
    dict(name="human_m", gender=1.0, age=0.0, muscle=0.6, weight=0.5,
         mods={"head/head-scale-vert-decr": 0.4, "head/head-scale-horiz-decr": 0.25,
               "head/head-scale-depth-decr": 0.3, "head/head-back-scale-depth-decr": 0.5,
               "measure/measure-upperarm-length-incr": 0.6, "measure/measure-lowerarm-length-decr": 0.4}),
]
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
FEMALE = dict(gender=0.0, muscle=0.5, weight=0.55, cup=0.6, firm=0.7, replace_mods={
    "head/head-scale-vert-decr": 0.3, "head/head-scale-horiz-decr": 0.35, "head/head-scale-depth-decr": 0.3,
    "head/head-back-scale-depth-decr": 0.5, "bodyshapes/bodyshapes-elvs-fem-neat-hourglass": 0.5,
    "hip/hip-scale-horiz-decr": 0.4, "measure/measure-hips-circ-decr": 0.15, "measure/measure-thigh-circ-decr": 0.15,
    "armslegs/l-upperleg-fat-decr": 0.2, "armslegs/r-upperleg-fat-decr": 0.2, "measure/measure-knee-circ-decr": 0.4,
    "measure/measure-calf-circ-decr": 0.3, "buttocks/buttocks-volume-decr": 1.0,
    "measure/measure-shoulder-dist-decr": 0.6, "measure/measure-upperarm-length-incr": 0.5,
    "measure/measure-lowerarm-length-incr": 0.2, "breast/breast-dist-decr": 0.15, "pelvis/bulge-decr": 1.0})


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


def skin_weights(W, order, nv):
    # До 4 костей на вершину: самые тяжёлые (при равенстве — меньший номер), сумма весов ровно 255
    acc = np.zeros((nv, len(order)))
    for k, n in enumerate(order):
        lst = W.get(n, [])
        if lst:
            a = np.array(lst)
            np.add.at(acc[:, k], a[:, 0].astype(int), a[:, 1])
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


def fields(v, J, Z):
    # Поля областей белья на вершинах MakeHuman (> 0 — внутри, ~ метры до края): трусы, резинка, лиф. Высоты — от
    # суставов скелета game_engine, поэтому морфы сложения бельё не сдвигают. Грани режутся по нулю поля (cut): край
    # ровный, а не лесенкой по граням
    y, ax = v[:, 1], np.abs(v[:, 0])
    hip = (J["thigh_l"][1] + J["thigh_r"][1]) * 0.5
    knee = (J["calf_l"][1] + J["calf_r"][1]) * 0.5
    top = J["pelvis"][1] + 0.075
    leg = hip - (hip - knee) * 0.30
    trunk = ax < np.abs(J["thigh_l"][0]) + 0.13  # кисти в A-позе висят сбоку, не задевать
    # Каждое поле — одна граница (почти линейное): тонкая полоса из двух границ одним полем min() потерялась бы между
    # вершинами ребра. Внутри области — все её поля > 0
    out = dict(top=top - y, leg=y - leg, band=y - (top - 0.025))
    # Лиф: чашки — где цель груди MakeHuman двигает вершины заметно (по топологии, форма тела не важна), под ними —
    # пояс вокруг груди 3.5 см; между чашками край спускается к поясу
    i, d = Z["targets/" + CUP + ".index"], np.linalg.norm(Z["targets/" + CUP + ".vector"].astype(np.float64), axis=1)
    cup = np.full(len(v), -0.12)
    cup[i] = d / d.max() - 0.12
    out["cup"] = cup * 0.06
    low = y[out["cup"] > 0].min()
    out["ring_lo"] = y - (low - 0.012)
    out["ring_hi"] = (low + 0.035) - y
    masks = dict(trunk=trunk, chest=(ax < np.abs(J["upperarm_l"][0]) - 0.01) & (y < J["spine_03"][1] + 0.2))
    return out, masks


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


def build(p, base, groups, Z, rigs, weights, data_dir, meshpack):
    body_q, eye_q = groups["body"], np.concatenate([groups["helper-l-eye"], groups["helper-r-eye"]])
    used = np.unique(body_q)

    def place(vo, fit):
        # Оси игры, рост HEIGHT, низ на нуле; fit — (низ, масштаб) тела стенда, общий для морфов
        g = vo * OBJ_TO_GAME
        return (g - np.array([0.0, fit[0], 0.0])) * fit[1]

    v_obj = morph(base, Z, p)
    y0 = v_obj[used, 1].min()
    fit = (y0, HEIGHT / (v_obj[used, 1].max() - y0))
    to_game = lambda x: place(x[None, :], fit)[0]
    v = place(v_obj, fit)
    skels = {s: skeleton(v_obj, groups, rigs[s], to_game) for s in SKELETONS}
    J = {b["name"]: b["pos"] for b in skels["game_engine"]}

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
    F, M = fields(v, J, Z)
    Tb = tri(quads[:nbody]).astype(np.int64)
    for name in sorted(F):
        Tb, _, EI, EW = cut(Tb, ext_eval(F[name], EI, EW), EI, EW)
    fe = {k: ext_eval(f, EI, EW)[Tb].mean(1) > 0 for k, f in F.items()}
    me = {k: ext_eval(m.astype(np.float64), EI, EW)[Tb].mean(1) > 0.5 for k, m in M.items()}
    bmat = np.full(len(Tb), SKIN)
    shorts = me["trunk"] & fe["top"] & fe["leg"]
    bmat[shorts] = LINEN
    bmat[shorts & fe["band"]] = BAND
    bmat[(bmat == SKIN) & me["chest"] & (fe["cup"] | (fe["ring_lo"] & fe["ring_hi"]))] = BRA
    print("humanconv: бельё — вершин на срезах %d" % (len(EI) - len(base)))
    Te = tri(quads[nbody:]).astype(np.int64)
    T = np.concatenate([Tb, Te])
    tm = np.concatenate([bmat, np.concatenate([eye_mat, eye_mat])])

    # Вершины делятся по материалу граней: край белья резкий. Вершина сетки — (вершина среза, материал)
    key = T * len(MATERIALS) + tm[:, None]
    uk, inv = np.unique(key.ravel(), return_inverse=True)
    src, vmat = uk // len(MATERIALS), uk % len(MATERIALS)
    T = inv.reshape(-1, 3)
    SI, SW = EI[src], EW[src]
    dom = SI[np.arange(len(src)), np.argmax(SW, 1)]  # веса костей — от главной вершины смеси
    at = lambda X: ext_eval(X, SI, SW)
    unit = lambda X: X / np.linalg.norm(X, axis=1, keepdims=True)
    n_s = unit(at(n))

    pos = grid(at(v), 17)
    nx, nz = oct_encode(n_s)
    vert = np.zeros(len(src), dtype=[("p", "<f4", 3), ("nx", "<u2"), ("nz", "<u2"), ("m0", "u1"), ("m1", "u1"),
                                       ("blend", "u1"), ("ao", "u1")])
    vert["p"], vert["nx"], vert["nz"] = pos, nx, nz
    vert["m0"] = vert["m1"] = vmat
    vert["blend"], vert["ao"] = 0, 255

    out = b"ANAMESH1"
    mn, mx = pos.min(0), pos.max(0)
    out += chunk(b"HEAD", struct.pack("<II6f", len(src), T.size, *mn, *mx))
    out += chunk(b"MATS", struct.pack("<I", len(MATERIALS)) + b"".join(name_bytes(m) for m in MATERIALS))
    out += chunk(b"VERT", vert.tobytes())
    out += chunk(b"INDX", T.astype("<u4").tobytes())
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
            vk = place(vo, f)
            nk = normals(vk, tri(quads[:nbody]))
            nk[np.unique(eye_q)] = n[np.unique(eye_q)]
            jk = {}
            for sn in SKELETONS:
                h = np.array([place(joint(vo, groups, rigs[sn][b["name"]]["head"])[None, :], f)[0] for b in skels[sn]])
                t = np.array([place(joint(vo, groups, rigs[sn][b["name"]]["tail"])[None, :], f)[0] for b in skels[sn]])
                jk[sn] = np.concatenate([h, np.linalg.norm(t - h, axis=1)[:, None]], 1)
            shapes[key] = (grid(at(vk), 17), unit(at(nk)), jk)
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
    for p in PEOPLE:
        build(p, base, groups, Z, rigs, weights, data_dir, meshpack)


if __name__ == "__main__":
    main()
