"""Вывод тел фабрики в .mesh (docs/parts.md): треугольники в каноническом порядке → сырой .mesh → meshpack.

Порядок вершин и граней из Blender меняется от запуска к запуску, сами координаты — нет (решатель MANIFOLD).
Поэтому здесь всё приводится к одному виду: положения округляются до float, длинные рёбра делятся пополам,
треугольники сортируются, нормали считаются заново в этом порядке. Одно описание — те же байты.
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from meshgen import Mesh  # noqa: E402

from kit import cross, dot, sub  # noqa: E402


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def p32(p):
    return (f32(p[0]), f32(p[1]), f32(p[2]))


def ear_clip(pts):
    """Простой многоугольник в пространстве → тройки номеров. Выпуклый даёт веер, вогнутый — уши."""
    n = len(pts)
    if n == 3:
        return [(0, 1, 2)]
    # Нормаль Ньюэлла и проекция на плоскость без главной оси
    nx = ny = nz = 0.0
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    ax = max(range(3), key=lambda k: abs((nx, ny, nz)[k]))
    u, v = (ax + 1) % 3, (ax + 2) % 3
    sgn = 1.0 if (nx, ny, nz)[ax] > 0 else -1.0
    q = [(p[u], p[v] * sgn) for p in pts]

    def area2(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    idx = list(range(n))
    out = []
    while len(idx) > 3:
        best = None
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = q[i0], q[i1], q[i2]
            s = area2(a, b, c)
            if s <= 1e-12:
                continue
            inside = False
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                p = q[j]
                if area2(a, b, p) >= -1e-12 and area2(b, c, p) >= -1e-12 and area2(c, a, p) >= -1e-12:
                    inside = True
                    break
            if not inside:
                best = k
                break
        if best is None:
            best = 0  # вырожденный остаток: срезать как есть
        out.append((idx[best - 1], idx[best], idx[(best + 1) % len(idx)]))
        idx.pop(best)
    out.append((idx[0], idx[1], idx[2]))
    return out


def triangles(body):
    """Грани тела → треугольники (a, b, c, материал, гладкая) с положениями float."""
    out = []
    for pts, mat, smooth in body.faces:
        pts = [p32(p) for p in pts]
        for i, j, k in ear_clip(pts):
            a, b, c = pts[i], pts[j], pts[k]
            if a == b or b == c or a == c:
                continue
            out.append((a, b, c, mat, smooth))
    return out


def subdivide(tris, max_edge):
    """Делить рёбра длиннее max_edge пополам, пока такие есть. Середина ребра одна для обеих граней — без щелей.
    Зачем: кластеру с гранью на десятки метров достаётся шар на всё здание (отсечение, поле расстояний)."""
    lim = max_edge * max_edge
    for _ in range(12):
        mids = {}

        def mid(a, b):
            key = (a, b) if a < b else (b, a)
            if key not in mids:
                d = sub(a, b)
                mids[key] = p32(((a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2)) if dot(d, d) > lim else None
            return mids[key]
        out = []
        split = False
        for a, b, c, mat, smooth in tris:
            mab, mbc, mca = mid(a, b), mid(b, c), mid(c, a)
            n = (mab is not None) + (mbc is not None) + (mca is not None)
            if n == 0:
                out.append((a, b, c, mat, smooth))
                continue
            split = True
            # Повернуть так, чтобы случаи свелись к «делится ab» и «делятся ab и bc»
            if n == 1:
                if mbc is not None:
                    a, b, c, mab = b, c, a, mbc
                elif mca is not None:
                    a, b, c, mab = c, a, b, mca
                new = [(a, mab, c), (mab, b, c)]
            elif n == 2:
                if mab is None:
                    a, b, c, mab, mbc = b, c, a, mbc, mca
                elif mbc is None:
                    a, b, c, mab, mbc = c, a, b, mca, mab
                new = [(mab, b, mbc), (a, mab, mbc), (a, mbc, c)]
            else:
                new = [(a, mab, mca), (mab, b, mbc), (mca, mbc, c), (mab, mbc, mca)]
            out += [(x, y, z, mat, smooth) for x, y, z in new if x != y and y != z and x != z]
        tris = out
        if not split:
            break
    return tris


def open_edges(tris):
    """Сколько рёбер не закрыто: у замкнутой сетки каждое ребро пройдено поровну в обе стороны."""
    cnt = {}
    for a, b, c, _, _ in tris:
        for p, q in ((a, b), (b, c), (c, a)):
            cnt[(p, q)] = cnt.get((p, q), 0) + 1
    return sum(1 for (p, q), n in cnt.items() if cnt.get((q, p), 0) != n)


def canonical(tris):
    """Один порядок на любой запуск: наименьшая вершина треугольника первой, треугольники по возрастанию."""
    out = []
    for a, b, c, mat, smooth in tris:
        if b < a and b <= c:
            a, b, c = b, c, a
        elif c < a and c < b:
            a, b, c = c, a, b
        out.append((a, b, c, mat, smooth))
    out.sort()
    return out


def _planes(tris, nrm):
    """Плоские участки: соседние по ребру треугольники одной группы сглаживания с одной нормалью — один участок.
    Возвращает номер участка на треугольник и площадь (×2) участка."""
    parent = list(range(len(tris)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    by_edge = {}
    for k, (a, b, c, _, g) in enumerate(tris):
        if not g or nrm[k][1] is None:
            continue
        for p, q in ((a, b), (b, c), (c, a)):
            by_edge.setdefault((p, q) if p < q else (q, p), []).append(k)
    for ks in by_edge.values():
        for i in range(len(ks)):
            for j in range(i + 1, len(ks)):
                x, y = ks[i], ks[j]
                if tris[x][4] == tris[y][4] and dot(nrm[x][1], nrm[y][1]) > 0.999999:
                    rx, ry = find(x), find(y)
                    if rx != ry:
                        parent[max(rx, ry)] = min(rx, ry)
    root = [find(k) for k in range(len(tris))]
    area = {}
    for k, r in enumerate(root):
        if nrm[k][1] is not None:
            area[r] = area.get(r, 0.0) + dot(nrm[k][0], nrm[k][0]) ** 0.5
    return root, area


def build_mesh(tris, cos_smooth=0.5, dominance=4.0):
    """Треугольники → Mesh (tools/meshgen.py). Поле «гладкая» треугольника — группа сглаживания: 0/False —
    своя нормаль, иначе нормаль угла делят грани той же группы у этой вершины не круче 60° к своей.

    Нормали взвешенные (docs/parts.md, «Гладкость»): если среди соседей вершины есть плоский участок, площадь
    которого в dominance раз больше всех остальных соседей вместе, угол берёт ровно его нормаль — плоская грань
    остаётся плоской до самой кромки, а фаска или скругление между двумя гранями плавно переходит от одной нормали
    к другой. Иначе (бок цилиндра, ком) — сумма нормалей соседей с весом площади."""
    mats = sorted({t[3] for t in tris})
    mi = {m: k for k, m in enumerate(mats)}
    nrm = []
    for a, b, c, _, _ in tris:
        n = cross(sub(b, a), sub(c, a))
        l = dot(n, n) ** 0.5
        nrm.append((n, (n[0] / l, n[1] / l, n[2] / l) if l > 1e-15 else None))
    root, area = _planes(tris, nrm)
    at = {}
    for k, (a, b, c, _, g) in enumerate(tris):
        if g and nrm[k][1] is not None:
            for p in (a, b, c):
                at.setdefault((p, g), []).append(k)
    m = Mesh(mats)
    for k, (a, b, c, mat, g) in enumerate(tris):
        own = nrm[k][1]
        for p in (a, b, c):
            n = own
            if g and own is not None:
                near = [j for j in at[(p, g)] if dot(nrm[j][1], own) >= cos_smooth]
                regions = {}
                for j in near:
                    regions[root[j]] = area[root[j]]
                top = max(regions, key=lambda r: (regions[r], -r))
                rest = sum(v for r, v in regions.items() if r != top)
                if regions[top] >= dominance * rest and len(regions) > 1:
                    n = nrm[top][1]
                else:
                    sx = sy = sz = 0.0
                    for j in near:
                        sx += nrm[j][0][0]
                        sy += nrm[j][0][1]
                        sz += nrm[j][0][2]
                    l = (sx * sx + sy * sy + sz * sz) ** 0.5
                    if l > 1e-15:
                        n = (sx / l, sy / l, sz / l)
            if n is None:
                n = (0.0, 1.0, 0.0)
            m.verts.append((p[0], p[1], p[2], n[0], n[1], n[2], mi[mat], mi[mat], 0, 255))
        base = len(m.verts) - 3
        m.indices += [base, base + 1, base + 2]
    return m


def write(name, body, out_dir, meshpack, boxes=(), max_edge=2.5):
    """Тело → data/meshes/<имя>.mesh. boxes — коллайдер: коробки (x0, x1, y0, y1, z0, z1) в осях сетки.
    Возвращает (треугольников, незакрытых рёбер)."""
    tris = canonical(subdivide(triangles(body), max_edge))
    bad = open_edges(tris)
    m = build_mesh(tris)
    for x0, x1, y0, y1, z0, z1 in boxes:
        b = (((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), ((x1 - x0) / 2, (y1 - y0) / 2, (z1 - z0) / 2),
             (0.0, 0.0, 0.0, 1.0))
        m.collider.append(b)
        m.coarse.append(b)
    raw = os.path.join(out_dir, name + ".raw")
    with open(raw, "wb") as f:
        f.write(m.encode())
    try:
        subprocess.run([meshpack, raw, os.path.join(out_dir, name + ".mesh")], check=True)
    finally:
        os.remove(raw)
    return len(tris), bad
