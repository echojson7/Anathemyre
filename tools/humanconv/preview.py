#!/usr/bin/env python3
"""Лист просмотра людей (docs/people.md, «Тело»): ряд фигур с морфами трассировкой Blender, без игры.

    python tools/humanconv/preview.py out.jpg [--mesh human_m[,вещь…]] [--lod 0] [--size 1600 800] [--view front|side|back|head]
        "female=1,full=0.6,scale=0.94" "thin=1" …        фигура — веса морфов через запятую, scale — рост

Только форма: свет и цвета здесь не игровые. Нужен bpy (pip install bpy==5.3.0), как у tools/parts/preview.py.
Поза — бинд (A-поза): суставы от морфов тут не нужны.
"""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meshread  # noqa: E402

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

_SK, _LN, _BD = (0.52, 0.37, 0.28), (0.33, 0.31, 0.26), (0.17, 0.16, 0.13)
# Цвет материала у мужчины и у женщины (бельё по полу, data/materials.txt): женщина — фигура с female > 0.5
COLORS = {
    "human_skin": (_SK, _SK), "human_linen": (_LN, _LN), "human_band": (_BD, _BD),
    "human_eye": ((0.46, 0.43, 0.39),) * 2, "human_iris": ((0.07, 0.05, 0.04),) * 2,
    "human_bra": (_SK, _LN), "human_bra_band": (_SK, _BD), "human_boxer": (_LN, _SK), "human_boxer_band": (_BD, _SK),
    "human_lb": (_LN, _BD), "human_bl": (_BD, _LN), "human_lip": ((0.48, 0.31, 0.23),) * 2,
    "human_areola": ((0.42, 0.25, 0.18), _LN), "human_nipple": ((0.36, 0.21, 0.15), _LN),
}
VIEWS = {  # куда смотрит камера: (сдвиг камеры от середины ряда, высота цели, объектив)
    "front": ((0.0, 0.05, 1.0), 0.92, 50.0), "side": ((1.0, 0.05, 0.0), 0.92, 50.0),
    "back": ((0.0, 0.05, -1.0), 0.92, 50.0), "q": ((0.6, 0.1, 0.8), 0.92, 50.0),
    "head": ((0.25, 0.0, 1.0), 1.66, 50.0),
}


def opt(args, flag, n, default):
    if flag not in args:
        return default
    i = args.index(flag)
    v = args[i + 1:i + 1 + n]
    del args[i:i + 1 + n]
    return v


def main():
    args = sys.argv[1:]
    path = args.pop(0)
    names = opt(args, "--mesh", 1, ["human_m"])[0].split(",")
    lod = int(opt(args, "--lod", 1, ["0"])[0])
    size = [int(x) for x in opt(args, "--size", 2, ["1600", "800"])]
    view = opt(args, "--view", 1, ["front"])[0]
    gap = float(opt(args, "--gap", 1, ["0.85"])[0])
    samples = int(opt(args, "--samples", 1, ["24"])[0])
    data = opt(args, "--data", 1, ["data/meshes"])[0]
    figures = args or [""]
    meshes = [meshread.read(os.path.join(data, n + ".mesh")) for n in names]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    mats = {}
    row = (len(figures) - 1) * gap
    for f, spec in enumerate(figures):
        w = {k: float(x) for k, x in (t.split("=") for t in spec.split(",") if t)}
        scale = w.pop("scale", 1.0)
        # Ряд идёт вдоль оси, поперечной взгляду: сбоку фигуры не перекрывают друг друга
        along = np.array([0.0, 0.0, 1.0]) if view == "side" else np.array([1.0, 0.0, 0.0])
        at = along * (row / 2 - f * gap)  # кадр потом отражается: первая фигура — слева
        for m in meshes:
            p, n = meshread.shaped(m, w)
            tri = meshread.lod_triangles(m, lod)
            me = bpy.data.meshes.new("m")
            me.from_pydata((p * scale + at).tolist(), [], tri[:, ::-1].tolist())
            me.polygons.foreach_set("use_smooth", [True] * len(tri))
            sex = 1 if w.get("female", 0.0) > 0.5 else 0
            for name in m.materials:
                key = (name, sex)
                if key not in mats:
                    mt = bpy.data.materials.new(name)
                    mt.use_nodes = True
                    b = mt.node_tree.nodes["Principled BSDF"]
                    b.inputs["Base Color"].default_value = (*COLORS.get(name, ((0.5, 0.2, 0.5),) * 2)[sex], 1)
                    b.inputs["Roughness"].default_value = 0.7
                    mats[key] = mt
                me.materials.append(mats[key])
            me.polygons.foreach_set("material_index", m.vert["m0"][tri[:, 0]].astype(int).tolist())
            me.update()
            sc.collection.objects.link(bpy.data.objects.new("o", me))
    # Пол
    fl = bpy.data.meshes.new("floor")
    fl.from_pydata([(-20, 0, -20), (20, 0, -20), (20, 0, 20), (-20, 0, 20)], [], [(0, 1, 2, 3)])
    sc.collection.objects.link(bpy.data.objects.new("floor", fl))

    off, ty, lens = VIEWS[view]
    # Расстояние: ряд и рост (или голова) целиком в кадре
    th = 18.0 / lens  # тангенс половины угла по ширине (кадр 36 мм)
    wide, tall = (row + 0.36, 0.44) if view == "head" else (row + 1.3, 2.05)
    dist = max(wide / (2 * th), tall / (2 * th * size[1] / size[0]))
    to = Vector((0, ty, 0))
    cam = to + Vector(off).normalized() * dist
    c = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(c)
    fwd = (to - cam).normalized()
    right = fwd.cross(Vector((0, 1, 0))).normalized()
    up = right.cross(fwd)
    c.matrix_world = Matrix(((right.x, up.x, -fwd.x, cam.x), (right.y, up.y, -fwd.y, cam.y),
                             (right.z, up.z, -fwd.z, cam.z), (0, 0, 0, 1)))
    c.data.lens = lens
    c.data.clip_start = 0.05
    sc.camera = c
    sun = bpy.data.lights.new("sun", 'SUN')
    sun.energy = 3.0
    sun.angle = 0.05
    so = bpy.data.objects.new("sun", sun)
    d = (Vector(off).normalized() + Vector((0.5, 1.2, 0.3))).normalized()  # свет сверху-сбоку со стороны камеры
    z = d
    x = Vector((0, 1, 0)).cross(z).normalized()
    y = z.cross(x)
    so.matrix_world = Matrix(((x.x, y.x, z.x, 0), (x.y, y.y, z.y, 5), (x.z, y.z, z.z, 0), (0, 0, 0, 1)))
    sc.collection.objects.link(so)
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'JPEG'
    wd = bpy.data.worlds.new("w")
    wd.use_nodes = True
    wd.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.6, 0.7, 1)
    wd.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
    sc.world = wd
    bpy.ops.render.render(write_still=True)
    # У игры левая тройка осей, у Blender правая: отразить кадр
    from PIL import Image, ImageOps
    ImageOps.mirror(Image.open(path)).save(path, quality=90)


if __name__ == "__main__":
    main()
