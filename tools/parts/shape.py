"""Гладкие рецепты без граней (правила ассетов 18.11, docs/decisions.md): скругления вместо одной фаски.

Число сегментов тела вращения — по допуску хорды (`seg_for`, ≤ 0.5 мм), кромки — дугой из нескольких сегментов
(`rbox`, `rloft`, `rcyl`), поэтому силуэт круглой детали не гранёный и блик на кромке идёт полосой, а не ступенькой.
Рецепты отдают замкнутые тела в kit.Body, как остальная фабрика (docs/parts.md). Первая машина на них — «Гридь»
(docs/gryd.md).
"""
import math

from kit import Body, add, cross, mat_vec, mul, sub, unit

CHORD = 0.0005   # допуск хорды, м


def seg_for(r, tol=CHORD, lo=8, hi=64):
    """Сегментов на полный круг радиуса r, чтобы хорда отходила от дуги не больше tol; кратно 4."""
    if r <= tol:
        return lo
    n = math.pi / math.acos(max(-1.0, 1.0 - tol / r))
    n = int(math.ceil(n / 4.0)) * 4
    return max(lo, min(hi, n))


def arc_n(r, ang, tol=CHORD, lo=2, hi=8):
    """Сегментов на дугу радиуса r и угла ang (скругление кромки): не меньше двух."""
    return max(lo, min(hi, int(math.ceil(seg_for(r, tol, 4, 256) * ang / (2 * math.pi)))))


def rrect(hw, hh, r, n):
    """Скруглённый прямоугольник (u, v) против часовой: полуширина hw, полувысота hh, радиус углов r, n сегментов
    на угол. Точек всегда 4 · (n + 1): при r = 0 точки угла совпадают (у торца их убирает loft)."""
    r = max(0.0, min(r, hw, hh))
    out = []
    for cu, cv, a0 in ((hw - r, hh - r, 0.0), (-(hw - r), hh - r, 0.5 * math.pi), (-(hw - r), -(hh - r), math.pi),
                       (hw - r, -(hh - r), 1.5 * math.pi)):
        for k in range(n + 1):
            a = a0 + 0.5 * math.pi * k / n
            out.append((cu + r * math.cos(a), cv + r * math.sin(a)))
    return out


def rpoly(pts, r, n):
    """Многоугольник (u, v) против часовой со скруглёнными углами: на каждый угол n + 1 точек дуги радиуса r
    (на остром угле радиус уменьшается, чтобы дуга уместилась в половину соседних сторон)."""
    out = []
    m = len(pts)
    for i in range(m):
        a, b, c = pts[i - 1], pts[i], pts[(i + 1) % m]
        d1 = (a[0] - b[0], a[1] - b[1])
        d2 = (c[0] - b[0], c[1] - b[1])
        l1, l2 = math.hypot(*d1), math.hypot(*d2)
        d1, d2 = (d1[0] / l1, d1[1] / l1), (d2[0] / l2, d2[1] / l2)
        ang = math.acos(max(-1.0, min(1.0, d1[0] * d2[0] + d1[1] * d2[1])))
        if ang > math.pi - 1e-4 or r <= 0:
            out += [b] * (n + 1)
            continue
        t = min(r / math.tan(ang / 2), 0.45 * l1, 0.45 * l2)
        rr = t * math.tan(ang / 2)
        p1 = (b[0] + d1[0] * t, b[1] + d1[1] * t)
        p2 = (b[0] + d2[0] * t, b[1] + d2[1] * t)
        bis = (d1[0] + d2[0], d1[1] + d2[1])
        bl = math.hypot(*bis)
        cen = (b[0] + bis[0] / bl * rr / math.sin(ang / 2), b[1] + bis[1] / bl * rr / math.sin(ang / 2))
        a1 = math.atan2(p1[1] - cen[1], p1[0] - cen[0])
        a2 = math.atan2(p2[1] - cen[1], p2[0] - cen[0])
        da = (a2 - a1 + math.pi) % (2 * math.pi) - math.pi
        for k in range(n + 1):
            a_ = a1 + da * k / n
            out.append((cen[0] + rr * math.cos(a_), cen[1] + rr * math.sin(a_)))
    return out


def _dedupe(ring, eps=1e-9):
    out = []
    for p in ring:
        if not out or max(abs(p[i] - out[-1][i]) for i in range(3)) > eps:
            out.append(p)
    while len(out) > 1 and max(abs(out[0][i] - out[-1][i]) for i in range(3)) <= eps:
        out.pop()
    return out


def loft(b, rings, mat, smooth=True):
    """Тело по сечениям (как kit.Body.loft), торцы без совпадающих точек: кольцо может сходиться в отрезок."""
    n = len(rings[0])
    faces = []
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(n):
            q = _dedupe([r0[k], r0[(k + 1) % n], r1[(k + 1) % n], r1[k]])
            if len(q) >= 3:   # сошедшиеся точки (скругление в ноль) — треугольник, а не вырожденный четырёхугольник
                faces.append((tuple(q), mat, smooth))
    for ring, flip in ((rings[0], True), (rings[-1], False)):
        cap = _dedupe(ring)
        if len(cap) >= 3:
            faces.append((tuple(reversed(cap)) if flip else tuple(cap), mat, False))
    b._closed(faces)


class Frame:
    """Оси детали: начало o, столбцы матрицы m — оси x, y, z детали в осях сетки."""

    def __init__(self, o, m=((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))):
        self.o, self.m = tuple(o), m

    @staticmethod
    def look(o, x, up=(0.0, 1.0, 0.0)):
        """Ось x — вдоль x, y — ближе всего к up."""
        x = unit(x)
        z = cross(x, up)
        if sum(v * v for v in z) < 1e-10:
            z = cross(x, (0.0, 0.0, 1.0))
        z = unit(z)
        y = cross(z, x)
        return Frame(o, ((x[0], y[0], z[0]), (x[1], y[1], z[1]), (x[2], y[2], z[2])))

    def at(self, x, y=0.0, z=0.0):
        return add(self.o, mat_vec(self.m, (x, y, z)))

    def dir(self, x, y=0.0, z=0.0):
        return mat_vec(self.m, (x, y, z))

    def sub(self, x, y=0.0, z=0.0, m=None):
        """Вложенные оси: сдвиг в осях этой детали, поворот m (в её же осях)."""
        mm = self.m if m is None else tuple(tuple(sum(self.m[i][k] * m[k][j] for k in range(3)) for j in range(3)) for i in range(3))
        return Frame(self.at(x, y, z), mm)


def band(b, outer, inner, y0, y1, mat):
    """Кольцевой пояс между двумя контурами (x, z) от y0 до y1: резец неглубокого шва вокруг тела."""
    n = len(outer)
    faces = []
    for k in range(n):
        j = (k + 1) % n
        o0, o1, i0, i1 = outer[k], outer[j], inner[k], inner[j]
        faces.append((((o0[0], y0, o0[1]), (o1[0], y0, o1[1]), (o1[0], y1, o1[1]), (o0[0], y1, o0[1])), mat, False))
        faces.append((((i1[0], y0, i1[1]), (i0[0], y0, i0[1]), (i0[0], y1, i0[1]), (i1[0], y1, i1[1])), mat, False))
        faces.append((((o1[0], y0, o1[1]), (o0[0], y0, o0[1]), (i0[0], y0, i0[1]), (i1[0], y0, i1[1])), mat, False))
        faces.append((((o0[0], y1, o0[1]), (o1[0], y1, o1[1]), (i1[0], y1, i1[1]), (i0[0], y1, i0[1])), mat, False))
    b._closed(faces)


def rbox(b, c, size, r, mat, rot=None, n=None, smooth=True):
    """Брус со скруглёнными рёбрами и углами: середина c, размеры size, радиус r, n сегментов на четверть дуги
    (по умолчанию — по допуску хорды, не меньше 2); rot — матрица поворота вокруг середины."""
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    r = max(1e-4, min(r, hx, hy, hz) * 0.999)
    n = n or arc_n(r, math.pi / 2, lo=2, hi=6)
    rings = []
    for k in range(2 * n + 2):
        if k <= n:
            t = -0.5 * math.pi + 0.5 * math.pi * k / n
            y0 = -(hy - r)
        else:
            t = 0.5 * math.pi * (k - n - 1) / n
            y0 = hy - r
        inset = r * (1.0 - math.cos(t))
        y = y0 + r * math.sin(t)
        ring = [(u, y, v) for u, v in rrect(hx - inset, hz - inset, r - inset, n)]
        rings.append(ring)
    out = Body()
    loft(out, rings, mat, smooth)
    for pts, m, s in out.faces:
        if rot is not None:
            pts = tuple(mat_vec(rot, p) for p in pts)
        b.faces.append((tuple(add(p, c) for p in pts), m, s))


def _smooth_secs(secs, tol=CHORD * 0.9):
    """Сечения балки с плавными переходами: где наклон стенки (ширина, высота, сдвиг) меняется на сечении, пролёты
    рядом делятся вставками по кубическому сплайну Эрмита. Иначе гладкая нормаль на изломе даёт прогиб хорды
    Δнаклона · длина / 8 по всему пролёту (линт `chord`, docs/assetlint.md); k вставок уменьшают его в k² раз."""
    if len(secs) < 3:
        return list(secs)
    xs = [s[0] for s in secs]
    P = (1, 2, 4)
    slope = [[(secs[i + 1][j] - secs[i][j]) / max(xs[i + 1] - xs[i], 1e-6) for j in P] for i in range(len(secs) - 1)]
    turn = [0.0] + [max(abs(a - b) for a, b in zip(slope[i - 1], slope[i])) for i in range(1, len(secs) - 1)] + [0.0]
    tan = [slope[0]] + [[(a + b) / 2 for a, b in zip(slope[i - 1], slope[i])] for i in range(1, len(secs) - 1)] + [slope[-1]]
    out = [secs[0]]
    for i in range(len(secs) - 1):
        h = xs[i + 1] - xs[i]
        k = max(1, min(8, math.ceil(math.sqrt(max(turn[i], turn[i + 1]) * h / (8 * tol)))))
        for q in range(1, k):
            t = q / k
            h00, h10, h01, h11 = 2 * t ** 3 - 3 * t ** 2 + 1, t ** 3 - 2 * t ** 2 + t, -2 * t ** 3 + 3 * t ** 2, t ** 3 - t ** 2
            v = list(secs[i])
            v[0] = xs[i] + h * t
            for n_, j in enumerate(P):
                v[j] = h00 * secs[i][j] + h10 * h * tan[i][n_] + h01 * secs[i + 1][j] + h11 * h * tan[i + 1][n_]
            v[3] = secs[i][3] + (secs[i + 1][3] - secs[i][3]) * t
            out.append(tuple(v))
        out.append(secs[i + 1])
    return out


def rloft(b, f, secs, mat, n=3, end=0.0, end_n=2, smooth=True):
    """Балка по сечениям в осях f (Frame): secs — [(x, полуширина по z, полувысота по y, радиус углов, сдвиг по y)],
    x вдоль оси детали. end — скругление торцов (м, дугой из end_n сегментов), 0 — плоские торцы. n и end_n —
    нижние границы: сверх них сегментов столько, чтобы хорда дуги не отходила больше допуска."""
    n = max(n, arc_n(max(s[3] for s in secs), math.pi / 2, lo=2, hi=8))
    secs = _smooth_secs(secs)
    end_n = max(end_n, arc_n(end, math.pi / 2, lo=2, hi=6)) if end > 0 else end_n
    def ring(x, hw, hh, r, dy, inset=0.0):
        return [f.at(x, dy + v, u) for u, v in rrect(max(hw - inset, 1e-4), max(hh - inset, 1e-4), max(r - inset, 0.0), n)]
    rings = []
    x0, x1 = secs[0][0], secs[-1][0]
    if end > 0:
        for k in range(end_n):
            t = 0.5 * math.pi * k / end_n
            s = secs[0]
            rings.append(ring(x0 + end * (1 - math.cos(t)), s[1], s[2], s[3], s[4], end * (1 - math.sin(t))))
    for s in secs:
        x = s[0]
        if end > 0 and (x <= x0 + end or x >= x1 - end):
            x = min(max(x, x0 + end), x1 - end)
        rings.append(ring(x, s[1], s[2], s[3], s[4]))
    if end > 0:
        for k in range(end_n - 1, -1, -1):
            t = 0.5 * math.pi * k / end_n
            s = secs[-1]
            rings.append(ring(x1 - end * (1 - math.cos(t)), s[1], s[2], s[3], s[4], end * (1 - math.sin(t))))
    loft(b, rings, mat, smooth)


def rcyl(b, o, axis, r, h, mat, e=None, seg=None, r1=None):
    """Цилиндр (или усечённый конус до радиуса r1) от o вдоль axis длиной h со скруглёнными кромками радиуса e."""
    r1 = r if r1 is None else r1
    e = min(e if e is not None else min(r, r1, h) * 0.12, min(r, r1) * 0.9, h * 0.45)
    seg = seg or seg_for(max(r, r1))
    prof = []
    k_n = arc_n(e, math.pi / 2, lo=2, hi=4) if e > 1e-4 else 0
    if k_n:
        for k in range(k_n + 1):
            t = 0.5 * math.pi * k / k_n
            prof.append((r - e + e * math.sin(t), e - e * math.cos(t)))
        for k in range(k_n + 1):
            t = 0.5 * math.pi * k / k_n
            prof.append((r1 - e + e * math.cos(t), h - e + e * math.sin(t)))
    else:
        prof = [(r, 0.0), (r1, h)]
    b.lathe(o, axis, prof, mat, seg=seg)


def rlathe(b, o, axis, prof, mat, seg=None):
    """Тело вращения с сегментами по хорде самого большого радиуса профиля."""
    b.lathe(o, axis, prof, mat, seg=seg or seg_for(max(p[0] for p in prof)))


def pin(b, c, axis, r, h, mat):
    """Ось сустава: стержень с фаской-дугой, выступает на h в обе стороны от c."""
    a = unit(axis)
    rcyl(b, sub(c, mul(a, h)), a, r, 2 * h, mat, e=r * 0.25)


def hexbolt(b, p, axis, r, h, mat, washer=True):
    """Болт: шестигранная головка (грани настоящие) на круглой шайбе."""
    a = unit(axis)
    if washer:
        rcyl(b, sub(p, mul(a, 0.0005)), a, r * 1.45, h * 0.3, mat, e=h * 0.1, seg=seg_for(r * 1.45, lo=12))
    # головка от листа сквозь шайбу: касается и шайбы, и листа (линт щелей, docs/assetlint.md)
    b.lathe(sub(p, mul(a, 0.0005)), a, [(r, 0.0), (r, h * 0.87), (r * 0.82, h)], mat, seg=6, smooth=False)


def helix(b, a, bb, r, wire, turns, mat, seg_turn=24, tseg=8):
    """Пружина: проволока радиуса wire по винтовой линии радиуса r от точки a к точке bb."""
    ax = sub(bb, a)
    ln = math.sqrt(sum(v * v for v in ax))
    f = Frame.look(a, ax)
    n = max(8, int(turns * seg_turn))
    pts = [f.at(ln * k / n, r * math.cos(2 * math.pi * turns * k / n), r * math.sin(2 * math.pi * turns * k / n)) for k in range(n + 1)]
    b.tube(pts, wire, mat, seg=tseg)
