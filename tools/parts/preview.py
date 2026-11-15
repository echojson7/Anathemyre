#!/usr/bin/env python3
"""Лист просмотра детали (docs/parts.md): быстрый кадр тел фабрики трассировкой Blender, без игры.

    python tools/parts/preview.py out.jpg --cam x y z --to x y z [--lens 22] [--size 1000 750] [--samples 24]
                                  [--light x y z сила]… [имена сеток]

Только для проверки формы глазами: свет и поверхности здесь не игровые (ровные цвета по имени материала).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

COLORS = {
    "concrete": (0.42, 0.40, 0.36), "concrete_floor": (0.30, 0.27, 0.24), "concrete_soffit": (0.38, 0.36, 0.33),
    "concrete_break": (0.52, 0.49, 0.44), "iron_cast": (0.25, 0.13, 0.08), "steel_paint": (0.50, 0.48, 0.42),
    "steel_galv": (0.50, 0.52, 0.53), "rubber_black": (0.03, 0.03, 0.03), "wood_tar": (0.16, 0.11, 0.07),
    "rust_metal": (0.33, 0.16, 0.08), "lamp_glass": (1.0, 0.95, 0.8), "lamp_red": (1.0, 0.1, 0.03),
}


def take(args, flag, n, default=None):
    out = []
    while flag in args:
        i = args.index(flag)
        out.append([float(v) for v in args[i + 1:i + 1 + n]])
        del args[i:i + 1 + n]
    return out or ([default] if default is not None else [])


def main():
    args = sys.argv[1:]
    path = args.pop(0)
    cam = take(args, "--cam", 3)[0]
    to = take(args, "--to", 3)[0]
    lens = take(args, "--lens", 1, [22.0])[0][0]
    size = take(args, "--size", 2, [1000.0, 750.0])[0]
    samples = int(take(args, "--samples", 1, [24.0])[0][0])
    lights = take(args, "--light", 4)
    import ispod_meshes
    bodies = [make()[0] for name, make in ispod_meshes.MESHES.items() if not args or name in args]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    mats = {}
    for n, body in enumerate(bodies):
        by_mat = {}
        for pts, mat, _ in body.faces:
            by_mat.setdefault(mat, []).append(pts)
        for mat, faces in by_mat.items():
            verts, index, polys = [], {}, []
            for pts in faces:
                f = []
                for p in pts:
                    k = index.get(p)
                    if k is None:
                        k = index[p] = len(verts)
                        verts.append(p)
                    f.append(k)
                polys.append(f)
            me = bpy.data.meshes.new("m")
            me.from_pydata(verts, [], polys)
            if mat not in mats:
                m = bpy.data.materials.new(str(mat))
                m.use_nodes = True
                b = m.node_tree.nodes["Principled BSDF"]
                c = COLORS.get(mat, (0.5, 0.2, 0.5))
                b.inputs["Base Color"].default_value = (*c, 1)
                b.inputs["Roughness"].default_value = 0.85
                if mat and mat.startswith("lamp"):
                    b.inputs["Emission Color"].default_value = (*c, 1)
                    b.inputs["Emission Strength"].default_value = 20.0
                mats[mat] = m
            me.materials.append(mats[mat])
            sc.collection.objects.link(bpy.data.objects.new("o", me))
    for x, y, z, power in lights:
        d = bpy.data.lights.new("l", 'POINT')
        d.energy = power
        d.shadow_soft_size = 0.1
        ob = bpy.data.objects.new("l", d)
        ob.location = (x, y, z)
        sc.collection.objects.link(ob)
    c = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(c)
    c.location = cam
    # Игра: y вверх. Камера Blender смотрит вдоль −Z, верх кадра — мировая ось y
    fwd = (Vector(to) - Vector(cam)).normalized()
    right = fwd.cross(Vector((0, 1, 0))).normalized()
    up = right.cross(fwd)
    c.matrix_world = Matrix(((right.x, up.x, -fwd.x, cam[0]), (right.y, up.y, -fwd.y, cam[1]),
                             (right.z, up.z, -fwd.z, cam[2]), (0, 0, 0, 1)))
    c.data.lens = lens
    c.data.clip_start = 0.05
    c.data.clip_end = 500
    sc.camera = c
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = int(size[0]), int(size[1])
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'JPEG'
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.6, 0.65, 0.75, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
    sc.world = w
    bpy.ops.render.render(write_still=True)
    # У игры левая тройка осей (восток справа, когда смотришь на север), у Blender правая: отразить кадр
    from PIL import Image, ImageOps
    ImageOps.mirror(Image.open(path)).save(path, quality=88)


if __name__ == "__main__":
    main()
