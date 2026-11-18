"""Карточки из причёски волосками (docs/hair.md, «Волоски и карточки»): пучок волосков groom.py → одна-две полосы
по оси пучка. Ширина полосы по длине — разлёт волосков пучка поперёк, плоскость — по самим волоскам: у лежащих на
голове — по коже, у хвоста и прядей у лица — по главному направлению разлёта. Толстый пучок — две полосы друг над
другом (слои), тонкий — одна. Столбец атласа — по слою: глубже — плотные волоски, снаружи — редкие, у кончиков
хвоста — сбегающие пряди. Так карточки повторяют форму и рисунок волосков, а не расставлены рукой."""
import numpy as np

import hair_core as C
from groom import resample, unit
from wear_core import smoothstep as ss

# Столбцы атласа прядей (tools/hairtex/strands.py, COLUMNS): плотные, средние, кончики, отбившиеся, подложка
DENSE, MEDIUM, TIPS, FLY, UNDER = (0, 1, 2), (3, 4), (5,), (6,), (7,)


def smooth1(x, it=3):
    x = x.copy()
    for _ in range(it):
        x[1:-1] = 0.25 * x[:-2] + 0.5 * x[1:-1] + 0.25 * x[2:]
    return x


def rows_for(G, step=0.03, lo=4, hi=14):
    # Рядов вдоль полосы: по длине и изгибу (кривой пучок — чаще)
    L = np.linalg.norm(G[1:] - G[:-1], axis=1).sum()
    t = unit(G[1:] - G[:-1])
    bend = np.arccos(np.clip((t[1:] * t[:-1]).sum(1), -1, 1)).sum()
    return int(np.clip(np.ceil(L / step + bend / 0.35), lo, hi))


def clump_frame(M, A, out_hint):
    """Плоскость пучка: касательная оси t, «наружу» o и поперёк e. out_hint (S × 3) — направление «наружу» (от
    кожи или от оси хвоста); если None — по главному разлёту волосков"""
    n = len(A)
    t = np.zeros_like(A)
    t[1:-1] = A[2:] - A[:-2]
    t[0] = A[1] - A[0]
    t[-1] = A[-1] - A[-2]
    t = unit(t)
    D = M - A[None]
    D = D - t[None] * (D * t[None]).sum(2, keepdims=True)
    if out_hint is None:
        # Главное направление разлёта по всей длине (вес — к кончику, у корня разлёта нет)
        w = np.linspace(0.2, 1.0, n)
        cov = np.array([[(D[..., i] * D[..., j] * w[None]).sum() for j in range(3)] for i in range(3)])
        # главный вектор — степенным методом (без LAPACK: одни и те же байты на любой машине)
        e0 = np.array([0.57735, 0.57735, 0.57735])
        for _ in range(60):
            e0 = cov[:, 0] * e0[0] + cov[:, 1] * e0[1] + cov[:, 2] * e0[2]
            e0 = e0 / max(np.sqrt((e0 * e0).sum()), 1e-30)
        e0 = e0 * (1.0 if e0[np.argmax(np.abs(e0))] > 0 else -1.0)  # знак собственного вектора — один на любой машине
        e = e0[None] - t * (t * e0[None]).sum(1, keepdims=True)
        o = np.cross(t, unit(e))
    else:
        o = out_hint - t * (out_hint * t).sum(1, keepdims=True)
    for _ in range(6):
        o[1:-1] = 0.25 * o[:-2] + 0.5 * o[1:-1] + 0.25 * o[2:]
        o = o - t * (o * t).sum(1, keepdims=True)
    o = unit(o)
    if (o * (A - C.HEAD_C)).sum() < 0:   # «наружу» — от головы
        o = -o
    e = np.cross(o, t)
    return t, o, e, D


def build(g, groom, mat, hd, rng, width_scale=1.15, min_w=0.004):
    """Полосы для всех пучков причёски в вещь g (hair_core.Hair). Возвращает число полос"""
    sk = groom.sk
    count = 0
    for region, P, cid, axis, out_kind in groom.parts:
        base = cid.min()
        for c in range(len(axis)):
            M = P[cid == c + base]
            if len(M) < 3:
                continue
            A = axis[c]
            if np.linalg.norm(A[1:] - A[:-1], axis=1).sum() < 0.04:
                continue   # короткий пучок у самой завязки — торчал бы над ней
            if out_kind in ("skin", "face"):   # лежат на голове и лице — лицом от кожи
                idx, _ = sk.nearest(A, reach=0.5)
                hint = hd.Ns[np.maximum(idx, 0)]
            elif out_kind == "tail":
                # Хвост — ленты лицом назад (от спины), с наклоном по окружности хвоста: сзади и сбоку хвост
                # закрыт, а не виден ребром карточек
                # «Наружу» без перекрута на дуге хвоста: перпендикуляр к касательной в плоскости спины (вверх у
                # завязки, назад ниже) — x × t, плюс наклон по окружности хвоста
                TA, TO = groom.tail_axis, groom.tail_out
                j = np.argmin(((A[:, None, :] - TA[None]) ** 2).sum(2), 1)
                ta = np.zeros_like(A)
                ta[1:-1] = A[2:] - A[:-2]
                ta[0], ta[-1] = A[1] - A[0], A[-1] - A[-2]
                back = unit(np.cross(np.array([1.0, 0.0, 0.0]), unit(ta)))
                hint = back + 0.6 * unit(A - TA[j] + 1e-6 * TO[j])
            else:
                hint = None
            if out_kind == "tail":   # оси пучков после сбора теснее волосков: хвост на карточках не уже
                A = A + (A - TA[j]) * 0.18
            t, o, e, D = clump_frame(M, A, hint)
            de = np.abs((D * e[None]).sum(2))
            do = (D * o[None]).sum(2)
            wid = smooth1(np.percentile(de, 88, axis=0)) * 2.0 * width_scale * (1.8 if out_kind == "tail" else 1.0)
            wid = np.maximum(wid, min_w)
            thick = np.percentile(np.abs(do), 85, axis=0)
            depth = float(np.median(sk.nearest(A[len(A) // 2:len(A) // 2 + 1], reach=0.5)[1]))
            edge = out_kind == "skin" and hd.scalp(A[:1], np.ones(1, bool))[0] < 0.012
            # Слои пучка: толстый — две полосы (нижняя плотная, верхняя средняя), тонкий — одна
            layers = [(0.0, DENSE if out_kind != "face" else DENSE + MEDIUM)]
            if np.median(thick[len(A) // 3:]) > 0.22 * np.median(wid[len(A) // 3:]) and len(M) > 40:
                th = smooth1(thick)
                layers = [(-0.5, UNDER if out_kind == "tail" else DENSE), (0.6, MEDIUM)]
            for k, (shift, cols) in enumerate(layers):
                G = A + o * (shift * (thick if len(layers) == 1 else th))[:, None]
                n_rows = rows_for(G)
                Gr = resample(G[None], n_rows)[0]
                if out_kind in ("skin", "face"):   # над шапкой (hair.combed: 1–4 мм) — карточки не тонут в ней
                    root = Gr[0].copy()
                    Gr = sk.push(Gr[None], 0.0055 + 0.002 * k + (0.003 if region != "combed" else 0.0), 0.03, it=1,
                                 smooth=1)[0]
                    Gr[0] = root   # корень — на коже, под шапкой: край полосы не виден ступенькой
                tt = np.linspace(0, len(A) - 1, n_rows)
                wr = np.interp(tt, np.arange(len(A)), wid) * (1.0 if k == 0 else 0.85)
                orr = np.stack([np.interp(tt, np.arange(len(A)), o[:, j]) for j in range(3)], 1)
                u = np.linspace(0.0, 1.0, n_rows)
                if out_kind in ("skin", "face"):   # у корня полоса сходит почти в ноль: линия роста без ступеньки
                    wr = wr * (0.2 + 0.8 * ss(0.0, 0.15, u))
                elif out_kind == "tail":   # хвост сходится в завязку, а не обрублен поперёк
                    wr = wr * (0.12 + 0.88 * ss(0.0, 0.22, u))
                else:
                    wr = wr * (0.65 + 0.35 * ss(0.0, 0.06, u))
                wr = wr * (1.0 - 0.75 * ss(0.6, 1.0, u))   # кончик пучка сходит на нет, а не обрублен
                col = cols[int(rng.u() * len(cols)) % len(cols)]
                if edge and k == len(layers) - 1:   # у линии роста — редкие волоски, без сплошного края
                    col = MEDIUM[int(rng.u() * 2) % 2]
                curl = 0.06 if out_kind == "skin" else 0.18
                v0 = rng.u() * (0.15 if out_kind == "skin" else 0.45)
                g.card(Gr, wr, mat, col, out=orr, curl=curl, ao=(0.55 + 0.25 * min(depth / 0.02, 1.0), 1.0), v0=v0,
                       rigid=out_kind == "tail")
                count += 1
    return count
