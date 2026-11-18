#!/usr/bin/env python3
"""Линт ассетов (docs/assetlint.md): python3 tools/assetlint/assetlint.py [сетки…] [--rules файл] [--verbose]

Без имён — все data/meshes/*.mesh. Проверки: замкнутость тел и Т-стыки, щели между телами, тонкие наружные
стены (только предупреждение), хорда гладких поверхностей LOD 0, фаски одним сегментом, бюджет треугольников и
ошибка LOD. Нарушение — ошибка, если в rules.txt нет строки allow с причиной; число в allow — потолок: больше
нарушений — снова ошибка (исключение не растёт молча).
"""
import fnmatch
import glob
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import geom  # noqa: E402
from meshfile import MeshFile  # noqa: E402

CHECKS = ("open", "tjunc", "gap", "chord", "chamfer", "budget", "lod")


def load_rules(path):
    """rules.txt: «budget шаблон треугольников [lodN ошибка]…» и «allow шаблон проверка [потолок] — причина»."""
    budgets, allows = [], []
    for no, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "—" in line:
            head, reason = line.split("—", 1)
            reason = reason.strip()
        else:
            head, reason = line, ""
        w = head.split()
        if w[0] == "budget":
            lods = {}
            for k in range(3, len(w), 2):
                lods[int(w[k][3:])] = float(w[k + 1])
            budgets.append((w[1], int(w[2]), lods))
        elif w[0] == "allow":
            if w[2] not in CHECKS:
                sys.exit("%s:%d: нет проверки %s" % (path, no, w[2]))
            if not reason:
                sys.exit("%s:%d: исключение без причины" % (path, no))
            cap = int(w[3]) if len(w) > 3 else None
            allows.append((w[1], w[2], cap, reason))
        else:
            sys.exit("%s:%d: непонятная строка" % (path, no))
    return budgets, allows


def fmt(p):
    return "(%.2f %.2f %.2f)" % tuple(p)


def lint(path, budgets):
    """Одна сетка → {проверка: (число нарушений, [места])}, предупреждения, сводка."""
    m = MeshFile(path)
    name = os.path.splitext(os.path.basename(path))[0]
    res = {}
    tris0 = m.lod(0)
    P, T = geom.welded_tris(m.pos, tris0)
    vcomp, tcomp = geom.components(T, len(P))
    nbody = int(vcomp.max()) + 1 if len(vcomp) else 0
    oe = geom.open_edges(T, len(P))
    # Т-стык: вершина лежит внутри незакрытого ребра того же тела
    tj = []
    if len(oe):
        a, b = P[oe[:, 0]], P[oe[:, 1]]
        vs = np.unique(oe)
        pi, ei = geom.candidates(P[vs], np.minimum(a, b) - 1e-5, np.maximum(a, b) + 1e-5, 0.02)
        keep = (vs[pi] != oe[ei, 0]) & (vs[pi] != oe[ei, 1])
        pi, ei = pi[keep], ei[keep]
        ab = b[ei] - a[ei]
        t = np.einsum("ij,ij->i", P[vs[pi]] - a[ei], ab) / np.maximum(np.einsum("ij,ij->i", ab, ab), 1e-30)
        q = a[ei] + ab * t[:, None]
        d = np.linalg.norm(P[vs[pi]] - q, axis=1)
        hit = (t > 1e-4) & (t < 1 - 1e-4) & (d < 1e-5)
        tj = [tuple(P[v]) for v in np.unique(vs[pi[hit]])]
    open_bodies = np.unique(vcomp[oe[:, 0]]) if len(oe) else []
    res["open"] = (len(open_bodies), [fmt(P[oe[0, 0]])] if len(oe) else [])
    res["tjunc"] = (len(tj), [fmt(p) for p in tj[:3]])
    g = geom.gaps(P, T, vcomp, tcomp)
    res["gap"] = (len(g), ["%.1f мм %s" % (d * 1000, fmt(p)) for d, p, _, _ in g[:3]])
    s, mid = geom.chord(m.pos, m.nrm, tris0)
    res["chord"] = (len(s), ["%.2f мм %s" % (s[k] * 1000, fmt(mid[k])) for k in range(min(3, len(s)))])
    ch = geom.chamfers(P, T)
    res["chamfer"] = (len(ch), ["%.0f см %s" % (ln * 100, fmt(c)) for ln, c in ch[:3]])
    over, lodbad = [], []
    for pat, cap, lods in budgets:
        if fnmatch.fnmatch(name, pat):
            if len(tris0) > cap:
                over.append("%d > %d" % (len(tris0), cap))
            for k, e in lods.items():
                if k >= len(m.lods):
                    lodbad.append("нет LOD %d" % k)
                elif m.lods[k][2] > e:
                    lodbad.append("LOD %d: %.3f м > %.3f" % (k, m.lods[k][2], e))
            break
    else:
        over.append("нет бюджета в rules.txt")
    res["budget"] = (len(over), over)
    res["lod"] = (len(lodbad), lodbad)
    warn = []
    if m.boxes and not len(oe):
        tw = geom.thin_walls(P, T)
        if tw:
            # места по метровым клеткам: одна стена — одна строка
            cells = {}
            for th, c in tw:
                k = tuple(int(np.floor(x)) for x in c)
                if k not in cells or th < cells[k][0]:
                    cells[k] = (th, c)
            lst = sorted(cells.values(), key=lambda x: x[0])
            warn.append("тонкие стены (< %.1f м): %d мест, %s" % (geom.WALL, len(lst), ", ".join(
                "%.1f м %s" % (th, fmt(c)) for th, c in lst[:6])))
    info = "треугольников %d, тел %d, LOD %s" % (len(tris0), nbody, "/".join("%.3f" % l[2] for l in m.lods[1:]) or "—")
    return name, res, warn, info


def main():
    args = sys.argv[1:]
    rules = os.path.join(HERE, "rules.txt")
    verbose = False
    if "--rules" in args:
        k = args.index("--rules")
        rules = args[k + 1]
        del args[k:k + 2]
    if "--verbose" in args:
        verbose = True
        args.remove("--verbose")
    paths = args or sorted(glob.glob(os.path.join(HERE, "..", "..", "data", "meshes", "*.mesh")))
    budgets, allows = load_rules(rules)
    used = set()
    errors = 0
    t0 = time.time()
    for path in paths:
        t1 = time.time()
        name, res, warn, info = lint(path, budgets)
        lines = []
        for ck in CHECKS:
            n, where = res[ck]
            if not n:
                continue
            rule = next(((k, a) for k, a in enumerate(allows) if a[1] == ck and fnmatch.fnmatch(name, a[0])), None)
            if rule is not None:
                used.add(rule[0])
                cap = rule[1][2]
                if cap is None or n <= cap:
                    if verbose:
                        lines.append("  допущено %-7s %d%s — %s" % (ck, n, "" if cap is None else " из %d" % cap, rule[1][3]))
                    continue
                lines.append("  ОШИБКА  %-7s %d, потолок исключения %d: %s" % (ck, n, cap, "; ".join(where)))
            else:
                lines.append("  ОШИБКА  %-7s %d: %s" % (ck, n, "; ".join(where)))
            errors += 1
        for w in warn:
            lines.append("  внимание " + w)
        print("%-18s %s  (%.1f с)" % (name, info, time.time() - t1))
        for ln in lines:
            print(ln)
    if not args:
        for k, a in enumerate(allows):
            if k not in used:
                print("лишнее исключение (нарушений нет): allow %s %s — убрать из rules.txt" % (a[0], a[1]))
                errors += 1
    print("линт ассетов: %d сеток, ошибок %d, %.0f с" % (len(paths), errors, time.time() - t0))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
