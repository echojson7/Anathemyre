#!/usr/bin/env python3
"""Собрать детали фабрики (docs/parts.md) в data/meshes: python tools/parts/build.py <папка> <meshpack> [имена…]

Нужен Blender как модуль Python (pip install bpy) — поэтому отдельно от `make meshes`: `make parts`.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export  # noqa: E402


def main():
    if len(sys.argv) < 3:
        sys.exit("использование: build.py <папка> <meshpack> [имена сеток]")
    out_dir, meshpack, only = sys.argv[1], sys.argv[2], sys.argv[3:]
    import ispod_meshes
    for name, make in ispod_meshes.MESHES.items():
        if only and name not in only:
            continue
        t0 = time.time()
        body, boxes = make()
        tris, bad = export.write(name, body, out_dir, meshpack, boxes)
        print("%-16s треугольников %7d  незакрытых рёбер %d  коробок %d  %.1f с" % (name, tris, bad, len(boxes), time.time() - t0))
        if bad:
            sys.exit("сетка %s не замкнута" % name)


if __name__ == "__main__":
    main()
