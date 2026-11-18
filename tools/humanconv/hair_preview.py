"""Лист просмотра волос без игры (docs/hair.md): голова тела стенда (или морфа), вещи одежды и волос, заливка по
нормали вершин (z-буфер numpy). Форма прядей, линия роста, посадка над шарфом и капюшоном — свет смотрится в игре.

    python3 -I tools/humanconv/hair_preview.py <исходники MakeHuman> out.jpg [--morph "female=1"]
        [--views front,side,back,q] [--zoom x y размер] [--wear wear_hood,…] волосы…
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import humanconv as H  # noqa: E402
import hair  # noqa: E402
import wear  # noqa: E402
from wear_preview import VIEWS, shape_of  # noqa: E402

COLORS = {"hair": (70, 52, 38), "hair_stubble": (95, 75, 60), "human_skin": (200, 150, 120), "hair_back": (70, 52, 38), "hair_card": (110, 80, 58), "hair_brow": (60, 45, 35),
          "hair_tie": (120, 90, 60), "hair_metal": (160, 160, 165)}


def raster(img, box, P, N, T, C, yaw, zoom, UV=None, card=None, atlas=None):
    c, s = np.cos(yaw), np.sin(yaw)
    x0, y0, w, h = box
    X = P[:, 0] * c - P[:, 2] * s
    D = P[:, 0] * s + P[:, 2] * c
    cx, cy, size = zoom
    sc = h / size
    u = w / 2 + (X - cx) * sc
    v = h / 2 - (P[:, 1] - cy) * sc
    L = np.array([0.45, 0.75, 0.5])
    L /= np.linalg.norm(L)
    Lw = np.array([L[0] * c + L[2] * s, L[1], -L[0] * s + L[2] * c])
    Hh = Lw + np.array([s, 0.0, c])
    Hh /= np.linalg.norm(Hh)
    lam = np.clip(N @ Lw, 0, 1) * 0.75 + 0.25
    spec = np.clip(N @ Hh, 0, 1) ** 40 * 0.35
    col = C * lam[:, None] + 255 * spec[:, None]
    zb = np.full((h, w), -1e9)
    cb = np.zeros((h, w, 3))
    nd = ((u[T[:, 1]] - u[T[:, 0]]) * (v[T[:, 2]] - v[T[:, 0]]) - (v[T[:, 1]] - v[T[:, 0]]) * (u[T[:, 2]] - u[T[:, 0]]))
    two = card if card is not None else np.zeros(len(T), bool)
    vis = ((nd < 0) | two) & (u[T].max(1) >= 0) & (u[T].min(1) < w) & (v[T].max(1) >= 0) & (v[T].min(1) < h)
    for t in np.where(vis)[0]:
        ux, vy = u[T[t]], v[T[t]]
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
        tone = None
        if two[t] and atlas is not None:
            # Карточка: покрытие атласа прядей в точке (порог 0.5) и тон волоска
            q = UV[T[t]]
            uu = l0 * q[0, 0] + l1 * q[1, 0] + l2 * q[2, 0]
            vv = l0 * q[0, 1] + l1 * q[1, 1] + l2 * q[2, 1]
            px = atlas[np.clip((vv * atlas.shape[0]).astype(int), 0, atlas.shape[0] - 1),
                       np.clip((uu * atlas.shape[1]).astype(int), 0, atlas.shape[1] - 1)]
            m &= px[..., 3] > 127
            tone = px[..., 0:1] / 255.0 * 1.6
        dz = D[T[t]]
        z = l0 * dz[0] + l1 * dz[1] + l2 * dz[2]
        sub = zb[i0:i1 + 1, j0:j1 + 1]
        m &= z > sub
        sub[m] = z[m]
        cc = col[T[t]]
        cv = l0[..., None] * cc[0] + l1[..., None] * cc[1] + l2[..., None] * cc[2]
        if tone is not None:
            cv = cv * tone
        cb[i0:i1 + 1, j0:j1 + 1][m] = cv[m]
    cb[zb < -1e8] = (30, 32, 36)
    img.paste(Image.fromarray(np.clip(cb, 0, 255).astype(np.uint8)), (x0, y0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("items", nargs="*")
    ap.add_argument("--morph", default="")
    ap.add_argument("--views", default="front,q,side,back")
    ap.add_argument("--zoom", nargs=3, type=float, default=(0.0, 1.62, 0.5))
    ap.add_argument("--wear", default="")
    ap.add_argument("--size", type=int, default=420)
    ap.add_argument("--atlas", default="", help="PNG цвета атласа прядей (tools/hairtex/strands.py): прозрачный край карточек")
    a = ap.parse_args()
    body = wear.load_body(a.src)
    shape, pos = shape_of(body, a.morph)
    hd = hair.head_of(body)
    wanted = [x for x in a.wear.split(",") if x]
    items = [g for g in wear.catalog(body) if g.name in wanted]
    import beard
    items += [g for g in hair.catalog(body, hd) + beard.catalog(body, hd) if g.name in a.items]
    hide = 0
    Ps, Ns, Ts, Cs, UVs, CD = [], [], [], [], [], []
    base = 0
    reg = body.regions()
    T = body.T
    for g in items:
        hide |= g.hide
    keep = ((1 << reg[T]) & hide).sum(1) == 0
    Tb = T[keep]
    Vb = shape
    Nb = H.normals(Vb, T)
    Ps.append(Vb)
    Ns.append(Nb)
    Ts.append(Tb)
    Cs.append(np.tile(np.array(COLORS["human_skin"], np.float64), (len(Vb), 1)))
    UVs.append(np.zeros((len(Vb), 2)))
    CD.append(np.zeros(len(Tb), bool))
    base = len(Vb)
    pal = wear_preview_palette()
    for g in items:
        g.body = body
        g.topology()
        p = pos(g)
        n = H.normals(p, g.Tc)
        cols = np.zeros((len(p), 3))
        m0 = g.VM[:, 0]
        for k, nm in enumerate(g.materials):
            cols[m0 == k] = COLORS.get(nm, pal[(hash(g.name) + k * 3) % len(pal)])
        cols *= (0.35 + 0.65 * g.VM[:, 3:4] / 255.0)
        Ps.append(p)
        Ns.append(n)
        Ts.append(g.Tc + base)
        Cs.append(cols)
        uv = getattr(g, "UV", None)
        UVs.append(uv if uv is not None else np.zeros((len(p), 2)))
        CD.append(np.array([g.materials[k] == "hair_card" for k in g.Tm], bool))
        base += len(p)
    P, N, T, C = np.concatenate(Ps), np.concatenate(Ns), np.concatenate(Ts), np.concatenate(Cs)
    UV, CDm = np.concatenate(UVs), np.concatenate(CD)
    atlas = np.asarray(Image.open(a.atlas).convert("RGBA")) if a.atlas else None
    views = a.views.split(",")
    W_, H_ = a.size, int(a.size * 1.2)
    img = Image.new("RGB", (W_ * len(views), H_), (30, 32, 36))
    for k, v in enumerate(views):
        raster(img, (k * W_, 0, W_, H_), P, N, T, C, VIEWS[v], a.zoom, UV, CDm, atlas)
    img.save(a.out, quality=92)
    print("hair_preview: %s — треугольников %d" % (a.out, len(T)))


def wear_preview_palette():
    from wear_preview import PALETTE
    return PALETTE


if __name__ == "__main__":
    main()
