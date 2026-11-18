"""Фабрика деталей (docs/parts.md): рецепты замкнутых тел.

Тело (`Body`) — набор замкнутых поверхностей из многоугольников в осях игры (x — восток, y — вверх, z — север),
обход против часовой, если смотреть снаружи. Простые рецепты (труба по пути, тело вращения, брус с фаской,
профиль вытяжкой) считаются здесь, без Blender. Цельный бетон — `solid()` в solid.py: тела складываются и
вычитаются булевыми операциями Blender (bpy), на выходе одна замкнутая сетка.
"""
import math

# Допуск хорды на LOD 0 (docs/parts.md, «Гладкость»): отклонение многоугольника от круга не больше 0.5 мм
CHORD = 0.0005


def seg_for(r, angle=2 * math.pi, tol=CHORD, lo=8, hi=160):
    """Сколько отрезков нужно дуге радиуса r на угол angle, чтобы хорда отходила от дуги не больше tol.
    Полный круг — кратно 4 (симметрия по осям), не меньше lo и не больше hi."""
    if r <= tol:
        n = 3
    else:
        n = math.ceil(angle / (2.0 * math.acos(1.0 - tol / r)) - 1e-9)
    if angle >= 2 * math.pi - 1e-9:
        n = max(lo, -(-n // 4) * 4)
    return max(1, min(n, hi))


def _round_seg(seg, r, exact):
    """Число отрезков круга: заданное как нижняя граница, поднятое до допуска хорды. exact — гранёная деталь по
    замыслу (шестигранник гайки), число как задано."""
    return seg if exact else max(seg, seg_for(r))


_GROUP = [1]


def new_group():
    """Своя группа сглаживания (поле «гладкая» грани, export.build_mesh): нормаль не делится через излом
    профиля. Номера с 2: True (= 1) — общая гладкая группа."""
    _GROUP[0] += 1
    return _GROUP[0]


TURN = math.radians(25.0)  # излом профиля или пути круче этого — резкая кромка, а не сглаженная


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def length(a): return math.sqrt(dot(a, a))


def unit(a):
    l = length(a)
    return (a[0] / l, a[1] / l, a[2] / l)


def lerp(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def rot_y(a):
    """Матрица поворота вокруг y (строки), угол в радианах."""
    c, s = math.cos(a), math.sin(a)
    return ((c, 0.0, s), (0.0, 1.0, 0.0), (-s, 0.0, c))


def rot_x(a):
    c, s = math.cos(a), math.sin(a)
    return ((1.0, 0.0, 0.0), (0.0, c, -s), (0.0, s, c))


def rot_z(a):
    c, s = math.cos(a), math.sin(a)
    return ((c, -s, 0.0), (s, c, 0.0), (0.0, 0.0, 1.0))


def mat_mul(m, n):
    return tuple(tuple(sum(m[i][k] * n[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def mat_vec(m, v): return (dot(m[0], v), dot(m[1], v), dot(m[2], v))


def _side(axis):
    """Два единичных вектора поперёк оси (правая тройка n, b, axis)."""
    up = (0.0, 1.0, 0.0) if abs(axis[1]) < 0.9 else (1.0, 0.0, 0.0)
    n = unit(cross(up, axis))
    return n, cross(axis, n)


def _volume6(faces):
    """Шесть объёмов со знаком: плюс — обход граней наружу."""
    v = 0.0
    for pts, _, _ in faces:
        a = pts[0]
        for i in range(1, len(pts) - 1):
            v += dot(a, cross(pts[i], pts[i + 1]))
    return v


class Body:
    """Набор замкнутых тел. Грань — (точки, материал, гладкая): гладкая делит нормаль с соседями (export.py)."""

    def __init__(self):
        self.faces = []
        self.lights = []  # лампы тела (огни 6е, docs/lamps.md): строки light заготовки пишет lights.py

    def extend(self, other):
        self.faces += other.faces
        self.lights += other.lights
        return self

    def light(self, c, axis, face, back, lm, kelvin, size):
        """Лампа: середина тела света c, полуось трубки axis (нули — колба), куда светит face (нули — во все
        стороны), доля света назад back, поток lm, цвет kelvin, радиус тела size. В сетку не идёт."""
        self.lights.append((c, axis, face, back, lm, kelvin, size))

    def _closed(self, faces):
        """Добавить одно замкнутое тело с согласованным обходом: развернуть наружу по знаку объёма."""
        if _volume6(faces) < 0:
            faces = [(tuple(reversed(p)), m, s) for p, m, s in faces]
        self.faces += faces

    # --- рецепты ---
    def tube(self, pts, r, mat, seg=12, fillet=0.0, radii=None, smooth=True):
        """Труба вдоль ломаной pts; fillet — радиус отвода на углах; radii(i, n) — радиус по узлам пути.
        seg — нижняя граница числа отрезков круга, сверх неё — по допуску хорды (smooth=False — ровно seg)."""
        # Без отвода пологие изломы (мельче TURN) всё равно скругляются дугой на всю длину соседних отрезков:
        # шланг по ломаной из трёх точек иначе виден ломаным при гладком блике. Крутые изломы — стык колен
        path = fillet_path(pts, fillet, r_out=r) if fillet > 0 else fillet_path(pts, 1e3, n=2, r_out=r, max_turn=TURN)
        rmax = max([r] + [radii(i, len(path)) for i in range(len(path))]) if radii else r
        seg = _round_seg(seg, rmax, not smooth)
        t0 = unit(sub(path[1], path[0]))
        n, _ = _side(t0)
        prev = t0
        rings = []
        cs = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg)]
        for i, p in enumerate(path):
            if i == 0:
                t = t0
            elif i == len(path) - 1:
                t = unit(sub(p, path[i - 1]))
            else:
                t = unit(add(unit(sub(path[i + 1], p)), unit(sub(p, path[i - 1]))))
            # Перенос поперечника без кручения: поворот от prev к t (Родриг без тригонометрии)
            ax = cross(prev, t)
            c = dot(prev, t)
            if dot(ax, ax) > 1e-16 and c > -0.999:
                n = add(add(mul(n, c), cross(ax, n)), mul(ax, dot(ax, n) / (1.0 + c)))
            n = unit(sub(n, mul(t, dot(n, t))))
            b = cross(t, n)
            rr = radii(i, len(path)) if radii else r
            # На изломе без отвода кольцо лежит в биссекторной плоскости: растянуть, чтобы труба не худела
            k = 1.0
            if 0 < i < len(path) - 1 and fillet <= 0:
                ca = dot(t, unit(sub(p, path[i - 1])))
                k = 1.0 / max(ca, 0.5)
            rings.append([add(p, mul(add(mul(n, cx), mul(b, sx)), rr * k)) for cx, sx in cs])
            prev = t
        # На изломе пути без отвода круче TURN — новая группа сглаживания (резкий стык колен)
        faces = []
        g = new_group() if smooth else False
        for i, (r0, r1) in enumerate(zip(rings, rings[1:])):
            if smooth and 0 < i < len(path) - 1:
                if dot(unit(sub(path[i], path[i - 1])), unit(sub(path[i + 1], path[i]))) < math.cos(TURN):
                    g = new_group()
            for k in range(seg):
                faces.append(((r0[k], r0[(k + 1) % seg], r1[(k + 1) % seg], r1[k]), mat, g))
        faces.append((tuple(reversed(rings[0])), mat, False))
        faces.append((tuple(rings[-1]), mat, False))
        self._closed(faces)

    def lathe(self, origin, axis, profile, mat, seg=16, smooth=True):
        """Тело вращения вокруг оси: profile — [(радиус, высота вдоль оси)], радиусы больше нуля.
        seg — нижняя граница, сверх неё — по допуску хорды; smooth=False или seg ≤ 6 — гранёное по замыслу
        (гайка, шестигранная головка: плоские грани), ровно seg."""
        seg = _round_seg(seg, max(p[0] for p in profile), not smooth or seg <= 6)
        axis = unit(axis)
        n, b = _side(axis)
        cs = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg)]
        rings = [[add(add(origin, mul(axis, h)), mul(add(mul(n, cx), mul(b, sx)), r)) for cx, sx in cs]
                 for r, h in profile]
        # Группа сглаживания на участок профиля между изломами круче TURN: плечо и бок не сливаются в один блик
        groups = []
        g = new_group()
        for i in range(len(profile) - 1):
            if i > 0:
                d0 = (profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1])
                d1 = (profile[i + 1][0] - profile[i][0], profile[i + 1][1] - profile[i][1])
                l0, l1 = math.hypot(*d0), math.hypot(*d1)
                if l0 > 0 and l1 > 0 and (d0[0] * d1[0] + d0[1] * d1[1]) / (l0 * l1) < math.cos(TURN):
                    g = new_group()
            groups.append(g if smooth and seg > 6 else False)
        faces = []
        for i, (r0, r1) in enumerate(zip(rings, rings[1:])):
            for k in range(seg):
                faces.append(((r0[k], r0[(k + 1) % seg], r1[(k + 1) % seg], r1[k]), mat, groups[i]))
        faces.append((tuple(reversed(rings[0])), mat, False))
        faces.append((tuple(rings[-1]), mat, False))
        self._closed(faces)

    def box(self, c, size, mat, bevel=0.004, rot=None, smooth=True, segs=None):
        """Брус с серединой c и размерами size, рёбра скруглены радиусом bevel; rot — матрица поворота вокруг
        середины. Скругление — дуга из 2·segs отрезков (segs по умолчанию: 2, если брус длиннее 5 см, иначе 1),
        углы — части сферы: кромка ловит блик, а не ломается одной фаской (docs/parts.md, «Гладкость»)."""
        h = (size[0] / 2, size[1] / 2, size[2] / 2)
        b = min(bevel, min(h) * 0.6)
        faces = []
        if b <= 0:
            for ax in range(3):
                for s in (-1, 1):
                    u, v = (ax + 1) % 3, (ax + 2) % 3
                    q = []
                    for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        p = [0.0, 0.0, 0.0]
                        p[ax], p[u], p[v] = s * h[ax], su * h[u], sv * h[v]
                        q.append(tuple(p))
                    faces.append((tuple(q), mat, False))
        else:
            m = segs or max(2 if max(size) > 0.05 else 1, -(-seg_for(b, math.pi / 2) // 2))
            hi = [h[k] - b for k in range(3)]
            # Узлы по оси на поверхности внешней коробки: внутри грани — два, на скруглении — tan углов до 45°;
            # точка внешней коробки проецируется на скругление (ближняя точка внутренней коробки + b по направлению)
            ts = [math.tan(math.pi / 4 * j / m) for j in range(m + 1)]
            nodes = [[-(hi[k] + b * t) for t in reversed(ts)] + [hi[k] + b * t for t in ts] for k in range(3)]

            def proj(p):
                q = [max(-hi[k], min(hi[k], p[k])) for k in range(3)]
                d = [p[k] - q[k] for k in range(3)]
                ln = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
                return tuple(q[k] + b * d[k] / ln for k in range(3))
            for ax in range(3):
                u, v = (ax + 1) % 3, (ax + 2) % 3
                for s in (-1, 1):
                    nu, nv = nodes[u], nodes[v]
                    grid = []
                    for pu in nu:
                        row = []
                        for pv in nv:
                            p = [0.0, 0.0, 0.0]
                            p[ax], p[u], p[v] = s * h[ax], pu, pv
                            row.append(proj(p))
                        grid.append(row)
                    for i in range(len(nu) - 1):
                        for j in range(len(nv) - 1):
                            faces.append(((grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]), mat, smooth))
        # Выпуклое тело: каждую грань развернуть от середины
        out = []
        for pts, m, s in faces:
            nrm = cross(sub(pts[2], pts[0]), sub(pts[3], pts[1]))  # по диагоналям четырёхугольника
            if dot(nrm, add(pts[0], pts[2])) < 0:
                pts = tuple(reversed(pts))
            if rot is not None:
                pts = tuple(mat_vec(rot, p) for p in pts)
            out.append((tuple(add(p, c) for p in pts), m, s))
        self.faces += out

    def prism(self, profile, axis, a, b, mat, plane=None, smooth=False):
        """Профиль (многоугольник в двух осях plane, по умолчанию — две следующие за axis) вытянут вдоль axis от a до b."""
        idx = {"x": 0, "y": 1, "z": 2}
        ax = idx[axis]
        u, v = (idx[plane[0]], idx[plane[1]]) if plane else ((ax + 1) % 3, (ax + 2) % 3)

        def ring(t):
            out = []
            for pu, pv in profile:
                p = [0.0, 0.0, 0.0]
                p[ax], p[u], p[v] = t, pu, pv
                out.append(tuple(p))
            return out
        r0, r1 = ring(a), ring(b)
        n = len(profile)
        faces = [((r0[k], r0[(k + 1) % n], r1[(k + 1) % n], r1[k]), mat, smooth) for k in range(n)]
        faces.append((tuple(reversed(r0)), mat, False))
        faces.append((tuple(r1), mat, False))
        self._closed(faces)

    def loft(self, rings, mat, smooth=False):
        """Тело по сечениям: rings — кольца с одинаковым числом точек (обход один и тот же), соседние кольца
        соединены боками, торцы закрыты. Литой гранёный корпус, клин, балка переменного сечения."""
        n = len(rings[0])
        faces = []
        for r0, r1 in zip(rings, rings[1:]):
            for k in range(n):
                faces.append(((r0[k], r0[(k + 1) % n], r1[(k + 1) % n], r1[k]), mat, smooth))
        faces.append((tuple(reversed(rings[0])), mat, False))
        faces.append((tuple(rings[-1]), mat, False))
        self._closed(faces)

    def torus(self, c, axis, big, small, mat, seg=14, tseg=8):
        """Кольцо (тор): середина c, ось axis, радиус кольца big, радиус прутка small. Рым, скоба, манжета.
        seg и tseg — нижние границы, сверх них — по допуску хорды (большой круг по наружному радиусу)."""
        seg = _round_seg(seg, big + small, False)
        tseg = _round_seg(tseg, small, False)
        axis = unit(axis)
        n, b = _side(axis)
        faces = []
        rings = []
        for i in range(seg):
            a = 2 * math.pi * i / seg
            d = add(mul(n, math.cos(a)), mul(b, math.sin(a)))
            o = add(c, mul(d, big))
            rings.append([add(o, add(mul(d, small * math.cos(2 * math.pi * k / tseg)),
                                     mul(axis, small * math.sin(2 * math.pi * k / tseg)))) for k in range(tseg)])
        for i in range(seg):
            r0, r1 = rings[i], rings[(i + 1) % seg]
            for k in range(tseg):
                faces.append(((r0[k], r0[(k + 1) % tseg], r1[(k + 1) % tseg], r1[k]), mat, True))
        self._closed(faces)

    def placed(self, rot=None, off=(0.0, 0.0, 0.0)):
        """Копия тела, повёрнутая матрицей rot (вокруг начала координат) и сдвинутая на off."""
        out = Body()
        for pts, m, s in self.faces:
            if rot is not None:
                pts = tuple(mat_vec(rot, p) for p in pts)
            out.faces.append((tuple(add(p, off) for p in pts), m, s))
        return out

    def blob(self, c, r, mat, seed, squash=(1.0, 1.0, 1.0)):
        """Неровный ком (икосаэдр с одним делением, узлы сдвинуты хешем): скол, камень, обломок."""
        t = (1.0 + 5.0 ** 0.5) / 2.0
        base = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
                (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        tri = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
               (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11),
               (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        verts = [unit(p) for p in base]
        mid = {}

        def midpoint(i, j):
            k = (min(i, j), max(i, j))
            if k not in mid:
                verts.append(unit(lerp(verts[i], verts[j], 0.5)))
                mid[k] = len(verts) - 1
            return mid[k]
        sub_t = []
        for i, j, k in tri:
            ij, jk, ki = midpoint(i, j), midpoint(j, k), midpoint(k, i)
            sub_t += [(i, ij, ki), (j, jk, ij), (k, ki, jk), (ij, jk, ki)]
        pts = []
        for i, p in enumerate(verts):
            k = r * (0.7 + 0.6 * rnd(seed, i))
            pts.append((c[0] + p[0] * k * squash[0], c[1] + p[1] * k * squash[1], c[2] + p[2] * k * squash[2]))
        self._closed([((pts[i], pts[j], pts[k]), mat, False) for i, j, k in sub_t])


def ihash(v):
    """Целочисленный хеш (как hash_u в шейдерах и tools/meshgen.py)."""
    v &= 0xFFFFFFFF
    s = (v * 747796405 + 2891336453) & 0xFFFFFFFF
    w = (((s >> ((s >> 28) + 4)) ^ s) * 277803737) & 0xFFFFFFFF
    return (w >> 22) ^ w


def rnd(seed, k=0):
    """Случайное 0..1 из зерна и номера: одно зерно — те же байты."""
    return ihash(ihash(seed * 7919 + 13) ^ (k * 104729 + 71)) / 4294967296.0


def uniform(seed, k, a, b): return a + (b - a) * rnd(seed, k)


def fillet_path(pts, radius, n=6, r_out=0.0, max_turn=None):
    """Ломаная со скруглёнными углами: дуга радиуса radius на каждом изломе, отрезков не меньше n и столько,
    чтобы наружная сторона трубы (радиус дуги + r_out) держала допуск хорды. max_turn — излом круче этого
    оставить углом."""
    pts = [tuple(map(float, p)) for p in pts]
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        a, b, c = pts[i - 1], pts[i], pts[i + 1]
        d1, d2 = unit(sub(a, b)), unit(sub(c, b))
        ca = max(-1.0, min(1.0, dot(d1, d2)))
        ang = math.acos(ca)
        if ang > math.pi - 1e-3 or (max_turn is not None and math.pi - ang >= max_turn):
            out.append(b)
            continue
        t = min(radius / math.tan(ang / 2), length(sub(a, b)) * 0.45, length(sub(c, b)) * 0.45)
        r = t * math.tan(ang / 2)
        cen = add(b, mul(unit(add(d1, d2)), r / math.sin(ang / 2)))
        v1, v2 = sub(add(b, mul(d1, t)), cen), sub(add(b, mul(d2, t)), cen)
        tot = math.acos(max(-1.0, min(1.0, dot(unit(v1), unit(v2)))))
        ax = unit(cross(v1, v2))
        nk = max(n, seg_for(r + r_out, tot, tol=CHORD * 0.7))  # с запасом: нормали на стыке с прямым участком усреднены
        for k in range(nk + 1):
            th = tot * k / nk
            # Поворот v1 вокруг ax на th (v1 перпендикулярен ax)
            out.append(add(cen, add(mul(v1, math.cos(th)), mul(cross(ax, v1), math.sin(th)))))
    out.append(pts[-1])
    return out


def sag(a, b, drop, n=12):
    """Провисший кабель от a до b: парабола со стрелой drop вниз."""
    return [add(lerp(a, b, k / n), (0.0, -drop * 4 * (k / n) * (1 - k / n), 0.0)) for k in range(n + 1)]
