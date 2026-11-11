"""Неровный бетон для построек (docs/ostov.md, «Как строится бетон»): плита с отверстиями `panel` — сколы рёбер,
волна опалубки, рваный излом с арматурой, выбоины низа — и запечённая доля неба в затенении вершин (`SkyField`).

Сдвиг узла — функция только точки: общее ребро двух граней сдвигается одинаково и щелей нет. Одно зерно — те же
байты: целочисленный хеш и арифметика double; math.hypot и ** не берём (их алгоритм зависит от версии Python).
"""
import functools
import math

from meshgen import Mesh, ihash

MATS = ["concrete", "concrete_soffit", "concrete_break", "concrete_floor", "rust_metal"]
M_WALL, M_SOFFIT, M_BREAK, M_FLOOR, M_REBAR = range(5)


# --- шум из зерна ---

@functools.lru_cache(maxsize=1 << 20)
def hash3(x, y, z, s):
    v = ihash((s * 0x165667B1 + 0x9E3779B9) & 0xFFFFFFFF)
    v = ihash(v ^ (x & 0xFFFFFFFF))
    v = ihash(v ^ (y & 0xFFFFFFFF))
    v = ihash(v ^ (z & 0xFFFFFFFF))
    return v / 4294967296.0


def smooth01(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def smoothstep(a, b, x):
    return smooth01((x - a) / (b - a))


def vnoise3(p, s):
    """Шум значений 0..1 на решётке 1."""
    fx, fy, fz = math.floor(p[0]), math.floor(p[1]), math.floor(p[2])
    ix, iy, iz = int(fx), int(fy), int(fz)
    tx, ty, tz = p[0] - fx, p[1] - fy, p[2] - fz
    tx, ty, tz = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty), tz * tz * (3 - 2 * tz)
    c = []
    for k in (0, 1):
        for j in (0, 1):
            a, b = hash3(ix, iy + j, iz + k, s), hash3(ix + 1, iy + j, iz + k, s)
            c.append(a + (b - a) * tx)
    y0 = c[0] + (c[1] - c[0]) * ty
    y1 = c[2] + (c[3] - c[2]) * ty
    return y0 + (y1 - y0) * tz


def fbm3(p, s):
    return (0.5 * vnoise3(p, s) + 0.3 * vnoise3((p[0] * 2.03, p[1] * 2.03, p[2] * 2.03), s + 1)
            + 0.2 * vnoise3((p[0] * 4.11, p[1] * 4.11, p[2] * 4.11), s + 2))


def dist2(a, b):
    """Длина вектора (a, b) без math.hypot: его алгоритм менялся между версиями Python, а байты сеток — нет."""
    return math.sqrt(a * a + b * b)


def sc(p, k):
    return (p[0] * k, p[1] * k, p[2] * k)


# --- векторы ---

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(v):
    l = math.sqrt(dot(v, v))
    return (v[0] / l, v[1] / l, v[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)


def rot_y(v, a):
    c, s = math.cos(a), math.sin(a)
    return (c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2])


def rot_x(v, a):
    c, s = math.cos(a), math.sin(a)
    return (v[0], c * v[1] - s * v[2], s * v[1] + c * v[2])


def quat_from_axes(U, V, W):
    """Кватернион поворота, который переводит оси x, y, z в U, V, W (столбцы матрицы)."""
    m00, m01, m02 = U[0], V[0], W[0]
    m10, m11, m12 = U[1], V[1], W[1]
    m20, m21, m22 = U[2], V[2], W[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s)
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        q = (0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s)
    elif m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        q = ((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s)
    else:
        s = math.sqrt(1.0 + m22 - m00 - m11) * 2
        q = ((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s)
    return q


class Part(Mesh):
    def __init__(self):
        super().__init__(MATS)

    def vert(self, p, n, mat, ao):
        self.verts.append((p[0], p[1], p[2], n[0], n[1], n[2], mat, mat, 0, int(max(0, min(255, round(ao))))))
        return len(self.verts) - 1

    def tri(self, a, b, c, out):
        pa, pb, pc = (self.verts[k][:3] for k in (a, b, c))
        if dot(cross(sub(pb, pa), sub(pc, pa)), out) < 0:
            b, c = c, b
        self.indices += [a, b, c]

# --- запечённое затенение (часть сетки, не свет: docs/ostov.md) ---

def ao_of(p, n):
    """Затенение вершины: у пола и у потолка темнее, плюс доля неба (sky_vis) — запечённая заслонка от самого
    здания: непрямого света ещё нет (этап 6г), без неё закрытое здание изнутри светится ровно, как снаружи."""
    y = p[1]
    a = 255.0
    if abs(n[1]) < 0.7:
        a *= 0.7 + 0.3 * smooth01((y - FLOOR_Y) / 1.6)
    elif n[1] < -0.7:
        a *= 0.92
    if SKY is not None:
        a *= 0.22 + 0.78 * SKY.vis(p, n)
    return a


# --- доля неба: лучи из решётки 0.5 м по сетке занятости 0.25 м (коробки коллайдеров всех частей) ---

SKY = None          # SkyField, пока строятся сетки с затенением; None — без него (сбор коробок)
FLOOR_Y = 0.0       # высота пола здания: у него стены темнее
SKY_CELL = 0.25     # клетка занятости
SKY_LAT = 0.5       # шаг решётки, где считается доля неба
SKY_RAYS = 40


def ray_dirs(n):
    """Направления по полусфере вокруг оси (n — одна из шести осей), по косинусу: золотая спираль, без случайности."""
    out = []
    ga = math.pi * (3 - math.sqrt(5))
    ax = [abs(c) > 0.5 for c in n]
    k = ax.index(True)
    sgn = 1.0 if n[k] > 0 else -1.0
    for i in range(SKY_RAYS):
        r = math.sqrt((i + 0.5) / SKY_RAYS)
        a = ga * i
        x, y = r * math.cos(a), r * math.sin(a)
        h = math.sqrt(max(0.0, 1 - r * r))
        d = [0.0, 0.0, 0.0]
        d[k] = h * sgn
        o = [j for j in range(3) if j != k]
        d[o[0]], d[o[1]] = x, y
        out.append(tuple(d))
    return out


class SkyField:
    """boxes — (центр, половины) по осям; lo, hi — углы сетки занятости (за ними препятствий нет)."""

    def __init__(self, boxes, lo, hi):
        self.lo, self.hi = lo, hi
        self.n = tuple(int(math.ceil((self.hi[k] - self.lo[k]) / SKY_CELL)) for k in range(3))
        nx, ny, nz = self.n
        self.occ = bytearray(nx * ny * nz)
        for c, h in boxes:
            i0 = [max(0, int(math.ceil((c[k] - h[k] - self.lo[k]) / SKY_CELL - 0.5))) for k in range(3)]
            i1 = [min(self.n[k] - 1, int(math.floor((c[k] + h[k] - self.lo[k]) / SKY_CELL - 0.5))) for k in range(3)]
            for i in range(i0[0], i1[0] + 1):
                for j in range(i0[1], i1[1] + 1):
                    base = (i * ny + j) * nz
                    for kk in range(i0[2], i1[2] + 1):
                        self.occ[base + kk] = 1
        self.dirs = {}
        self.lat = {}

    def solid(self, p):
        i = int(math.floor((p[0] - self.lo[0]) / SKY_CELL))
        j = int(math.floor((p[1] - self.lo[1]) / SKY_CELL))
        k = int(math.floor((p[2] - self.lo[2]) / SKY_CELL))
        if i < 0 or j < 0 or k < 0 or i >= self.n[0] or j >= self.n[1] or k >= self.n[2]:
            return None  # вне сетки — свободно и дальше препятствий нет
        return self.occ[(i * self.n[1] + j) * self.n[2] + k]

    def point(self, key):
        """Доля неба в узле решётки для оси нормали; None — узел внутри бетона."""
        if key in self.lat:
            return self.lat[key]
        i, j, k, ax = key
        p = (i * SKY_LAT, j * SKY_LAT, k * SKY_LAT)
        if self.solid(p):
            self.lat[key] = None
            return None
        if ax not in self.dirs:
            self.dirs[ax] = ray_dirs(ax)
        free = 0
        step = SKY_CELL * 0.6
        for d in self.dirs[ax]:
            t = step
            hit = False
            while True:
                q = (p[0] + d[0] * t, p[1] + d[1] * t, p[2] + d[2] * t)
                s = self.solid(q)
                if s is None:
                    break
                if s:
                    hit = True
                    break
                t += step
            if not hit:
                free += 1
        v = free / SKY_RAYS
        self.lat[key] = v
        return v

    def vis(self, p, n):
        """Доля неба для вершины: из точки на 0.3 м от поверхности, трилинейно по живым узлам решётки."""
        k = max(range(3), key=lambda a: abs(n[a]))
        ax = [0.0, 0.0, 0.0]
        ax[k] = 1.0 if n[k] > 0 else -1.0
        ax = tuple(ax)
        q = (p[0] + n[0] * 0.3, p[1] + n[1] * 0.3, p[2] + n[2] * 0.3)
        f = [c / SKY_LAT for c in q]
        b = [int(math.floor(c)) for c in f]
        t = [f[a] - b[a] for a in range(3)]
        acc, wsum = 0.0, 0.0
        for di in (0, 1):
            for dj in (0, 1):
                for dk in (0, 1):
                    w = (t[0] if di else 1 - t[0]) * (t[1] if dj else 1 - t[1]) * (t[2] if dk else 1 - t[2])
                    if w <= 0.0:
                        continue
                    v = self.point((b[0] + di, b[1] + dj, b[2] + dk, ax))
                    if v is None:
                        continue
                    acc += w * v
                    wsum += w
        return acc / wsum if wsum > 1e-6 else 0.5

# --- отверстия плиты ---

class RectHole:
    """Прямоугольное отверстие (u0, u1) × (v0, v1): дверь, щель, просвет."""
    snaps = False

    def __init__(self, u0, u1, v0, v1, broken=False):
        self.u0, self.u1, self.v0, self.v1, self.broken = u0, u1, v0, v1, broken

    def empty(self, u, v):
        return self.u0 < u < self.u1 and self.v0 < v < self.v1

    def dist(self, u, v):
        du = max(self.u0 - u, 0.0, u - self.u1)
        dv = max(self.v0 - v, 0.0, v - self.v1)
        return math.sqrt(du * du + dv * dv)

    def lines(self):
        us = [self.u0, self.u1, self.u0 - 0.07, self.u0 - 0.18, self.u1 + 0.07, self.u1 + 0.18]
        vs = [self.v0, self.v1, self.v0 - 0.07, self.v0 - 0.18, self.v1 + 0.07, self.v1 + 0.18]
        return us, vs


class CircleHole:
    """Круглое отверстие: узлы края садятся на окружность."""
    snaps = True
    broken = False

    def __init__(self, cu, cv, r):
        self.cu, self.cv, self.r = cu, cv, r

    def empty(self, u, v):
        return dist2(u - self.cu, v - self.cv) < self.r

    def snap(self, u, v):
        d = dist2(u - self.cu, v - self.cv)
        return (self.cu + (u - self.cu) * self.r / d, self.cv + (v - self.cv) * self.r / d)

    def normal(self, u, v):
        d = dist2(u - self.cu, v - self.cv)
        return ((self.cu - u) / d, (self.cv - v) / d)

    def dist(self, u, v):
        return abs(dist2(u - self.cu, v - self.cv) - self.r)

    def lines(self):
        return [], []


class RaggedHole:
    """Пролом: эллипс с рваным краем по зерну. Край — излом (concrete_break), к нему плита тоньше."""
    snaps = True
    broken = True

    def __init__(self, cu, cv, ru, rv, seed, amp=0.28):
        self.cu, self.cv, self.ru, self.rv, self.seed, self.amp = cu, cv, ru, rv, seed, amp

    def g(self, u, v):
        k = 1 + self.amp * (2 * vnoise3((u * 0.7, 0.0, v * 0.7), self.seed) - 1) \
            + 0.4 * self.amp * (2 * vnoise3((u * 2.6, 3.0, v * 2.6), self.seed + 1) - 1)
        return dist2((u - self.cu) / self.ru, (v - self.cv) / self.rv) - k

    def empty(self, u, v):
        return self.g(u, v) < 0

    def snap(self, u, v):
        j = 0.07
        return (u + j * (2 * hash3(int(u * 64), int(v * 64), 1, self.seed + 5) - 1),
                v + j * (2 * hash3(int(u * 64), int(v * 64), 2, self.seed + 5) - 1))

    def normal(self, u, v):
        e = 0.05
        gu = (self.g(u + e, v) - self.g(u - e, v)) / (2 * e)
        gv = (self.g(u, v + e) - self.g(u, v - e)) / (2 * e)
        l = dist2(gu, gv) or 1.0
        return (-gu / l, -gv / l)

    def dist(self, u, v):
        return max(0.0, self.g(u, v)) * min(self.ru, self.rv)


class SlantCut:
    """Косой срез: пусто там, где u > a + b·v (контрфорс тоньше кверху)."""
    snaps = True
    broken = False

    def __init__(self, a, b):
        self.a, self.b = a, b
        self.k = math.sqrt(1 + b * b)

    def empty(self, u, v):
        return u > self.a + self.b * v

    def snap(self, u, v):
        return (self.a + self.b * v, v)

    def normal(self, u, v):
        return (1 / self.k, -self.b / self.k)

    def dist(self, u, v):
        return max(0.0, (self.a + self.b * v - u) / self.k)


# --- сетка по одной оси ---

def axis_lines(size, cell, edge_band, extra):
    """Линии сетки: края, полосы у краёв (там сколы), отверстия, равномерно в середине; близкие сливаются."""
    hard = [0.0, size] + [x for x in extra if 0.0 < x < size]
    soft = []
    for b in (0.07, 0.18, 0.32):
        if edge_band[0]:
            soft.append(b)
        if edge_band[1]:
            soft.append(size - b)
    n = max(1, int(math.ceil(size / cell)))
    soft += [size * i / n for i in range(1, n)]
    soft = [x for x in soft if 0.0 < x < size]
    out = sorted(set(hard))
    for x in sorted(soft):
        if all(abs(x - h) > min(0.05, cell * 0.3) for h in out):
            out.append(x)
            out.sort()
    return out


# --- плита с отверстиями ---

def panel(m, O, U, V, W, su, sv, T, *, holes=(), sides=None, front="chip", back="chip", cell=0.4,
          bump=0.012, chip=0.03, chip_w=0.07, brk=0.18, seed=1, mats=(M_WALL, M_WALL, M_WALL), soffit=0.0,
          taper=0.0, extra_u=(), extra_v=(), rebar=0.0, coll=True):
    """Плита su × sv × T в осях O + U·u + V·v + W·w. Лицо — w = T (front), оборот — w = 0 (back).
    sides — флаги четырёх торцов (u = 0, u = su, v = 0, v = sv): "hide" (упирается в соседнюю деталь, не строится,
    рёбра к ней не сколоты), "chip", "broken" (рваный излом с арматурой). mats — лицо, оборот, торцы.
    soffit — глубина выбоин оборота (низ плиты), taper — насколько плита тоньше у рваного края."""
    sides = sides or ("chip",) * 4
    ul = axis_lines(su, cell, (sides[0] != "hide", sides[1] != "hide"),
                    [x for h in holes if hasattr(h, "u0") for x in h.lines()[0]] + list(extra_u))
    vl = axis_lines(sv, cell, (sides[2] != "hide", sides[3] != "hide"),
                    [x for h in holes if hasattr(h, "u0") for x in h.lines()[1]] + list(extra_v))
    nu, nv = len(ul) - 1, len(vl) - 1

    def owner(u, v):
        for k, h in enumerate(holes):
            if h.empty(u, v):
                return k
        return -1

    cells = [[owner((ul[i] + ul[i + 1]) / 2, (vl[j] + vl[j + 1]) / 2) for j in range(nv)] for i in range(nu)]
    solid = lambda i, j: 0 <= i < nu and 0 <= j < nv and cells[i][j] < 0

    def to3(u, v, w):
        return (O[0] + U[0] * u + V[0] * v + W[0] * w, O[1] + U[1] * u + V[1] * v + W[1] * w,
                O[2] + U[2] * u + V[2] * v + W[2] * w)

    # Края: ребро клетки между твёрдой и пустой (или краем плиты). Ключ — (узел a, узел b), значение — (нормаль uv, вид)
    edges = []
    node_faces = {}
    for i in range(nu):
        for j in range(nv):
            if not solid(i, j):
                continue
            for di, dj, a, b, n in ((-1, 0, (i, j), (i, j + 1), (-1, 0)), (1, 0, (i + 1, j), (i + 1, j + 1), (1, 0)),
                                    (0, -1, (i, j), (i + 1, j), (0, -1)), (0, 1, (i, j + 1), (i + 1, j + 1), (0, 1))):
                ii, jj = i + di, j + dj
                if solid(ii, jj):
                    continue
                if 0 <= ii < nu and 0 <= jj < nv:
                    kind = ("hole", cells[ii][jj])
                else:
                    kind = ("side", (0 if di < 0 else 1) if di else (2 if dj < 0 else 3))
                edges.append((a, b, n, kind))
                for nd in (a, b):
                    node_faces.setdefault(nd, []).append((n, kind))

    used = set()
    for i in range(nu):
        for j in range(nv):
            if solid(i, j):
                used.update(((i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1)))

    # Положение узла в плоскости и грани торцов, на которых он лежит
    uv = {}
    faces = {}
    for nd in used:
        u, v = ul[nd[0]], vl[nd[1]]
        fl = []
        hole_snap = None
        for n, kind in node_faces.get(nd, []):
            if kind[0] == "hole" and holes[kind[1]].snaps:
                hole_snap = kind[1]
        if hole_snap is not None:
            h = holes[hole_snap]
            u, v = h.snap(u, v)
            fl.append((h.normal(u, v), ("hole", hole_snap)))
        for n, kind in node_faces.get(nd, []):
            if kind[0] == "hole" and holes[kind[1]].snaps:
                continue
            if all(not (f[0] == n and f[1] == kind) for f in fl):
                fl.append((n, kind))
        uv[nd] = (u, v)
        faces[nd] = fl

    def kind_flag(kind):
        if kind[0] == "side":
            return sides[kind[1]]
        return "broken" if holes[kind[1]].broken else "chip"

    def edge_dists(u, v):
        """Расстояния до рёбер, которые сколоты: [(вид, расстояние, излом)]."""
        out = []
        for k, (d, f) in enumerate(((u, sides[0]), (su - u, sides[1]), (v, sides[2]), (sv - v, sides[3]))):
            if f != "hide":
                out.append((("side", k), max(0.0, d), f == "broken"))
        for k, h in enumerate(holes):
            out.append((("hole", k), h.dist(u, v), h.broken))
        return out

    def chip_term(p, ds):
        spots = smoothstep(0.3, 0.75, vnoise3(sc(p, 1.6), seed + 7))
        bite = smoothstep(0.6, 0.85, vnoise3(sc(p, 0.45), seed + 9))
        k = 0.0
        for _, d, b in ds:
            cw = chip_w * (2.0 if b else 1.0)
            k = max(k, (2.5 if b else 1.0) * (1.0 - smoothstep(0.0, cw, d)))
        return chip * min(k, 1.0) * (0.2 + 0.8 * spots + 2.2 * bite) + chip * max(k - 1.0, 0.0)

    def thin(u, v):
        """Рваный край тоньше: доля taper у излома, к 1 м от него — ноль."""
        if taper <= 0:
            return 0.0
        d = min([h.dist(u, v) for h in holes if h.broken] + [9.0])
        return taper * (1 - smooth01(d / 1.0))

    cache = {}

    def pos(nd, w):
        key = (nd, w)
        if key in cache:
            return cache[key]
        u, v = uv[nd]
        p = to3(u, v, w)
        ds = edge_dists(u, v)
        du, dv, dw = 0.0, 0.0, 0.0
        if w == T and front != "hide":
            dw -= bump * (fbm3(sc(p, 0.9), seed + 11) - 0.5) * 2 + chip_term(p, ds) + thin(u, v) * 0.4
        if w == 0.0 and back != "hide":
            inb = bump * (fbm3(sc(p, 0.9), seed + 12) - 0.5) * 2 + chip_term(p, ds) + thin(u, v) * 0.6
            if soffit > 0:
                inb += soffit_depth(u, v, seed) * soffit * smooth01(min(ds_min(ds), 9.0) / 0.4)
            dw += inb
        for n, kind in faces[nd]:
            f = kind_flag(kind)
            if f == "hide":
                continue
            wd = []
            if front != "hide":
                wd.append((("f",), T - w, False))
            if back != "hide":
                wd.append((("b",), w, False))
            others = [x for x in ds if x[0] != kind] + wd
            ins = bump * (fbm3(sc(p, 0.9), seed + 13) - 0.5) * 2 + chip_term(p, others)
            if f == "broken":
                ins += brk * (0.2 + 0.8 * fbm3(sc(p, 2.3), seed + 50))
            du -= n[0] * ins
            dv -= n[1] * ins
        q = to3(u + du, v + dv, w + dw)
        cache[key] = q
        return q

    def ds_min(ds):
        return min([d for _, d, _ in ds] + [9.0])

    # Лицо и оборот: сетка узлов, нормали по сдвинутым соседям
    for w, face, mat, outw in ((T, front, mats[0], W), (0.0, back, mats[1], (-W[0], -W[1], -W[2]))):
        if face == "hide":
            continue
        vid = {}
        for nd in used:
            i, j = nd
            p = pos(nd, w)
            a = pos((i + 1, j), w) if (i + 1, j) in used else p
            b = pos((i - 1, j), w) if (i - 1, j) in used else p
            c = pos((i, j + 1), w) if (i, j + 1) in used else p
            d = pos((i, j - 1), w) if (i, j - 1) in used else p
            n = norm(cross(sub(a, b), sub(c, d)))
            if dot(n, outw) < 0:
                n = (-n[0], -n[1], -n[2])
            if dot(n, outw) < 0.3:
                n = outw
            vid[nd] = m.vert(p, n, mat, ao_of(p, n))
        for i in range(nu):
            for j in range(nv):
                if solid(i, j):
                    a, b, c, d = vid[(i, j)], vid[(i + 1, j)], vid[(i + 1, j + 1)], vid[(i, j + 1)]
                    m.tri(a, b, c, outw)
                    m.tri(a, c, d, outw)

    # Торцы: по рёбрам края, слоями по толщине
    wl = axis_lines(T, max(cell, 0.3), (back != "hide", front != "hide"), [])
    for a, b, n, kind in edges:
        f = kind_flag(kind)
        if f == "hide":
            continue
        mat = M_BREAK if f == "broken" else mats[2]
        n3 = norm(add(sc(U, n[0]), sc(V, n[1])))
        for wa, wb in zip(wl, wl[1:]):
            pts = [pos(a, wa), pos(b, wa), pos(b, wb), pos(a, wb)]
            fn = norm(cross(sub(pts[1], pts[0]), sub(pts[3], pts[0])))
            if dot(fn, n3) < 0:
                fn = (-fn[0], -fn[1], -fn[2])
            if dot(fn, n3) < 0.3:
                fn = n3
            ids = [m.vert(p, fn, mat, ao_of(p, fn)) for p in pts]
            m.tri(ids[0], ids[1], ids[2], fn)
            m.tri(ids[0], ids[2], ids[3], fn)
        # Арматура из излома: прутья вдоль нормали края, часть загнута
        if f == "broken" and rebar > 0:
            ua, va = uv[a]
            hk = (int(ua * 37), int(va * 37))
            if hash3(hk[0], hk[1], 3, seed + 60) < rebar:
                wk = T * (0.25 + 0.5 * hash3(hk[0], hk[1], 4, seed + 60))
                base = to3(ua, va, wk)
                ln = 0.15 + 0.6 * hash3(hk[0], hk[1], 5, seed + 60)
                tip = add(base, sc(n3, ln))
                bar(m, add(base, sc(n3, -0.25)), tip)
                if hash3(hk[0], hk[1], 6, seed + 60) < 0.6:
                    bend = norm((2 * hash3(hk[0], hk[1], 7, seed + 61) - 1, -1.2 * hash3(hk[0], hk[1], 8, seed + 61) - 0.2,
                                 2 * hash3(hk[0], hk[1], 9, seed + 61) - 1))
                    bar(m, tip, add(tip, sc(bend, 0.2 + 0.5 * hash3(hk[0], hk[1], 10, seed + 61))))

    if coll:
        boxes = merge_cells(nu, nv, solid)
        # Коробка в осях плиты: поворот должен быть правым, иначе оси V и W меняются местами
        swap = dot(cross(U, V), W) < 0
        q = quat_from_axes(U, W, V) if swap else quat_from_axes(U, V, W)
        axis_aligned = all(abs(c) in (0.0, 1.0) for c in U + V + W)
        for i0, i1, j0, j1 in boxes:
            u0, u1, v0, v1 = ul[i0], ul[i1], vl[j0], vl[j1]
            c = to3((u0 + u1) / 2, (v0 + v1) / 2, T / 2)
            hu, hv, hw = (u1 - u0) / 2, (v1 - v0) / 2, T / 2
            if axis_aligned:
                h = tuple(abs(U[k]) * hu + abs(V[k]) * hv + abs(W[k]) * hw for k in range(3))
                m.collider.append((c, h, (0.0, 0.0, 0.0, 1.0)))
                m.coarse.append((c, h, (0.0, 0.0, 0.0, 1.0)))
            else:
                h = (hu, hw, hv) if swap else (hu, hv, hw)
                m.collider.append((c, h, q))
                m.coarse.append((c, h, q))


def merge_cells(nu, nv, solid):
    """Твёрдые клетки → прямоугольники (жадно: ряды по u, потом одинаковые ряды подряд по v)."""
    runs = []
    for j in range(nv):
        i = 0
        while i < nu:
            if solid(i, j):
                k = i
                while k < nu and solid(k, j):
                    k += 1
                runs.append((i, k, j))
                i = k
            else:
                i += 1
    open_ = {}
    out = []
    for i0, i1, j in runs:
        key = (i0, i1)
        if key in open_ and open_[key][1] == j:
            open_[key] = (open_[key][0], j + 1)
        else:
            if key in open_:
                out.append((i0, i1, open_[key][0], open_[key][1]))
            open_[key] = (j, j + 1)
    for (i0, i1), (j0, j1) in open_.items():
        out.append((i0, i1, j0, j1))
    return sorted(out)


def soffit_depth(u, v, seed):
    """Низ плиты: бугры и выбоины (0..~1, умножается на soffit)."""
    s = 0.25 * vnoise3((u / 0.7, 1.0, v / 0.7), seed + 70) + 0.15 * vnoise3((u / 0.23, 2.0, v / 0.23), seed + 71)
    cu, cv = math.floor(u / 0.6), math.floor(v / 0.6)
    for i in (cu - 1, cu, cu + 1):
        for j in (cv - 1, cv, cv + 1):
            if hash3(i, j, 0, seed + 72) > 0.4:
                continue
            pu = (i + 0.2 + 0.6 * hash3(i, j, 1, seed + 72)) * 0.6
            pv = (j + 0.2 + 0.6 * hash3(i, j, 2, seed + 72)) * 0.6
            h = hash3(i, j, 3, seed + 72)
            r = 0.06 + 0.2 * h * h
            d2 = ((u - pu) * (u - pu) + (v - pv) * (v - pv)) / (r * r)
            if d2 < 1:
                s += (0.4 + 1.2 * hash3(i, j, 4, seed + 72)) * (1 - d2) * (1 - d2)
    return s


def bar(m, a, b, r=0.014):
    """Прут арматуры: четырёхгранник от a до b."""
    d = norm(sub(b, a))
    t = norm(cross(d, (0.0, 1.0, 0.0) if abs(d[1]) < 0.9 else (1.0, 0.0, 0.0)))
    s = cross(d, t)
    ring = [sc(t, r), sc(s, r), sc(t, -r), sc(s, -r)]
    for k in range(4):
        o0, o1 = ring[k], ring[(k + 1) % 4]
        n = norm(add(o0, o1))
        pts = [add(a, o0), add(a, o1), add(b, o1), add(b, o0)]
        ids = [m.vert(p, n, M_REBAR, 200) for p in pts]
        m.tri(ids[0], ids[1], ids[2], n)
        m.tri(ids[0], ids[2], ids[3], n)


# --- оси ---
PX, NX = (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0)
PY, NY = (0.0, 1.0, 0.0), (0.0, -1.0, 0.0)
PZ, NZ = (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)


def box(m, x0, x1, y0, y1, z0, z1, *, hide=(), seed=1, top=M_WALL, bottom=M_SOFFIT, **kw):
    """Брус по осям: лицо — верх, оборот — низ, торцы — четыре бока. hide — из "x0 x1 z0 z1 top bottom"."""
    sides = tuple("hide" if s in hide else kw.pop("side_" + s, "chip") for s in ("x0", "x1", "z0", "z1"))
    panel(m, (x0, y0, z0), PX, PZ, PY, x1 - x0, z1 - z0, y1 - y0, sides=sides,
          front="hide" if "top" in hide else "chip", back="hide" if "bottom" in hide else "chip",
          mats=(top, bottom, M_WALL), seed=seed, **kw)


def wall_x(m, x0, x1, z0, z1, y0, y1, holes=(), hide=("bottom", "top"), seed=1, **kw):
    """Стена вдоль x (лицо — +z): отверстия в координатах (x от x0, высота от y0)."""
    sides = tuple("hide" if s in hide else "chip" for s in ("x0", "x1", "bottom", "top"))
    panel(m, (x0, y0, z0), PX, PY, PZ, x1 - x0, y1 - y0, z1 - z0, holes=holes, sides=sides, seed=seed,
          extra_v=(0.25, 0.6, 1.0), **kw)


def wall_z(m, z0, z1, x0, x1, y0, y1, holes=(), hide=("bottom", "top"), seed=1, **kw):
    """Стена вдоль z (лицо — +x): отверстия в координатах (z от z0, высота от y0)."""
    sides = tuple("hide" if s in hide else "chip" for s in ("z0", "z1", "bottom", "top"))
    panel(m, (x0, y0, z0), PZ, PY, PX, z1 - z0, y1 - y0, x1 - x0, holes=holes, sides=sides, seed=seed,
          extra_v=(0.25, 0.6, 1.0), **kw)


def collar(m, cx, cz, r0, r1, y0, y1, seg=48):
    for k in range(seg):
        a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
        c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
        for r, sgn in ((r0, -1.0), (r1, 1.0)):
            n0, n1 = (sgn * c0, 0.0, sgn * s0), (sgn * c1, 0.0, sgn * s1)
            pts = [(cx + r * c0, y0, cz + r * s0), (cx + r * c1, y0, cz + r * s1),
                   (cx + r * c1, y1, cz + r * s1), (cx + r * c0, y1, cz + r * s0)]
            ns = [n0, n1, n1, n0]
            ids = [m.vert(p, n, M_WALL, ao_of(p, n)) for p, n in zip(pts, ns)]
            out = norm(add(n0, n1))
            m.tri(ids[0], ids[1], ids[2], out)
            m.tri(ids[0], ids[2], ids[3], out)
        pts = [(cx + r0 * c0, y0, cz + r0 * s0), (cx + r1 * c0, y0, cz + r1 * s0),
               (cx + r1 * c1, y0, cz + r1 * s1), (cx + r0 * c1, y0, cz + r0 * s1)]
        ids = [m.vert(p, NY, M_SOFFIT, 210) for p in pts]
        m.tri(ids[0], ids[1], ids[2], NY)
        m.tri(ids[0], ids[2], ids[3], NY)
