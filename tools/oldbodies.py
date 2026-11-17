#!/usr/bin/env python3
"""Тела беты для сравнения в ряду показа (docs/body.md, «Показ»): сетки MakeHuman беты (AMSH 3, ветка
stage0/test-room, data/meshes/mh_*.mesh) → неподвижные сетки v2 old_body_<n>.mesh в позе привязки беты.

    python3 tools/oldbodies.py data/meshes build/tools/meshpack [ветка беты]

Скелет и веса отброшены: сетка стоит как деталь сцены, морфы беты запечены по таблице BODIES. Цвета вершин беты
сведены к материалам людей v2 (кожа, бельё, резинка, белок, радужка, брови). Не в CI: ветки беты там нет.
"""
import math
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from meshgen import Mesh  # noqa: E402

# Морфы беты по порядку: полнее, худее, сильнее, старше, нос, разрез глаз, челюсть, губы
MORPH_POS, MORPH_NRM = 0.25, 2.0  # диапазоны сдвигов в файле беты (tools/humanconv/humanconv.py)
BODIES = (
    ("female", {}),
    ("female", {0: 0.6}),
    ("thin", {}),
    ("male", {}),
    ("male", {2: 1.0}),
    ("male", {0: 0.7, 3: 0.6}),
)
MATS = ["human_skin", "human_linen", "human_band", "human_eye", "human_iris", "hair_brow"]


def read_amsh(data):
    magic, ver, nv, ni = struct.unpack_from("<4sIII", data, 0)
    assert magic == b"AMSH" and ver == 3, "ожидается AMSH 3"
    o = 16
    verts = [struct.unpack_from("<6f4B4B", data, o + 32 * i) for i in range(nv)]
    o += 32 * nv
    idx = list(struct.unpack_from("<%dI" % ni, data, o))
    o += 4 * ni + 8 * nv  # поток скиннинга не нужен
    nm = struct.unpack_from("<I", data, o)[0]
    o += 4
    morph = struct.unpack_from("<%dh" % (nv * nm * 6), data, o)
    return verts, idx, nm, morph


def material(v):
    """Номер в MATS по материалу беты, классу оттенка и цвету вершины."""
    mat, tint, r = v[10], v[12], v[6]
    if mat == 3:  # бельё беты: светлый холст и тёмная резинка
        return 1 if r > 55 else 2
    if tint == 2:
        return 5
    if tint == 0:
        return 3 if r > 60 else 4
    return 0


def build(data, weights):
    verts, idx, nm, morph = read_amsh(data)
    m = Mesh(MATS)
    for i, v in enumerate(verts):
        p = list(v[0:3])
        n = list(v[3:6])
        for k, w in weights.items():
            b = (i * nm + k) * 6
            for a in range(3):
                p[a] += w * morph[b + a] / 32767.0 * MORPH_POS
                n[a] += w * morph[b + 3 + a] / 32767.0 * MORPH_NRM
        ln = math.sqrt(sum(c * c for c in n)) or 1.0
        mi = material(v)
        m.verts.append((p[0], p[1], p[2], n[0] / ln, n[1] / ln, n[2] / ln, mi, mi, 0, 255))
    # Низ сетки — на нуле (морф полноты может опустить стопы)
    y0 = min(v[1] for v in m.verts)
    m.verts = [(x, y - y0, *rest) for (x, y, *rest) in m.verts]
    m.indices = idx
    return m


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit("использование: oldbodies.py <папка> <meshpack> [ветка беты]")
    out_dir, meshpack = sys.argv[1], sys.argv[2]
    ref = sys.argv[3] if len(sys.argv) == 4 else "origin/stage0/test-room"
    src = {}
    with tempfile.TemporaryDirectory() as tmp:
        for k, (who, weights) in enumerate(BODIES):
            if who not in src:
                src[who] = subprocess.run(["git", "show", "%s:data/meshes/mh_%s.mesh" % (ref, who)], check=True,
                                          capture_output=True).stdout
            raw = os.path.join(tmp, "raw.mesh")
            with open(raw, "wb") as f:
                f.write(build(src[who], weights).encode())
            subprocess.run([meshpack, raw, os.path.join(out_dir, "old_body_%d.mesh" % k)], check=True)


if __name__ == "__main__":
    main()
