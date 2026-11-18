"""Причёска волосками (docs/hair.md, «Волоски и карточки»): десятки тысяч волосков-кривых над телом стенда — как
groom в студийном конвейере. Из него собираются карточки (cards.py): пучок волосков → полоса, ширина и изгиб — по
самим волоскам. Тот же groom трассирует Cycles для снимка рядом с концептом (groom_render.py).

Волосок — S точек от корня к кончику (своя длина у каждого). Области причёски — свои рецепты: зачёс к завязке
лежит по коже (радиус кожи гладкий по направлению, без ступенек вершин), чёлка и пряди у лица — путь по опорам в
осях головы, хвост — путь с тяжестью и упором в тело. Потом — пучки (волоски к оси своего пучка к кончику), волна
и отбившиеся волоски.

Случайность — splitmix64 (hair_core.Rng): байты не зависят от версии numpy.
"""
import numpy as np

import hair_core as C
from hair_core import HEAD_C, TAU, Rng
from wear_core import smoothstep as ss

S = 32  # точек на волосок


def unit(x):
    return x / np.maximum(np.linalg.norm(x, axis=-1, keepdims=True), 1e-12)


def rnd(rng, n, lo=0.0, hi=1.0):
    return rng.a(n, lo, hi)


def sph(az, el):
    # Направление от середины черепа: az — угол вокруг головы (0 — вперёд, + — к x > 0), el — над горизонтом
    az, el = np.radians(az), np.radians(el)
    return np.stack([np.sin(az) * np.cos(el), np.sin(el), np.cos(az) * np.cos(el)], -1)


class Skin:
    """Кожа для волосков: гладкий радиус головы по направлению (взвешенный по углу, без ступенек вершин; таблица
    по сетке углов), ближняя вершина сглаженного тела и упор над ним. Только float64: байты сетки в CI те же
    на любой машине (make meshes сверяется с git)"""

    STEP = 1.5    # шаг таблицы радиуса, градусы

    def __init__(self, hd):
        self.hd = hd
        b = hd.b
        hv = np.where(hd.is_head & np.isin(np.arange(b.nv), b.used))[0]
        rel = hd.S[hv] - HEAD_C
        r = np.linalg.norm(rel, axis=1)
        u = rel / r[:, None]
        # Таблица радиуса: az −180…180, el −90…90
        self.az = np.arange(-180.0, 180.0 + 1e-9, self.STEP)
        self.el = np.arange(-90.0, 90.0 + 1e-9, self.STEP)
        A, E = np.meshgrid(self.az, self.el, indexing="ij")
        d = sph(A.ravel(), E.ravel())
        tab = np.empty(len(d))
        sig2 = 0.07 ** 2
        for s in range(0, len(d), 512):
            q = d[s:s + 512]
            c = q[:, 0:1] * u[None, :, 0] + q[:, 1:2] * u[None, :, 1] + q[:, 2:3] * u[None, :, 2]
            w = np.exp((c - c.max(1, keepdims=True)) / sig2) * (c > 0.0)
            tab[s:s + 512] = (w * r[None, :]).sum(1) / np.maximum(w.sum(1), 1e-300)
        self.tab = tab.reshape(A.shape)
        # Упор: кандидаты тела в float64 (BLAS float64 расходится между машинами на 1e-16 — ничья ближних вершин с
        # такой точностью почти невозможна; float32 давал бы другие байты)
        self.cand = hd.cand
        self.Sc = hd.S[self.cand]
        self.sq = (self.Sc ** 2).sum(1)

    def radius(self, d):
        d = unit(np.asarray(d, np.float64))
        az = np.degrees(np.arctan2(d[..., 0], d[..., 2]))
        el = np.degrees(np.arcsin(np.clip(d[..., 1], -1.0, 1.0)))
        fa = (az + 180.0) / self.STEP
        fe = (el + 90.0) / self.STEP
        ia = np.clip(np.floor(fa).astype(np.int64), 0, len(self.az) - 2)
        ie = np.clip(np.floor(fe).astype(np.int64), 0, len(self.el) - 2)
        ta, te = np.clip(fa - ia, 0, 1), np.clip(fe - ie, 0, 1)
        T = self.tab
        return ((T[ia, ie] * (1 - ta) + T[ia + 1, ie] * ta) * (1 - te)
                + (T[ia, ie + 1] * (1 - ta) + T[ia + 1, ie + 1] * ta) * te)

    def at(self, d, off):
        # Точка над кожей головы в направлении d на off от кожи
        d = unit(np.asarray(d, np.float64))
        return HEAD_C + d * (self.radius(d) + off)[..., None]

    def nearest(self, P, reach=0.06):
        """Ближняя вершина сглаженного тела (кандидаты Head.cand) и высота над ней по нормали. Точки — кусками по
        высоте, у куска — только кандидаты в его рамке ± reach; дальше reach от тела — номер −1, высота 1 м"""
        sh = P.shape[:-1]
        Q = P.reshape(-1, 3)
        idx = np.full(len(Q), -1, np.int64)
        order = np.argsort(Q[:, 1], kind="stable")
        for s in range(0, len(Q), 4096):
            o = order[s:s + 4096]
            q = Q[o]
            lo, hi = q.min(0) - reach, q.max(0) + reach
            m = np.all((self.Sc >= lo) & (self.Sc <= hi), 1)
            if not m.any():
                continue
            d2 = self.sq[None, m] - 2.0 * (q @ self.Sc[m].T)
            j = np.argmin(d2, 1)
            near = d2[np.arange(len(q)), j] + (q ** 2).sum(1) < reach ** 2
            idx[o] = np.where(near, self.cand[m][j], -1)
        ok = idx >= 0
        h = np.ones(len(Q))
        ii = np.maximum(idx, 0)
        h[ok] = ((Q[ok] - self.hd.S[ii[ok]]) * self.hd.Ns[ii[ok]]).sum(1)
        return idx.reshape(sh), h.reshape(sh)

    def push(self, P, head, body, it=2, smooth=2):
        """Упор: точки волосков не ближе запаса к телу (у головы head, ниже body — одежда), между проходами —
        сглаживание вдоль волоска (без изломов от ступенек вершин). Корень не двигается"""
        for k in range(it + 1):
            idx, h = self.nearest(P)
            ii = np.maximum(idx, 0)
            c = np.where(self.hd.is_head[ii], head, body)
            P = P + self.hd.Ns[ii] * np.maximum(0.0, c - h)[..., None]
            if k < it:
                for _ in range(smooth):
                    P[:, 1:-1] = 0.25 * P[:, :-2] + 0.5 * P[:, 1:-1] + 0.25 * P[:, 2:]
        return P


def interp_rows(t, u, P):
    """Построчная линейная выборка: для каждой строки k точки P[k] (m × 3) с возрастающим параметром u[k] (m) —
    в значениях t[k] (n). Без цикла по строкам: строки сдвинуты на номер × 2 в одном searchsorted"""
    k, m = u.shape
    off = np.arange(k)[:, None] * 2.0
    uu = (u + off).ravel()
    tt = np.clip(t, u[:, :1], u[:, -1:]) + off
    j = np.clip(np.searchsorted(uu, tt.ravel(), side="right") - 1, 0, k * m - 2).reshape(t.shape)
    j = np.minimum(j, (np.arange(k)[:, None] + 1) * m - 2)
    u0, u1 = uu[j], uu[j + 1]
    w = np.clip((tt - u0) / np.maximum(u1 - u0, 1e-15), 0.0, 1.0)[..., None]
    Pf = P.reshape(k * m, 3)
    return Pf[j] * (1 - w) + Pf[j + 1] * w


def resample(P, n=S):
    # Волоски (k × m × 3) ровным шагом по длине в n точек
    seg = np.linalg.norm(P[:, 1:] - P[:, :-1], axis=2)
    u = np.concatenate([np.zeros((len(P), 1)), np.cumsum(seg, 1)], 1)
    u /= np.maximum(u[:, -1:], 1e-12)
    # нулевые отрезки (совпавшие точки) — строго возрастающий параметр
    u = u + np.arange(u.shape[1])[None, :] * 1e-12
    t = np.repeat(np.linspace(0.0, 1.0, n)[None], len(P), 0) * u[:, -1:]
    return interp_rows(t, u, P)


def cut(P, frac):
    # Укоротить волоски до доли frac своей длины (k), снова S точек (доля — по номеру точки, как раньше)
    n = P.shape[1]
    t = np.linspace(0.0, 1.0, n)
    u = np.repeat(t[None], len(P), 0)
    return interp_rows(t[None, :] * frac[:, None], u, P)


def catmull(Q, n=S):
    # Сплайн Кэтмелла — Рома по опорам (k × m × 3) → k × n точек ровным шагом
    Q = np.concatenate([Q[:, :1] * 2 - Q[:, 1:2], Q, Q[:, -1:] * 2 - Q[:, -2:-1]], 1)
    out = []
    for i in range(1, Q.shape[1] - 2):
        p0, p1, p2, p3 = Q[:, i - 1], Q[:, i], Q[:, i + 1], Q[:, i + 2]
        for t in np.linspace(0.0, 1.0, 12, endpoint=False):
            out.append(0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                              + (3 * p1 - p0 - 3 * p2 + p3) * t ** 3))
    out.append(Q[:, -2])
    return resample(np.stack(out, 1), n)


def scalp_samples(hd, n, rng):
    """Корни: n точек по коже головы (треугольники тела, вероятность по площади), внутри линии роста. Возвращает
    точки на сглаженном теле, их направления от середины черепа, az, el и поле линии роста (м)"""
    b = hd.b
    T = b.T[hd.is_head[b.T].all(1)]
    A = 0.5 * np.linalg.norm(np.cross(hd.S[T[:, 1]] - hd.S[T[:, 0]], hd.S[T[:, 2]] - hd.S[T[:, 0]]), axis=1)
    f = hd.scalp(hd.S[T].mean(1), np.ones(len(T), bool))
    keep = f > -0.004
    T, A = T[keep], A[keep]
    cdf = np.cumsum(A) / A.sum()
    pick = np.searchsorted(cdf, rnd(rng, 3 * n))
    r1, r2 = rnd(rng, 3 * n), rnd(rng, 3 * n)
    sq = np.sqrt(r1)
    w = np.stack([1 - sq, sq * (1 - r2), sq * r2], 1)
    P = (hd.S[T[pick]] * w[:, :, None]).sum(1)
    f = hd.scalp(P, np.ones(len(P), bool), ear=0.024)
    P = P[f > 0.0][:n]
    d = unit(P - HEAD_C)
    az = np.degrees(np.arctan2(d[:, 0], d[:, 2]))
    el = np.degrees(np.arcsin(np.clip(d[:, 1], -1, 1)))
    return P, d, az, el, hd.scalp(P, np.ones(len(P), bool), ear=0.024)


def slerp(a, b, t):
    # По большому кругу от a к b (k × 3), t — доли (m) → k × m × 3
    c = np.clip((a * b).sum(1), -1.0, 1.0)
    om = np.arccos(c)[:, None]
    so = np.maximum(np.sin(om), 1e-9)
    t = t[None, :]
    return (np.sin((1 - t) * om) / so)[..., None] * a[:, None] + (np.sin(t * om) / so)[..., None] * b[:, None]


def clump(P, key, n_clumps, strength, rng, power=0.8, lo=0.55):
    """Пучки: волоски k собраны к оси своего пучка к кончику (сила strength × t^power, у волоска — своя доля от
    lo до 1). Пучки — дальние точки по key (k × m: корень, середина, кончик), волосок — к ближнему. Возвращает
    волоски, номер пучка волоска и оси пучков"""
    k = len(P)
    seeds = [0]
    dmin = np.linalg.norm(key - key[0], axis=1)
    for _ in range(n_clumps - 1):
        i = int(np.argmax(dmin))
        seeds.append(i)
        dmin = np.minimum(dmin, np.linalg.norm(key - key[i], axis=1))
    D = np.stack([np.linalg.norm(key - key[s], axis=1) for s in seeds], 1)
    cid = np.argmin(D, 1)
    axis = np.stack([P[cid == c].mean(0) for c in range(n_clumps)])
    t = np.linspace(0.0, 1.0, P.shape[1])
    f = strength * t ** power
    share = lo + (1 - lo) * rnd(rng, k)
    Q = P + (axis[cid] - P) * (f[None, :, None] * share[:, None, None])
    Q[:, 0] = P[:, 0]
    axis = np.stack([Q[cid == c].mean(0) for c in range(n_clumps)])
    return Q, cid, axis


def frames_of(G):
    # Оси вдоль кривых (k × m × 3): касательная
    t = np.zeros_like(G)
    t[:, 1:-1] = G[:, 2:] - G[:, :-2]
    t[:, 0] = G[:, 1] - G[:, 0]
    t[:, -1] = G[:, -1] - G[:, -2]
    return unit(t)


def wave(P, rng, amp, waves, out):
    """Волна волоска: смещение поперёк (в плоскости, перпендикулярной out) синусом по длине, растёт к кончику"""
    k, n = P.shape[:2]
    t = frames_of(P)
    o = unit(out - t * (out * t).sum(2, keepdims=True))
    s = np.cross(t, o)
    u = np.linspace(0.0, 1.0, n)
    ph, fr, a = rnd(rng, k, 0, TAU), rnd(rng, k, waves * 0.6, waves * 1.4), rnd(rng, k, 0.3, 1.0) * amp
    w = np.sin(ph[:, None] + fr[:, None] * TAU * u[None, :]) * a[:, None] * ss(0.0, 0.25, u)[None, :]
    w2 = np.cos(ph[:, None] * 1.7 + fr[:, None] * 1.3 * TAU * u[None, :]) * a[:, None] * 0.4 * ss(0.0, 0.25, u)[None, :]
    return P + s * w[..., None] + o * w2[..., None]


def flyaway(P, rng, share, amp, out):
    # Отбившиеся волоски: доля share уходит от пучка наружу (out) к кончику
    k, n = P.shape[:2]
    pick = rnd(rng, k) < share
    u = np.linspace(0.0, 1.0, n)
    side = unit(rnd(rng, k * 3, -1, 1).reshape(k, 3))
    d = unit(out + side[:, None, :] * 0.8)
    a = rnd(rng, k, 0.3, 1.0) * amp
    return P + (d * (a[:, None] * ss(0.2, 1.0, u)[None, :])[..., None]) * pick[:, None, None]


class Groom:
    """Причёска: волоски (k × S × 3), область волоска, пучок (сквозной номер), радиус у корня и тон"""

    def __init__(self, name, hd):
        self.name, self.hd = name, hd
        self.sk = Skin(hd)
        self.rng = Rng(name)
        self.parts = []  # (имя области, волоски, пучки, оси пучков, «наружу» для карточек)

    def add(self, region, P, cid, axis, out):
        base = sum(len(p[3]) for p in self.parts)
        self.parts.append((region, P, cid + base, axis, out))

    @property
    def P(self):
        return np.concatenate([p[1] for p in self.parts])

    @property
    def cid(self):
        return np.concatenate([p[2] for p in self.parts])

    @property
    def region(self):
        return np.concatenate([np.full(len(p[1]), i) for i, p in enumerate(self.parts)])

    def save(self, path):
        np.savez_compressed(path, P=self.P.astype(np.float32), cid=self.cid, region=self.region,
                            names=np.array([p[0] for p in self.parts]))


# ---------------------------------------------------------------- высокий хвост с чёлкой набок (концепт)
PART_AZ = 24.0   # пробор: справа от середины (x > 0), чёлка уходит на левую сторону лица (x < 0)


def pony(hd, n_scale=1.0):
    """Высокий длинный хвост на макушке сзади, чёлка от бокового пробора через лоб на левый глаз и щёку, длинные
    пряди у лица с обеих сторон до груди (концепт пользователя)"""
    g = Groom("hair_pony", hd)
    sk, rng = g.sk, g.rng
    i, _ = hd.nearest(np.array([[0.0, 1.80, -0.075]]), np.where(hd.is_head)[0])
    nK = hd.Ns[i[0]]
    K = hd.S[i[0]] + nK * 0.016
    # Плоскость завязки: оси поперёк хвоста
    tK = unit(nK * 0.3 + np.array([0.0, 0.8, -1.0]))
    e1 = unit(np.cross(tK, np.array([0.0, 1.0, 0.0])))
    e2 = np.cross(tK, e1)

    P0, d0, az, el, f = scalp_samples(hd, int(42000 * n_scale), rng)
    bang = (az > -62.0) & (az < PART_AZ) & (f < 0.030) & (el > 8.0)
    face_r = (az >= PART_AZ) & (az < 100.0) & (f < 0.032) & (el > -14.0)
    face_l = (az <= -62.0) & (az > -100.0) & (f < 0.032) & (el > -14.0)
    # Зачёс берёт и корни чёлки и прядей у лица: под ними волосы тоже идут назад — у границ областей нет
    # голого шва из корней
    combed = np.ones(len(az), bool)

    # --- зачёс: по коже большим кругом к завязке; слой волоска — высота над кожей; у завязки сходятся в пучок
    idx = np.where(combed)[0][: int(15000 * n_scale)]
    k = len(idx)
    t = np.linspace(0.0, 1.0, S)
    dK = unit(K - HEAD_C)
    D = slerp(d0[idx], np.repeat(dK[None], k, 0), t)
    layer = rnd(rng, k)
    h = 0.0012 + 0.0045 * layer[:, None] * np.sin(np.pi * np.clip(t, 0, 1))[None, :] ** 0.5 + 0.0008 * t[None, :]
    G = sk.at(D, h)
    ang = rnd(rng, k, 0, TAU)
    rad = 0.009 * np.sqrt(rnd(rng, k))
    E = K + (np.cos(ang)[:, None] * e1 + np.sin(ang)[:, None] * e2) * rad[:, None] - nK * 0.004
    bl = ss(0.80, 1.0, t)[None, :, None]
    G = G * (1 - bl) + (G[:, -1:] * 0 + E[:, None]) * bl
    G[:, 0] = P0[idx]
    G = resample(G)
    key = np.concatenate([G[:, 0], G[:, S // 2] * 1.5], 1)
    G, cid, ax = clump(G, key, 150, 0.45, rng, power=1.2, lo=0.3)
    G = wave(G, rng, 0.0006, 2.0, unit(G - HEAD_C))
    G = flyaway(G, rng, 0.012, 0.006, unit(G - HEAD_C))
    g.add("combed", G, cid, ax, "skin")

    # --- хвост: ось с тяжестью и упором в спину; волоски — в круге у завязки, к середине шире (овал поперёк
    # спины), внешние короче — хвост сходит на конус
    L = 0.72
    path = C.grow(hd, K[None], tK[None], np.array([L]), step=0.01, gravity=0.4, stiff=0.92, flat=0.0, head=0.05,
                  body=0.05)[0]
    path = C.resample(path, 0.01)
    path = resample(path[None], 64)[0]
    tt, oo, ss_ = C.frames(path, hd)
    nT = int(14000 * n_scale)
    ang = rnd(rng, nT, 0, TAU)
    rr = np.sqrt(rnd(rng, nT))
    u = np.linspace(0.0, 1.0, 64)
    R = 0.012 + 0.066 * ss(0.0, 0.2, u) * (1.0 - 0.72 * u ** 1.3)
    Ro = R * (0.55 + 0.15 * ss(0.0, 0.3, u))
    off = (np.cos(ang)[:, None, None] * ss_[None] * R[None, :, None]
           + np.sin(ang)[:, None, None] * oo[None] * Ro[None, :, None]) * rr[:, None, None]
    G = path[None] + off + oo[None] * (0.35 * Ro)[None, :, None]   # сечение — от спины наружу: меньше упора
    frac = np.clip(1.0 - 0.55 * rr ** 2.2 * rnd(rng, nT, 0.4, 1.0) - 0.08 * rnd(rng, nT), 0.25, 1.0)
    G = cut(G, frac)
    G = resample(G)
    G = sk.push(G, 0.006, 0.035, it=3, smooth=4)
    key = np.concatenate([np.cos(ang)[:, None] * rr[:, None] * 0.05, np.sin(ang)[:, None] * rr[:, None] * 0.05,
                          frac[:, None] * 0.06], 1)
    G, cid, ax = clump(G, key, 90, 0.45, rng, power=0.9)
    G = wave(G, rng, 0.0008, 1.0, np.repeat(oo[None, ::2], len(G), 0)[:, :S])
    G = flyaway(G, rng, 0.01, 0.012, unit(G - path[None, ::2]))
    g.add("tail", G, cid, ax, "tail")
    g.tie = (path[0] + tt[0] * 0.004, tt[0])
    g.tail_axis = path
    g.tail_out = oo

    # --- чёлка набок: от пробора вперёд-вверх над лбом, через лоб на левую бровь и глаз, вниз у щеки. Волосок
    # дальше от пробора ложится ниже и короче; глубже от линии роста — поверх
    idx = np.where(bang)[0][: int(4200 * n_scale)]
    k = len(idx)
    s = np.clip((PART_AZ - az[idx]) / (PART_AZ + 62.0), 0, 1)
    dep = np.clip(f[idx] / 0.03, 0, 1)
    j = rnd(rng, k, -1, 1)
    a_r, e_r = az[idx], el[idx]
    A = np.stack([a_r,
                  a_r,
                  a_r - 6 - 8 * s,
                  -30 - 14 * s + 3 * j,
                  -58 - 6 * s,
                  -74 - 4 * s], 1)
    Eh = np.stack([e_r,
                   e_r + 1,
                   10 + 12 * dep - 6 * s + 3 * j,
                   -2 + 8 * dep - 4 * s + 3 * j,
                   -16 - 6 * s + 4 * dep,
                   -34 - 8 * s + 3 * j], 1)
    O = np.stack([np.full(k, 0.001), 0.007 + 0.004 * dep, 0.008 + 0.006 * dep, 0.020 + 0.005 * dep,
                  0.022 + 0.005 * dep, 0.024 + 0.004 * dep], 1)
    Q = sk.at(sph(A, Eh), O)
    Q[:, 0] = P0[idx]
    G = catmull(Q)
    frac = np.clip(0.6 + 0.4 * rnd(rng, k) + 0.15 * dep, 0.45, 1.0)
    G = cut(G, frac)
    G = sk.push(G, 0.004, 0.02, smooth=1)
    key = np.concatenate([G[:, 0], G[:, S // 2] * 2.0, G[:, -1]], 1)
    G, cid, ax = clump(G, key, 34, 0.55, rng, power=0.9)
    G = wave(G, rng, 0.0008, 1.2, unit(G - HEAD_C))
    G = flyaway(G, rng, 0.01, 0.006, unit(G - HEAD_C))
    g.add("bang", G, cid, ax, "skin")

    # --- пряди у лица: от висков перед ухом вниз, у ключицы — на грудь
    for side, sel, n_ in ((1.0, face_r, 4000), (-1.0, face_l, 3000)):
        idx = np.where(sel)[0][: int(n_ * n_scale)]
        k = len(idx)
        a_r, e_r = az[idx], el[idx]
        q = np.clip((np.abs(a_r) - 60.0) / 36.0, 0, 1)   # 0 — у лба, 1 — у уха
        j = rnd(rng, k, -1, 1)
        P1 = sk.at(sph(a_r - side * 4, e_r - 6), 0.010)
        P2 = sk.at(sph(side * (62 + 24 * q) + 4 * j, -18 + 4 * j), 0.016 + 0.004 * q)
        x3 = side * (0.074 + 0.03 * q + 0.005 * j)
        P3 = np.stack([x3, np.full(k, 1.585), 0.08 - 0.035 * q + 0.006 * j], 1)
        P4 = np.stack([side * (0.088 + 0.04 * q + 0.006 * j), 1.47 - 0.02 * q + 0.012 * j,
                       0.10 - 0.035 * q], 1)
        Q = np.stack([P0[idx], P1, P2, P3, P4], 1)
        G = catmull(Q)
        frac = np.clip(0.75 + 0.25 * rnd(rng, k) - 0.15 * q * rnd(rng, k), 0.5, 1.0)
        G = cut(G, frac)
        G = sk.push(G, 0.005, 0.03, it=3)
        key = np.concatenate([G[:, 0], G[:, S // 2], G[:, -1] * 0.5], 1)
        G, cid, ax = clump(G, key, 30, 0.3, rng, power=0.9)
        G = wave(G, rng, 0.0005, 0.8, unit(G - np.array([0.0, G[0, 0, 1], 0.02])))
        G = flyaway(G, rng, 0.006, 0.006, unit(G - np.array([0.0, 1.6, 0.02])))
        g.add("face_r" if side > 0 else "face_l", G, cid, ax, "face")
    # Концепт — чёлка на правую сторону лица (x > 0 в осях игры): рецепт выше написан зеркально, голова симметрична
    g.parts = [(nm, P * np.array([-1.0, 1.0, 1.0]), c, a * np.array([-1.0, 1.0, 1.0]), o) for nm, P, c, a, o in g.parts]
    g.tie = (g.tie[0] * np.array([-1.0, 1.0, 1.0]), g.tie[1] * np.array([-1.0, 1.0, 1.0]))
    g.tail_axis = g.tail_axis * np.array([-1.0, 1.0, 1.0])
    g.tail_out = g.tail_out * np.array([-1.0, 1.0, 1.0])
    return g
