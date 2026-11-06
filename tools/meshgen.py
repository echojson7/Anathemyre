#!/usr/bin/env python3
"""Генератор деталей-заглушек в формат .mesh (docs/mesh.md). Игра читает только .mesh (шов «ассеты офлайн»).

    python3 tools/meshgen.py data/meshes      (или make meshes)

Байты результата зависят только от этого файла: CI пересобирает и сравнивает с data/meshes в git.
Детали — коробки с плоскими гранями, без UV (узор шейдер проецирует по миру). Начало координат сетки — середина
низа: экземпляр с высотой земли стоит на земле.
"""
import os
import struct
import sys

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


MESHES = {"cube": cube, "pillar": pillar, "plate": plate}


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "data/meshes"
    os.makedirs(out_dir, exist_ok=True)
    for name, make in MESHES.items():
        data = make().encode()
        path = os.path.join(out_dir, name + ".mesh")
        with open(path, "wb") as f:
            f.write(data)
        print("meshgen: %s (%d байт)" % (path, len(data)))


if __name__ == "__main__":
    main()
