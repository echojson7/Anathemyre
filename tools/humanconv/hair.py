"""Причёски, бороды и брови (docs/hair.md): рецепты над телом стенда на ядре hair_core. Каждая — вещь Hair:
шапка по коже головы (Part), пряди-жгуты по направляющим, завязки. Набор у человека — функция зерна в виде
(src/view/wear.cpp, как одежда)."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hair_core as C  # noqa: E402
import wear_core as W  # noqa: E402
from hair_core import TAU, Rng  # noqa: E402
from wear import mats  # noqa: E402
from wear_core import smoothstep as ss  # noqa: E402

LODS = (0.5, 0.2, 0.07, 0.025)
_HEAD = {}


def head_of(b):
    if id(b) not in _HEAD:
        _HEAD[id(b)] = C.Head(b)
    return _HEAD[id(b)]


def cap(g, hd, m, height, edge=0.012, mat="hair", skin="human_skin", **scalp):
    """Шапка волос по коже головы: высота height(v) (м), у линии роста сходит на нет за edge и смешивается с кожей"""
    b = hd.b

    def corner(v, t):
        f = hd.scalp_v(v, **scalp)
        return 0.0012 + height(v) * ss(0.0, edge, f)

    def dirt(v, t):
        f = hd.scalp_v(v, **scalp)
        return m[skin], np.clip((1.0 - ss(0.0, edge * 0.9, f)) / W.DIRT, 0.0, 1.0)

    def ao(v, t):
        return 0.55 + 0.25 * ss(0.0, edge, hd.scalp_v(v, **scalp))

    keep = [hd.scalp(**scalp)]
    return g.add(W.Part(b, keep, (), corner, lambda t: m[mat], dirt, thick=0.002, ao=ao))


def unit(x):
    return x / np.maximum(np.linalg.norm(x, axis=-1, keepdims=True), 1e-12)


def nape_point(hd, y=1.665, h=0.012):
    # Место завязки на затылке: над кожей на высоте h
    i, _ = hd.nearest(np.array([[0.0, y, -0.12]]), np.where(hd.is_head)[0])
    return hd.S[i[0]] + hd.Ns[i[0]] * h, hd.Ns[i[0]]


def combed(g, hd, m, target, mat, lift=0.007, base=0.0045, **scalp):
    """Зачёсанные к target волосы: гладкая шапка (пряди — узором шейдера назад), к месту завязки толще —
    волосы там собраны. Отдельные жгуты по голове пробовались: пересекались пятнами"""
    def height(v):
        d = np.linalg.norm(np.stack([v["x"], v["y"], v["z"]], 1) - target, axis=1)
        return base + 0.0015 * ss(1.70, 1.79, v["y"]) + lift * np.exp(-(d / 0.045) ** 2)
    cap(g, hd, m, height, mat=mat, edge=0.009, **scalp)


def braid(g, hd, m, start, d0, length, rng, mat, w0=0.034, w1=0.02, period=0.06, tie="hair_tie"):
    """Коса из трёх прядей от start вниз: путь — с тяжестью и упором в тело (поверх шарфа и капюшона), пряди —
    восьмёркой вокруг пути; на конце — завязка и кисточка"""
    path = C.grow(hd, start[None], d0[None], np.array([length]), step=0.01, gravity=0.7, stiff=0.55,
                  flat=0.25, head=0.03, body=0.026)[0]
    path = C.resample(path, 0.005)
    t, o, s = C.frames(path, hd)
    seg = np.linalg.norm(path[1:] - path[:-1], axis=1)
    u = np.concatenate([[0.0], np.cumsum(seg)])
    L = u[-1]
    wb = np.interp(u, [0.0, 0.06, L * 0.85, L], [w0 * 0.75, w0, w1 * 1.1, w1 * 0.8])
    end = len(path) - 4
    for i in range(3):
        ph = TAU * u / period + TAU * i / 3
        c = path + s * (0.24 * wb * np.sin(ph))[:, None] + o * (0.16 * wb * np.sin(2 * ph))[:, None]
        ws = 0.55 * wb
        g.strand(c[:end], ws[:end], 0.55 * ws[:end], m[mat], out=o[:end], sides=7, ridge=0.2, twist=0.0,
                 ao=(0.45, 1.0))
    # Завязка у затылка и на конце
    for at, r in ((2, w0 * 0.42), (end - 3, w1 * 0.45)):
        V, T = C.ring_mesh(path[at], t[at], r * 0.85, r * 1.15, 0.012, sides=10)
        g.rigid_free(V, T, m[tie], ao=0.8, anchor=path[at][None])
    # Кисточка: короткие пряди веером вниз
    base = path[end - 2]
    for k in range(7):
        a = TAU * k / 7 + rng.u() * 0.5
        dirn = unit(t[-1] + 0.35 * (np.cos(a) * s[-1] + np.sin(a) * o[-1]))
        n = 8
        Gk = base + dirn[None, :] * np.linspace(0.0, 0.05 + 0.03 * rng.u(), n)[:, None]
        Gk = Gk + np.array([0.0, -1.0, 0.0]) * (np.linspace(0, 1, n) ** 2 * 0.01)[:, None]
        uu = np.linspace(0, 1, n)
        g.strand(Gk, 0.009 * (1 - 0.6 * uu), 0.005 * (1 - 0.5 * uu), m[mat], sides=5, ridge=0.15)
    return path


def locks(g, hd, m, R, d0, length, rng, mat, width, thick, sides=5, ridge=0.18, twist=1.0, stop=None, layer=0.0,
          tip=0.7, **kw):
    """Пряди от корней R (вершины тела) в начальном направлении d0, длины length; stop(G) — до какой точки
    оставить направляющую (обрезка у бровей, над шарфом). Ширина и толщина — пары (от, до), сходят к концу"""
    root = hd.S[R] + hd.Ns[R] * 0.002
    if layer:
        # Слоями: корни выше — пряди поверх (макушка накрывает затылок и виски)
        y = hd.S[R, 1]
        kw["head"] = kw.get("head", 0.006) + layer * (y - y.min()) / max(y.max() - y.min(), 1e-6)
    G = C.grow(hd, root, d0, length, **kw)
    n_ok = 0
    for k, Gk in enumerate(G):
        if stop is not None:
            Gk = Gk[:max(3, stop(Gk))]
        n = len(Gk)
        if n < 3:
            continue
        u = np.linspace(0.0, 1.0, n)
        w = width[0] + rng.u() * (width[1] - width[0])
        th = thick[0] + rng.u() * (thick[1] - thick[0])
        rf = ss(-0.2, 0.15, u)
        g.strand(Gk, w * (1.0 - tip * u ** 1.6) * rf, th * (1.0 - 0.5 * u) * rf, m[mat], sides=sides, ridge=ridge,
                 twist=rng.bell(1)[0] * twist)
        n_ok += 1
    return n_ok


def coil(g, hd, m, c, axis, R0, r0, height, turns, rng, mat, n=3):
    """Пучок: n прядей витками вокруг оси axis от основания c, радиус витка сходит от R0 к трети, подъём height;
    внутри — сердцевина, чтобы между витками не просвечивало"""
    axis = unit(axis)
    ref = np.array([0.0, 1.0, 0.0]) if abs(axis[1]) < 0.9 else np.array([0.0, 0.0, -1.0])
    e1 = unit(np.cross(axis, ref))
    e2 = np.cross(axis, e1)
    for k in range(n):
        s_ = np.linspace(0.0, TAU * turns, int(28 * turns) + 2) + TAU * k / n + rng.u() * 0.4
        u = np.linspace(0.0, 1.0, len(s_))
        R = R0 * (1.0 - 0.62 * u)
        h = height * (0.15 + 0.85 * u ** 0.8)
        G = c + axis * h[:, None] + (np.cos(s_)[:, None] * e1 + np.sin(s_)[:, None] * e2) * R[:, None]
        outd = G - c - axis * ((G - c) @ axis)[:, None] + axis * 0.3 * R0
        prof = ss(-0.1, 0.12, u) * (1.0 - 0.5 * u ** 3)
        g.strand(G, 2.1 * r0 * prof, 1.3 * r0 * prof, m[mat], out=outd, sides=7, ridge=0.2, twist=2.0,
                 ao=(0.5, 1.0))
    core = c + axis * np.linspace(0.0, height * 0.95, 8)[:, None]
    u = np.linspace(0.0, 1.0, 8)
    wd = 2 * R0 * 0.9 * np.sqrt(np.clip(1.0 - u ** 2, 0.05, 1.0))
    g.strand(core, wd, wd, m[mat], out=np.tile(e1, (8, 1)), sides=8, ridge=0.1, ao=(0.45, 0.7))


def band(g, hd, m, c, axis, r, h=0.012, mat="hair_tie"):
    V, T = C.ring_mesh(c, unit(axis), r * 0.85, r * 1.18, h, sides=10)
    g.rigid_free(V, T, m[mat], ao=0.8, anchor=c[None])


def tail(g, hd, m, start, d0, length, rng, mat, n=18, r0=0.012, spread=0.03):
    """Хвост: путь от завязки вниз с тяжестью, вокруг него n прядей веером (к концу расходятся и короче)"""
    path = C.grow(hd, start[None], d0[None], np.array([length]), step=0.012, gravity=0.8, stiff=0.6, flat=0.2,
                  head=0.03, body=0.03)[0]
    path = C.resample(path, 0.014)
    t, o, s_ = C.frames(path, hd)
    L = len(path)
    for k in range(n):
        a = TAU * k / n + rng.u() * 0.6
        rr = (0.3 + 0.7 * rng.u())
        cut = int(L * (0.7 + 0.3 * rng.u()))
        u = np.linspace(0.0, 1.0, L)[:cut]
        off = (r0 + spread * u ** 1.3) * rr
        G = path[:cut] + (np.cos(a) * s_[:cut] + np.sin(a) * o[:cut]) * off[:, None]
        uu = np.linspace(0.0, 1.0, cut)
        w = (0.012 + 0.008 * rng.u()) * (1.0 - 0.8 * uu ** 1.5)
        g.strand(G, w, w * 0.5, m[mat], out=(np.cos(a) * s_[:cut] + np.sin(a) * o[:cut]), sides=5, ridge=0.18,
                 twist=rng.bell(1)[0] * 1.5)
    band(g, hd, m, path[1], t[1], r0 * 1.1)
    return path


# ---------------------------------------------------------------- бритая голова (щетина)
def shaved(b, hd):
    M = ["hair_stubble", "human_skin"]
    m = mats(M)
    g = C.Hair("hair_shaved", M, hd)
    cap(g, hd, m, lambda v: 0.0004 + 0.0 * v["y"], edge=0.01, mat="hair_stubble")
    return g


# ---------------------------------------------------------------- короткая стрижка: шапка и короткие пучки от макушки
def crop(b, hd):
    M = ["hair", "human_skin"]
    m = mats(M)
    g = C.Hair("hair_crop", M, hd)
    rng = Rng(g.name)
    # Короткие волосы — сплошная шапка, пряди — узором шейдера (пучки-чешуйки читались как перья)
    cap(g, hd, m, lambda v: 0.007 + 0.006 * ss(1.68, 1.79, v["y"]), edge=0.01)
    return g


# ---------------------------------------------------------------- узел на макушке
def topknot(b, hd):
    M = ["hair_back", "human_skin", "hair_ring"]
    m = mats(M)
    g = C.Hair("hair_topknot", M, hd)
    rng = Rng(g.name)
    i, _ = hd.nearest(np.array([[0.0, 1.85, 0.005]]), np.where(hd.is_head)[0])
    K = hd.S[i[0]] + hd.Ns[i[0]] * 0.012
    combed(g, hd, m, K, "hair_back")
    up = unit(hd.Ns[i[0]] + np.array([0.0, 0.0, -0.35]))
    coil(g, hd, m, K, up, 0.026, 0.011, 0.04, 1.3, rng, "hair_back")
    band(g, hd, m, K + up * 0.006, up, 0.022, h=0.014, mat="hair_ring")  # медное кольцо
    return g


# ---------------------------------------------------------------- хвост от затылка
def tail_style(b, hd):
    M = ["hair_back", "human_skin", "hair_tie"]
    m = mats(M)
    g = C.Hair("hair_tail", M, hd)
    rng = Rng(g.name)
    T, n = nape_point(hd, 1.672, 0.014)
    combed(g, hd, m, T, "hair_back")
    tail(g, hd, m, T, unit(np.array([0.0, -0.45, -0.9])), 0.30, rng, "hair_back")
    return g


# ---------------------------------------------------------------- низкий пучок со шпилькой
def bun(b, hd):
    M = ["hair_back", "human_skin", "hair_tie"]
    m = mats(M)
    g = C.Hair("hair_bun", M, hd)
    rng = Rng(g.name)
    B, n = nape_point(hd, 1.69, 0.012)
    combed(g, hd, m, B, "hair_back")
    axis = unit(n + np.array([0.0, 0.15, 0.0]))
    coil(g, hd, m, B, axis, 0.034, 0.012, 0.036, 1.4, rng, "hair_back")
    band(g, hd, m, B + axis * 0.004, axis, 0.03, h=0.01)
    # Шпилька: деревянная палочка наискось через пучок
    c = B + axis * 0.022
    d = unit(np.array([1.0, 0.75, 0.0]) - axis * 0.1)
    G = c + d[None, :] * np.linspace(-0.075, 0.085, 6)[:, None]
    w = np.full(6, 0.0045)
    w[-1] = 0.002
    g.strand(G, w, w, m["hair_tie"], out=np.tile(axis, (6, 1)), sides=6, ridge=0.0, ao=(0.8, 1.0))
    return g


# ---------------------------------------------------------------- лохматые до шеи
def shag(b, hd):
    M = ["hair", "human_skin"]
    m = mats(M)
    g = C.Hair("hair_shag", M, hd)
    rng = Rng(g.name)
    cap(g, hd, m, lambda v: 0.004 + 0.002 * ss(1.70, 1.78, v["y"]))
    crown = np.array([0.0, 1.81, 0.0])
    f = hd.scalp()
    R = C.roots(hd, f, 0.0125, rng, margin=0.005)
    d0 = -C.tangent_to(hd, R, crown) + hd.Ns[R] * 0.35
    a = np.degrees(np.abs(hd.az[R]))
    ln = np.interp(a, [0, 40, 80, 120, 180], [0.085, 0.10, 0.12, 0.13, 0.13]) * (0.8 + 0.35 * rng.a(len(R)))
    brow = hd.eye_top + 0.024

    def stop(G):
        # Чёлка не ниже бровей, сзади и с боков — выше шарфа
        d = G - C.HEAD_C
        az = np.degrees(np.abs(np.arctan2(d[:, 0], d[:, 2])))
        lim = np.interp(az, [0, 35, 60, 90, 180], [brow, brow, 1.655, 1.625, 1.612])
        low = np.where(G[:, 1] < lim)[0]
        return low[0] if len(low) else len(G)
    locks(g, hd, m, R, d0, ln, rng, "hair", (0.014, 0.021), (0.006, 0.009), sides=6, ridge=0.2, twist=1.0,
          stop=stop, layer=0.012, tip=0.6, step=0.015, gravity=0.55, stiff=0.75, flat=0.35, head=0.0075)
    return g


# ---------------------------------------------------------------- коса от затылка
def braid_style(b, hd):
    M = ["hair_back", "human_skin", "hair_tie"]
    m = mats(M)
    g = C.Hair("hair_braid", M, hd)
    rng = Rng(g.name)
    T, n = nape_point(hd, 1.668, 0.014)
    combed(g, hd, m, T, "hair_back")
    braid(g, hd, m, T, unit(np.array([0.0, -0.6, -0.8])), 0.46, rng, "hair_back")
    return g


# ---------------------------------------------------------------- высокий хвост карточками (концепт пользователя)
def pony(b, hd):
    """Высокий длинный хвост, чёлка от бокового пробора, пряди у лица (концепт пользователя): причёска волосками
    (groom.py) → карточки по пучкам (cards.py), под ними — шапка цвета волос (кожа не просвечивает)"""
    import cards
    import groom
    M = ["hair_back", "human_skin", "hair_tie", "hair_card"]
    m = mats(M)
    g = C.Hair("hair_pony", M, hd)
    gr = groom.pony(hd)
    rng = Rng(g.name + "-карточки")
    K, nK = gr.tie
    combed(g, hd, m, K, "hair_back", lift=0.002, base=0.0008)
    cards.build(g, gr, m["hair_card"], hd, rng)
    band(g, hd, m, K, nK, 0.014, h=0.016)
    return g


def catalog(b, hd=None):
    hd = hd or head_of(b)
    return [shaved(b, hd), crop(b, hd), topknot(b, hd), tail_style(b, hd), braid_style(b, hd), bun(b, hd),
            shag(b, hd), pony(b, hd)]


def build_all(b, data_dir, meshpack):
    import beard  # бороды и брови — на тех же помощниках
    out = []
    hd = head_of(b)
    for g in catalog(b, hd) + beard.catalog(b, hd):
        g.write(b, data_dir, meshpack, LODS)
        out.append(g)
    return out
