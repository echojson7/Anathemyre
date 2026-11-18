"""Снимок волос карточками в Cycles (docs/hair.md): сетка вещи с развёрткой атласа прядей, прозрачный край по
альфе атласа. Та же камера и свет, что у groom_render.py, — сравнение карточек с волосками.

    python3 -I tools/humanconv/cards_render.py cards.npz body.npz atlas_color.png out.png [--view q]
"""
import argparse
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import groom_render as R  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cards")
    ap.add_argument("body")
    ap.add_argument("atlas")
    ap.add_argument("out")
    ap.add_argument("--view", default="q")
    ap.add_argument("--samples", type=int, default=64)
    ap.add_argument("--size", default="900x1100")
    a = ap.parse_args()
    sc = R.scene(a.body, a.samples, a.size)
    D = np.load(a.cards)
    P = R.game_to_blender(D["P"])
    T, UV, mat, names = D["T"], D["UV"], D["mat"], [str(x) for x in D["names"]]
    me = bpy.data.meshes.new("hair")
    me.from_pydata(P.tolist(), [], T.tolist())
    uvl = me.uv_layers.new(name="uv")
    loops = np.zeros(len(me.loops) * 2, np.float32)
    me.loops.foreach_get("vertex_index", (vi := np.zeros(len(me.loops), np.int32)))
    uv = UV[vi].copy()
    uv[:, 1] = 1.0 - uv[:, 1]
    uvl.data.foreach_set("uv", uv.astype(np.float32).ravel())
    me.polygons.foreach_set("material_index", mat.astype(np.int32))
    me.shade_smooth()
    img = bpy.data.images.load(a.atlas)
    for nm in names:
        m = bpy.data.materials.new(nm)
        nt = m.node_tree
        b = nt.nodes["Principled BSDF"]
        hair_col = (0.045, 0.028, 0.018, 1)
        b.inputs["Roughness"].default_value = 0.38
        if nm == "hair_card":
            tx = nt.nodes.new("ShaderNodeTexImage")
            tx.image = img
            tx.interpolation = "Cubic"
            mul = nt.nodes.new("ShaderNodeMix")
            mul.data_type = "RGBA"
            mul.blend_type = "MULTIPLY"
            mul.inputs[0].default_value = 1.0
            mul.inputs[6].default_value = (0.11, 0.07, 0.045, 1)
            nt.links.new(tx.outputs["Color"], mul.inputs[7])
            nt.links.new(mul.outputs[2], b.inputs["Base Color"])
            nt.links.new(tx.outputs["Alpha"], b.inputs["Alpha"])
            b.inputs["Anisotropic"].default_value = 0.6
        elif nm == "human_skin":
            b.inputs["Base Color"].default_value = (0.42, 0.27, 0.2, 1)
        else:
            b.inputs["Base Color"].default_value = hair_col
        me.materials.append(m)
    ob = bpy.data.objects.new("hair", me)
    sc.collection.objects.link(ob)
    R.shoot(sc, a.view, a.out)


if __name__ == "__main__":
    main()
