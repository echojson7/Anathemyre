"""Машина из жёстких частей на костях (docs/vezha.md): части фабрики → одна сетка с весами (`SKIN`) и скелет `.skel`.

Часть — замкнутое тело (kit.Body), привязанное к кости целиком или весами по точке (кабель через сустав). На
вершину считаются два запечённых слоя, которых у движущейся детали в игре взять неоткуда:
  износ   доля второго материала (голая сталь из-под краски) — по расстоянию до выпуклой острой кромки;
  затенение  лучи по полусфере до соседних тел той же кости (поле расстояний мира движущихся тел не знает).
Кости — без поворота в покое (оси кости = оси сетки, сдвинутые в голову кости): местный поворот сустава
задаётся прямо в осях сетки.
"""
import math
import os
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from meshgen import oct_encode  # noqa: E402

import export  # noqa: E402
from kit import ihash  # noqa: E402


class Part:
    """Тело на кости. skin — имя кости или функция точка → [(кость, вес)…]; wear — (материал, ширина, у кромки,
    на плоскости) или None; edge — делить рёбра длиннее (м): слой износа и затенение лежат в вершинах."""

    def __init__(self, body, skin, wear=None, edge=0.12, ao=True, name=""):
        self.body, self.skin, self.wear, self.edge, self.ao, self.name = body, skin, wear, edge, ao, name


class Rig:
    def __init__(self, name):
        self.name = name
        self.bones = []   # (имя, родитель, голова)
        self.parts = []

    def bone(self, name, parent, head):
        assert parent is None or any(b[0] == parent for b in self.bones), parent
        self.bones.append((name, parent, tuple(float(v) for v in head)))

    def add(self, body, skin, **kw):
        self.parts.append(Part(body, skin, **kw))

    def index(self, name):
        for k, b in enumerate(self.bones):
            if b[0] == name:
                return k
        raise KeyError(name)

    def head(self, name):
        return self.bones[self.index(name)][2]


def _quant_weights(ws):
    """[(кость, вес)] → 4 кости и 4 веса u8 по убыванию, сумма ровно 255."""
    ws = sorted(ws, key=lambda t: (-t[1], t[0]))[:4]
    tot = sum(w for _, w in ws)
    q = [int(round(255.0 * w / tot)) for _, w in ws]
    q[0] += 255 - sum(q)
    b = [k for k, _ in ws]
    while len(b) < 4:
        b.append(0)
        q.append(0)
    return tuple(b), tuple(q)


def _sharp_edges(tris, cos_sharp=0.94):
    """Выпуклые острые рёбра части: отрезки (a, b). Ребро острое, если нормали граней расходятся больше ~20°,
    выпуклое — если сосед лежит за плоскостью грани."""
    nrm = []
    for a, b, c, _, _ in tris:
        n = np.cross(np.subtract(b, a), np.subtract(c, a))
        l = np.linalg.norm(n)
        nrm.append(n / l if l > 1e-12 else None)
    half = {}
    for k, (a, b, c, _, _) in enumerate(tris):
        for p, q, o in ((a, b, c), (b, c, a), (c, a, b)):
            half[(p, q)] = (k, o)
    out = []
    for (p, q), (k, o) in half.items():
        if p > q:
            continue
        other = half.get((q, p))
        if other is None:
            continue
        j, o2 = other
        if nrm[k] is None or nrm[j] is None or float(np.dot(nrm[k], nrm[j])) > cos_sharp:
            continue
        if float(np.dot(nrm[k], np.subtract(o2, p))) < -1e-7:  # вершина соседа ниже плоскости грани — ребро выпуклое
            out.append((p, q))
    return out


def _edge_distance(pts, edges):
    """Расстояние от каждой точки до ближайшего отрезка (numpy, кусками)."""
    if not edges:
        return np.full(len(pts), 1e9)
    P = np.asarray(pts, dtype=np.float64)
    A = np.asarray([e[0] for e in edges], dtype=np.float64)
    B = np.asarray([e[1] for e in edges], dtype=np.float64)
    AB = B - A
    L2 = np.maximum((AB * AB).sum(1), 1e-18)
    out = np.empty(len(P))
    step = max(1, 4000000 // max(len(A), 1))
    for s in range(0, len(P), step):
        Q = P[s:s + step, None, :] - A[None, :, :]
        t = np.clip((Q * AB[None]).sum(2) / L2[None], 0.0, 1.0)
        D = Q - t[..., None] * AB[None]
        out[s:s + step] = np.sqrt((D * D).sum(2).min(1))
    return out


# Направления лучей затенения: 24 по полусфере (z — вдоль нормали), вес — косинус
def _hemi(n):
    out = []
    for k in range(n):
        u = (k + 0.5) / n
        v = (ihash(k * 2654435761 + 17) & 0xFFFF) / 65536.0
        r, a = math.sqrt(u), 2 * math.pi * ((k * 0.61803398875 + v * 0.05) % 1.0)
        out.append((r * math.cos(a), r * math.sin(a), math.sqrt(max(0.0, 1.0 - u))))
    return out


_HEMI = _hemi(24)
AO_REACH = 0.32    # дальше лучи не смотрят, м
AO_POWER = 0.9     # доля затенения, которая идёт в вершину
AO_FLOOR = 0.25    # темнее вершина не бывает


def _occlusion(verts, normals, tris_xyz):
    """Затенение 0..1 (1 — открыто) на вершину: лучи до треугольников своей группы."""
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    index, vs, polys = {}, [], []
    for tri in tris_xyz:
        f = []
        for p in tri:
            k = index.get(p)
            if k is None:
                k = index[p] = len(vs)
                vs.append(p)
            f.append(k)
        polys.append(f)
    tree = BVHTree.FromPolygons(vs, polys, epsilon=0.0)
    out = []
    for p, n in zip(verts, normals):
        nz = Vector(n)
        up = Vector((0.0, 1.0, 0.0)) if abs(n[1]) < 0.9 else Vector((1.0, 0.0, 0.0))
        nx = up.cross(nz).normalized()
        ny = nz.cross(nx)
        o = Vector(p) + nz * 0.004
        occ = 0.0
        for dx, dy, dz in _HEMI:
            hit = tree.ray_cast(o, nx * dx + ny * dy + nz * dz, AO_REACH)
            if hit[0] is not None:
                occ += 1.0 - hit[3] / AO_REACH
        out.append(max(AO_FLOOR, 1.0 - AO_POWER * occ / len(_HEMI)))
    return out


def build(rig, log=None):
    """Части → вершины и треугольники сетки. Возвращает словарь: verts — записи (положение, нормаль, материал 0,
    материал 1, доля, затенение, кости, веса), tris — тройки номеров, mats — имена, open — незакрытых рёбер."""
    mats = sorted({f[1] for p in rig.parts for f in p.body.faces} | {p.wear[0] for p in rig.parts if p.wear})
    mi = {m: k for k, m in enumerate(mats)}
    bone_ix = {b[0]: k for k, b in enumerate(rig.bones)}
    # Затенение — по группам: все части одной кости (жёсткие) затеняют друг друга
    groups = {}
    prepared = []
    bad = 0
    for part in rig.parts:
        tris = export.canonical(export.subdivide(export.triangles(part.body), part.edge))
        bad += export.open_edges(tris)
        key = part.skin if isinstance(part.skin, str) else None
        if key is not None:
            groups.setdefault(key, []).extend((a, b, c) for a, b, c, _, _ in tris)
        prepared.append((part, tris, key))
    verts, out_tris = [], []
    for part, tris, key in prepared:
        mesh = export.build_mesh(tris)  # нормали углов (гладкие грани делят нормаль с соседями)
        pos = [(v[0], v[1], v[2]) for v in mesh.verts]
        nrm = [(v[3], v[4], v[5]) for v in mesh.verts]
        local_mats = sorted({t[3] for t in tris})
        if part.wear:
            wmat, width, at_edge, flat = part.wear
            d = _edge_distance(pos, _sharp_edges(tris))
            k = np.clip(1.0 - d / width, 0.0, 1.0)
            blend = flat + (at_edge - flat) * k * k
        else:
            wmat, blend = None, np.zeros(len(pos))
        if part.ao and key is not None:
            ao = _occlusion(pos, nrm, groups[key])
        else:
            ao = [1.0] * len(pos)
        base = len(verts)
        for i, v in enumerate(mesh.verts):
            m0 = mi[local_mats[v[6]]]
            m1 = mi[wmat] if wmat else m0
            if isinstance(part.skin, str):
                ws = [(bone_ix[part.skin], 1.0)]
            else:
                ws = [(bone_ix[b], w) for b, w in part.skin(pos[i]) if w > 0.0]
            bones, weights = _quant_weights(ws)
            verts.append((pos[i], nrm[i], m0, m1, int(round(255 * float(blend[i]))) if wmat else 0,
                          int(round(255 * ao[i])), bones, weights))
        out_tris += [(base + mesh.indices[k], base + mesh.indices[k + 1], base + mesh.indices[k + 2])
                     for k in range(0, len(mesh.indices), 3)]
        if log:
            log("  %-18s треугольников %6d" % (part.name or "?", len(tris)))
    return {"verts": verts, "tris": out_tris, "mats": mats, "open": bad}


def _chunk(tag, body):
    return tag + struct.pack("<IQ", 1, len(body)) + body


def _name(s):
    b = s.encode()
    return struct.pack("<B", len(b)) + b


def write(rig, built, data_dir, meshpack, lods):
    """Сетка data/meshes/<имя>.mesh (meshpack --lods) и скелет data/people/<имя>.<имя>.skel."""
    verts, tris = built["verts"], built["tris"]
    out = b"ANAMESH1"
    xs = np.asarray([v[0] for v in verts], dtype=np.float32)
    out += _chunk(b"HEAD", struct.pack("<II6f", len(verts), len(tris) * 3, *xs.min(0), *xs.max(0)))
    out += _chunk(b"MATS", struct.pack("<I", len(built["mats"])) + b"".join(_name(m) for m in built["mats"]))
    vb = bytearray()
    for p, n, m0, m1, blend, ao, _, _ in verts:
        ox, oz = oct_encode(n)
        vb += struct.pack("<3fHH4B", p[0], p[1], p[2], ox, oz, m0, m1, blend, ao)
    out += _chunk(b"VERT", bytes(vb))
    out += _chunk(b"INDX", struct.pack("<%dI" % (len(tris) * 3), *[i for t in tris for i in t]))
    skin = _name(rig.name) + struct.pack("<I", len(rig.bones)) + b"".join(_name(b[0]) for b in rig.bones)
    sb = bytearray()
    for v in verts:
        sb += struct.pack("<8B", *v[6], *v[7])
    out += _chunk(b"SKIN", skin + bytes(sb))
    raw = os.path.join(data_dir, "meshes", rig.name + ".raw")
    with open(raw, "wb") as f:
        f.write(out)
    try:
        subprocess.run([meshpack, "--prune", "--lods", ",".join(str(n) for n in lods), raw,
                        os.path.join(data_dir, "meshes", rig.name + ".mesh")], check=True)
    finally:
        os.remove(raw)
    # Скелет: кости без поворота в покое, обратная бинд-матрица — сдвиг в голову кости
    sk = struct.pack("<I", len(rig.bones))
    order = {b[0]: k for k, b in enumerate(rig.bones)}
    for name, parent, head in rig.bones:
        kids = [b[2] for b in rig.bones if b[1] == name]
        length = max([math.dist(head, k) for k in kids] + [0.05])
        h = struct.unpack("<3f", struct.pack("<3f", *head))
        sk += _name(name) + struct.pack("<if", order[parent] if parent else -1, length)
        sk += struct.pack("<3f", *h) + struct.pack("<4f", 0.0, 0.0, 0.0, 1.0)
        sk += struct.pack("<12f", 1.0, 0.0, 0.0, -h[0], 0.0, 1.0, 0.0, -h[1], 0.0, 0.0, 1.0, -h[2])
    path = os.path.join(data_dir, "people", "%s.%s.skel" % (rig.name, rig.name))
    with open(path, "wb") as f:
        f.write(b"ANASKEL1" + _chunk(b"BONE", sk))
    return path


# --- поза (лист просмотра и проверка хода поршней): та же кинематика, что pose_world() игры ---

def pose(rig, rot=None, trans=None):
    """rot — {кость: матрица 3×3 местного поворота}, trans — {кость: голова в осях сетки}. Возвращает на кость
    (R, p): точка бинда x → R · (x − голова) + p."""
    out = []
    order = {b[0]: k for k, b in enumerate(rig.bones)}
    for name, parent, head in rig.bones:
        R = np.eye(3)
        p = np.asarray(head)
        if parent:
            PR, pp = out[order[parent]]
            p = pp + PR @ (np.asarray(head) - np.asarray(rig.bones[order[parent]][2]))
            R = PR
        if trans and name in trans:
            p = np.asarray(trans[name], dtype=np.float64)
        if rot and name in rot:
            R = R @ np.asarray(rot[name])
        out.append((R, p))
    return out
