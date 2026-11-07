"""Детали бетонной руины этапа 4 (docs/ruin.md): стены с опалубкой, контрфорс, колонны, плиты (целая, с круглым
отверстием, с рваным краем), обломанные балки, лестничный марш, стена с дверным проёмом.

Шаг 1 м, этаж 4 м. Начало координат детали — середина низа (у балки — точка крепления, у марша — его подножие).
Узор бетона (щиты опалубки, анкерные отверстия, пятна) рисует шейдер материала по миру; здесь только форма:
бугристый низ плит с выбоинами и сколами, излом из зерна, запечённое затенение в вершинах. Одно зерно — те же
байты: только целочисленный хеш и арифметика double, без случайности.

Материалы вершин: concrete (стены, колонны, торцы), concrete_soffit (низ плит), concrete_break (излом),
concrete_floor (верх плит и цоколя — пол).
"""
import math

from meshgen import Mesh, ihash

FLOOR = 4.0     # этаж: от пола до пола
SLAB_T = 0.5    # толщина плиты
WALL_H = FLOOR - SLAB_T  # стены и колонны — от верха плиты до низа следующей
WALL_T = 0.4    # толщина стены
MAT_WALL, MAT_SOFFIT, MAT_BREAK, MAT_FLOOR = 0, 1, 2, 3
MATS = ["concrete", "concrete_soffit", "concrete_break", "concrete_floor"]


# --- шум из зерна ---

def h01(seed, *k):
    """Хеш целых → [0, 1)."""
    v = ihash(seed & 0xFFFFFFFF)
    for x in k:
        v = ihash((v ^ (x & 0xFFFFFFFF)) & 0xFFFFFFFF)
    return v / 4294967296.0


def vnoise(x, z, seed):
    """Шум значений на решётке 1, сглаженная билинейная смесь, [-1, 1]."""
    ix, iz = math.floor(x), math.floor(z)
    fx, fz = x - ix, z - iz
    fx, fz = fx * fx * (3 - 2 * fx), fz * fz * (3 - 2 * fz)
    a, b = h01(seed, ix, iz), h01(seed, ix + 1, iz)
    c, d = h01(seed, ix, iz + 1), h01(seed, ix + 1, iz + 1)
    return (a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz) * 2 - 1


def smooth01(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


# --- геометрия ---

def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def norm(v):
    l = math.sqrt(dot(v, v))
    return (v[0] / l, v[1] / l, v[2] / l) if l > 1e-12 else (0.0, 1.0, 0.0)


def newell(pts):
    n = [0.0, 0.0, 0.0]
    for i, a in enumerate(pts):
        b = pts[(i + 1) % len(pts)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return norm(n)


class Part(Mesh):
    """Mesh с гранями по подсказке «куда наружу»: порядок обхода выбирается сам."""

    def __init__(self):
        super().__init__(MATS)

    def vert(self, p, n, mat, ao):
        self.verts.append((p[0], p[1], p[2], n[0], n[1], n[2], mat, mat, 0, int(max(0, min(255, round(ao))))))
        return len(self.verts) - 1

    def poly(self, pts, out, mat, ao=lambda p: 255):
        """Плоский выпуклый многоугольник; out — вектор наружу (по нему выбирается обход)."""
        n = newell(pts)
        if dot(n, out) < 0:
            pts = pts[::-1]
            n = (-n[0], -n[1], -n[2])
        base = [self.vert(p, n, mat, ao(p)) for p in pts]
        for i in range(1, len(base) - 1):
            self.indices += [base[0], base[i], base[i + 1]]

    def facet(self, a, b, c, out, mat, ao=lambda p: 255):
        """Треугольник с плоской нормалью (излом)."""
        self.poly([a, b, c], out, mat, ao)

    def grid(self, P, N, out, mat, ao):
        """Гладкая поверхность: P[i][j] — точки, N[i][j] — нормали (наружу), out(i, j) — подсказка обхода."""
        idx = [[self.vert(P[i][j], N[i][j], mat, ao(i, j)) for j in range(len(P[0]))] for i in range(len(P))]
        for i in range(len(P) - 1):
            for j in range(len(P[0]) - 1):
                a, b, c, d = idx[i][j], idx[i + 1][j], idx[i + 1][j + 1], idx[i][j + 1]
                for t in ((a, b, c), (a, c, d)):
                    pa, pb, pc = (self.verts[k][:3] for k in t)
                    n = cross(sub(pb, pa), sub(pc, pa))
                    if dot(n, N[i][j]) < 0:
                        t = (t[0], t[2], t[1])
                    self.indices += list(t)

    def boxes(self, items, coarse=True):
        """Коробки коллайдера (и грубой формы): (центр, половины[, кватернион])."""
        for it in items:
            c, h = it[0], it[1]
            q = it[2] if len(it) > 2 else (0.0, 0.0, 0.0, 1.0)
            b = (tuple(c), tuple(h), tuple(q))
            self.collider.append(b)
            if coarse:
                self.coarse.append(b)


def ao_wall(y, low=150.0):
    """Затенение у пола: темнее в нижних 1.5 м."""
    return low + (255.0 - low) * smooth01(y / 1.5)


def levels(h, extra=()):
    """Деления по высоте для градиента затенения у пола."""
    ys = sorted(set([0.0, 0.25, 0.6, 1.0, 1.5] + list(extra) + [h]))
    return [y for y in ys if y <= h]


def slab_side(m, x0, z0, x1, z1, y0, y1, out, mat=MAT_WALL):
    m.poly([(x0, y0, z0), (x1, y0, z1), (x1, y1, z1), (x0, y1, z0)], out, mat)


def prism(m, x0, x1, z0, z1, y0, y1, ao=ao_wall, top=True, bottom=False, sides=(True, True, True, True), top_mat=MAT_WALL):
    """Коробка, бока поделены по высоте под градиент затенения."""
    ys = [y for y in levels(y1) if y >= y0] if y0 == 0 else [y0, y1]
    if ys[-1] != y1:
        ys.append(y1)
    walls = [((x1, z0), (x1, z1), (1, 0, 0)), ((x0, z1), (x0, z0), (-1, 0, 0)),
             ((x1, z1), (x0, z1), (0, 0, 1)), ((x0, z0), (x1, z0), (0, 0, -1))]
    for k, (a, b, out) in enumerate(walls):
        if not sides[k]:
            continue
        for ya, yb in zip(ys, ys[1:]):
            m.poly([(a[0], ya, a[1]), (b[0], ya, b[1]), (b[0], yb, b[1]), (a[0], yb, a[1])], out, MAT_WALL,
                   lambda p: ao(p[1]))
    if top:
        m.poly([(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], (0, 1, 0), top_mat)
    if bottom:
        m.poly([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0), MAT_WALL)


# --- стены ---

def wall(length, height=WALL_H):
    """Монолитная стена length × height × 0.4 м вдоль x. Щиты опалубки и анкеры — узор материала."""
    m = Part()
    hl, ht = length / 2, WALL_T / 2
    prism(m, -hl, hl, -ht, ht, 0.0, height)
    m.boxes([((0, height / 2, 0), (hl, height / 2, ht))])
    return m


def wall_broken(length, seed, height=WALL_H):
    """Стена с обломанным верхом: край рваный по зерну, верх излома — материал concrete_break."""
    m = Part()
    hl, ht = length / 2, WALL_T / 2
    n = int(length / 0.25)
    xs = [-hl + length * i / n for i in range(n + 1)]
    # Профиль облома: две-три крупные выемки и мелкая зубчатость
    def top(x, side):
        k = (x + hl) / length
        big = 0.5 + 0.5 * vnoise(k * 2.3, 0.5, seed)
        drop = 0.4 + 2.2 * big * big
        jag = 0.12 * vnoise(x * 4.0, side * 7.0, seed + 1) + 0.06 * (h01(seed + 2, int(x * 8), side) - 0.5)
        end = smooth01(min(x + hl, hl - x) / 0.6)  # у торцов стена почти целая: стык с соседней деталью
        return height - (drop + jag) * end - (1 - end) * 0.15 * big
    tf = [top(x, 0) for x in xs]
    tb = [top(x, 1) for x in xs]
    for side, zs, tops, out in ((0, ht, tf, (0, 0, 1)), (1, -ht, tb, (0, 0, -1))):
        for i in range(n):
            x0, x1 = xs[i], xs[i + 1]
            lo = min(tops[i], tops[i + 1])
            ys = [y for y in levels(lo) if y < lo] + [lo]
            for ya, yb in zip(ys, ys[1:]):
                m.poly([(x0, ya, zs), (x1, ya, zs), (x1, yb, zs), (x0, yb, zs)], out, MAT_WALL, lambda p: ao_wall(p[1]))
            # Зубец сверху до рваного края
            m.poly([(x0, lo, zs), (x1, lo, zs), (x1, tops[i + 1], zs), (x0, tops[i], zs)], out, MAT_WALL)
    for i in range(n):
        x0, x1 = xs[i], xs[i + 1]
        a, b = (x0, tf[i], ht), (x1, tf[i + 1], ht)
        c, d = (x1, tb[i + 1], -ht), (x0, tb[i], -ht)
        m.facet(a, b, c, (0, 1, 0), MAT_BREAK)
        m.facet(a, c, d, (0, 1, 0), MAT_BREAK)
    for x, i, out in ((-hl, 0, (-1, 0, 0)), (hl, n, (1, 0, 0))):
        m.poly([(x, 0, -ht), (x, 0, ht), (x, tf[i], ht), (x, tb[i], -ht)], out, MAT_WALL)
    lo = min(tf + tb)
    m.boxes([((0, lo / 2, 0), (hl, lo / 2, ht))])
    # Над целой частью — коробки по метру до средней высоты облома
    for i in range(int(length)):
        k0, k1 = i * 4, i * 4 + 4
        hmid = sum(tf[k0:k1 + 1] + tb[k0:k1 + 1]) / (2 * (k1 - k0 + 1))
        if hmid > lo + 0.05:
            m.boxes([((-hl + i + 0.5, (lo + hmid) / 2, 0), (0.5, (hmid - lo) / 2, ht))])
    return m


def wall_door(length=4.0, height=WALL_H, door_w=1.2, door_h=2.4, depth=1.4):
    """Стена с дверным проёмом и тёмной нишей за ним (пристройка на обратной стороне, −z)."""
    m = Part()
    hl, ht, hw = length / 2, WALL_T / 2, door_w / 2
    ann = 0.3  # толщина стенок пристройки
    zb = -ht - depth  # задняя стенка ниши изнутри
    # Лицо стены (+z): слева, справа, над проёмом
    for x0, x1, y0 in ((-hl, -hw, 0.0), (hw, hl, 0.0), (-hw, hw, door_h)):
        ys = [y for y in levels(height) if y >= y0] if y0 == 0 else [door_h, height]
        for ya, yb in zip(ys, ys[1:]):
            m.poly([(x0, ya, ht), (x1, ya, ht), (x1, yb, ht), (x0, yb, ht)], (0, 0, 1), MAT_WALL, lambda p: ao_wall(p[1]))
    # Оборот стены (−z) вокруг пристройки
    xa = hw + ann
    for x0, x1, y0 in ((-hl, -xa, 0.0), (xa, hl, 0.0), (-xa, xa, door_h + ann)):
        ys = [y for y in levels(height) if y >= y0] if y0 == 0 else [door_h + ann, height]
        for ya, yb in zip(ys, ys[1:]):
            m.poly([(x0, ya, -ht), (x1, ya, -ht), (x1, yb, -ht), (x0, yb, -ht)], (0, 0, -1), MAT_WALL, lambda p: ao_wall(p[1]))
    m.poly([(-hl, height, -ht), (hl, height, -ht), (hl, height, ht), (-hl, height, ht)], (0, 1, 0), MAT_WALL)
    for x, out in ((-hl, (-1, 0, 0)), (hl, (1, 0, 0))):
        m.poly([(x, 0, -ht), (x, 0, ht), (x, height, ht), (x, height, -ht)], out, MAT_WALL)
    # Пристройка снаружи
    zo = zb - ann
    for x, out in ((-xa, (-1, 0, 0)), (xa, (1, 0, 0))):
        m.poly([(x, 0, zo), (x, 0, -ht), (x, door_h + ann, -ht), (x, door_h + ann, zo)], out, MAT_WALL, lambda p: ao_wall(p[1]))
    m.poly([(-xa, 0, zo), (xa, 0, zo), (xa, door_h + ann, zo), (-xa, door_h + ann, zo)], (0, 0, -1), MAT_WALL,
           lambda p: ao_wall(p[1]))
    m.poly([(-xa, door_h + ann, zo), (xa, door_h + ann, zo), (xa, door_h + ann, -ht), (-xa, door_h + ann, -ht)],
           (0, 1, 0), MAT_WALL)
    # Ниша изнутри: темнее вглубь
    def ao_in(p):
        k = smooth01((ht - p[2]) / (ht - zb))
        return (230 - 190 * k) * (0.75 + 0.25 * smooth01(p[1] / 1.0))
    zs = [ht, -ht, (-ht + zb) / 2, zb]
    for za, zc in zip(zs, zs[1:]):
        m.poly([(-hw, 0, za), (-hw, 0, zc), (-hw, door_h, zc), (-hw, door_h, za)], (1, 0, 0), MAT_WALL, ao_in)
        m.poly([(hw, 0, za), (hw, 0, zc), (hw, door_h, zc), (hw, door_h, za)], (-1, 0, 0), MAT_WALL, ao_in)
        m.poly([(-hw, door_h, za), (hw, door_h, za), (hw, door_h, zc), (-hw, door_h, zc)], (0, -1, 0), MAT_WALL, ao_in)
    m.poly([(-hw, 0, zb), (hw, 0, zb), (hw, door_h, zb), (-hw, door_h, zb)], (0, 0, 1), MAT_WALL, ao_in)
    m.boxes([((-(hl + xa) / 2, height / 2, 0), ((hl - xa) / 2, height / 2, ht)),
             (((hl + xa) / 2, height / 2, 0), ((hl - xa) / 2, height / 2, ht)),
             ((0, (door_h + height) / 2, 0), (xa, (height - door_h) / 2, ht)),
             ((0, (door_h + ann) / 2, (zo - ht) / 2), (xa, (door_h + ann) / 2, (-ht - zo) / 2))], coarse=False)
    # Свет внутрь ниши не нужен: грубая форма — стена целиком и пристройка
    m.coarse += [((0, height / 2, 0), (hl, height / 2, ht), (0, 0, 0, 1)),
                 ((0, (door_h + ann) / 2, (zo - ht) / 2), (xa, (door_h + ann) / 2, (-ht - zo) / 2), (0, 0, 0, 1))]
    return m


def buttress(width=2.0, depth0=1.8, depth1=0.35, height=WALL_H):
    """Наклонный контрфорс у стены: клин от стены (z = 0) в +z, у пола глубже, наверху тоньше."""
    m = Part()
    hw = width / 2
    ys = levels(height, (2.0, 3.0))
    d = lambda y: depth0 + (depth1 - depth0) * y / height
    for ya, yb in zip(ys, ys[1:]):
        m.poly([(-hw, ya, d(ya)), (hw, ya, d(ya)), (hw, yb, d(yb)), (-hw, yb, d(yb))], (0, 0.4, 1), MAT_WALL,
               lambda p: ao_wall(p[1]))
        for x, out in ((-hw, (-1, 0, 0)), (hw, (1, 0, 0))):
            m.poly([(x, ya, 0), (x, ya, d(ya)), (x, yb, d(yb)), (x, yb, 0)], out, MAT_WALL, lambda p: ao_wall(p[1]))
    m.poly([(-hw, height, 0), (hw, height, 0), (hw, height, depth1), (-hw, height, depth1)], (0, 1, 0), MAT_WALL)
    m.poly([(-hw, 0, 0), (hw, 0, 0), (hw, height, 0), (-hw, height, 0)], (0, 0, -1), MAT_WALL)
    bx = []
    for k in range(4):
        y0, y1 = height * k / 4, height * (k + 1) / 4
        dd = (d(y0) + d(y1)) / 2
        bx.append(((0, (y0 + y1) / 2, dd / 2), (hw, (y1 - y0) / 2, dd / 2)))
    m.boxes(bx)
    return m


def floor_tile(w=4.0, d=4.0, t=SLAB_T):
    """Плита пола на земле (цоколь): низа не видно, поэтому он ровный и без выбоин."""
    m = Part()
    prism(m, -w / 2, w / 2, -d / 2, d / 2, 0.0, t, ao=lambda y: 205 + 50 * y / t, top_mat=MAT_FLOOR)
    m.boxes([((0, t / 2, 0), (w / 2, t / 2, d / 2))])
    return m


def girder(length=4.0, b=0.4, h=0.6):
    """Балка под плитой вдоль x: верх на y = 0 (низ плиты), висит вниз на h."""
    m = Part()
    prism(m, -length / 2, length / 2, -b / 2, b / 2, -h, 0.0, ao=lambda y: 200, top=False, bottom=True)
    m.boxes([((0, -h / 2, 0), (length / 2, h / 2, b / 2))])
    return m


def column_rect(sx=0.7, sz=1.1, height=WALL_H):
    m = Part()
    prism(m, -sx / 2, sx / 2, -sz / 2, sz / 2, 0.0, height)
    m.boxes([((0, height / 2, 0), (sx / 2, height / 2, sz / 2))])
    return m


def column_round(r=0.2, height=WALL_H, seg=20, cap=0.56, cap_h=0.16):
    """Тонкая круглая колонна с квадратной подушкой под плитой."""
    m = Part()
    hc = height - cap_h
    ys = levels(hc)
    P, N = [], []
    for k in range(seg + 1):
        a = 2 * math.pi * k / seg
        c, s = math.cos(a), math.sin(a)
        P.append([(r * c, y, r * s) for y in ys])
        N.append([(c, 0.0, s)] * len(ys))
    m.grid(P, N, None, MAT_WALL, lambda i, j: ao_wall(ys[j]))
    prism(m, -cap / 2, cap / 2, -cap / 2, cap / 2, hc, height, ao=lambda y: 235, bottom=True)
    m.boxes([((0, hc / 2, 0), (r, hc / 2, r)), ((0, hc + cap_h / 2, 0), (cap / 2, cap_h / 2, cap / 2))])
    return m


# --- плиты ---

def soffit(x, z, seed, fade):
    """Низ плиты: высота впадины над низом (м, ≥ 0 — в тело плиты). Бугры, выбоины, сколы."""
    u = 0.010 * (vnoise(x / 0.7, z / 0.7, seed) + 1) + 0.006 * (vnoise(x / 0.23, z / 0.23, seed + 1) + 1)
    # Выбоины: в клетке 0.5 м одна с вероятностью 0.45
    cx, cz = math.floor(x / 0.5), math.floor(z / 0.5)
    for i in (cx - 1, cx, cx + 1):
        for j in (cz - 1, cz, cz + 1):
            if h01(seed + 11, i, j) > 0.45:
                continue
            px = (i + 0.2 + 0.6 * h01(seed + 12, i, j)) * 0.5
            pz = (j + 0.2 + 0.6 * h01(seed + 13, i, j)) * 0.5
            r = 0.05 + 0.17 * h01(seed + 14, i, j) ** 2
            dep = 0.015 + 0.055 * h01(seed + 15, i, j)
            d2 = ((x - px) ** 2 + (z - pz) ** 2) / (r * r)
            if d2 < 1:
                u += dep * (1 - d2) ** 2
    # Сколы: крупные мелкие впадины с крутым краем, в клетке 2 м с вероятностью 0.5
    cx, cz = math.floor(x / 2.0), math.floor(z / 2.0)
    for i in (cx - 1, cx, cx + 1):
        for j in (cz - 1, cz, cz + 1):
            if h01(seed + 21, i, j) > 0.5:
                continue
            px = (i + h01(seed + 22, i, j)) * 2.0
            pz = (j + h01(seed + 23, i, j)) * 2.0
            r = 0.35 + 0.6 * h01(seed + 24, i, j)
            r *= 1 + 0.35 * vnoise(x / 0.3, z / 0.3, seed + 25)
            d = math.sqrt((x - px) ** 2 + (z - pz) ** 2)
            if d < r:
                u += (0.02 + 0.03 * h01(seed + 26, i, j)) * smooth01((r - d) / 0.06)
    return u * fade


def soffit_normal(x, z, seed, fade_fn):
    e = 0.02
    dx = (soffit(x + e, z, seed, fade_fn(x + e, z)) - soffit(x - e, z, seed, fade_fn(x - e, z))) / (2 * e)
    dz = (soffit(x, z + e, seed, fade_fn(x, z + e)) - soffit(x, z - e, seed, fade_fn(x, z - e))) / (2 * e)
    return norm((dx, -1.0, dz))


def ao_soffit(u):
    """Затенение низа плиты: глубже выбоина — темнее."""
    return max(90.0, 225.0 - 1500.0 * u)


def square_fade(w, d, margin=0.25):
    return lambda x, z: smooth01(min(w / 2 - abs(x), d / 2 - abs(z)) / margin)


def slab(seed, w=4.0, d=4.0, t=SLAB_T, step=0.125):
    """Целая плита w × d × t: низ бугристый с выбоинами (у края — ровный, стыки соседних плит сходятся)."""
    m = Part()
    nx, nz = int(w / step), int(d / step)
    fade = square_fade(w, d)
    P, N, U = [], [], []
    for i in range(nx + 1):
        x = -w / 2 + w * i / nx
        row_p, row_n, row_u = [], [], []
        for j in range(nz + 1):
            z = -d / 2 + d * j / nz
            u = soffit(x, z, seed, fade(x, z))
            row_p.append((x, u, z))
            row_n.append(soffit_normal(x, z, seed, fade))
            row_u.append(u)
        P.append(row_p)
        N.append(row_n)
        U.append(row_u)
    m.grid(P, N, None, MAT_SOFFIT, lambda i, j: ao_soffit(U[i][j]))
    m.poly([(-w / 2, t, -d / 2), (w / 2, t, -d / 2), (w / 2, t, d / 2), (-w / 2, t, d / 2)], (0, 1, 0), MAT_FLOOR)
    for x0, z0, x1, z1, out in ((w / 2, -d / 2, w / 2, d / 2, (1, 0, 0)), (-w / 2, d / 2, -w / 2, -d / 2, (-1, 0, 0)),
                                (w / 2, d / 2, -w / 2, d / 2, (0, 0, 1)), (-w / 2, -d / 2, w / 2, -d / 2, (0, 0, -1))):
        slab_side(m, x0, z0, x1, z1, 0.0, t, out)
    m.boxes([((0, t / 2, 0), (w / 2, t / 2, d / 2))])
    return m


def slab_hole(seed, w=4.0, d=4.0, t=SLAB_T, r=1.5, collar=0.22, collar_h=0.3, nside=16):
    """Плита с круглым отверстием (световой колодец): обечайка-кольцо ниже плиты, низ бугристый."""
    m = Part()
    ro = r + collar
    # Точки по периметру квадрата (углы входят), против часовой с угла (−w/2, −d/2)
    per = []
    for side in range(4):
        for k in range(nside):
            s = k / nside
            per.append([(-w / 2 + w * s, -d / 2), (w / 2, -d / 2 + d * s), (w / 2 - w * s, d / 2), (-w / 2, d / 2 - d * s)][side])
    per.append(per[0])
    ang = [math.atan2(p[1], p[0]) for p in per]
    fade_sq = square_fade(w, d)
    fade = lambda x, z: fade_sq(x, z) * smooth01((math.sqrt(x * x + z * z) - ro) / 0.25)
    rows = 10
    P, N, U = [], [], []
    for k, (p, a) in enumerate(zip(per, ang)):
        c = (ro * math.cos(a), ro * math.sin(a))
        row_p, row_n, row_u = [], [], []
        for j in range(rows + 1):
            q = (j / rows) ** 1.3  # гуще у обечайки
            x, z = c[0] + (p[0] - c[0]) * q, c[1] + (p[1] - c[1]) * q
            u = soffit(x, z, seed, fade(x, z))
            row_p.append((x, u, z))
            row_n.append(soffit_normal(x, z, seed, fade))
            row_u.append(u)
        P.append(row_p)
        N.append(row_n)
        U.append(row_u)
    m.grid(P, N, None, MAT_SOFFIT, lambda i, j: ao_soffit(U[i][j]))
    # Верх: кольцо между окружностью r и квадратом
    for k in range(len(per) - 1):
        a0, a1 = ang[k], ang[k + 1]
        if a1 < a0 - math.pi:
            a1 += 2 * math.pi
        p0, p1 = per[k], per[k + 1]
        m.poly([(r * math.cos(a0), t, r * math.sin(a0)), (p0[0], t, p0[1]), (p1[0], t, p1[1]),
                (r * math.cos(a1), t, r * math.sin(a1))], (0, 1, 0), MAT_FLOOR)
    for x0, z0, x1, z1, out in ((w / 2, -d / 2, w / 2, d / 2, (1, 0, 0)), (-w / 2, d / 2, -w / 2, -d / 2, (-1, 0, 0)),
                                (w / 2, d / 2, -w / 2, d / 2, (0, 0, 1)), (-w / 2, -d / 2, w / 2, -d / 2, (0, 0, -1))):
        slab_side(m, x0, z0, x1, z1, 0.0, t, out)
    # Колодец: внутренняя стенка от низа обечайки до верха, обечайка снаружи и снизу
    seg = 48
    ring = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg + 1)]
    ys = [-collar_h, 0.0, t]
    Pi = [[(r * c, y, r * s) for y in ys] for c, s in ring]
    Ni = [[(-c, 0.0, -s)] * len(ys) for c, s in ring]
    m.grid(Pi, Ni, None, MAT_WALL, lambda i, j: 175 + 60 * j / 2)
    Po = [[(ro * c, -collar_h, ro * s), (ro * c, 0.0, ro * s)] for c, s in ring]
    No = [[(c, 0.0, s)] * 2 for c, s in ring]
    m.grid(Po, No, None, MAT_SOFFIT, lambda i, j: 150 + 50 * j)
    Pb = [[(r * c, -collar_h, r * s), (ro * c, -collar_h, ro * s)] for c, s in ring]
    Nb = [[(0.0, -1.0, 0.0)] * 2 for _ in ring]
    m.grid(Pb, Nb, None, MAT_SOFFIT, lambda i, j: 190)
    # Коробки: четыре вокруг квадрата, вписанного в окружность обечайки, и обечайка восемью
    a = r * 0.92
    m.boxes([((0, t / 2, (-d / 2 - a) / 2), (w / 2, t / 2, (d / 2 - a) / 2)),
             ((0, t / 2, (d / 2 + a) / 2), (w / 2, t / 2, (d / 2 - a) / 2)),
             (((-w / 2 - a) / 2, t / 2, 0), ((w / 2 - a) / 2, t / 2, a)),
             (((w / 2 + a) / 2, t / 2, 0), ((w / 2 - a) / 2, t / 2, a))])
    return m


def fracture_line(seed, xs, base, amp):
    """Линия излома в плане: ломаная с крупными зубцами и мелкой рванью (по зерну)."""
    out = []
    for i, x in enumerate(xs):
        k = i / (len(xs) - 1)
        z = base(k) + amp * (0.55 * vnoise(k * 3.1, 0.3, seed) + 0.3 * vnoise(k * 9.0, 1.7, seed + 1))
        z += 0.07 * (h01(seed + 2, i) - 0.5)
        out.append(z)
    return out


def slab_broken(seed, base, w=4.0, d=4.0, t=SLAB_T, step=0.125, amp=0.6):
    """Плита с рваным краем: целая сторона на −z, излом со стороны +z по линии base(k) (k — доля ширины, от
    −d/2 до d/2) с зубцами из зерна. Торец излома неровный: верх, середина и низ края идут своими линиями."""
    m = Part()
    nx = int(w / step)
    xs = [-w / 2 + w * i / nx for i in range(nx + 1)]
    fb = fracture_line(seed, xs, base, amp)
    fm = [z + 0.12 * vnoise(i * 0.37, 4.0, seed + 5) - 0.05 for i, z in enumerate(fb)]
    ft = [z + 0.22 * vnoise(i * 0.29, 9.0, seed + 6) - 0.12 for i, z in enumerate(fb)]
    ym = [t / 2 + 0.1 * vnoise(i * 0.5, 2.0, seed + 7) for i in range(len(xs))]
    fb = [min(d / 2, max(-d / 2 + 0.4, z)) for z in fb]
    fm = [min(d / 2, max(-d / 2 + 0.4, z)) for z in fm]
    ft = [min(d / 2, max(-d / 2 + 0.4, z)) for z in ft]
    # Сколы у излома крупнее: низ у края отбит глубже
    fade_sq = square_fade(w, d)
    def fade(x, z):
        return fade_sq(x, z) if z < 0 else smooth01(min(w / 2 - abs(x), d / 2 + z) / 0.25)
    def edge_u(i):
        return 0.02 + 0.06 * h01(seed + 8, i)
    rows = 24
    P, N, U = [], [], []
    for i, x in enumerate(xs):
        row_p, row_n, row_u = [], [], []
        for j in range(rows + 1):
            q = j / rows
            z = -d / 2 + (fb[i] + d / 2) * q
            u = soffit(x, z, seed, fade(x, z))
            if j == rows:
                u = max(u, edge_u(i))
            row_p.append((x, u, z))
            row_n.append(soffit_normal(x, z, seed, fade))
            row_u.append(u)
        P.append(row_p)
        N.append(row_n)
        U.append(row_u)
    m.grid(P, N, None, MAT_SOFFIT, lambda i, j: ao_soffit(U[i][j]))
    for i in range(nx):
        m.poly([(xs[i], t, -d / 2), (xs[i + 1], t, -d / 2), (xs[i + 1], t, ft[i + 1]), (xs[i], t, ft[i])], (0, 1, 0), MAT_FLOOR)
    # Торец излома: три линии — низ, середина, верх; грани плоские
    for i in range(nx):
        b0, b1 = (xs[i], U[i][rows], fb[i]), (xs[i + 1], U[i + 1][rows], fb[i + 1])
        m0, m1 = (xs[i], ym[i], fm[i]), (xs[i + 1], ym[i + 1], fm[i + 1])
        t0, t1 = (xs[i], t, ft[i]), (xs[i + 1], t, ft[i + 1])
        for a, b, c in ((b0, b1, m1), (b0, m1, m0), (m0, m1, t1), (m0, t1, t0)):
            m.facet(a, b, c, (0, 0, 1), MAT_BREAK, lambda p: 205)
    # Бока: целая сторона и два торца по x до излома
    slab_side(m, -w / 2, -d / 2, w / 2, -d / 2, 0.0, t, (0, 0, -1))
    for i, x, out in ((0, -w / 2, (-1, 0, 0)), (nx, w / 2, (1, 0, 0))):
        a, b = (x, 0.0, -d / 2), (x, U[i][rows], fb[i])
        c, e = (x, ym[i], fm[i]), (x, t, ft[i])
        f = (x, t, -d / 2)
        m.poly([a, b, c], out, MAT_WALL)
        m.poly([a, c, f], out, MAT_WALL)
        m.poly([c, e, f], out, MAT_WALL)
    # Коробки: полосы по метру до ближайшего края излома
    bx = []
    per = int(1 / step)
    for k in range(int(w)):
        zmax = min(min(fb[k * per:(k + 1) * per + 1]), min(ft[k * per:(k + 1) * per + 1]))
        bx.append(((-w / 2 + k + 0.5, t / 2, (zmax - d / 2) / 2), (0.5, t / 2, (zmax + d / 2) / 2)))
    m.boxes(bx)
    return m


def beam_broken(seed, length, pitch_deg, b=0.36, h=0.5):
    """Обломанная балка-консоль: висит из края плиты вниз под углом pitch к горизонту, в +z. Начало координат —
    верх балки в точке крепления. Конец — излом из зерна."""
    m = Part()
    a = math.radians(pitch_deg)
    ax = (0.0, -math.sin(a), math.cos(a))   # вдоль балки
    up = (0.0, math.cos(a), math.sin(a))    # «верх» сечения
    sd = (1.0, 0.0, 0.0)
    # Сечение: 4 угла и середины сторон, против часовой при взгляде с конца
    sec = [(-b / 2, 0.0), (0.0, 0.0), (b / 2, 0.0), (b / 2, -h / 2), (b / 2, -h), (0.0, -h), (-b / 2, -h), (-b / 2, -h / 2)]
    def at(s, k, ds=0.0):
        v, w = sec[k]
        return tuple(ax[i] * (s + ds) + sd[i] * v + up[i] * w for i in range(3))
    jag = [0.22 * (h01(seed, k) - 0.5) + 0.12 * vnoise(k * 0.9, 0.0, seed + 1) for k in range(8)]
    s_mid = length - 0.7
    rings = [[at(-0.3, k) for k in range(8)], [at(s_mid, k) for k in range(8)], [at(length, k, jag[k]) for k in range(8)]]
    centre = tuple(ax[i] * (length + 0.08 * (h01(seed, 99) - 0.7)) + up[i] * (-h * (0.35 + 0.3 * h01(seed, 98)))
                   + sd[i] * b * 0.3 * (h01(seed, 97) - 0.5) for i in range(3))
    mid_c = tuple(ax[i] * s_mid + up[i] * (-h / 2) for i in range(3))
    for r in range(2):
        for k in range(8):
            p0, p1 = rings[r][k], rings[r][(k + 1) % 8]
            q0, q1 = rings[r + 1][k], rings[r + 1][(k + 1) % 8]
            mid = tuple((p0[i] + p1[i] + q0[i] + q1[i]) / 4 for i in range(3))
            c_ax = tuple(ax[i] * (s_mid if r == 0 else length) for i in range(3))
            out = sub(mid, mid_c) if r == 0 else sub(mid, tuple(c_ax[i] + up[i] * (-h / 2) for i in range(3)))
            ao = lambda p: 200 if dot(sub(p, (0, 0, 0)), up) < -h * 0.8 else 235
            if r == 0:
                m.poly([p0, p1, q1, q0], out, MAT_WALL, ao)
            else:
                m.facet(p0, p1, q1, out, MAT_WALL, ao)
                m.facet(p0, q1, q0, out, MAT_WALL, ao)
    for k in range(8):
        m.facet(rings[2][k], rings[2][(k + 1) % 8], centre, ax, MAT_BREAK, lambda p: 215)
    # Коробка вдоль балки: поворот вокруг x на −pitch
    q = (-math.sin(a / 2), 0.0, 0.0, math.cos(a / 2))
    c = tuple(ax[i] * (length - 0.3) / 2 + up[i] * (-h / 2) for i in range(3))
    m.boxes([(c, (b / 2, h / 2, (length + 0.3) / 2), q)])
    return m


def stair(height=FLOOR, steps=22, run=0.28, width=1.3):
    """Сплошной лестничный марш вдоль +x от подножия (x = 0) на высоту этажа; ширина по z."""
    m = Part()
    rise = height / steps
    hw = width / 2
    for i in range(steps):
        x0, x1 = i * run, (i + 1) * run
        y0, y1 = i * rise, (i + 1) * rise
        ao_r = lambda p: 175 + 60 * smooth01((p[1] - y0) / rise)
        m.poly([(x0, y0, -hw), (x0, y0, hw), (x0, y1, hw), (x0, y1, -hw)], (-1, 0, 0), MAT_WALL, ao_r)  # подступёнок
        m.poly([(x0, y1, -hw), (x1, y1, -hw), (x1, y1, hw), (x0, y1, hw)], (0, 1, 0), MAT_FLOOR, lambda p: 245)
        for z, out in ((-hw, (0, 0, -1)), (hw, (0, 0, 1))):
            ys = [y for y in levels(y1) if y < y1] + [y1]
            for ya, yb in zip(ys, ys[1:]):
                m.poly([(x0, ya, z), (x1, ya, z), (x1, yb, z), (x0, yb, z)], out, MAT_WALL, lambda p: ao_wall(p[1]))
    xe = steps * run
    ys = levels(height)
    for ya, yb in zip(ys, ys[1:]):
        m.poly([(xe, ya, -hw), (xe, ya, hw), (xe, yb, hw), (xe, yb, -hw)], (1, 0, 0), MAT_WALL, lambda p: ao_wall(p[1]))
    coll = [((x0 + run / 2, (i + 1) * rise / 2, 0), (run / 2, (i + 1) * rise / 2, hw))
            for i, x0 in enumerate(k * run for k in range(steps))]
    m.boxes(coll, coarse=False)
    for k in range(4):
        i0, i1 = k * steps // 4, (k + 1) * steps // 4
        y = (i0 + 1) * rise
        m.coarse.append(((((i0 + i1) * run) / 2, y / 2, 0), ((i1 - i0) * run / 2, y / 2, hw), (0, 0, 0, 1)))
    return m


def fan_base(k0, k1):
    """Линия излома для плиты: доля глубины от −d/2 на левом и правом краю."""
    return lambda k: -2.0 + 4.0 * (k0 + (k1 - k0) * k)


MESHES = {
    "wall_4": lambda: wall(4.0),
    "wall_2": lambda: wall(2.0),
    "wall_4_broken_a": lambda: wall_broken(4.0, 401),
    "wall_4_broken_b": lambda: wall_broken(4.0, 402),
    "wall_door": wall_door,
    "buttress": buttress,
    "column_rect": column_rect,
    "floor_4": floor_tile,
    "girder_4": girder,
    "column_round": column_round,
    "slab_a": lambda: slab(501),
    "slab_b": lambda: slab(502),
    "slab_c": lambda: slab(503),
    "slab_hole": lambda: slab_hole(511),
    "slab_broken_a": lambda: slab_broken(521, fan_base(0.55, 0.35)),
    "slab_broken_b": lambda: slab_broken(522, fan_base(0.25, 0.6)),
    "slab_broken_c": lambda: slab_broken(523, fan_base(0.7, 0.15), amp=0.8),
    "beam_broken_a": lambda: beam_broken(531, 3.4, 38),
    "beam_broken_b": lambda: beam_broken(532, 2.6, 55),
    "stair_4": stair,
}
