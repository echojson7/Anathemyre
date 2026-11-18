"""Литой узел: детальная модель → игровая с запечёнными картами (docs/bake.md). Нужен bpy 5.3 (pip install bpy).

Из одного описания (список «add»/«cut» тел plan.py, оси игры) строятся две модели:
- **детальная** (high): булевы MANIFOLD вместе со сколами → воксельный ремеш VOXEL → смещение по полю высот
  surface.py (доски опалубки, стяжки) проекцией по трём осям; мелочь (поры, волокно, наплывы) — рельефом
  материала (Bump), его тоже видит запекание нормали;
- **игровая** (low): то же описание без сколов, булевы MANIFOLD → скругление кромок Bevel из SEGS отрезков с
  жёсткими нормалями граней (плоская грань плоская до кромки) — чистое тело, а не прореживание ремеша.
Развёртка low — Smart UV Project, острова невидимых граней (заход в стену и пол) сжаты, упаковка с полем.
Запекание Cycles «с выбранного на активный»: нормаль (касательная, OpenGL), затенение, кривизна (pointiness),
цвет и шероховатость (излучением материала high). Поля за краем островов — EXTEND на MARGIN текселей: на
мипах остров не тянет фон (тёмные швы пробы). Выход — glTF → meshpack → .mesh, PNG → texpack → .tex.
"""
import json
import math
import os
import subprocess
import sys

import bpy
import bmesh
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "parts"))
sys.path.insert(0, os.path.join(HERE, ".."))
import surface  # noqa: E402
from texprobe import png  # noqa: E402

VOXEL = float(os.environ.get("VOXEL", "0.008"))   # ремеш детальной модели, м
BEVEL, SEGS = 0.016, 4     # скругление кромок low: хорда 0.016·(1 − cos 11.25°) = 0.31 мм, на стыке трёх кромок ≤ 0.5 мм линта
MARGIN = 16                # поле за краем островов, текселей
CAGE, RAY = 0.035, 0.09    # вынос луча над low и его длина: сколы до 4 см глубиной


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.use_denoising = False
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = os.cpu_count() or 4
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.light_settings.distance = 0.6   # дальность затенения: углы и щели, не весь зал


def body_object(name, body):
    """Тело kit.Body → объект Blender в осях игры (вершины сварены по положению)."""
    index, verts, faces = {}, [], []
    for pts, _, _ in body.faces:
        f = []
        for p in pts:
            k = index.get(p)
            if k is None:
                k = index[p] = len(verts)
                verts.append(p)
            f.append(k)
        faces.append(f)
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def apply_all(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    ob.modifiers.clear()
    old = ob.data
    ob.data = me
    bpy.data.meshes.remove(old)


def csg(name, ops):
    """Операции по порядку, подряд одинаковые — одной коллекцией (как solid.py). → объект с применёнными булевыми."""
    base = body_object(name, ops[0][1].body() if hasattr(ops[0][1], "body") else ops[0][1])
    k, g = 1, 0
    while k < len(ops):
        op = ops[k][0]
        coll = bpy.data.collections.new("%s_g%d" % (name, g))
        while k < len(ops) and ops[k][0] == op:
            b = ops[k][1].body() if hasattr(ops[k][1], "body") else ops[k][1]
            o = body_object("%s_o%d" % (name, k), b)
            bpy.context.scene.collection.objects.unlink(o)
            coll.objects.link(o)
            k += 1
        m = base.modifiers.new("b%d" % g, 'BOOLEAN')
        m.operand_type = 'COLLECTION'
        m.collection = coll
        m.operation = 'UNION' if op == "add" else 'DIFFERENCE'
        m.solver = 'MANIFOLD'
        g += 1
    apply_all(base)
    return base


# ---------------------------------------------------------------- поверхность в картинки Blender
def image(name, arr, channels=1):
    """numpy (строка 0 — верх) → картинка Blender float без цветового пространства (строка 0 — низ)."""
    n = arr.shape[0]
    im = bpy.data.images.new(name, n, n, alpha=True, float_buffer=True)
    im.colorspace_settings.name = 'Non-Color'
    a = np.flipud(arr)
    if channels == 1:
        rgba = np.stack([a, a, a, np.ones_like(a)], axis=-1)
    else:
        rgba = np.concatenate([a, np.ones(a.shape[:2] + (1,))], axis=-1)
    im.pixels.foreach_set(rgba.astype(np.float32).ravel())
    im.pack()
    return im


def surface_images():
    out = {}
    for kind, fn in (("wall", surface.wall), ("floor", surface.floor)):
        h, col, rough = fn()
        lo = h.copy()
        for _ in range(3):   # крупная часть рельефа — в геометрию, мелкая — в Bump
            lo = surface._blur(lo, 4)
        out[kind] = {"lo": image(kind + "_lo", lo), "hi": image(kind + "_hi", h - lo),
                     "col": image(kind + "_col", col, 3), "rough": image(kind + "_rough", rough)}
    return out


# Проекции: ось нормали → (оси картинки u, v) в осях игры и вид поверхности. y — вверх: пол и проступи.
PROJ = (("X", "Z", "Y", "wall"), ("Z", "X", "Y", "wall"), ("Y", "X", "Z", "floor"))


def tri_geo(ob, imgs):
    """Смещение по нормали крупным рельефом: три проекции, вес |n|⁴ (как узор материала)."""
    g = bpy.data.node_groups.new("tri_disp", 'GeometryNodeTree')
    g.interface.new_socket("Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    g.interface.new_socket("Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    N, L = g.nodes.new, g.links.new
    gi, go = N("NodeGroupInput"), N("NodeGroupOutput")
    pos, nrm = N("GeometryNodeInputPosition"), N("GeometryNodeInputNormal")
    sc = N("ShaderNodeVectorMath"); sc.operation = 'SCALE'; sc.inputs["Scale"].default_value = 1.0 / surface.SIZE
    L(pos.outputs[0], sc.inputs[0])
    sp = N("ShaderNodeSeparateXYZ"); L(sc.outputs[0], sp.inputs[0])
    sn = N("ShaderNodeSeparateXYZ"); L(nrm.outputs[0], sn.inputs[0])
    total = wsum = None
    for ax, a, b, kind in PROJ:
        cv = N("ShaderNodeCombineXYZ")
        L(sp.outputs[a], cv.inputs["X"]); L(sp.outputs[b], cv.inputs["Y"])
        it = N("GeometryNodeImageTexture")
        it.inputs["Image"].default_value = imgs[kind]["lo"]
        it.interpolation = 'Linear'
        it.extension = 'REPEAT'
        L(cv.outputs[0], it.inputs["Vector"])
        ab = N("ShaderNodeMath"); ab.operation = 'ABSOLUTE'; L(sn.outputs[ax], ab.inputs[0])
        pw = N("ShaderNodeMath"); pw.operation = 'POWER'; pw.inputs[1].default_value = 4.0; L(ab.outputs[0], pw.inputs[0])
        sepc = N("FunctionNodeSeparateColor"); L(it.outputs["Color"], sepc.inputs[0])
        mul = N("ShaderNodeMath"); mul.operation = 'MULTIPLY'; L(sepc.outputs[0], mul.inputs[0]); L(pw.outputs[0], mul.inputs[1])
        if total is None:
            total, wsum = mul, pw
        else:
            t2 = N("ShaderNodeMath"); t2.operation = 'ADD'; L(total.outputs[0], t2.inputs[0]); L(mul.outputs[0], t2.inputs[1]); total = t2
            w2 = N("ShaderNodeMath"); w2.operation = 'ADD'; L(wsum.outputs[0], w2.inputs[0]); L(pw.outputs[0], w2.inputs[1]); wsum = w2
    dv = N("ShaderNodeMath"); dv.operation = 'DIVIDE'; L(total.outputs[0], dv.inputs[0]); L(wsum.outputs[0], dv.inputs[1])
    off = N("ShaderNodeVectorMath"); off.operation = 'SCALE'; L(nrm.outputs[0], off.inputs[0]); L(dv.outputs[0], off.inputs["Scale"])
    setp = N("GeometryNodeSetPosition")
    L(gi.outputs[0], setp.inputs["Geometry"]); L(off.outputs[0], setp.inputs["Offset"]); L(setp.outputs[0], go.inputs[0])
    m = ob.modifiers.new("disp", 'NODES')
    m.node_group = g


def high_material(imgs):
    """Материал детальной модели: те же три проекции, выход переключается под каждое запекание (set_output)."""
    mat = bpy.data.materials.new("high")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    N, L = nt.nodes.new, nt.links.new
    geo = N("ShaderNodeNewGeometry")
    sc = N("ShaderNodeVectorMath"); sc.operation = 'SCALE'; sc.inputs["Scale"].default_value = 1.0 / surface.SIZE
    L(geo.outputs["Position"], sc.inputs[0])
    sp = N("ShaderNodeSeparateXYZ"); L(sc.outputs[0], sp.inputs[0])
    sn = N("ShaderNodeSeparateXYZ"); L(geo.outputs["Normal"], sn.inputs[0])
    acc = {"hi": None, "col": None, "rough": None}
    wsum = None
    for ax, a, b, kind in PROJ:
        cv = N("ShaderNodeCombineXYZ"); L(sp.outputs[a], cv.inputs["X"]); L(sp.outputs[b], cv.inputs["Y"])
        ab = N("ShaderNodeMath"); ab.operation = 'ABSOLUTE'; L(sn.outputs[ax], ab.inputs[0])
        pw = N("ShaderNodeMath"); pw.operation = 'POWER'; pw.inputs[1].default_value = 4.0; L(ab.outputs[0], pw.inputs[0])
        for key in acc:
            it = N("ShaderNodeTexImage"); it.image = imgs[kind][key]; it.interpolation = 'Linear'
            L(cv.outputs[0], it.inputs["Vector"])
            mul = N("ShaderNodeVectorMath"); mul.operation = 'SCALE'
            L(it.outputs["Color"], mul.inputs[0]); L(pw.outputs[0], mul.inputs["Scale"])
            if acc[key] is None:
                acc[key] = mul
            else:
                ad = N("ShaderNodeVectorMath"); ad.operation = 'ADD'
                L(acc[key].outputs[0], ad.inputs[0]); L(mul.outputs[0], ad.inputs[1]); acc[key] = ad
        if wsum is None:
            wsum = pw
        else:
            w2 = N("ShaderNodeMath"); w2.operation = 'ADD'; L(wsum.outputs[0], w2.inputs[0]); L(pw.outputs[0], w2.inputs[1]); wsum = w2
    inv = N("ShaderNodeMath"); inv.operation = 'DIVIDE'; inv.inputs[0].default_value = 1.0; L(wsum.outputs[0], inv.inputs[1])
    val = {}
    for key, node in acc.items():
        d = N("ShaderNodeVectorMath"); d.operation = 'SCALE'
        L(node.outputs[0], d.inputs[0]); L(inv.outputs[0], d.inputs["Scale"])
        val[key] = d
    sepv = N("ShaderNodeSeparateXYZ"); L(val["hi"].outputs[0], sepv.inputs[0])
    bump = N("ShaderNodeBump"); bump.inputs["Distance"].default_value = 1.0; bump.inputs["Strength"].default_value = 1.0
    L(sepv.outputs[0], bump.inputs["Height"])
    bsdf = N("ShaderNodeBsdfPrincipled"); L(bump.outputs[0], bsdf.inputs["Normal"])
    em = N("ShaderNodeEmission")
    out = N("ShaderNodeOutputMaterial")
    # Кривизна: pointiness 0.5 — плоско; растянуть, чтобы кромка 2 см и скол давали заметный сдвиг
    pt = N("ShaderNodeMath"); pt.operation = 'MULTIPLY_ADD'
    L(geo.outputs["Pointiness"], pt.inputs[0]); pt.inputs[1].default_value = 6.0; pt.inputs[2].default_value = -2.5
    return mat, {"bsdf": bsdf, "em": em, "out": out, "col": val["col"], "rough": val["rough"], "curv": pt}


def set_output(mat, h, what):
    nt = mat.node_tree
    for lk in list(nt.links):
        if lk.to_node in (h["out"], h["em"]):
            nt.links.remove(lk)
    if what == "normal":
        nt.links.new(h["bsdf"].outputs[0], h["out"].inputs["Surface"])
        return
    src = {"color": h["col"].outputs[0], "rough": h["rough"].outputs[0], "curv": h["curv"].outputs[0]}[what]
    nt.links.new(src, h["em"].inputs["Color"])
    nt.links.new(h["em"].outputs[0], h["out"].inputs["Surface"])


# ---------------------------------------------------------------- low: тело, развёртка
def make_low(name, ops, hidden):
    ob = csg(name + "_low", ops)
    bv = ob.modifiers.new("bevel", 'BEVEL')
    bv.width = BEVEL
    bv.segments = SEGS
    bv.profile = 0.5
    bv.limit_method = 'ANGLE'
    bv.angle_limit = math.radians(30)
    bv.harden_normals = True
    bv.miter_outer = 'MITER_ARC'
    apply_all(ob)
    tri = ob.modifiers.new("tri", 'TRIANGULATE')
    tri.keep_custom_normals = True
    apply_all(ob)
    unwrap(ob, hidden)
    return ob


def unwrap(ob, hidden):
    """Smart UV Project; острова, где все грани невидимы (hidden(центр, нормаль)), сжаты в 20 раз; упаковка."""
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.0, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    uvl = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    parent = list(range(len(bm.faces)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for e in bm.edges:
        if len(e.link_faces) != 2:
            continue
        f0, f1 = e.link_faces
        same = True
        for v in e.verts:
            u0 = [l[uvl].uv for l in f0.loops if l.vert == v][0]
            u1 = [l[uvl].uv for l in f1.loops if l.vert == v][0]
            if (u0 - u1).length > 1e-6:
                same = False
        if same:
            a, b = find(f0.index), find(f1.index)
            if a != b:
                parent[max(a, b)] = min(a, b)
    islands = {}
    for f in bm.faces:
        islands.setdefault(find(f.index), []).append(f)
    shrunk = 0
    for faces in islands.values():
        if all(hidden(tuple(f.calc_center_median()), tuple(f.normal)) for f in faces):
            ls = [l for f in faces for l in f.loops]
            c = sum((l[uvl].uv for l in ls), ls[0][uvl].uv * 0) / len(ls)
            for l in ls:
                l[uvl].uv = c + (l[uvl].uv - c) * 0.05
            shrunk += 1
    bm.to_mesh(me)
    bm.free()
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.select_all(action='SELECT')
    bpy.ops.uv.pack_islands(rotate=True, margin_method='FRACTION', margin=0.006, shape_method='CONCAVE')
    bpy.ops.object.mode_set(mode='OBJECT')
    print("развёртка: островов %d, сжато невидимых %d" % (len(islands), shrunk))


# ---------------------------------------------------------------- high
def make_high(name, ops, chips, imgs):
    from kit import Body
    cuts = []
    for c, r, seed in chips:
        b = Body()
        from kit import rnd
        b.blob(c, r, None, seed, squash=(0.8 + 0.6 * rnd(seed, 90), 0.7 + 0.6 * rnd(seed, 91), 0.8 + 0.6 * rnd(seed, 92)))
        cuts.append(("cut", b))
    ob = csg(name + "_high", list(ops) + cuts)
    rm = ob.modifiers.new("rm", 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = VOXEL
    rm.adaptivity = 0.0
    apply_all(ob)
    sm = ob.modifiers.new("sm", 'CORRECTIVE_SMOOTH')
    sm.iterations = 3
    apply_all(ob)
    tri_geo(ob, imgs)
    apply_all(ob)
    for p in ob.data.polygons:
        p.use_smooth = True
    print("детальная модель: треугольников %d" % sum(len(p.vertices) - 2 for p in ob.data.polygons))
    return ob


# ---------------------------------------------------------------- запекание
def bake_all(low, high, mat, h, size):
    sc = bpy.context.scene
    lm = bpy.data.materials.new("low")
    lm.use_nodes = True
    low.data.materials.clear()
    low.data.materials.append(lm)
    high.data.materials.clear()
    high.data.materials.append(mat)
    tex = lm.node_tree.nodes.new("ShaderNodeTexImage")
    lm.node_tree.nodes.active = tex
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    high.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    b = sc.render.bake
    b.use_selected_to_active = True
    b.use_cage = False
    b.cage_extrusion = CAGE
    b.max_ray_distance = RAY
    b.margin = MARGIN
    b.margin_type = 'EXTEND'
    b.use_clear = True
    out = {}
    for what, kind, samples in (("normal", 'NORMAL', 8), ("ao", 'AO', 96), ("curv", 'EMIT', 4),
                                ("color", 'EMIT', 8), ("rough", 'EMIT', 4)):
        im = bpy.data.images.new("bake_" + what, size, size, alpha=False, float_buffer=True)
        im.colorspace_settings.name = 'Non-Color'
        tex.image = im
        sc.cycles.samples = samples
        if what != "ao":
            set_output(mat, h, "normal" if what == "normal" else what)
        else:
            set_output(mat, h, "normal")
        if kind == 'NORMAL':
            b.normal_space = 'TANGENT'
            b.normal_r, b.normal_g, b.normal_b = 'POS_X', 'POS_Y', 'POS_Z'
        bpy.ops.object.bake(type=kind)
        a = np.empty(size * size * 4, dtype=np.float32)
        im.pixels.foreach_get(a)
        out[what] = np.flipud(a.reshape(size, size, 4))[..., :3].astype(np.float64)
        print("запечено: %s" % what)
    return out


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def write_maps(name, maps, data, texpack, work):
    to8 = lambda a: np.clip(np.floor(a * 255.0 + 0.5), 0, 255).astype(np.uint8)
    ao = np.clip(maps["ao"][..., 0], 0, 1)
    col = np.concatenate([srgb(maps["color"]), ao[..., None]], axis=-1)
    nrm = np.concatenate([np.clip(maps["normal"], 0, 1), np.ones(ao.shape + (1,))], axis=-1)
    one = lambda a: np.repeat(np.clip(a, 0, 1)[..., None], 4, axis=-1)
    for key, img, kind in (("color", col, "color"), ("normal", nrm, "normal"),
                           ("curv", one(maps["curv"][..., 0]), "mask"), ("rough", one(maps["rough"][..., 0]), "mask")):
        p = os.path.join(work, "%s_%s.png" % (name, key))
        png(p, to8(img))
        subprocess.run([texpack, kind, p, os.path.join(data, "textures", "%s_%s.tex" % (name, key))], check=True)


def write_gltf(path, ob, material):
    """Сетка low → glTF: вершина на угол (meshpack сварит), нормали угла (жёсткие у кромок), uv (v вниз)."""
    me = ob.data
    me.calc_loop_triangles()
    uvl = me.uv_layers.active.data
    cn = me.corner_normals
    pos, nrm, uv, idx = [], [], [], []
    for t in me.loop_triangles:
        for li, vi in zip(t.loops, t.vertices):
            idx.append(len(pos))
            pos.append(tuple(me.vertices[vi].co))
            nrm.append(tuple(cn[li].vector))
            u, v = uvl[li].uv
            uv.append((u, 1.0 - v))
    pos = np.array(pos, dtype="<f4")
    nrm = np.array(nrm, dtype="<f4")
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    uv = np.array(uv, dtype="<f4")
    idx = np.array(idx, dtype="<u4")
    blobs = [pos.tobytes(), nrm.astype("<f4").tobytes(), uv.tobytes(), idx.tobytes()]
    offs, o = [], 0
    for bl in blobs:
        offs.append(o)
        o += len(bl)
    binp = path[:-5] + ".bin"
    with open(binp, "wb") as f:
        f.write(b"".join(blobs))
    doc = {
        "asset": {"version": "2.0", "generator": "tools/bake"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": ob.name}],
        "materials": [{"name": material}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "buffers": [{"uri": os.path.basename(binp), "byteLength": o}],
        "bufferViews": [{"buffer": 0, "byteOffset": offs[k], "byteLength": len(blobs[k])} for k in range(4)],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
             "min": pos.min(0).tolist(), "max": pos.max(0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(idx), "type": "SCALAR"}],
    }
    with open(path, "w") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
    return len(idx) // 3


def cast(name, ops, chips, hidden, data, meshpack, texpack, size=2048, material=None):
    """Литой узел целиком: high, low, развёртка, запекание, .mesh и четыре .tex. → треугольников low."""
    import time
    t0 = time.time()
    work = os.path.join("build", "bake", name)
    os.makedirs(work, exist_ok=True)
    os.makedirs(os.path.join(data, "textures"), exist_ok=True)
    reset()
    imgs = surface_images()
    low = make_low(name, ops, hidden)
    print("low: треугольников %d  %.0f с" % (len(low.data.polygons), time.time() - t0))
    high = make_high(name, ops, chips, imgs)
    print("high  %.0f с" % (time.time() - t0))
    mat, h = high_material(imgs)
    maps = bake_all(low, high, mat, h, size)
    print("запекание  %.0f с" % (time.time() - t0))
    write_maps(name, maps, data, texpack, work)
    gp = os.path.join(work, name + ".gltf")
    n = write_gltf(gp, low, material or name)
    subprocess.run([meshpack, gp, os.path.join(data, "meshes", name + ".mesh")], check=True)
    return n
