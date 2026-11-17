#!/usr/bin/env python3
"""Лист просмотра машины на костях (docs/vezha.md): кадр трассировкой Blender без игры, в позе.

    python tools/parts/rig_preview.py <папка> [--shot имя поза cx cy cz tx ty tz]… [--lens 35] [--size 1200 800]
                                      [--samples 48] [--hull град] [--gun град] [--parts]

Позы — vezha_pose.POSES (stand, turret, high, low). Каждый --shot — кадр <папка>/<имя>.jpg.

Поверхности здесь не игровые: ровные цвета материалов, сколы краски — по доле износа вершины, затенение — из
вершин. Нужен для формы, пропорций и поз; свет и узор оценивают в игре.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rig as R  # noqa: E402
import vezha  # noqa: E402
import vezha_pose  # noqa: E402

COLORS = {  # цвет, шероховатость, металл
    "vezha_paint": ((0.55, 0.24, 0.035), 0.6, 0.0), "vezha_worn": ((0.20, 0.17, 0.15), 0.5, 0.8),
    "vezha_iron": ((0.085, 0.08, 0.075), 0.55, 0.7), "vezha_steel": ((0.45, 0.45, 0.46), 0.3, 1.0),
    "rubber_black": ((0.02, 0.02, 0.022), 0.8, 0.0), "vezha_glass": ((0.01, 0.012, 0.015), 0.08, 0.0),
    "vezha_eye": ((1.0, 0.07, 0.03), 0.4, 0.0), "vezha_mark": ((0.03, 0.028, 0.025), 0.7, 0.0),
    "vezha_brass": ((0.62, 0.45, 0.17), 0.35, 1.0), "vezha_canvas": ((0.20, 0.185, 0.13), 0.9, 0.0),
    "vezha_olive": ((0.17, 0.19, 0.10), 0.66, 0.0),
}


def take(args, flag, n, default):
    if flag in args:
        i = args.index(flag)
        out = [float(v) for v in args[i + 1:i + 1 + n]]
        del args[i:i + 1 + n]
        return out
    return default


def main():
    args = sys.argv[1:]
    out_dir = args.pop(0)
    lens = take(args, "--lens", 1, [35.0])[0]
    size = take(args, "--size", 2, [1200.0, 800.0])
    samples = int(take(args, "--samples", 1, [48.0])[0])
    hull_deg = take(args, "--hull", 1, [0.0])[0]
    gun_deg = take(args, "--gun", 1, [0.0])[0]
    shots = []
    while "--shot" in args:
        i = args.index("--shot")
        shots.append((args[i + 1], args[i + 2], [float(v) for v in args[i + 3:i + 6]], [float(v) for v in args[i + 6:i + 9]]))
        del args[i:i + 9]
    if not shots:
        shots = [("stand", "stand", [3.2, 1.9, 3.4], [0.0, 0.95, 0.1])]
    rig = vezha.make()
    built = R.build(rig, log=print if "--parts" in args else None)
    print("треугольников %d, незакрытых рёбер %d" % (len(built["tris"]), built["open"]))
    for name, pose_name, cam, to in shots:
        render(rig, built, os.path.join(out_dir, name + ".jpg"), pose_name, cam, to, lens, size, samples, hull_deg, gun_deg)


def render(rig, built, path, pose_name, cam, to, lens, size, samples, hull_deg, gun_deg):
    rot, trans = vezha_pose.pose(rig, pose_name, hull=math.radians(hull_deg), gun=math.radians(gun_deg))
    mats = R.pose(rig, rot, trans)
    heads = np.asarray([b[2] for b in rig.bones])
    P = np.asarray([v[0] for v in built["verts"]], dtype=np.float64)
    out = np.zeros_like(P)
    for k in range(4):
        bi = np.asarray([v[6][k] for v in built["verts"]])
        w = np.asarray([v[7][k] for v in built["verts"]], dtype=np.float64) / 255.0
        for b in np.unique(bi[w > 0]):
            sel = (bi == b) & (w > 0)
            Rm, p = mats[b]
            out[sel] += w[sel, None] * ((P[sel] - heads[b]) @ Rm.T + p)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    me = bpy.data.meshes.new("vezha")
    me.from_pydata([tuple(p) for p in out], [], built["tris"])
    wear = me.color_attributes.new("wear", 'FLOAT_COLOR', 'POINT')
    for i, v in enumerate(built["verts"]):
        wear.data[i].color = (v[4] / 255.0, v[5] / 255.0, 0.0, 1.0)
    slots = {}
    for ti, t in enumerate(built["tris"]):
        v = built["verts"][t[0]]
        key = (v[2], v[3])
        if key not in slots:
            c0, r0, m0 = COLORS.get(built["mats"][v[2]], ((0.5, 0.1, 0.5), 0.5, 0.0))
            c1, r1, m1 = COLORS.get(built["mats"][v[3]], ((0.5, 0.1, 0.5), 0.5, 0.0))
            m = bpy.data.materials.new("m%d_%d" % key)
            m.use_nodes = True
            nt = m.node_tree
            bs = nt.nodes["Principled BSDF"]
            att = nt.nodes.new("ShaderNodeAttribute")
            att.attribute_name = "wear"
            sep = nt.nodes.new("ShaderNodeSeparateColor")
            nt.links.new(att.outputs["Color"], sep.inputs["Color"])
            noise = nt.nodes.new("ShaderNodeTexNoise")
            noise.inputs["Scale"].default_value = 28.0
            noise.inputs["Detail"].default_value = 6.0
            less = nt.nodes.new("ShaderNodeMath")
            less.operation = 'LESS_THAN'
            nmul = nt.nodes.new("ShaderNodeMath")
            nmul.operation = 'MULTIPLY_ADD'
            nmul.inputs[1].default_value = 1.5
            nmul.inputs[2].default_value = -0.25
            nt.links.new(noise.outputs["Fac"], nmul.inputs[0])
            nt.links.new(nmul.outputs[0], less.inputs[0])
            nt.links.new(sep.outputs["Red"], less.inputs[1])
            mix = nt.nodes.new("ShaderNodeMix")
            mix.data_type = 'RGBA'
            mix.inputs["A"].default_value = (*c0, 1)
            mix.inputs["B"].default_value = (*c1, 1)
            nt.links.new(less.outputs[0], mix.inputs["Factor"])
            ao = nt.nodes.new("ShaderNodeMix")
            ao.data_type = 'RGBA'
            ao.blend_type = 'MULTIPLY'
            ao.inputs["Factor"].default_value = 1.0
            nt.links.new(mix.outputs["Result"], ao.inputs["A"])
            gray = nt.nodes.new("ShaderNodeCombineColor")
            for ch in ("Red", "Green", "Blue"):
                nt.links.new(sep.outputs["Green"], gray.inputs[ch])
            nt.links.new(gray.outputs["Color"], ao.inputs["B"])
            nt.links.new(ao.outputs["Result"], bs.inputs["Base Color"])
            mr = nt.nodes.new("ShaderNodeMix")
            mr.inputs["A"].default_value = m0
            mr.inputs["B"].default_value = m1
            nt.links.new(less.outputs[0], mr.inputs["Factor"])
            nt.links.new(mr.outputs["Result"], bs.inputs["Metallic"])
            bs.inputs["Roughness"].default_value = r0
            if built["mats"][v[2]] == "vezha_eye":
                bs.inputs["Emission Color"].default_value = (*c0, 1)
                bs.inputs["Emission Strength"].default_value = 6.0
            slots[key] = len(me.materials)
            me.materials.append(m)
        me.polygons[ti].material_index = slots[key]
    # нормали вершин — из сетки (гладкие грани)
    ob = bpy.data.objects.new("vezha", me)
    sc.collection.objects.link(ob)
    for p in me.polygons:
        p.use_smooth = False
    # земля и свет: песок, солнце сбоку, небо
    g = bpy.data.meshes.new("ground")
    g.from_pydata([(-30, 0, -30), (30, 0, -30), (30, 0, 30), (-30, 0, 30)], [], [(0, 3, 2, 1)])
    gm = bpy.data.materials.new("sand")
    gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.42, 0.30, 0.19, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    g.materials.append(gm)
    sc.collection.objects.link(bpy.data.objects.new("ground", g))
    sun = bpy.data.lights.new("sun", 'SUN')
    sun.energy = 4.0
    sun.angle = math.radians(1.5)
    so = bpy.data.objects.new("sun", sun)
    d = Vector((-0.55, -0.62, -0.35)).normalized()  # куда светит
    so.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.collection.objects.link(so)
    c = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(c)
    fwd = (Vector(to) - Vector(cam)).normalized()
    right = fwd.cross(Vector((0, 1, 0))).normalized()
    up = right.cross(fwd)
    c.matrix_world = Matrix(((right.x, up.x, -fwd.x, cam[0]), (right.y, up.y, -fwd.y, cam[1]),
                             (right.z, up.z, -fwd.z, cam[2]), (0, 0, 0, 1)))
    c.data.lens = lens
    c.data.clip_start = 0.05
    sc.camera = c
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = int(size[0]), int(size[1])
    sc.render.filepath = path
    sc.render.image_settings.file_format = 'JPEG'
    sc.view_settings.view_transform = 'AgX'
    w = bpy.data.worlds.new("w")
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.68, 0.9, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.1
    sc.world = w
    bpy.ops.render.render(write_still=True)
    from PIL import Image, ImageOps
    ImageOps.mirror(Image.open(path)).save(path, quality=90)  # у игры левая тройка осей


if __name__ == "__main__":
    main()
