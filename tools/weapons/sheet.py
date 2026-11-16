#!/usr/bin/env python3
"""Лист оружия без игры и без Blender (docs/weapons.md): python3 tools/weapons/sheet.py out.png [имена…]

Растр numpy с z-буфером: каждая вещь — вид на плоскость клинка и вид в три четверти, остриём вправо, в одном
масштабе. Свет — ламбертовский от одного источника плюс небо, цвет — из таблицы материалов (data/materials.txt).
Проверка формы и материалов до игры: лист собирается за секунды, игра под lavapipe — за минуты.
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build  # noqa: E402
import wkit  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


def materials():
    """Цвет и металл материалов из data/materials.txt (только то, что нужно листу)."""
    out, cur = {}, None
    for line in open(os.path.join(ROOT, "data", "materials.txt"), encoding="utf-8"):
        w = line.split("#")[0].split()
        if not w:
            continue
        if w[0] == "material":
            cur = out.setdefault(w[1], {"color": (1.0, 0.0, 1.0), "metal": 0.0, "rough": 0.5})
        elif cur is not None and w[0] in ("color", "metal", "rough"):
            v = tuple(float(x) for x in w[1:4]) if w[0] == "color" else float(w[1])
            cur[w[0]] = v
    return out


def raster(img, zb, tris, cols, view, scale, off):
    """Треугольники в осях игры → картинка: view — матрица 3×3 (строки — оси экрана x, y и глубина)."""
    H, W = zb.shape
    v = tris @ view.T
    sx = v[..., 0] * scale + off[0]
    sy = -v[..., 1] * scale + off[1]
    sz = v[..., 2]
    for k in range(len(tris)):
        xa, ya, za = sx[k], sy[k], sz[k]
        x0, x1 = max(int(math.floor(xa.min())), 0), min(int(math.ceil(xa.max())), W - 1)
        y0, y1 = max(int(math.floor(ya.min())), 0), min(int(math.ceil(ya.max())), H - 1)
        if x0 > x1 or y0 > y1:
            continue
        d = (xa[1] - xa[0]) * (ya[2] - ya[0]) - (xa[2] - xa[0]) * (ya[1] - ya[0])
        if abs(d) < 1e-12:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w1 = ((gx - xa[0]) * (ya[2] - ya[0]) - (xa[2] - xa[0]) * (gy - ya[0])) / d
        w2 = ((xa[1] - xa[0]) * (gy - ya[0]) - (gx - xa[0]) * (ya[1] - ya[0])) / d
        w0 = 1 - w1 - w2
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * za[0] + w1 * za[1] + w2 * za[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        hit = inside & (z < sub)
        sub[hit] = z[hit]
        c = (w0[..., None] * cols[k, 0] + w1[..., None] * cols[k, 1] + w2[..., None] * cols[k, 2])
        img[y0:y1 + 1, x0:x1 + 1][hit] = c[hit]


def shade(tris, nrm, mats, mtab, view):
    """Цвет в углах: альбедо × (солнце + небо), у металла — блик по нормали к виду."""
    sun = np.array([0.35, 0.8, 0.5])
    sun /= np.linalg.norm(sun)
    eye = view[2] * -1.0
    cols = np.zeros(tris.shape)
    for k, m in enumerate(mats):
        t = mtab.get(m, {"color": (1, 0, 1), "metal": 0, "rough": 0.5})
        base = np.array(t["color"])
        for j in range(3):
            n = nrm[k, j]
            lam = max(np.dot(n, sun), 0.0)
            sky = 0.35 + 0.25 * n[1]
            if t["metal"] > 0.5:
                h = sun + eye
                h /= np.linalg.norm(h)
                spec = max(np.dot(n, h), 0.0) ** (2 + 60 * (1 - t["rough"]))
                c = base * (0.25 * sky + 0.35 * lam) + base * spec * 1.6
            else:
                c = base * (0.9 * lam + sky)
            cols[k, j] = c
    return cols


def main():
    if len(sys.argv) < 2:
        sys.exit("использование: sheet.py out.png [имена]")
    names = sys.argv[2:] or list(build.ITEMS)
    mtab = materials()
    rows = []
    for n in names:
        w, fist = build.ITEMS[n]()
        t = wkit.build(w, fist)
        m = wkit.mesh_of(t)
        mats = [m.materials[m.verts[3 * k][6]] for k in range(len(t))]
        P = np.array([[v[:3] for v in m.verts[3 * k:3 * k + 3]] for k in range(len(t))])
        N = np.array([[v[3:6] for v in m.verts[3 * k:3 * k + 3]] for k in range(len(t))])
        rows.append((n, P, N, mats))
    side = np.array([[0, 1, 0], [1, 0, 0], [0, 0, 1.0]])      # остриё вправо, лезвие вверх, смотрим с +Z
    a, b = math.radians(35), math.radians(-25)
    rot = np.array([[math.cos(a), 0, math.sin(a)], [0, 1, 0], [-math.sin(a), 0, math.cos(a)]])
    tilt = np.array([[1, 0, 0], [0, math.cos(b), -math.sin(b)], [0, math.sin(b), math.cos(b)]])
    q34 = tilt @ side @ rot
    span = max(r[1][..., 1].max() - r[1][..., 1].min() for r in rows)
    W, RH = 1800, 150
    scale = (W / 2 - 120) / span
    H = RH * len(rows) + 20
    img = np.full((H, W, 3), 0.42)
    zb = np.full((H, W), 1e9)
    labels = []
    for i, (n, P, N, mats) in enumerate(rows):
        y = 20 + RH * i + RH // 2
        for c, view in enumerate((side, q34)):
            lo = (P @ view.T)[..., 0].min()
            hi = (P @ view.T)[..., 0].max()
            cx = (W // 2) * c + 60 + (W / 2 - 120 - (hi - lo) * scale) / 2 - lo * scale
            cols = shade(P, N, mats, mtab, view)
            raster(img, zb, P, cols, view, scale, (cx, y))
        labels.append((10, 20 + RH * i, "%s  %.2f м" % (n, P[..., 1].max() - P[..., 1].min())))
    out = Image.fromarray((np.clip(img, 0, 1) ** (1 / 2.2) * 255).astype(np.uint8))
    d = ImageDraw.Draw(out)
    for x, y, s in labels:
        d.text((x, y), s, fill=(20, 20, 20))
    out.save(sys.argv[1])


if __name__ == "__main__":
    main()
