#!/usr/bin/env python3
"""Пилот гибрида (docs/bake.md): сетки и карты двух секций Исподи — «было» и «стало».

    python tools/bake/build.py <data> <meshpack> <texpack> [old kit fill cast]

Нужен bpy 5.3. Сетки — data/meshes/ispod_pilot_*.mesh, карты литого узла — data/textures/ispod_pilot_cast_*.tex.
Печатает строки part и light заготовки data/prefabs/ispod_pilot.txt.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "parts"))
import pilot as P  # noqa: E402

GAP = -12.0     # «стало» к югу от «было», по z заготовки
LIFT = 0.05     # пол секции над землёй площадки


def hidden(c, n):
    """Грань литого узла не видна: заход в западную стену, в северную, низ в полу."""
    return c[0] < -0.005 or c[1] < -0.005 or c[2] > P.Z_N + 0.005


def main():
    data, meshpack, texpack = sys.argv[1:4]
    only = sys.argv[4:] or ["old", "kit", "fill", "cast"]
    meshes = os.path.join(data, "meshes")
    import export
    from kit import Body
    from plan import colliders
    boxes = colliders(P.collider_ops())
    if "old" in only:
        from solid import solid
        t0 = time.time()
        body = solid([(o, b.body()) for o, b in P.room_ops()], bevel=0.012)
        out = Body()
        for pts, _, _ in body.faces:
            a, b, c = pts
            u = [b[k] - a[k] for k in range(3)]
            v = [c[k] - a[k] for k in range(3)]
            ny = u[2] * v[0] - u[0] * v[2]
            l = sum(x * x for x in (u[1] * v[2] - u[2] * v[1], ny, u[0] * v[1] - u[1] * v[0])) ** 0.5
            ny = ny / l if l > 1e-12 else 0.0
            mid = (a[1] + b[1] + c[1]) / 3
            mat = "concrete_floor" if ny > 0.8 and mid < P.L1 + 0.05 else ("concrete_soffit" if ny < -0.8 else "concrete")
            out.faces.append((pts, mat, True))
        n, bad = export.write("ispod_pilot_old", out, meshes, meshpack, boxes)
        print("ispod_pilot_old   треугольников %7d  незакрытых рёбер %d  %.0f с" % (n, bad, time.time() - t0))
    if "kit" in only:
        body = Body()
        for _, b in P.kit_parts():
            body.extend(b)
        n, bad = export.write("ispod_pilot_kit", body, meshes, meshpack, boxes)
        print("ispod_pilot_kit   треугольников %7d  незакрытых рёбер %d  деталей %d" % (n, bad, len(P.kit_parts())))
    if "fill" in only:
        n, bad = export.write("ispod_pilot_fill", P.fill(), meshes, meshpack)
        print("ispod_pilot_fill  треугольников %7d  незакрытых рёбер %d" % (n, bad))
    if "cast" in only:
        import bake
        size = int(os.environ.get("SIZE", "2048"))
        n = bake.cast("ispod_pilot_cast", P.march_ops(), P.march_chips(), hidden, data, meshpack, texpack, size)
        print("ispod_pilot_cast  треугольников %7d  карты %d²" % (n, size))
    lines = []
    for mesh, dz in (("ispod_pilot_old", 0.0), ("ispod_pilot_kit", GAP), ("ispod_pilot_cast", GAP)):
        lines.append("part %s 0 %s %s" % (mesh, P._n(LIFT), P._n(dz)))
    for dz in (0.0, GAP):
        lines.append("part ispod_pilot_fill 0 %s %s" % (P._n(LIFT), P._n(dz)))
        lines += P.lamp_lines(dz, LIFT)
    print("\n".join(lines))


main()
