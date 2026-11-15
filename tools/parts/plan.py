"""Описание цельного здания телами (docs/parts.md): одно и то же описание даёт и сетку, и коллайдер.

Здание — список операций («add» или «cut», тело) над коробками и вытянутыми профилями. solid.py превращает
его в одну замкнутую сетку, а `colliders()` — в коробки для шага мира: план режется по всем гранями тел на
клетки, в клетке по вертикали считаются отрезки бетона, одинаковые соседи сливаются.
"""
from kit import Body


class Box:
    def __init__(self, x0, x1, y0, y1, z0, z1):
        self.b = (x0, x1, y0, y1, z0, z1)

    def body(self):
        x0, x1, y0, y1, z0, z1 = self.b
        body = Body()
        body.box(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), None, bevel=0)
        return body

    def breaks(self, axis):
        return (self.b[0], self.b[1]) if axis == "x" else (self.b[4], self.b[5])

    def span(self, x, z):
        x0, x1, y0, y1, z0, z1 = self.b
        return (y0, y1) if x0 < x < x1 and z0 < z < z1 else None


class Prism:
    """Профиль [(u, y)] вытянут поперёк: axis "x" — профиль в плоскости (z, y), тянется по x от a до b;
    axis "z" — профиль в плоскости (x, y), тянется по z."""

    def __init__(self, axis, a, b, profile):
        self.axis, self.a, self.b, self.profile = axis, a, b, [(float(u), float(y)) for u, y in profile]

    def body(self):
        body = Body()
        body.prism(self.profile, self.axis, self.a, self.b, None, plane=("z", "y") if self.axis == "x" else ("x", "y"))
        return body

    def breaks(self, axis):
        if axis == self.axis:
            return (self.a, self.b)
        out = []
        n = len(self.profile)
        for k in range(n):
            (u0, y0), (u1, y1) = self.profile[k], self.profile[(k + 1) % n]
            out.append(u0)
            # Наклонный край: клетки не шире 0.3 м, иначе коробки торчат из-под откоса
            if abs(y1 - y0) > 1e-6 and abs(u1 - u0) > 0.3:
                m = int(abs(u1 - u0) / 0.3) + 1
                out += [u0 + (u1 - u0) * i / m for i in range(1, m)]
        return out

    def span(self, x, z):
        t, u = (x, z) if self.axis == "x" else (z, x)
        if not self.a < t < self.b:
            return None
        ys = []
        n = len(self.profile)
        for k in range(n):
            (u0, y0), (u1, y1) = self.profile[k], self.profile[(k + 1) % n]
            if (u0 <= u) != (u1 <= u):
                ys.append(y0 + (y1 - y0) * (u - u0) / (u1 - u0))
        return (min(ys), max(ys)) if len(ys) >= 2 else None


def _add(solid, a, b):
    out = []
    for s in solid:
        if s[1] < a or s[0] > b:
            out.append(s)
        else:
            a, b = min(a, s[0]), max(b, s[1])
    out.append((a, b))
    out.sort()
    return out


def _cut(solid, a, b):
    out = []
    for s in solid:
        if s[1] <= a or s[0] >= b:
            out.append(s)
            continue
        if s[0] < a:
            out.append((s[0], a))
        if s[1] > b:
            out.append((b, s[1]))
    return out


def colliders(ops, min_cell=0.04, min_h=0.03):
    """Коробки (x0, x1, y0, y1, z0, z1) бетона по списку операций. Клетки плана уже min_cell и слои тоньше min_h
    отбрасываются (нахлёсты тел в сантиметр — не стены)."""
    def cuts(axis):
        vals = sorted({round(v, 4) for _, shape in ops for v in shape.breaks(axis)})
        out = [vals[0]]
        for v in vals[1:]:
            if v - out[-1] >= min_cell:
                out.append(v)
        return out
    xs, zs = cuts("x"), cuts("z")
    layers = {}
    for i in range(len(xs) - 1):
        xm = (xs[i] + xs[i + 1]) / 2
        for j in range(len(zs) - 1):
            zm = (zs[j] + zs[j + 1]) / 2
            solid = []
            for op, shape in ops:
                s = shape.span(xm, zm)
                if s is not None:
                    solid = _add(solid, *s) if op == "add" else _cut(solid, *s)
            for a, b in solid:
                if b - a >= min_h:
                    layers.setdefault((round(a, 3), round(b, 3)), set()).add((i, j))
    boxes = []
    for (y0, y1), cells in sorted(layers.items()):
        # Жадно прямоугольниками: от первой свободной клетки полоса вдоль z, потом вширь по x, пока полоса цела
        left = set(cells)
        for i, j in sorted(cells):
            if (i, j) not in left:
                continue
            j1 = j
            while (i, j1 + 1) in left:
                j1 += 1
            i1 = i
            while all((i1 + 1, q) in left for q in range(j, j1 + 1)):
                i1 += 1
            for p in range(i, i1 + 1):
                for q in range(j, j1 + 1):
                    left.discard((p, q))
            boxes.append((xs[i], xs[i1 + 1], y0, y1, zs[j], zs[j1 + 1]))
    return boxes
