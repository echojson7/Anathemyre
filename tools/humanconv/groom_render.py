"""Снимок причёски волосками в Cycles (docs/hair.md, «Волоски и карточки»): тело стенда и волоски groom.py, свет —
ключ, контровой и заполняющий. Сравнение с концептом той же камерой; цель для карточек.

    python3 -I tools/humanconv/groom_render.py groom.npz body.npz out.png [--view front|q|back|side]
        [--samples 64] [--size 900x1100]
Нужен bpy 5.3 (как tools/hairtex/strands.py), вне CI.
"""
import argparse

import bpy
import numpy as np

VIEWS = {  # положение камеры, точка взгляда, фокусное
    "front": ((0.0, 1.66, 0.75), (0.0, 1.63, 0.05), 70),
    "q": ((0.45, 1.68, 0.62), (0.0, 1.64, 0.04), 70),
    "side": ((0.75, 1.64, 0.02), (0.0, 1.62, 0.0), 60),
    "tailside": ((0.85, 1.6, -0.15), (0.0, 1.55, -0.12), 60),
    "back": ((0.25, 1.55, -1.35), (0.0, 1.42, -0.05), 50),
    "top": ((0.35, 2.05, 0.55), (0.0, 1.72, 0.0), 60),
}


def game_to_blender(P):
    # Оси игры (y вверх, z вперёд) → Blender (z вверх, −y вперёд)
    P = np.asarray(P, np.float64)
    return np.stack([P[..., 0], -P[..., 2], P[..., 1]], -1)


def look(obj, target):
    from mathutils import Vector
    d = Vector(target) - obj.location
    obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def scene(body_path, samples, size):
    """Сцена: Cycles, тело стенда с кожей (под волосами — цвет волос), свет"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    w, h = (int(x) for x in size.split("x"))
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.exposure = -0.9

    # Тело: кожа, под волосами — цвет волос (кожа головы не светится сквозь редкие места)
    B = np.load(body_path)
    V = game_to_blender(B["V"])
    T = B["T"]
    used = np.zeros(len(V), bool)
    used[B["used"]] = True
    T = T[used[T].all(1)]
    me = bpy.data.meshes.new("body")
    me.from_pydata(V.tolist(), [], T.tolist())
    me.shade_smooth()
    if "scalp" in B:
        ca = me.color_attributes.new("scalp", "FLOAT_COLOR", "POINT")
        sc_ = np.clip(B["scalp"] / 0.006, 0, 1)
        col = np.stack([sc_, sc_, sc_, np.ones_like(sc_)], 1).astype(np.float32)
        ca.data.foreach_set("color", col.ravel())
    body = bpy.data.objects.new("body", me)
    sc.collection.objects.link(body)
    m = bpy.data.materials.new("skin")
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.42, 0.27, 0.2, 1)
    bsdf.inputs["Roughness"].default_value = 0.5
    bsdf.inputs["Subsurface Weight"].default_value = 0.2
    if "scalp" in B:
        attr = nt.nodes.new("ShaderNodeAttribute")
        attr.attribute_name = "scalp"
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.inputs[6].default_value = (0.42, 0.27, 0.2, 1)
        mix.inputs[7].default_value = (0.05, 0.035, 0.025, 1)
        nt.links.new(attr.outputs["Fac"], mix.inputs[0])
        nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
    me.materials.append(m)

    # Свет
    def area(name, loc, size, energy, color=(1, 1, 1)):
        L = bpy.data.lights.new(name, "AREA")
        L.size = size
        L.energy = energy
        L.color = color
        o = bpy.data.objects.new(name, L)
        o.location = loc
        sc.collection.objects.link(o)
        look(o, (0, 0, 1.6))
    area("key", (1.2, -1.6, 2.4), 1.2, 150)
    area("rim", (-1.0, 1.8, 2.3), 1.0, 160, (0.85, 0.9, 1.0))
    area("fill", (-1.6, -1.2, 1.5), 2.0, 50, (0.9, 0.95, 1.0))
    world = bpy.data.worlds.new("w")
    world.color = (0.05, 0.05, 0.06)
    sc.world = world

    return sc


def shoot(sc, view, out):
    loc, tgt, lens = VIEWS[view]
    cam = bpy.data.cameras.new("cam")
    cam.lens = lens
    co = bpy.data.objects.new("cam", cam)
    co.location = tuple(game_to_blender(loc))
    sc.collection.objects.link(co)
    look(co, tuple(game_to_blender(tgt)))
    sc.camera = co
    sc.render.filepath = out
    bpy.ops.render.render(write_still=True)



def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("groom")
    ap.add_argument("body")
    ap.add_argument("out")
    ap.add_argument("--view", default="q")
    ap.add_argument("--samples", type=int, default=64)
    ap.add_argument("--size", default="900x1100")
    ap.add_argument("--melanin", type=float, default=0.55)
    ap.add_argument("--radius", type=float, default=0.00005)
    a = ap.parse_args()
    sc = scene(a.body, a.samples, a.size)

    # Волоски
    G = np.load(a.groom)
    P = game_to_blender(G["P"])
    k, n = P.shape[:2]
    cv = bpy.data.hair_curves.new("hair")
    cv.add_curves([n] * k)
    cv.attributes["position"].data.foreach_set("vector", P.reshape(-1).astype(np.float32))
    r = a.radius * (1.0 - 0.6 * np.linspace(0, 1, n) ** 2)
    rad = cv.attributes.new("radius", "FLOAT", "POINT") if "radius" not in cv.attributes else cv.attributes["radius"]
    rad.data.foreach_set("value", np.tile(r, k).astype(np.float32))
    hob = bpy.data.objects.new("hair", cv)
    sc.collection.objects.link(hob)
    hm = bpy.data.materials.new("hair")
    nt = hm.node_tree
    for nd in list(nt.nodes):
        if nd.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(nd)
    hb = nt.nodes.new("ShaderNodeBsdfHairPrincipled")
    hb.parametrization = "MELANIN"
    hb.inputs["Melanin"].default_value = a.melanin
    hb.inputs["Melanin Redness"].default_value = 0.6
    hb.inputs["Roughness"].default_value = 0.28
    hb.inputs["Random Roughness"].default_value = 0.3
    hb.inputs["Random Color"].default_value = 0.15
    nt.links.new(hb.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    cv.materials.append(hm)

    shoot(sc, a.view, a.out)


if __name__ == "__main__":
    main()
