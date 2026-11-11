#!/usr/bin/env python3
"""Генератор деталей в формат .mesh (docs/mesh.md). Игра читает только .mesh (шов «ассеты офлайн»).

    make meshes      (python3 tools/meshgen.py data/meshes build/tools/meshpack)

Здесь заглушки этапов 1–2 (кубы, столб, плита, камни), здание Остов — в meshgen_ostov.py. Генератор
пишет сетку одним LOD, а meshpack (tools/meshpack, meshoptimizer) склеивает вершины и строит LOD и кластеры.
Байты результата зависят только от этих файлов и версии meshoptimizer: CI пересобирает и сравнивает с data/meshes.
Детали без UV (узор шейдер проецирует по миру). Начало координат сетки — середина низа: экземпляр с высотой
земли стоит на земле.
"""
import os
import struct
import subprocess
import sys
import tempfile

MAGIC = b"ANAMESH1"
MAX_CLUSTER_TRIS = 124


def chunk(tag, version, body):
    return tag + struct.pack("<IQ", version, len(body)) + body


def oct_encode(n):
    """Единичная нормаль → октаэдр с осью y (как oct_decode в data/shaders/common.glsl), два snorm16."""
    x, y, z = n
    s = abs(x) + abs(y) + abs(z)
    px, pz = x / s, z / s
    if y < 0:
        px, pz = (1 - abs(pz)) * (1 if px >= 0 else -1), (1 - abs(px)) * (1 if pz >= 0 else -1)
    q = lambda v: max(-32767, min(32767, round(v * 32767)))
    return q(px) & 0xFFFF, q(pz) & 0xFFFF


class Mesh:
    def __init__(self, materials):
        self.materials = materials  # имена, номер в вершине — место в списке
        self.verts = []             # (x, y, z, nx, ny, nz, m0, m1, blend, ao)
        self.indices = []
        self.collider = []          # коробки (центр, половины, кватернион)
        self.coarse = []

    def quad(self, corners, normal, vert_attr):
        """Грань из 4 углов против часовой стрелки, если смотреть снаружи (лицевая сторона)."""
        base = len(self.verts)
        for c in corners:
            m0, m1, blend, ao = vert_attr(c)
            self.verts.append((*c, *normal, m0, m1, blend, ao))
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, sx, sy, sz, vert_attr, y0=0.0):
        """Коробка sx × sy × sz, низ на высоте y0, по центру по x и z."""
        hx, hz = sx / 2, sz / 2
        y1 = y0 + sy
        faces = [
            ((1, 0, 0), [(hx, y0, hz), (hx, y0, -hz), (hx, y1, -hz), (hx, y1, hz)]),
            ((-1, 0, 0), [(-hx, y0, -hz), (-hx, y0, hz), (-hx, y1, hz), (-hx, y1, -hz)]),
            ((0, 1, 0), [(-hx, y1, hz), (hx, y1, hz), (hx, y1, -hz), (-hx, y1, -hz)]),
            ((0, -1, 0), [(-hx, y0, -hz), (hx, y0, -hz), (hx, y0, hz), (-hx, y0, hz)]),
            ((0, 0, 1), [(-hx, y0, hz), (hx, y0, hz), (hx, y1, hz), (-hx, y1, hz)]),
            ((0, 0, -1), [(hx, y0, -hz), (-hx, y0, -hz), (-hx, y1, -hz), (hx, y1, -hz)]),
        ]
        for n, corners in faces:
            self.quad(corners, n, vert_attr)
        b = ((0.0, y0 + sy / 2, 0.0), (hx, sy / 2, hz), (0.0, 0.0, 0.0, 1.0))
        self.collider.append(b)
        self.coarse.append(b)

    def encode(self):
        out = bytearray(MAGIC)
        xs = [v[0] for v in self.verts]
        ys = [v[1] for v in self.verts]
        zs = [v[2] for v in self.verts]
        head = struct.pack("<II6f", len(self.verts), len(self.indices),
                           min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
        out += chunk(b"HEAD", 1, head)
        mats = struct.pack("<I", len(self.materials))
        for name in self.materials:
            b = name.encode()
            mats += struct.pack("<B", len(b)) + b
        out += chunk(b"MATS", 1, mats)
        vb = bytearray()
        for x, y, z, nx, ny, nz, m0, m1, blend, ao in self.verts:
            ox, oz = oct_encode((nx, ny, nz))
            vb += struct.pack("<3fHH4B", x, y, z, ox, oz, m0, m1, blend, ao)
        out += chunk(b"VERT", 1, bytes(vb))
        out += chunk(b"INDX", 1, struct.pack("<%dI" % len(self.indices), *self.indices))
        # Кластеры: подряд по MAX_CLUSTER_TRIS треугольников (для коробок это один кластер).
        # Настоящие кластеры по соседству строит meshoptimizer, когда появятся детали сложнее (этап 4)
        ntri = len(self.indices) // 3
        clusters = []
        for t0 in range(0, ntri, MAX_CLUSTER_TRIS):
            n = min(MAX_CLUSTER_TRIS, ntri - t0)
            pts = [self.verts[i] for i in self.indices[t0 * 3:(t0 + n) * 3]]
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            cz = sum(p[2] for p in pts) / len(pts)
            r = max(((p[0] - cx) ** 2 + (p[1] - cy) ** 2 + (p[2] - cz) ** 2) ** 0.5 for p in pts)
            clusters.append((t0 * 3, n, cx, cy, cz, r))
        cb = struct.pack("<I", len(clusters))
        for c in clusters:
            cb += struct.pack("<II4f", *c)
        out += chunk(b"CLUS", 1, cb)
        # Один LOD: вся сетка, ошибка 0
        out += chunk(b"LODS", 1, struct.pack("<I", 1) + struct.pack("<IIIIf", 0, len(clusters), 0, len(self.indices), 0.0))
        for tag, boxes in ((b"COLL", self.collider), (b"COAR", self.coarse)):
            bb = struct.pack("<I", len(boxes))
            for (c, h, q) in boxes:
                bb += struct.pack("<10f", *c, *h, *q)
            out += chunk(tag, 1, bb)
        return bytes(out)


def ao_by_height(y, h, low=150):
    """Запечённое затенение: у земли темнее, к верху без тени."""
    k = min(1.0, max(0.0, y / h))
    return int(round(low + (255 - low) * k))


def cube():
    """Бетонный куб 2 м: заглушка блока."""
    m = Mesh(["concrete"])
    m.box(2.0, 2.0, 2.0, lambda c: (0, 0, 0, ao_by_height(c[1], 2.0)))
    return m


def pillar():
    """Столб 1 × 4 × 1 м: бетон, к низу переходит в ржавый металл (проверка смеси двух материалов вершины)."""
    m = Mesh(["concrete", "rust_metal"])
    m.box(1.0, 4.0, 1.0, lambda c: (0, 1, 255 if c[1] < 0.5 else 0, ao_by_height(c[1], 4.0, 170)))
    return m


def plate():
    """Плита 4 × 0.4 × 3 м из ржавого металла."""
    m = Mesh(["rust_metal"])
    m.box(4.0, 0.4, 3.0, lambda c: (0, 0, 0, 255 if c[1] > 0.2 else 190))
    return m


def ihash(v):
    """Целочисленный хеш (как hash_u в шейдерах): байты сетки не зависят от платформы."""
    v &= 0xFFFFFFFF
    s = (v * 747796405 + 2891336453) & 0xFFFFFFFF
    w = (((s >> ((s >> 28) + 4)) ^ s) * 277803737) & 0xFFFFFFFF
    return (w >> 22) ^ w


def rock(seed, squash):
    """Камень рассыпки (docs/terrain.md): икосаэдр, разбитый один раз, узлы сдвинуты по лучу хешем,
    грани плоские (низкополигональный скол). Поперечник ~1 м, низ на y = 0; масштаб ставит рассыпка."""
    t = (1.0 + 5.0 ** 0.5) / 2.0
    base = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
            (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
             (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
             (6, 2, 10), (8, 6, 7), (9, 8, 1)]

    def norm(v):
        l = (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) ** 0.5
        return (v[0] / l, v[1] / l, v[2] / l)

    verts = [norm(v) for v in base]
    mid = {}

    def midpoint(a, b):
        k = (min(a, b), max(a, b))
        if k not in mid:
            va, vb = verts[a], verts[b]
            verts.append(norm(((va[0] + vb[0]) / 2, (va[1] + vb[1]) / 2, (va[2] + vb[2]) / 2)))
            mid[k] = len(verts) - 1
        return mid[k]

    sub = []
    for a, b, c in faces:
        ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
        sub += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
    # Сдвиг по лучу и сплющивание; низ — на y = 0
    pts = []
    for i, v in enumerate(verts):
        r = 0.78 + 0.32 * (ihash(i * 7919 + seed * 104729) / 4294967295.0)
        pts.append((v[0] * r * 0.5, v[1] * r * 0.5 * squash, v[2] * r * 0.5 * 0.85))
    y0 = min(p[1] for p in pts)
    pts = [(p[0], p[1] - y0, p[2]) for p in pts]
    h = max(p[1] for p in pts)
    m = Mesh(["stone"])
    for a, b, c in sub:
        pa, pb, pc = pts[a], pts[b], pts[c]
        u = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        w = (pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2])
        # Грани икосаэдра — против часовой снаружи: нормаль u × w наружу
        n = norm((u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]))
        base_i = len(m.verts)
        for q in (pa, pb, pc):
            m.verts.append((*q, *n, 0, 0, 0, ao_by_height(q[1], h, 120)))
        m.indices += [base_i, base_i + 1, base_i + 2]
    xs = [p[0] for p in pts]
    zs = [p[2] for p in pts]
    hx, hz = (max(xs) - min(xs)) / 2, (max(zs) - min(zs)) / 2
    b = ((0.0, h / 2, 0.0), (hx, h / 2, hz), (0.0, 0.0, 0.0, 1.0))
    m.collider.append(b)
    m.coarse.append(b)
    return m


def sword():
    """Двуручный меч-заглушка (этап 10): начало — хват (середина кисти у гарды), клинок вдоль +y на 0.10–1.00 м,
    как отрезок клинка в дорожках (tools/retarget/bake.py, SWORD); рукоять на две кисти вниз."""
    m = Mesh(["sword_steel", "sword_grip"])
    steel = lambda c: (0, 0, 0, 255)
    grip = lambda c: (1, 1, 0, 220)
    m.box(0.050, 0.86, 0.008, steel, 0.08)    # клинок
    m.box(0.030, 0.05, 0.006, steel, 0.94)    # сужение к острию
    m.box(0.012, 0.03, 0.004, steel, 0.99)    # остриё
    m.box(0.22, 0.025, 0.032, steel, 0.055)   # гарда
    m.box(0.032, 0.29, 0.032, grip, -0.235)   # рукоять
    m.box(0.050, 0.045, 0.050, steel, -0.28)  # навершие
    m.collider.clear()
    m.coarse.clear()
    return m


MESHES = {"cube": cube, "sword": sword, "pillar": pillar, "plate": plate,
          "rock_a": lambda: rock(1, 0.62), "rock_b": lambda: rock(2, 0.8), "rock_c": lambda: rock(3, 0.45)}


def main():
    if len(sys.argv) != 3:
        sys.exit("использование: meshgen.py <папка> <meshpack>")
    out_dir, meshpack = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    import meshgen_ostov  # Остов берёт Mesh отсюда
    meshes = dict(MESHES)
    meshes.update(meshgen_ostov.MESHES)
    with tempfile.TemporaryDirectory() as tmp:
        for name, make in meshes.items():
            raw = os.path.join(tmp, name + ".mesh")
            with open(raw, "wb") as f:
                f.write(make().encode())
            path = os.path.join(out_dir, name + ".mesh")
            subprocess.run([meshpack, raw, path], check=True)


if __name__ == "__main__":
    main()
