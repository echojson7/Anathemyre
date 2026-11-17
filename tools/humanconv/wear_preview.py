"""Лист просмотра одежды без игры и без Blender (docs/wear.md): тело стенда (или морфа) и вещи в A-позе, плоская
заливка по нормали, художник по глубине (PIL). Форма, слои и края, а не свет — свет смотрится в игре.

    python3 -I tools/humanconv/wear_preview.py <исходники MakeHuman> out.jpg [--morph "female=1,full=0.5"]
        [--views front,side,back,q] [--zoom x y z размер] вещь…
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import humanconv as H  # noqa: E402
import wear  # noqa: E402
import wear_core as W  # noqa: E402

VIEWS = {"front": 0.0, "q": 0.7, "side": 1.5708, "back": 3.1416, "q2": -0.7}


def shape_of(body, spec):
    # Тело морфа: тело стенда + Σ вес × сумма тел морфа (как скиннинг: доли тел морфа в сумме ноль)
    V0 = body.shape(H.BASE)[0]
    w = {k: float(v) for k, v in (x.split("=") for x in spec.split(","))} if spec else {}
    terms = [(c * w[name], q) for name, ts in H.MORPHS if name in w for c, q in ts]
    V = V0 + sum((c * body.shape(q)[0] for c, q in terms), np.zeros_like(V0))

    def pos(g):
        p = g.positions(body.shape(H.BASE))
        return p + sum((c * g.positions(body.shape(q)) for c, q in terms), np.zeros_like(p))
    return V, pos


def draw(img, box, tris, cols, yaw, zoom):
    # Орфография: поворот вокруг y на yaw; z-буфер по пикселям рамки, заливка по нормали грани
    c, s = np.cos(yaw), np.sin(yaw)
    x0, y0, w, h = box
    P = tris
    X = P[..., 0] * c - P[..., 2] * s
    D = P[..., 0] * s + P[..., 2] * c  # к зрителю
    Y = P[..., 1]
    cx, cy, size = zoom if zoom else (0.0, 0.92, 1.95)
    sc = h / size
    u = w / 2 + (X - cx) * sc
    v = h / 2 - (Y - cy) * sc
    n = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    nd = n[:, 0] * s + n[:, 2] * c
    L = np.array([0.45, 0.7, 0.55])
    L /= np.linalg.norm(L)
    Lw = np.array([L[0] * c + L[2] * s, L[1], -L[0] * s + L[2] * c])
    lam = np.clip(n @ Lw, 0, 1) * 0.7 + 0.3
    zb = np.full((h, w), -1e9)
    cb = np.zeros((h, w, 3))
    cols = np.asarray(cols, np.float64)
    for t in np.where(nd > 0)[0]:
        ux, vy, dz = u[t], v[t], D[t]
        i0, i1 = int(max(0, np.floor(vy.min()))), int(min(h - 1, np.ceil(vy.max())))
        j0, j1 = int(max(0, np.floor(ux.min()))), int(min(w - 1, np.ceil(ux.max())))
        if i1 < i0 or j1 < j0:
            continue
        jj, ii = np.meshgrid(np.arange(j0, j1 + 1) + 0.5, np.arange(i0, i1 + 1) + 0.5)
        d = (ux[1] - ux[0]) * (vy[2] - vy[0]) - (ux[2] - ux[0]) * (vy[1] - vy[0])
        if abs(d) < 1e-12:
            continue
        l1 = ((jj - ux[0]) * (vy[2] - vy[0]) - (ux[2] - ux[0]) * (ii - vy[0])) / d
        l2 = ((ux[1] - ux[0]) * (ii - vy[0]) - (jj - ux[0]) * (vy[1] - vy[0])) / d
        l0 = 1 - l1 - l2
        m = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        z = l0 * dz[0] + l1 * dz[1] + l2 * dz[2]
        sub = zb[i0:i1 + 1, j0:j1 + 1]
        m &= z > sub
        sub[m] = z[m]
        cb[i0:i1 + 1, j0:j1 + 1][m] = cols[t] * lam[t]
    bg = zb < -1e8
    cb[bg] = (30, 32, 36)
    img.paste(Image.fromarray(np.clip(cb, 0, 255).astype(np.uint8)), (x0, y0))


PALETTE = [(200, 190, 175), (70, 72, 80), (60, 75, 110), (150, 130, 95), (90, 95, 70), (110, 70, 50),
           (175, 165, 140), (40, 40, 44), (130, 120, 110), (95, 60, 45), (150, 150, 160), (120, 110, 90)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("items", nargs="*")
    ap.add_argument("--morph", default="")
    ap.add_argument("--views", default="front,side,back,q")
    ap.add_argument("--zoom", nargs=4, type=float)
    ap.add_argument("--size", type=int, default=560)
    a = ap.parse_args()
    body = wear.load_body(a.src)
    shape, pos = shape_of(body, a.morph)
    items = [g for g in wear.catalog(body) if not a.items or g.name in a.items]
    hide = 0
    tris, cols = [], []
    for g in items:
        g.body = body
        g.topology()
        p = pos(g)
        tris.append(p[g.Tc])
        cols += [PALETTE[(hash(g.name) + m * 3) % len(PALETTE)] for m in g.Tm]
        hide |= g.hide
    reg = body.regions()
    T = body.T
    keep = ((1 << reg[T]) & hide).sum(1) == 0
    tris.insert(0, shape[T[keep]])
    cols = [(205, 160, 130)] * int(keep.sum()) + cols
    tris = np.concatenate(tris)
    views = a.views.split(",")
    W_, H_ = a.size, int(a.size * 1.25)
    img = Image.new("RGB", (W_ * len(views), H_), (30, 32, 36))
    zoom = (a.zoom[0], a.zoom[1], a.zoom[3]) if a.zoom else None
    for k, v in enumerate(views):
        draw(img, (k * W_, 0, W_, H_), tris, cols, VIEWS[v], zoom)
    img.save(a.out, quality=90)
    print("wear_preview: %s — треугольников %d" % (a.out, len(tris)))


if __name__ == "__main__":
    main()
