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
MATERIALS = ("human_skin", "human_linen", "human_band", "human_eye", "human_iris")
SKIN, LINEN, BAND, EYE, IRIS = range(5)

# Тело стенда: мужчина средних лет, сложение чуть крепче среднего (макро-морфы MakeHuman)
PEOPLE = [
    dict(name="human_m", gender=1.0, age=0.0, muscle=0.6, weight=0.5,
         mods={"head/head-scale-vert-decr": 0.4, "head/head-scale-horiz-decr": 0.25,
               "head/head-scale-depth-decr": 0.3, "head/head-back-scale-depth-decr": 0.5,
               "measure/measure-upperarm-length-incr": 0.6, "measure/measure-lowerarm-length-decr": 0.4}),
]
# Морфы внешности: сдвиги вершин от тела выше, вид смешивает их весами экземпляра (скиннинг, docs/people.md)
MORPHS = (
    ("full", dict(weight=1.0)),
    ("thin", dict(weight=0.0)),
    ("strong", dict(muscle=1.0)),
    ("nose", dict(mods={"nose/nose-volume-incr": 0.5, "nose/nose-scale-horiz-incr": 0.5,
                        "nose/nose-scale-vert-incr": 0.3, "nose/nose-scale-depth-incr": 0.4})),
    ("jaw", dict(mods={"chin/chin-width-incr": 0.7, "chin/chin-bones-incr": 0.6, "head/head-square": 0.6})),
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
    race = {"african": 1 / 3, "asian": 1 / 3, "caucasian": 1 / 3}
    for gn, gw in gen.items():
        for an, aw in age.items():
            for mn, mw in three(p["muscle"]).items():
                for wn, ww in three(p["weight"]).items():
                    add("macrodetails/universal-%s-%s-%smuscle-%sweight" % (gn, an, mn, wn), gw * aw * mw * ww)
            for rn, rw in race.items():
                add("macrodetails/%s-%s-%s" % (rn, gn, an), gw * aw * rw)
    for name, w in sorted(p.get("mods", {}).items()):
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


def face_materials(v, quads, J):
    # Материал грани по её середине: бельё (трусы до середины бедра) с тёмной резинкой поверх кожи.
    # Высоты — от суставов скелета game_engine, поэтому морфы сложения бельё не сдвигают
    c = v[quads].mean(1)
    hip = (J["thigh_l"][1] + J["thigh_r"][1]) * 0.5
    knee = (J["calf_l"][1] + J["calf_r"][1]) * 0.5
    top = J["pelvis"][1] + 0.075
    leg = hip - (hip - knee) * 0.30
    trunk = np.abs(c[:, 0]) < np.abs(J["thigh_l"][0]) + 0.13  # кисти в A-позе висят сбоку, не задевать
    mat = np.full(len(quads), SKIN)
    mat[(c[:, 1] < top) & (c[:, 1] > leg) & trunk] = LINEN
    mat[(c[:, 1] < top) & (c[:, 1] > top - 0.025) & trunk] = BAND
    return mat


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
    qmat = face_materials(v, quads, J)
    n = normals(v, tri(quads[:nbody]))
    # Глаза: нормали шаром от середины глазного яблока, радужка — грани, смотрящие вперёд
    for gname in ("helper-l-eye", "helper-r-eye"):
        ix = np.unique(groups[gname])
        d = v[ix] - (v[ix].min(0) + v[ix].max(0)) / 2
        n[ix] = d / np.linalg.norm(d, axis=1, keepdims=True)
    eye_c = n[quads[nbody:]].mean(1)
    qmat[nbody:] = np.where(eye_c[:, 2] > 0.85, IRIS, EYE)

    # Вершины делятся по материалу граней: край белья резкий. Вершина сетки — (вершина MakeHuman, материал)
    T = tri(quads)
    tm = np.concatenate([qmat, qmat])
    key = T.astype(np.int64) * len(MATERIALS) + tm[:, None]
    uk, inv = np.unique(key.ravel(), return_inverse=True)
    src, vmat = uk // len(MATERIALS), uk % len(MATERIALS)
    T = inv.reshape(-1, 3)

    pos = grid(v[src], 17)
    nx, nz = oct_encode(n[src])
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
        rec["b"], rec["w"] = bones[src], wq[src]
        out += chunk(b"SKIN", body + rec.tobytes())
    # Морфы: тот же рост и низ, что у тела (fit), сдвиг положения int16 × шаг морфа, нормали int8 / 127
    head = struct.pack("<I", len(MORPHS))
    recs = []
    for name, m in MORPHS:
        q = dict(p, **{k: x for k, x in m.items() if k != "mods"})
        q["mods"] = dict(p.get("mods", {}))
        for k, x in m.get("mods", {}).items():
            q["mods"][k] = q["mods"].get(k, 0.0) + x
        vk = place(morph(base, Z, q), fit)
        nk = normals(vk, tri(quads[:nbody]))
        nk[np.unique(eye_q)] = n[np.unique(eye_q)]
        dp = grid(vk[src], 17) - pos
        dn = nk[src] - n[src]
        step = float(2.0 ** np.ceil(np.log2(max(float(np.abs(dp).max()), 1e-6) / 32767.0)))  # степень двойки
        ip = np.clip(np.round(dp / step), -32767, 32767).astype("<i2")
        inn = np.clip(np.round(dn * 127), -127, 127).astype("i1")
        head += name_bytes(name) + struct.pack("<f", step)
        recs.append((ip, inn))
    rec = np.zeros(len(src), dtype=[("m", [("p", "<i2", 3), ("n", "i1", 3)], len(MORPHS))])
    for k, (ip, inn) in enumerate(recs):
        rec["m"]["p"][:, k], rec["m"]["n"][:, k] = ip, inn
    out += chunk(b"MRPH", head + rec.tobytes())

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
