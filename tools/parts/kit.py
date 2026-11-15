"""Фабрика деталей (docs/parts.md): рецепты замкнутых тел.

Тело (`Body`) — набор замкнутых поверхностей из многоугольников в осях игры (x — восток, y — вверх, z — север),
обход против часовой, если смотреть снаружи. Простые рецепты (труба по пути, тело вращения, брус с фаской,
профиль вытяжкой) считаются здесь, без Blender. Цельный бетон — `solid()` в solid.py: тела складываются и
вычитаются булевыми операциями Blender (bpy), на выходе одна замкнутая сетка.
"""
import math


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

    def extend(self, other):
        self.faces += other.faces
        return self

    def _closed(self, faces):
        """Добавить одно замкнутое тело с согласованным обходом: развернуть наружу по знаку объёма."""
        if _volume6(faces) < 0:
            faces = [(tuple(reversed(p)), m, s) for p, m, s in faces]
        self.faces += faces

    # --- рецепты ---
    def tube(self, pts, r, mat, seg=12, fillet=0.0, radii=None, smooth=True):
        """Труба вдоль ломаной pts; fillet — радиус отвода на углах; radii(i, n) — радиус по узлам пути."""
        path = fillet_path(pts, fillet) if fillet > 0 else [tuple(map(float, p)) for p in pts]
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
        faces = []
        for r0, r1 in zip(rings, rings[1:]):
            for k in range(seg):
                faces.append(((r0[k], r0[(k + 1) % seg], r1[(k + 1) % seg], r1[k]), mat, smooth))
        faces.append((tuple(reversed(rings[0])), mat, False))
        faces.append((tuple(rings[-1]), mat, False))
        self._closed(faces)

    def lathe(self, origin, axis, profile, mat, seg=16, smooth=True):
        """Тело вращения вокруг оси: profile — [(радиус, высота вдоль оси)], радиусы больше нуля."""
        axis = unit(axis)
        n, b = _side(axis)
        cs = [(math.cos(2 * math.pi * k / seg), math.sin(2 * math.pi * k / seg)) for k in range(seg)]
        rings = [[add(add(origin, mul(axis, h)), mul(add(mul(n, cx), mul(b, sx)), r)) for cx, sx in cs]
                 for r, h in profile]
        faces = []
        for r0, r1 in zip(rings, rings[1:]):
            for k in range(seg):
                faces.append(((r0[k], r0[(k + 1) % seg], r1[(k + 1) % seg], r1[k]), mat, smooth and seg > 6))
        faces.append((tuple(reversed(rings[0])), mat, False))
        faces.append((tuple(rings[-1]), mat, False))
        self._closed(faces)

    def box(self, c, size, mat, bevel=0.004, rot=None, smooth=True):
        """Брус с серединой c и размерами size, рёбра сняты фаской bevel; rot — матрица поворота вокруг середины."""
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
            def vert(ax, sg):
                """Вершина у угла sg на грани поперёк оси ax."""
                return tuple(sg[k] * (h[k] if k == ax else h[k] - b) for k in range(3))
            for ax in range(3):
                u, v = (ax + 1) % 3, (ax + 2) % 3
                for s in (-1, 1):
                    q = []
                    for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        sg = [0, 0, 0]
                        sg[ax], sg[u], sg[v] = s, su, sv
                        q.append(vert(ax, sg))
                    faces.append((tuple(q), mat, smooth))
                # Фаски рёбер вдоль оси ax: между гранями u и v
                for su in (-1, 1):
                    for sv in (-1, 1):
                        q = []
                        for s, first in ((-1, u), (1, u), (1, v), (-1, v)):
                            sg = [0, 0, 0]
                            sg[ax], sg[u], sg[v] = s, su, sv
                            q.append(vert(first, sg))
                        faces.append((tuple(q), mat, smooth))
            for sx in (-1, 1):
                for sy in (-1, 1):
                    for sz in (-1, 1):
                        sg = (sx, sy, sz)
                        faces.append((tuple(vert(ax, sg) for ax in range(3)), mat, smooth))
        # Выпуклое тело: каждую грань развернуть от середины
        out = []
        for pts, m, s in faces:
            nrm = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
            if dot(nrm, pts[0]) < 0:
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


def fillet_path(pts, radius, n=6):
    """Ломаная со скруглёнными углами: дуга радиуса radius из n отрезков на каждом изломе."""
    pts = [tuple(map(float, p)) for p in pts]
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        a, b, c = pts[i - 1], pts[i], pts[i + 1]
        d1, d2 = unit(sub(a, b)), unit(sub(c, b))
        ca = max(-1.0, min(1.0, dot(d1, d2)))
        ang = math.acos(ca)
        if ang > math.pi - 1e-3:
            out.append(b)
            continue
        t = min(radius / math.tan(ang / 2), length(sub(a, b)) * 0.45, length(sub(c, b)) * 0.45)
        r = t * math.tan(ang / 2)
        cen = add(b, mul(unit(add(d1, d2)), r / math.sin(ang / 2)))
        v1, v2 = sub(add(b, mul(d1, t)), cen), sub(add(b, mul(d2, t)), cen)
        tot = math.acos(max(-1.0, min(1.0, dot(unit(v1), unit(v2)))))
        ax = unit(cross(v1, v2))
        for k in range(n + 1):
            th = tot * k / n
            # Поворот v1 вокруг ax на th (v1 перпендикулярен ax)
            out.append(add(cen, add(mul(v1, math.cos(th)), mul(cross(ax, v1), math.sin(th)))))
    out.append(pts[-1])
    return out


def sag(a, b, drop, n=12):
    """Провисший кабель от a до b: парабола со стрелой drop вниз."""
    return [add(lerp(a, b, k / n), (0.0, -drop * 4 * (k / n) * (1 - k / n), 0.0)) for k in range(n + 1)]
