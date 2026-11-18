"""Проверки формы для линта ассетов (docs/assetlint.md). Только numpy: CI ставит python3-numpy, scipy нет.

Все проверки работают по сваренным положениям: meshpack уже сварил одинаковые вершины, но вершина с другой
нормалью или материалом в .mesh остаётся отдельной. Тело — связная по общим положениям часть сетки.
"""
import numpy as np

GAP = 0.005          # щель между телами меньше этого — ошибка, м
TOUCH = 0.0002       # ближе этого — касание, м
CHORD = 0.0005       # допуск хорды на LOD 0, м
CHAMFER_LEN = 0.05   # фаска одним сегментом на кромке длиннее этого — ошибка, м
WALL = 0.5           # наружная стена тоньше этого — предупреждение, м


def weld(pos):
    """Положения → (уникальные положения, номер уникального на вершину)."""
    key = np.ascontiguousarray(pos).view(np.dtype((np.void, pos.dtype.itemsize * 3))).ravel()
    _, first, inv = np.unique(key, return_index=True, return_inverse=True)
    return pos[first], inv.ravel()


def welded_tris(pos, tris):
    """Треугольники на сваренных положениях без вырожденных: (P, T)."""
    P, w = weld(pos)
    T = w[tris]
    ok = (T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 2] != T[:, 0])
    return P, T[ok]


def components(T, n):
    """Номер тела на положение и на треугольник (связность по общим положениям)."""
    lab = np.arange(n)
    while True:
        m = lab[T].min(axis=1)
        new = lab.copy()
        for k in range(3):
            np.minimum.at(new, T[:, k], m)
        for _ in range(4):
            new = new[new]
        if np.array_equal(new, lab):
            break
        lab = new
    _, comp = np.unique(lab, return_inverse=True)
    comp = comp.ravel()
    return comp, comp[T[:, 0]]


def _edge_keys(T, n):
    a = np.concatenate([T[:, 0], T[:, 1], T[:, 2]])
    b = np.concatenate([T[:, 1], T[:, 2], T[:, 0]])
    return a, b


def open_edges(T, n):
    """Незакрытые рёбра: направленное ребро встречается не столько же раз, сколько встречное.
    Возвращает массив (k×2) концов незакрытых рёбер (без повторов)."""
    a, b = _edge_keys(T, n)
    key = a * n + b
    uk, cnt = np.unique(key, return_counts=True)
    rev = (uk % n) * n + uk // n
    pos = np.searchsorted(uk, rev)
    pos = np.minimum(pos, len(uk) - 1)
    rcnt = np.where(uk[pos] == rev, cnt[pos], 0)
    bad = uk[cnt != rcnt]
    lo = np.minimum(bad // n, bad % n)
    hi = np.maximum(bad // n, bad % n)
    und = np.unique(lo * n + hi)
    return np.stack([und // n, und % n], axis=1)


def _enc(c):
    c = c + (1 << 20)
    return (c[:, 0] << 42) | (c[:, 1] << 21) | c[:, 2]


def candidates(pts, lo, hi, h, limit=40_000_000):
    """Пары (точка, коробка): точка лежит в клетке сетки шага h, которую задевает коробка [lo, hi].
    Шаг растёт вдвое, пока клеток слишком много."""
    while True:
        clo = np.floor(lo / h).astype(np.int64)
        dims = np.floor(hi / h).astype(np.int64) - clo + 1
        cnt = dims.prod(axis=1)
        if cnt.sum() <= limit:
            break
        h *= 2
    tot = int(cnt.sum())
    owner = np.repeat(np.arange(len(lo)), cnt)
    off = np.arange(tot) - np.repeat(np.cumsum(cnt) - cnt, cnt)
    d = dims[owner]
    cell = clo[owner] + np.stack([off % d[:, 0], (off // d[:, 0]) % d[:, 1], off // (d[:, 0] * d[:, 1])], axis=1)
    key = _enc(cell)
    order = np.argsort(key, kind="stable")
    key, owner = key[order], owner[order]
    pk = _enc(np.floor(pts / h).astype(np.int64))
    left = np.searchsorted(key, pk, "left")
    n = np.searchsorted(key, pk, "right") - left
    pi = np.repeat(np.arange(len(pts)), n)
    bi = owner[np.repeat(left, n) + np.arange(int(n.sum())) - np.repeat(np.cumsum(n) - n, n)]
    return pi, bi


def closest_on_tri(p, a, b, c):
    """Ближайшие точки треугольников (a, b, c) к точкам p, построчно (Эриксон, «Real-Time Collision Detection»)."""
    def dot(x, y):
        return np.einsum("ij,ij->i", x, y)
    ab, ac, ap = b - a, c - a, p - a
    d1, d2 = dot(ab, ap), dot(ac, ap)
    bp = p - b
    d3, d4 = dot(ab, bp), dot(ac, bp)
    cp = p - c
    d5, d6 = dot(ab, cp), dot(ac, cp)
    va, vb, vc = d3 * d6 - d5 * d4, d5 * d2 - d1 * d6, d1 * d4 - d3 * d2
    with np.errstate(divide="ignore", invalid="ignore"):
        den = va + vb + vc
        out = a + ab * (vb / den)[:, None] + ac * (vc / den)[:, None]
        m = (va <= 0) & (d4 - d3 >= 0) & (d5 - d6 >= 0)
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        out[m] = (b + (c - b) * w[:, None])[m]
        m = (vb <= 0) & (d2 >= 0) & (d6 <= 0)
        w = d2 / (d2 - d6)
        out[m] = (a + ac * w[:, None])[m]
        m = (d6 >= 0) & (d5 <= d6)
        out[m] = c[m]
        m = (vc <= 0) & (d1 >= 0) & (d3 <= 0)
        v = d1 / (d1 - d3)
        out[m] = (a + ab * v[:, None])[m]
        m = (d3 >= 0) & (d4 <= d3)
        out[m] = b[m]
        m = (d1 <= 0) & (d2 <= 0)
        out[m] = a[m]
    bad = ~np.isfinite(out).all(axis=1)
    out[bad] = a[bad]
    return out


def gaps(P, T, vcomp, tcomp, chunk=4_000_000):
    """Щели между телами: пара тел, которые подходят друг к другу ближе GAP, но нигде не касаются и не входят
    друг в друга. Касание — точка ближе TOUCH к другому телу или внутри него (по нормали ближайшего треугольника).
    Возвращает [(расстояние, точка, тело A, тело B)] по возрастанию расстояния."""
    ncomp = int(vcomp.max()) + 1 if len(vcomp) else 0
    if ncomp < 2:
        return []
    A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    lo = np.minimum(np.minimum(A, B), C) - GAP
    hi = np.maximum(np.maximum(A, B), C) + GAP
    N = np.cross(B - A, C - A)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-30)
    pi, ti = candidates(P, lo, hi, 0.02)
    keep = vcomp[pi] != tcomp[ti]
    pi, ti = pi[keep], ti[keep]
    best = {}
    touch = set()
    for s in range(0, len(pi), chunk):
        p, t = pi[s:s + chunk], ti[s:s + chunk]
        q = closest_on_tri(P[p], A[t], B[t], C[t])
        dv = P[p] - q
        d = np.linalg.norm(dv, axis=1)
        side = np.einsum("ij,ij->i", dv, N[t])
        ca, cb = vcomp[p], tcomp[t]
        pair = np.minimum(ca, cb) * ncomp + np.maximum(ca, cb)
        # Ближайший треугольник другого тела для каждой точки: по нему решается «снаружи или внутри»
        key = p * ncomp + cb
        order = np.lexsort((d, key))
        key, d, side, pair, p = key[order], d[order], side[order], pair[order], p[order]
        first = np.ones(len(key), bool)
        first[1:] = key[1:] != key[:-1]
        d, side, pair, p = d[first], side[first], pair[first], p[first]
        con = (d < TOUCH) | ((d < GAP) & (side < 0))
        touch.update(np.unique(pair[con]).tolist())
        near = (d < GAP) & ~con
        for pr, dd, pp in zip(pair[near].tolist(), d[near].tolist(), p[near].tolist()):
            if pr not in best or dd < best[pr][0]:
                best[pr] = (dd, pp)
    out = []
    for pr, (dd, pp) in best.items():
        if pr in touch:
            continue
        out.append((dd, tuple(P[pp]), pr // ncomp, pr % ncomp))
    out.sort()
    return out


def chord(pos, nrm, tris):
    """Отклонение хорды гладких поверхностей LOD 0: по ребру с нормалями na, nb в концах кривизна вдоль ребра
    κ ≈ |(nb − na)·e| / |e|², прогиб дуги над хордой ≈ κ|e|²/8 (с поправкой 1/cos²(φ/4) — точно для круга).
    Рёбра с изломом нормали круче 61° — не гладкие (фаска со взвешенными нормалями), их не считаем.
    Запас 10 % — на оценку по усреднённым нормалям вершин. Считаются положения вершин сетки как есть: вершины с разными нормалями в одной точке — разные (резкая кромка).
    Возвращает (прогиб на ребро, середина ребра) только для рёбер выше допуска, по убыванию."""
    # у треугольника берутся два коротких ребра: длинное — диагональ четырёхугольника, на поверхности двойной
    # кривизны она складывает прогибы обоих направлений, а допуск — на хорду круга
    i = np.stack([tris[:, 0], tris[:, 1], tris[:, 2]], axis=1)
    j = np.stack([tris[:, 1], tris[:, 2], tris[:, 0]], axis=1)
    ln = np.linalg.norm(pos[j] - pos[i], axis=2)
    keep = np.argsort(ln, axis=1, kind="stable")[:, :2]
    i = np.take_along_axis(i, keep, axis=1).ravel()
    j = np.take_along_axis(j, keep, axis=1).ravel()
    lo, hi = np.minimum(i, j), np.maximum(i, j)
    u = np.unique(lo * len(pos) + hi)
    i, j = u // len(pos), u % len(pos)
    e = pos[j] - pos[i]
    cphi = np.clip(np.einsum("ij,ij->i", nrm[i], nrm[j]), -1.0, 1.0)
    phi = np.arccos(cphi)
    s = np.abs(np.einsum("ij,ij->i", nrm[j] - nrm[i], e)) / (8.0 * np.cos(phi / 4) ** 2)
    # Ребро от резкой кромки (нормаль в одном конце ровно поперёк ребра: край группы сглаживания, плечо
    # профиля) — переход блика, а не хорда дуги: гладкая дуга сглажена с обеих сторон
    eh = e / np.maximum(np.linalg.norm(e, axis=1, keepdims=True), 1e-30)
    edge = np.minimum(np.abs(np.einsum("ij,ij->i", nrm[i], eh)), np.abs(np.einsum("ij,ij->i", nrm[j], eh))) < 2e-3
    bad = (phi <= np.radians(61.0)) & (s > CHORD * 1.1) & ~edge
    order = np.argsort(-s[bad], kind="stable")
    mid = ((pos[i] + pos[j]) / 2)[bad][order]
    return s[bad][order], mid


def chamfers(P, T):
    """Фаски одним сегментом: узкая плоская полоса (≤ 3 см) длиннее CHAMFER_LEN, по обоим длинным краям которой
    нормаль ломается на 35–65° к грани вчетверо больше полосы (прямой угол, снятый одной гранью; скругление из
    2+ отрезков ломается мельче).
    Возвращает [(длина, середина)] по убыванию длины."""
    if len(T) == 0:
        return []
    n = len(P)
    A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    N = np.cross(B - A, C - A)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-30)
    a, b = _edge_keys(T, n)
    tri = np.concatenate([np.arange(len(T))] * 3)
    key = np.minimum(a, b) * n + np.maximum(a, b)
    order = np.argsort(key, kind="stable")
    key, tri, a, b = key[order], tri[order], a[order], b[order]
    # ровно два треугольника на ребре — соседи
    _, first, cnt = np.unique(key, return_index=True, return_counts=True)
    k = first[cnt == 2]
    t1, t2 = tri[k], tri[k + 1]
    ln = np.linalg.norm(P[a[k]] - P[b[k]], axis=1)
    cosd = np.einsum("ij,ij->i", N[t1], N[t2])
    flat = cosd > 0.99999
    # плоские участки: связность треугольников по рёбрам без излома
    lab = np.arange(len(T))
    f1, f2 = t1[flat], t2[flat]
    while True:
        m = np.minimum(lab[f1], lab[f2])
        new = lab.copy()
        np.minimum.at(new, f1, m)
        np.minimum.at(new, f2, m)
        for _ in range(4):
            new = new[new]
        if np.array_equal(new, lab):
            break
        lab = new
    ang = np.degrees(np.arccos(np.clip(cosd, -1, 1)))
    rng = (ang >= 35.0) & (ang <= 65.0)
    area = np.zeros(len(T))
    np.add.at(area, lab, np.linalg.norm(np.cross(B - A, C - A), axis=1) / 2)
    # Фаска — полоса между двумя крупными гранями: сосед по излому вчетверо больше по площади. Грань восьмигранного
    # прутка (соседи такие же полосы) — не фаска, её проверяет хорда
    big1 = area[lab[t2]] >= 4 * area[lab[t1]]
    big2 = area[lab[t1]] >= 4 * area[lab[t2]]
    crease = np.zeros(len(T))
    np.add.at(crease, lab[t1[rng & big1]], ln[rng & big1])
    np.add.at(crease, lab[t2[rng & big2]], ln[rng & big2])
    out = []
    for r in np.nonzero(crease >= 2 * CHAMFER_LEN)[0]:
        ts = np.nonzero(lab == r)[0]
        v = P[np.unique(T[ts])]
        c = v.mean(axis=0)
        nr = N[r]
        q = v - c
        q -= np.outer(q @ nr, nr)
        w, vec = np.linalg.eigh(q.T @ q)
        ax1, ax2 = vec[:, 2], vec[:, 1]
        length = np.ptp(q @ ax1)
        width = np.ptp(q @ ax2)
        if width <= 0.03 and length >= CHAMFER_LEN and crease[r] >= 1.5 * length:
            out.append((float(length), tuple(c)))
    out.sort(key=lambda x: -x[0])
    return out


def thin_walls(P, T, h=0.1):
    """Тонкие наружные стены: сетка заполняется по клеткам h (чётность пересечений по вертикали), снаружи —
    пустые клетки, видные с края сетки вдоль одной из осей, внутри — остальные пустые. Стена — отрезок заполненных
    клеток вдоль оси между наружной и внутренней пустотой; тоньше WALL — место в списке.
    Возвращает [(толщина, середина)]."""
    lo = P.min(axis=0) - 2 * h
    hi = P.max(axis=0) + 2 * h
    dims = np.ceil((hi - lo) / h).astype(np.int64)
    if dims.prod() > 80_000_000:
        h = float(h * (dims.prod() / 80_000_000) ** (1 / 3))
        dims = np.ceil((hi - lo) / h).astype(np.int64)
    nx, ny, nz = (int(d) for d in dims)
    jit = 0.0137  # центры клеток чуть сдвинуты: луч не попадает в ребро и вершину ровно
    A, B, C = P[T[:, 0]], P[T[:, 1]], P[T[:, 2]]
    xz = lambda v: v[:, [0, 2]]
    ta, tb, tc = xz(A), xz(B), xz(C)
    clo = np.floor((np.minimum(np.minimum(ta, tb), tc) - lo[[0, 2]]) / h - 0.5 - jit).astype(np.int64) + 1
    chi = np.floor((np.maximum(np.maximum(ta, tb), tc) - lo[[0, 2]]) / h - 0.5 - jit).astype(np.int64)
    d = np.maximum(chi - clo + 1, 0)
    cnt = d[:, 0] * d[:, 1]
    owner = np.repeat(np.arange(len(T)), cnt)
    off = np.arange(int(cnt.sum())) - np.repeat(np.cumsum(cnt) - cnt, cnt)
    ix = clo[owner, 0] + off % np.maximum(d[owner, 0], 1)
    iz = clo[owner, 1] + off // np.maximum(d[owner, 0], 1)
    px = lo[0] + (ix + 0.5 + jit) * h
    pz = lo[2] + (iz + 0.5 + jit) * h
    a, b, c = ta[owner], tb[owner], tc[owner]
    den = (b[:, 1] - c[:, 1]) * (a[:, 0] - c[:, 0]) + (c[:, 0] - b[:, 0]) * (a[:, 1] - c[:, 1])
    with np.errstate(divide="ignore", invalid="ignore"):
        l1 = ((b[:, 1] - c[:, 1]) * (px - c[:, 0]) + (c[:, 0] - b[:, 0]) * (pz - c[:, 1])) / den
        l2 = ((c[:, 1] - a[:, 1]) * (px - c[:, 0]) + (a[:, 0] - c[:, 0]) * (pz - c[:, 1])) / den
        l3 = 1 - l1 - l2
        ins = (den != 0) & (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
        y = l1 * A[owner, 1] + l2 * B[owner, 1] + l3 * C[owner, 1]
    col = (ix * nz + iz)[ins]
    y = y[ins]
    order = np.lexsort((y, col))
    col, y = col[order], y[order]
    occ = np.zeros((nx, ny, nz), bool)
    start = np.ones(len(col), bool)
    start[1:] = col[1:] != col[:-1]
    rank = np.arange(len(col)) - np.maximum.accumulate(np.where(start, np.arange(len(col)), 0))
    # пары пересечений (чётное, нечётное) одной колонки — внутри тела
    ev = np.nonzero((rank % 2 == 0))[0]
    ev = ev[(ev + 1 < len(col))]
    ev = ev[col[ev + 1] == col[ev]]
    y0 = np.ceil((y[ev] - lo[1]) / h - 0.5 - jit).astype(np.int64)
    y1 = np.floor((y[ev + 1] - lo[1]) / h - 0.5 - jit).astype(np.int64)
    m = np.maximum(y1 - y0 + 1, 0)
    cc = np.repeat(col[ev], m)
    yy = np.repeat(y0, m) + np.arange(int(m.sum())) - np.repeat(np.cumsum(m) - m, m)
    occ[cc // nz, np.clip(yy, 0, ny - 1), cc % nz] = True
    # снаружи: пустые клетки, видные с края вдоль оси
    ext = np.zeros_like(occ)
    for ax in range(3):
        o = np.moveaxis(occ, ax, 0)
        e = np.moveaxis(ext, ax, 0)
        e |= np.cumsum(o, axis=0) == 0
        e |= np.cumsum(o[::-1], axis=0)[::-1] == 0
    out = []
    for ax in range(3):
        o = np.moveaxis(occ, ax, -1)
        e = np.moveaxis(ext, ax, -1)
        sh = o.shape
        o2 = o.reshape(-1, sh[-1])
        e2 = e.reshape(-1, sh[-1])
        pad = np.zeros((o2.shape[0], 1), bool)
        op = np.concatenate([pad, o2, pad], axis=1)
        st = np.nonzero(op[:, 1:-1] & ~op[:, :-2])
        en = np.nonzero(op[:, 1:-1] & ~op[:, 2:])
        ln = en[1] - st[1] + 1
        line = st[0]
        bef, aft = st[1] - 1, en[1] + 1
        okb = (bef >= 0) & (aft < sh[-1])
        line, s0, s1, ln = line[okb], st[1][okb], en[1][okb], ln[okb]
        eb = e2[line, s0 - 1]
        ea = e2[line, s1 + 1]
        thin = (ln * h < WALL) & (eb != ea)
        for li, a0, a1 in zip(line[thin].tolist(), s0[thin].tolist(), s1[thin].tolist()):
            idx = list(np.unravel_index(li, sh[:-1]))
            idx.insert(ax, (a0 + a1) / 2)
            out.append(((a1 - a0 + 1) * h, tuple(lo + (np.array(idx, float) + 0.5 + jit) * h)))
    return out
