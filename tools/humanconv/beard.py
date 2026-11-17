"""Бороды и брови (docs/hair.md): кусок по коже лица (Part) внутри поля, у края смешан с кожей; у полной и
длинной бороды — пряди от подбородка. Ориентиры лица — по целям MakeHuman (губы, брови, подбородок) у стенда."""
import numpy as np

import hair_core as C
import wear_core as W
from hair import locks, unit
from hair_core import Rng
from wear import mats
from wear_core import smoothstep as ss

MOUTH = np.array([0.0, 1.626, 0.16])  # середина рта стенда (цели губ MakeHuman)


def pts(v):
    return np.stack([v["x"], v["y"], v["z"]], 1)


def face_front(P):
    # Перед головы и шеи: от уха вперёд
    d = P - C.HEAD_C
    return 96.0 - np.degrees(np.abs(np.arctan2(d[:, 0], d[:, 2])))


def lips(P, grow=1.0):
    # Губы и уголки рта: снаружи эллипса > 0
    e = np.sqrt((P[:, 0] / (0.027 * grow)) ** 2 + ((P[:, 1] - MOUTH[1]) / (0.0125 * grow)) ** 2)
    return (e - 1.0) * 0.012


def beard_field(P, cheek=0.0, neck=1.556, burns=True):
    """Борода: ниже линии щёк (от бакенбард к уголку рта; cheek поднимает её), выше neck на шее, без губ"""
    r = np.abs(P[:, 0])
    yc = np.interp(r, [0.0, 0.028, 0.045, 0.062, 0.078], [1.648, 1.638, 1.652, 1.676, 1.70]) + cheek
    f = yc - P[:, 1]
    # Снизу: на шее спереди до neck, к бокам граница поднимается к углу челюсти (бок шеи без бороды)
    f = np.minimum(f, P[:, 1] - (neck + np.interp(r, [0.0, 0.03, 0.05, 0.068, 0.08], [0.0, 0.008, 0.04, 0.085, 0.12])))
    f = np.minimum(f, lips(P))
    if burns:
        f = np.minimum(f, face_front(P) * 0.002)
    else:
        f = np.minimum(f, (0.062 - r))
    return f


def mustache_field(P):
    r = np.abs(P[:, 0])
    top = 1.6445 - 0.004 * ss(0.0, 0.025, r)
    return np.minimum.reduce([top - P[:, 1], P[:, 1] - 1.6345 + 0.012 * ss(0.018, 0.03, r), 0.031 - r,
                              lips(P), P[:, 2] - 0.12])


def chin_field(P):
    r = np.abs(P[:, 0])
    return np.minimum.reduce([0.03 - r, 1.611 - P[:, 1], P[:, 1] - 1.572, lips(P), P[:, 2] - 0.10])


def shell(g, hd, m, field, height, mat, edge=0.006):
    """Кусок волос по коже внутри поля field(P): высота height(P, f), у края — на нет и смесь с кожей"""
    b = hd.b
    keep = [np.minimum(field(b.V), face_front(b.V) * 0.002)]

    def corner(v, t):
        P = pts(v)
        f = field(P)
        return 0.0006 + height(P, f) * ss(0.0, edge, f)

    def dirt(v, t):
        f = field(pts(v))
        return m["human_skin"], np.clip((1.0 - ss(0.0, edge * 0.9, f)) / W.DIRT, 0.0, 1.0)

    def ao(v, t):
        return 0.6 + 0.3 * ss(0.0, edge, field(pts(v)))
    g.add(W.Part(b, keep, (), corner, lambda t: m[mat], dirt, thick=0.0015, ao=ao))


def full_field(P):
    return np.maximum(beard_field(P), mustache_field(P))


# ---------------------------------------------------------------- брови
def brows(b, hd):
    M = ["hair_brow", "human_skin"]
    m = mats(M)
    g = C.Hair("hair_brows", M, hd)

    def field(P):
        r = np.abs(P[:, 0])
        yb = np.interp(r, [0.010, 0.022, 0.036, 0.05], [1.7165, 1.7215, 1.7235, 1.7185])
        th = np.interp(r, [0.010, 0.02, 0.04, 0.052], [0.0052, 0.0055, 0.0042, 0.0016])
        return np.minimum.reduce([th - np.abs(P[:, 1] - yb), r - 0.0105, 0.053 - r, P[:, 2] - 0.12])
    shell(g, hd, m, field, lambda P, f: 0.0015 + 0.0015 * ss(0.0, 0.004, f), "hair_brow", edge=0.0018)
    return g


# ---------------------------------------------------------------- бороды
def stubble(b, hd):
    M = ["hair_stubble", "human_skin"]
    m = mats(M)
    g = C.Hair("beard_stubble", M, hd)
    shell(g, hd, m, full_field, lambda P, f: 0.0003 + 0.0 * f, "hair_stubble", edge=0.01)
    return g


def short(b, hd):
    M = ["hair", "human_skin"]
    m = mats(M)
    g = C.Hair("beard_short", M, hd)
    shell(g, hd, m, full_field, lambda P, f: 0.003 + 0.004 * ss(1.64, 1.585, P[:, 1]), "hair", edge=0.006)
    return g


def chin_locks(g, hd, m, rng, length, n=9, width=0.046, thick=0.013, mat="hair"):
    """Длинная борода: n широких прядей от дуги под подбородком вниз по груди (поверх одежды), средние длиннее"""
    for k in range(n):
        s_ = (k + 0.5) / n * 2.0 - 1.0  # −1..1 по дуге
        x = 0.04 * s_
        root = np.array([[x, 1.594 + 0.035 * s_ ** 2, 0.152 - 0.035 * s_ ** 2]])
        i, h = hd.nearest(root, np.where(hd.is_head)[0])
        root = hd.S[i] + hd.Ns[i] * 0.008
        ln = length * (1.0 - 0.5 * s_ ** 2) * (0.9 + 0.2 * rng.u())
        d0 = unit(np.array([[-x * 1.6, -1.0, 0.8]]))  # к середине: борода сходится клином
        G = C.grow(hd, root, d0, np.array([ln]), step=0.012, gravity=0.45, stiff=0.85, flat=0.0, head=0.014,
                   body=0.034)[0]
        u = np.linspace(0.0, 1.0, len(G))
        w = width * (0.75 + 0.3 * rng.u()) * (1.0 - 0.75 * u ** 1.4) * ss(-0.3, 0.15, u)
        g.strand(G, w, thick * (1.0 - 0.5 * u) * ss(-0.3, 0.15, u), m[mat], sides=8, ridge=0.22,
                 twist=rng.bell(1)[0] * 0.8, ao=(0.5, 1.0))


def full(b, hd):
    M = ["hair", "human_skin"]
    m = mats(M)
    g = C.Hair("beard_full", M, hd)
    rng = Rng(g.name)
    # Объём у подбородка и под ним — толщиной куска (пряди-сосульки читались хуже)
    shell(g, hd, m, full_field, lambda P, f: 0.005 + 0.011 * ss(1.645, 1.59, P[:, 1]) * ss(0.06, 0.02, np.abs(P[:, 0])),
          "hair", edge=0.008)
    return g


def goatee(b, hd):
    M = ["hair", "human_skin"]
    m = mats(M)
    g = C.Hair("beard_goatee", M, hd)
    rng = Rng(g.name)
    field = lambda P: np.maximum(mustache_field(P), chin_field(P))
    shell(g, hd, m, field, lambda P, f: 0.003 + 0.005 * ss(1.62, 1.58, P[:, 1]), "hair", edge=0.005)
    # Кончики усов вниз мимо уголков рта
    for sx in (-1.0, 1.0):
        G = np.array([[sx * 0.024, 1.637, 0.150], [sx * 0.030, 1.628, 0.146], [sx * 0.031, 1.615, 0.142],
                      [sx * 0.030, 1.602, 0.139]])
        G = C.resample(G, 0.005)
        u = np.linspace(0.0, 1.0, len(G))
        g.strand(G, 0.007 * (1 - 0.7 * u), 0.004 * (1 - 0.5 * u), m["hair"], sides=5, ridge=0.15)
    return g


def long(b, hd):
    M = ["hair", "human_skin", "hair_tie"]
    m = mats(M)
    g = C.Hair("beard_long", M, hd)
    rng = Rng(g.name)
    shell(g, hd, m, full_field, lambda P, f: 0.006 + 0.01 * ss(1.65, 1.585, P[:, 1]), "hair", edge=0.008)
    chin_locks(g, hd, m, rng, 0.19)
    return g


def catalog(b, hd):
    return [brows(b, hd), stubble(b, hd), short(b, hd), full(b, hd), goatee(b, hd), long(b, hd)]
