"""Пробные комнаты: одна и та же бетонная насосная четырьмя способами (docs/assets_rooms.md). Офлайн, Blender bpy.
  1 now     — как сейчас в v2: коробки встык со щелями, цилиндры по 10 граней, узор формулой без сканов
  2 kit     — модульный набор: замкнутые детали с нахлёстом, карнизы и плинтусы на стыках, тайловые сканы
  3 cast    — цельная отливка: всё бетонное одним телом, воксельный ремеш, рельеф смещением по скану
  4 hybrid  — оболочка, марш и галерея отливкой, колонна и балки деталями набора с нахлёстом
Запуск: python rooms.py <scans_dir> <out_dir> now|kit|cast|hybrid [wide|joint|close]; QUICK=1 черновик, DUST=1 пыль
"""
import math
import os
import sys
import bpy
import bmesh
from mathutils import Vector

SCANS, OUT, MODE = sys.argv[1], sys.argv[2], sys.argv[3]
SHOT = sys.argv[4] if len(sys.argv) > 4 else "wide"
QUICK = os.environ.get("QUICK") == "1"
os.makedirs(OUT, exist_ok=True)
R = math.radians

# ---------------------------------------------------------------- план комнаты (метры, z вверх)
W, D, H = 10.0, 7.0, 4.5        # внутренний размер
T = 0.5                          # толщина наружных стен
SLAB = 0.6                       # плита перекрытия
SLIT = (1.5, 8.5, 3.25, 3.95)    # щель под потолком в задней стене: x0 x1 z0 z1
DOOR = (1.6, 2.8, 2.3)           # проём в правой стене: y0 y1 высота
BEAMS = (3.3, 6.6)               # балки поперёк, по x
COL = (6.6, 2.6)                 # колонна под балкой
PLAT = (7.6, 5.2, 2.4)           # галерея: x0, y0, высота верха (до правой и задней стены)
STAIR = (4.0, 5.9, 8)            # марш: x начала, y начала (до задней стены), ступеней
RISE = PLAT[2] / STAIR[2]
RUN = (PLAT[0] - STAIR[0]) / STAIR[2]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.device = 'CPU'
    sc.cycles.samples = 24 if QUICK else 64
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 6
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = 4
    sc.render.resolution_x = 960 if QUICK else 1600
    sc.render.resolution_y = 540 if QUICK else 900
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Medium High Contrast'
    sc.view_settings.exposure = 0.5
    w = bpy.data.worlds.new("w")
    sc.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.62, 0.66, 0.72, 1)
    bg.inputs[1].default_value = 1.2
    return sc


def link(ob):
    bpy.context.scene.collection.objects.link(ob)
    return ob


def activate(ob):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)


def apply_mods(ob):
    activate(ob)
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def mesh_ob(name, bm):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    return link(bpy.data.objects.new(name, me))


def box(name, x0, x1, y0, y1, z0, z1, mat=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector((x1 - x0, y1 - y0, z1 - z0)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)), verts=bm.verts)
    ob = mesh_ob(name, bm)
    if mat:
        ob.data.materials.append(mat)
    return ob


def cyl(name, r, p0, p1, seg, mat=None):
    """Цилиндр по оси p0 → p1."""
    a, b = Vector(p0), Vector(p1)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r, depth=(b - a).length)
    ob = mesh_ob(name, bm)
    ob.location = (a + b) / 2
    ob.rotation_euler = (b - a).to_track_quat('Z', 'Y').to_euler()
    if mat:
        ob.data.materials.append(mat)
    return ob


def bevel(ob, w, seg=2, angle=40, harden=True):
    m = ob.modifiers.new("bv", 'BEVEL')
    m.width = w
    m.segments = seg
    m.limit_method = 'ANGLE'
    m.angle_limit = R(angle)
    m.harden_normals = harden
    return m


def boolean(ob, cutter, op='DIFFERENCE'):
    m = ob.modifiers.new("b", 'BOOLEAN')
    m.object = cutter
    m.operation = op
    m.solver = 'MANIFOLD'
    cutter.hide_render = True
    cutter.hide_viewport = True
    return m


def smooth(ob, angle=35):
    activate(ob)
    bpy.ops.object.shade_smooth_by_angle(angle=R(angle))


def tris(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    e = ob.evaluated_get(dg)
    me = e.to_mesh()
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    e.to_mesh_clear()
    return n


# ---------------------------------------------------------------- материалы
def img(nt, name, colorspace):
    p = os.path.join(SCANS, name)
    if not os.path.exists(p):
        return None
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(p, check_existing=True)
    t.image.colorspace_settings.name = colorspace
    t.projection = 'BOX'
    t.projection_blend = 0.25
    return t


def scan_mat(name, prefix, tile, tint=(1, 1, 1), space='WORLD', dirt=False, edges=False, rough_mul=1.0, metal=0.0,
             disp_scale=0.0):
    """Скан CC0 проекцией по трём осям (как в движке). space: WORLD — по миру, OBJECT — по детали (повтор набора).
    dirt — грязь по затенению (как запечённое AO), edges — светлые сбитые кромки (как кривизна)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1 / tile, 1 / tile, 1 / tile)
    nt.links.new(tc.outputs["Object" if space == 'OBJECT' else "Generated"], mp.inputs["Vector"])
    if space == 'WORLD':
        # мировые координаты: позиция геометрии
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        nt.links.new(geo.outputs["Position"], mp.inputs["Vector"])
    else:
        # разброс набора: свой сдвиг узора у каждой копии
        info = nt.nodes.new("ShaderNodeObjectInfo")
        off = nt.nodes.new("ShaderNodeVectorMath")
        off.operation = 'SCALE'
        nt.links.new(info.outputs["Random"], off.inputs[0])
        off.inputs["Scale"].default_value = 7.0
        add = nt.nodes.new("ShaderNodeVectorMath")
        add.operation = 'ADD'
        nt.links.new(tc.outputs["Object"], add.inputs[0])
        nt.links.new(off.outputs[0], add.inputs[1])
        nt.links.new(add.outputs[0], mp.inputs["Vector"])
    d = img(nt, prefix + "_diff" + SUF, 'sRGB')
    n = img(nt, prefix + "_nor" + ("_gl" if SUF != ".jpg" else "") + SUF, 'Non-Color')
    r = img(nt, prefix + "_rough" + SUF, 'Non-Color')
    a = img(nt, prefix + "_ao" + SUF, 'Non-Color')
    for t in (d, n, r, a):
        if t:
            nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
    col = d.outputs["Color"]
    tn = nt.nodes.new("ShaderNodeMix")
    tn.data_type = 'RGBA'
    tn.blend_type = 'MULTIPLY'
    tn.inputs["Factor"].default_value = 1.0
    tn.inputs[7].default_value = (*tint, 1)
    nt.links.new(col, tn.inputs[6])
    col = tn.outputs[2]
    if a:
        am = nt.nodes.new("ShaderNodeMix")
        am.data_type = 'RGBA'
        am.blend_type = 'MULTIPLY'
        am.inputs["Factor"].default_value = 0.35
        nt.links.new(col, am.inputs[6])
        nt.links.new(a.outputs["Color"], am.inputs[7])
        col = am.outputs[2]
    if dirt:
        ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
        ao.samples = 8
        ao.inputs["Distance"].default_value = 0.6
        ramp = nt.nodes.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].position = 0.25
        ramp.color_ramp.elements[0].color = (0.38, 0.34, 0.30, 1)
        ramp.color_ramp.elements[1].position = 0.9
        ramp.color_ramp.elements[1].color = (1, 1, 1, 1)
        nt.links.new(ao.outputs["AO"], ramp.inputs["Fac"])
        # пятна, чтобы грязь не была ровной
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 1.5
        nz.inputs["Detail"].default_value = 6
        geo2 = nt.nodes.new("ShaderNodeNewGeometry")
        nt.links.new(geo2.outputs["Position"], nz.inputs["Vector"])
        mx = nt.nodes.new("ShaderNodeMix")
        mx.data_type = 'RGBA'
        mx.blend_type = 'MULTIPLY'
        nt.links.new(nz.outputs["Fac"], mx.inputs["Factor"])
        nt.links.new(col, mx.inputs[6])
        nt.links.new(ramp.outputs["Color"], mx.inputs[7])
        col = mx.outputs[2]
        # потёки сверху вниз: полосы по x, тянутся по z
        wv = nt.nodes.new("ShaderNodeTexNoise")
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(geo2.outputs["Position"], sep.inputs[0])
        comb = nt.nodes.new("ShaderNodeCombineXYZ")
        nt.links.new(sep.outputs["X"], comb.inputs["X"])
        nt.links.new(sep.outputs["Y"], comb.inputs["Y"])
        zs = nt.nodes.new("ShaderNodeMath")
        zs.operation = 'MULTIPLY'
        zs.inputs[1].default_value = 0.08
        nt.links.new(sep.outputs["Z"], zs.inputs[0])
        nt.links.new(zs.outputs[0], comb.inputs["Z"])
        nt.links.new(comb.outputs[0], wv.inputs["Vector"])
        wv.inputs["Scale"].default_value = 9.0
        wv.inputs["Detail"].default_value = 4
        st = nt.nodes.new("ShaderNodeValToRGB")
        st.color_ramp.elements[0].position = 0.55
        st.color_ramp.elements[0].color = (1, 1, 1, 1)
        st.color_ramp.elements[1].position = 0.75
        st.color_ramp.elements[1].color = (0.55, 0.5, 0.45, 1)
        nt.links.new(wv.outputs["Fac"], st.inputs["Fac"])
        mx2 = nt.nodes.new("ShaderNodeMix")
        mx2.data_type = 'RGBA'
        mx2.blend_type = 'MULTIPLY'
        mx2.inputs["Factor"].default_value = 0.8
        nt.links.new(col, mx2.inputs[6])
        nt.links.new(st.outputs["Color"], mx2.inputs[7])
        col = mx2.outputs[2]
    if edges:
        geo3 = nt.nodes.new("ShaderNodeNewGeometry")
        er = nt.nodes.new("ShaderNodeValToRGB")
        er.color_ramp.elements[0].position = 0.53
        er.color_ramp.elements[0].color = (1, 1, 1, 1)
        er.color_ramp.elements[1].position = 0.62
        er.color_ramp.elements[1].color = (1.25, 1.22, 1.15, 1)
        nt.links.new(geo3.outputs["Pointiness"], er.inputs["Fac"])
        mx3 = nt.nodes.new("ShaderNodeMix")
        mx3.data_type = 'RGBA'
        mx3.blend_type = 'MULTIPLY'
        mx3.inputs["Factor"].default_value = 1.0
        nt.links.new(col, mx3.inputs[6])
        nt.links.new(er.outputs["Color"], mx3.inputs[7])
        col = mx3.outputs[2]
    nt.links.new(col, p.inputs["Base Color"])
    if r:
        rm = nt.nodes.new("ShaderNodeMath")
        rm.operation = 'MULTIPLY'
        rm.inputs[1].default_value = rough_mul
        nt.links.new(r.outputs["Color"], rm.inputs[0])
        nt.links.new(rm.outputs[0], p.inputs["Roughness"])
    # рельеф: карта нормалей скана в касательном пространстве без UV не работает (давала «песок»),
    # поэтому мелкий рельеф — из карты высот через Bump (как триплановая нормаль в движке)
    h = img(nt, prefix + "_disp" + SUF, 'Non-Color')
    if h:
        nt.links.new(mp.outputs["Vector"], h.inputs["Vector"])
        bp = nt.nodes.new("ShaderNodeBump")
        bp.inputs["Strength"].default_value = 0.6
        bp.inputs["Distance"].default_value = 0.004
        nt.links.new(h.outputs["Color"], bp.inputs["Height"])
        nt.links.new(bp.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Metallic"].default_value = metal
    return m


def flat_mat(name, col, rough=0.7, metal=0.0, noise=0.0, emit=0.0):
    """Материал как сейчас в v2: цвет + шум формулой по миру, без сканов."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*col, 1)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metal
    if noise > 0:
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        nz = nt.nodes.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 0.8
        nz.inputs["Detail"].default_value = 3
        nt.links.new(geo.outputs["Position"], nz.inputs["Vector"])
        mx = nt.nodes.new("ShaderNodeMix")
        mx.data_type = 'RGBA'
        mx.inputs["Factor"].default_value = noise
        mx.inputs[6].default_value = (*col, 1)
        nt.links.new(nz.outputs["Color"], mx.inputs[7])
        nt.links.new(mx.outputs[2], p.inputs["Base Color"])
    if emit > 0:
        p.inputs["Emission Color"].default_value = (*col, 1)
        p.inputs["Emission Strength"].default_value = emit
    return m


# какие файлы сканов есть: 2K с картой высот или 1K без неё
SUF = "_2k.jpg" if os.path.exists(os.path.join(SCANS, "concrete_layers_02_diff_2k.jpg")) else ".jpg"


def names():
    if SUF == ".jpg":
        return dict(wall="wall_concrete_layers_02", floor="floor_concrete_floor_worn_001", rust="rust_rust_coarse_01",
                    paint="paint_green_metal_rust", wood="wood_dark_wooden_planks")
    return dict(wall="concrete_layers_02", floor="concrete_floor_worn_001", rust="rust_coarse_01",
                paint="green_metal_rust", wood="dark_wooden_planks")


def materials(mode):
    nm = names()
    M = {}
    if mode == "now":
        M["wall"] = flat_mat("wall", (0.36, 0.34, 0.31), 0.85, noise=0.15)
        M["floor"] = flat_mat("floor", (0.30, 0.29, 0.27), 0.8, noise=0.12)
        M["rust"] = flat_mat("rust", (0.25, 0.12, 0.06), 0.7, noise=0.2)
        M["paint"] = flat_mat("paint", (0.18, 0.24, 0.18), 0.5, noise=0.1)
        M["steel"] = flat_mat("steel", (0.5, 0.5, 0.5), 0.45, metal=1.0)
    else:
        # одинаковые материалы у 2–4: сравниваем только способ сборки
        dirt = True
        edges = False
        space = 'OBJECT' if mode == "kit" else 'WORLD'
        # яркость сканов подогнана к «now» (средний альбедо стены ~0.35, пола ~0.28), чтобы сравнивать не освещённость
        M["wall"] = scan_mat("wall", nm["wall"], 2.0, (1.75, 1.72, 1.68), space, dirt, edges)
        M["floor"] = scan_mat("floor", nm["floor"], 2.0, (3.0, 2.95, 2.9), 'WORLD', dirt, edges)
        M["rust"] = scan_mat("rust", nm["rust"], 1.0, (0.7, 0.6, 0.55), 'OBJECT', False, False, metal=0.0)
        M["paint"] = scan_mat("paint", nm["paint"], 1.0, (0.9, 0.9, 0.9), 'OBJECT', False, False)
        M["steel"] = flat_mat("steel", (0.45, 0.45, 0.45), 0.4, metal=1.0)
    M["lamp"] = flat_mat("lamp", (1.0, 0.92, 0.8), 0.3, emit=25.0)
    M["sand"] = flat_mat("sand", (0.55, 0.45, 0.32), 0.95, noise=0.1)
    return M


# ---------------------------------------------------------------- бетон: список объёмов (общий для всех способов)
def concrete_volumes():
    """[(имя, x0,x1,y0,y1,z0,z1, материал, вид)] — вид: shell (оболочка), struct (колонна/балки), stair, plat."""
    v = []
    v.append(("floor", -T, W + T, -T, D + T, -0.4, 0.0, "floor", "shell"))
    v.append(("roof", -T, W + T, -T, D + T, H, H + SLAB, "wall", "shell"))
    v.append(("wall_back", -T, W + T, D, D + T, 0, H, "wall", "shell"))
    v.append(("wall_front", -T, W + T, -T, 0, 0, H, "wall", "shell"))
    v.append(("wall_left", -T, 0, 0, D, 0, H, "wall", "shell"))
    v.append(("wall_right", W, W + T, 0, D, 0, H, "wall", "shell"))
    for bx in BEAMS:
        v.append(("beam", bx - 0.22, bx + 0.22, 0, D, H - 0.55, H, "wall", "struct"))
    v.append(("col", COL[0] - 0.25, COL[0] + 0.25, COL[1] - 0.25, COL[1] + 0.25, 0, H - 0.55, "wall", "struct"))
    v.append(("plat", PLAT[0], W, PLAT[1], D, PLAT[2] - 0.3, PLAT[2], "floor", "plat"))
    v.append(("plat_col", PLAT[0] + 0.05, PLAT[0] + 0.45, PLAT[1] + 0.05, PLAT[1] + 0.45, 0, PLAT[2] - 0.3, "wall", "struct"))
    for k in range(STAIR[2]):
        x0 = STAIR[0] + k * RUN
        v.append(("step", x0, PLAT[0], STAIR[1], D, 0, (k + 1) * RISE, "floor", "stair"))
    return v


def cutters():
    c = []
    c.append(box("slit", SLIT[0], SLIT[1], D - 0.1, D + T + 0.1, SLIT[2], SLIT[3]))
    c.append(box("door", W - 0.1, W + T + 0.1, DOOR[0], DOOR[1], 0.0, DOOR[2]))
    return c


# ---------------------------------------------------------------- наполнение (одинаковое у 2–4, примитивное у 1)
def props(M, good):
    obs = []
    seg = 40 if good else 10
    # две трубы вдоль правой стены, фланцы, кронштейны
    for k, (z, r) in enumerate(((3.25, 0.09), (3.6, 0.06))):
        x = W - 0.25 - k * 0.05
        obs.append(cyl("pipe", r, (x, 0.0, z), (x, PLAT[1] - 0.1, z), seg, M["rust"]))
        if good:
            for y in (0.9, 2.9, 4.6):
                f = cyl("flange", r + 0.035, (x, y - 0.02, z), (x, y + 0.02, z), seg, M["rust"])
                bevel(f, 0.006, 2)
                obs.append(f)
                for b in range(8):
                    a = 2 * math.pi * b / 8
                    obs.append(cyl("bolt", 0.009, (x + math.cos(a) * (r + 0.022), y - 0.035, z + math.sin(a) * (r + 0.022)),
                                   (x + math.cos(a) * (r + 0.022), y + 0.035, z + math.sin(a) * (r + 0.022)), 6, M["steel"]))
            for y in (1.9, 3.8):
                br = box("bracket", x - 0.02, W + 0.03, y - 0.03, y + 0.03, z - r - 0.06, z - r - 0.02, M["paint"])
                bevel(br, 0.004, 2)
                obs.append(br)
    # вертикальный спуск трубы в пол
    obs.append(cyl("pipe_v", 0.09, (W - 0.25, PLAT[1] - 0.1, 3.25), (W - 0.25, PLAT[1] - 0.1, 0.0), seg, M["rust"]))
    if good:
        el = bpy.data.curves.new("elbow", 'CURVE')
        el.dimensions = '3D'
        sp = el.splines.new('BEZIER')
        sp.bezier_points.add(1)
        sp.bezier_points[0].co = (W - 0.25, PLAT[1] - 0.4, 3.25)
        sp.bezier_points[1].co = (W - 0.25, PLAT[1] - 0.1, 2.95)
        for bp in sp.bezier_points:
            bp.handle_left_type = bp.handle_right_type = 'AUTO'
        el.bevel_depth = 0.092
        el.bevel_resolution = 8
        eo = link(bpy.data.objects.new("elbow", el))
        eo.data.materials.append(M["rust"])
        obs.append(eo)
    # перила галереи и марша
    rail_h = 1.0
    pts = [(PLAT[0], D - 0.05), (PLAT[0], PLAT[1]), (W - 0.05, PLAT[1])]
    for (ax, ay), (bx, by) in zip(pts[:-1], pts[1:]):
        z = PLAT[2] + rail_h
        obs.append(cyl("rail", 0.022, (ax, ay, z), (bx, by, z), seg // 2 + 4, M["paint"]))
        obs.append(cyl("rail_mid", 0.014, (ax, ay, z - 0.5), (bx, by, z - 0.5), seg // 2 + 4, M["paint"]))
        n = max(2, int(Vector((bx - ax, by - ay)).length / 1.1) + 1)
        for i in range(n):
            t = i / (n - 1)
            px, py = ax + (bx - ax) * t, ay + (by - ay) * t
            obs.append(cyl("post", 0.02, (px, py, PLAT[2]), (px, py, z), seg // 2 + 4, M["paint"]))
    # перила марша по открытой стороне
    y = STAIR[1] + 0.06
    a = (STAIR[0] + 0.2, y, RISE + rail_h)
    b = (PLAT[0], y, PLAT[2] + rail_h)
    obs.append(cyl("rail_s", 0.022, a, b, seg // 2 + 4, M["paint"]))
    for k in range(0, STAIR[2], 3):
        px = STAIR[0] + (k + 0.5) * RUN
        pz = (k + 1) * RISE
        obs.append(cyl("post_s", 0.02, (px, y, pz), (px, y, pz + rail_h), seg // 2 + 4, M["paint"]))
    # лампа дневного света на задней стене и у двери
    for (lx, ly, lz, ax) in ((2.4, D - 0.08, 2.9, 'x'), (W - 0.08, 4.2, 2.9, 'y')):
        if ax == 'x':
            body = box("lamp_box", lx - 0.65, lx + 0.65, ly - 0.08, ly, lz - 0.06, lz + 0.06, M["paint"])
            tube = cyl("lamp_tube", 0.018, (lx - 0.6, ly - 0.1, lz - 0.03), (lx + 0.6, ly - 0.1, lz - 0.03), 16, M["lamp"])
        else:
            body = box("lamp_box", lx - 0.08, lx, ly - 0.65, ly + 0.65, lz - 0.06, lz + 0.06, M["paint"])
            tube = cyl("lamp_tube", 0.018, (lx - 0.1, ly - 0.6, lz - 0.03), (lx - 0.1, ly + 0.6, lz - 0.03), 16, M["lamp"])
        if good:
            bevel(body, 0.008, 2)
        obs += [body, tube]
        la = bpy.data.lights.new("lamp", 'AREA')
        la.shape = 'RECTANGLE'
        la.size, la.size_y = (1.2, 0.05) if ax == 'x' else (0.05, 1.2)
        la.energy = 45
        la.color = (1.0, 0.9, 0.75)
        lo = link(bpy.data.objects.new("lamp_l", la))
        lo.location = (lx, ly - 0.14, lz - 0.05) if ax == 'x' else (lx - 0.14, ly, lz - 0.05)
        lo.rotation_euler = (R(90), 0, 0) if ax == 'x' else (0, R(-90), 0)
    # кабельный лоток вдоль балки
    tray_z = H - 0.62
    tray = box("tray", BEAMS[1] - 0.15, BEAMS[1] + 0.15, 0.2, D - 0.2, tray_z - 0.06, tray_z, M["steel"])
    obs.append(tray)
    for o in obs:
        if o.type == 'MESH':
            smooth(o)
    return obs


def debris(M, rich):
    """Обломки у колонны и песок у двери."""
    obs = []
    import random
    rnd = random.Random(7)
    for i in range(14 if rich else 6):
        bm = bmesh.new()
        bmesh.ops.create_icosphere(bm, subdivisions=2 if rich else 1, radius=1.0)
        for v in bm.verts:
            v.co *= 1.0 + rnd.uniform(-0.25, 0.25)
        ob = mesh_ob("rock", bm)
        s = rnd.uniform(0.05, 0.22)
        ob.scale = (s * rnd.uniform(0.8, 1.6), s * rnd.uniform(0.8, 1.4), s * rnd.uniform(0.4, 0.8))
        ob.location = (COL[0] + rnd.uniform(-1.2, 1.5), COL[1] + rnd.uniform(-1.4, 1.0), s * 0.25)
        ob.rotation_euler = (rnd.random(), rnd.random(), rnd.random() * 6)
        ob.data.materials.append(M["wall"])
        if rich:
            smooth(ob, 50)
        obs.append(ob)
    if rich:
        # язык песка, намётанный в проём
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=1.0)
        ob = mesh_ob("sand", bm)
        ob.scale = (1.5, 0.9, 0.18)
        ob.location = (W - 0.6, (DOOR[0] + DOOR[1]) / 2, -0.08)
        ob.data.materials.append(M["sand"])
        dp = ob.modifiers.new("d", 'DISPLACE')
        tx = bpy.data.textures.new("sn", 'CLOUDS')
        tx.noise_scale = 0.4
        dp.texture = tx
        dp.strength = 0.05
        smooth(ob, 80)
        obs.append(ob)
    return obs


# ---------------------------------------------------------------- способы
def build_now(M):
    """Как сейчас: стены плитами по 2 м встык со щелью 1.5 см, ступени отдельными блоками, всё коробками."""
    obs = []
    gap = 0.015
    for (name, x0, x1, y0, y1, z0, z1, mat, kind) in concrete_volumes():
        if kind == "shell" and name.startswith("wall"):
            # стену режем на плиты по 2 м вдоль длинной оси, между ними щель
            lx, ly = x1 - x0, y1 - y0
            if lx >= ly:
                n = max(1, round(lx / 2.0))
                for i in range(n):
                    a = x0 + lx * i / n + (gap if i else 0)
                    b = x0 + lx * (i + 1) / n
                    obs.append(box(name, a, b, y0, y1, z0 + gap, z1 - gap, M[mat]))
            else:
                n = max(1, round(ly / 2.0))
                for i in range(n):
                    a = y0 + ly * i / n + (gap if i else 0)
                    b = y0 + ly * (i + 1) / n
                    obs.append(box(name, x0, x1, a, b, z0 + gap, z1 - gap, M[mat]))
        elif kind == "stair":
            obs.append(box(name, x0, x0 + RUN - gap, y0, y1 - gap, z1 - RISE + gap, z1, M[mat]))
        else:
            obs.append(box(name, x0, x1, y0, y1, z0, z1, M[mat]))
    cs = cutters()
    for o in obs:
        if o.name.startswith("wall_back") or o.name.startswith("wall_right"):
            for c in cs:
                boolean(o, c)
        bevel(o, 0.004, 1, harden=False)
        smooth(o, 60)
    return obs


def build_kit(M):
    """Модульный набор: стены — замкнутые щиты 2 м с нахлёстом 3 см, на стыках пилястры, внизу плинтус, вверху карниз;
    марш — одна деталь набора; узор скана в осях детали (повтор набора виден), сдвиг узора по копии."""
    obs = []
    ov = 0.03
    for (name, x0, x1, y0, y1, z0, z1, mat, kind) in concrete_volumes():
        if kind == "shell" and name.startswith("wall"):
            lx, ly = x1 - x0, y1 - y0
            along_x = lx >= ly
            L = lx if along_x else ly
            n = max(1, round(L / 2.0))
            for i in range(n):
                a = (x0 if along_x else y0) + L * i / n - ov
                b = (x0 if along_x else y0) + L * (i + 1) / n + ov
                o = box(name, a, b, y0, y1, z0 - ov, z1 + ov, M[mat]) if along_x else \
                    box(name, x0, x1, a, b, z0 - ov, z1 + ov, M[mat])
                obs.append(o)
                # пилястра на стыке щитов (прячет линию нахлёста)
                s = (x0 if along_x else y0) + L * i / n
                in_door = name == "wall_right" and DOOR[0] - 0.2 < s < DOOR[1] + 0.2
                if i and name in ("wall_back", "wall_right") and not in_door:
                    if along_x:
                        p = box("pilaster", s - 0.14, s + 0.14, y0 - 0.12, y0 + 0.02, 0, H, M["wall"])
                    else:
                        p = box("pilaster", x0 - 0.12, x0 + 0.02, s - 0.14, s + 0.14, 0, H, M["wall"])
                    obs.append(p)
        elif kind == "stair":
            continue
        else:
            obs.append(box(name, x0 - (ov if kind == "struct" else 0), x1 + (ov if kind == "struct" else 0),
                           y0, y1, z0, z1 + (ov if kind == "struct" else 0), M[mat]))
    # марш — одна деталь набора (профиль ступеней вытянут), заходит в заднюю стену и в галерею
    bm = bmesh.new()
    prof = [(STAIR[0], 0.0)]
    for k in range(STAIR[2]):
        x = STAIR[0] + k * RUN
        prof += [(x, (k + 1) * RISE), (x + RUN, (k + 1) * RISE)]
    prof += [(PLAT[0] + ov, PLAT[2]), (PLAT[0] + ov, 0.0)]
    vs0 = [bm.verts.new((x, STAIR[1], z)) for x, z in prof]
    vs1 = [bm.verts.new((x, D + ov, z)) for x, z in prof]
    bm.faces.new(vs0[::-1])
    bm.faces.new(vs1)
    for i in range(len(prof)):
        j = (i + 1) % len(prof)
        bm.faces.new((vs0[i], vs0[j], vs1[j], vs1[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    st = mesh_ob("stair", bm)
    st.data.materials.append(M["floor"])
    obs.append(st)
    # плинтус и карниз по периметру (прячут стык стены с полом и потолком)
    for (a, b, c, d) in ((0, W, D - 0.06, D), (W - 0.06, W, 0, D), (0, W, 0, 0.06), (0, 0.06, 0, D)):
        obs.append(box("plinth", a, b, c, d, -0.02, 0.18, M["wall"]))
        obs.append(box("cornice", a, b, c, d, H - 0.25, H + 0.02, M["wall"]))
    cs = cutters()
    for o in obs:
        if o.name.startswith(("wall_back", "wall_right", "plinth", "cornice", "pilaster")):
            for c in cs:
                boolean(o, c)
        bevel(o, 0.015, 2)
        smooth(o)
    return obs


def tri_displace(ob, prefix, tile, strength):
    """Рельеф по карте высот скана проекцией по трём осям (как узор материала): Geometry Nodes, сдвиг по нормали."""
    hp = os.path.join(SCANS, prefix + "_disp" + SUF)
    if not os.path.exists(hp):
        hp = os.path.join(SCANS, prefix + "_diff" + SUF)
    im = bpy.data.images.load(hp, check_existing=True)
    im.colorspace_settings.name = 'Non-Color'
    g = bpy.data.node_groups.new("tri_disp", 'GeometryNodeTree')
    g.interface.new_socket("Geometry", in_out='INPUT', socket_type='NodeSocketGeometry')
    g.interface.new_socket("Geometry", in_out='OUTPUT', socket_type='NodeSocketGeometry')
    N = g.nodes.new
    L = g.links.new
    gi, go = N("NodeGroupInput"), N("NodeGroupOutput")
    pos, nrm = N("GeometryNodeInputPosition"), N("GeometryNodeInputNormal")
    sc = N("ShaderNodeVectorMath"); sc.operation = 'SCALE'; sc.inputs["Scale"].default_value = 1.0 / tile
    L(pos.outputs[0], sc.inputs[0])
    sp = N("ShaderNodeSeparateXYZ"); L(sc.outputs[0], sp.inputs[0])
    sn = N("ShaderNodeSeparateXYZ"); L(nrm.outputs[0], sn.inputs[0])
    total = None
    wsum = None
    for a, b, wax in (("Y", "Z", "X"), ("X", "Z", "Y"), ("X", "Y", "Z")):
        cv = N("ShaderNodeCombineXYZ")
        L(sp.outputs[a], cv.inputs["X"]); L(sp.outputs[b], cv.inputs["Y"])
        it = N("GeometryNodeImageTexture")
        it.inputs["Image"].default_value = im
        it.extension = 'REPEAT'
        L(cv.outputs[0], it.inputs["Vector"])
        ab = N("ShaderNodeMath"); ab.operation = 'ABSOLUTE'; L(sn.outputs[wax], ab.inputs[0])
        pw = N("ShaderNodeMath"); pw.operation = 'POWER'; pw.inputs[1].default_value = 4.0; L(ab.outputs[0], pw.inputs[0])
        sepc = N("FunctionNodeSeparateColor"); L(it.outputs["Color"], sepc.inputs[0])
        mul = N("ShaderNodeMath"); mul.operation = 'MULTIPLY'; L(sepc.outputs[0], mul.inputs[0]); L(pw.outputs[0], mul.inputs[1])
        if total is None:
            total, wsum = mul, pw
        else:
            t2 = N("ShaderNodeMath"); t2.operation = 'ADD'; L(total.outputs[0], t2.inputs[0]); L(mul.outputs[0], t2.inputs[1]); total = t2
            w2 = N("ShaderNodeMath"); w2.operation = 'ADD'; L(wsum.outputs[0], w2.inputs[0]); L(pw.outputs[0], w2.inputs[1]); wsum = w2
    dv = N("ShaderNodeMath"); dv.operation = 'DIVIDE'; L(total.outputs[0], dv.inputs[0]); L(wsum.outputs[0], dv.inputs[1])
    sb = N("ShaderNodeMath"); sb.operation = 'SUBTRACT'; sb.inputs[1].default_value = 0.5; L(dv.outputs[0], sb.inputs[0])
    st = N("ShaderNodeMath"); st.operation = 'MULTIPLY'; st.inputs[1].default_value = strength; L(sb.outputs[0], st.inputs[0])
    off = N("ShaderNodeVectorMath"); off.operation = 'SCALE'; L(nrm.outputs[0], off.inputs[0]); L(st.outputs[0], off.inputs["Scale"])
    setp = N("GeometryNodeSetPosition")
    L(gi.outputs[0], setp.inputs["Geometry"]); L(off.outputs[0], setp.inputs["Offset"]); L(setp.outputs[0], go.inputs[0])
    m = ob.modifiers.new("disp", 'NODES')
    m.node_group = g


def build_cast(M, disp=True, kinds=("shell", "struct", "stair", "plat")):
    """Цельная отливка: все бетонные объёмы складываются и вычитаются в одно тело, воксельный ремеш (замкнуто по
    построению, литые переходы), рельеф смещением по карте высот скана."""
    vols = concrete_volumes()
    parts = []
    for (name, x0, x1, y0, y1, z0, z1, mat, kind) in vols:
        if kind in kinds:
            parts.append(box(name, x0, x1, y0, y1, z0, z1))
    base = parts[0]
    for p in parts[1:]:
        boolean(base, p, 'UNION')
    apply_mods(base)
    for c in cutters():
        boolean(base, c)
    apply_mods(base)
    rm = base.modifiers.new("rm", 'REMESH')
    rm.mode = 'VOXEL'
    rm.voxel_size = 0.03 if QUICK else 0.018
    apply_mods(base)
    sm = base.modifiers.new("sm", 'CORRECTIVE_SMOOTH')
    sm.iterations = 4
    if disp:
        tri_displace(base, names()["wall"], 2.0, 0.014)
    # материал по нормали: пол/верх ступеней — пол, остальное — стена
    apply_mods(base)
    base.data.materials.clear()   # пустые слоты от вспомогательных коробок
    base.data.materials.append(M["wall"])
    base.data.materials.append(M["floor"])
    me = base.data
    for poly in me.polygons:
        poly.material_index = 1 if poly.normal.z > 0.7 else 0
    activate(base)
    bpy.ops.object.shade_smooth()
    return [base]


def build_hybrid(M):
    """Гибрид: оболочка, пол, марш и галерея — литым телом (как cast), колонна и балки — детали набора с нахлёстом,
    материалы с грязью по затенению, светлыми кромками и потёками."""
    obs = build_cast(M, kinds=("shell", "stair", "plat"))
    ov = 0.03
    for (name, x0, x1, y0, y1, z0, z1, mat, kind) in concrete_volumes():
        if kind == "struct":
            o = box(name, x0, x1, y0, y1, z0 - ov, z1 + ov, M[mat])
            bevel(o, 0.02, 3)
            sub = o.modifiers.new("ss", 'SUBSURF')
            sub.subdivision_type = 'SIMPLE'
            sub.levels = sub.render_levels = 3
            tri_displace(o, names()["wall"], 2.0, 0.008)
            smooth(o)
            obs.append(o)
    return obs


# ---------------------------------------------------------------- свет и кадр
def lights(sc):
    s = bpy.data.objects.new("sun", bpy.data.lights.new("sun", 'SUN'))
    s.data.energy = 9.0
    s.data.angle = R(0.8)
    s.data.color = (1.0, 0.93, 0.82)
    # солнце сзади-справа, низко: луч через щель на пол и колонну
    s.rotation_euler = (R(60), R(0), R(205))
    link(s)
    if os.environ.get("DUST") != "1":
        return
    # пыль в воздухе комнаты (лучи)
    v = box("dust", 0.05, W - 0.05, 0.05, D - 0.05, 0.02, H - 0.02)
    m = bpy.data.materials.new("dust")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.remove(nt.nodes["Principled BSDF"])
    pv = nt.nodes.new("ShaderNodeVolumePrincipled")
    pv.inputs["Density"].default_value = 0.05
    pv.inputs["Color"].default_value = (0.9, 0.85, 0.78, 1)
    pv.inputs["Anisotropy"].default_value = 0.4
    nt.links.new(pv.outputs[0], nt.nodes["Material Output"].inputs["Volume"])
    v.data.materials.append(m)
    v.visible_shadow = False


def camera(shot):
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    link(cam)
    if shot == "wide":
        cam.data.lens = 18
        pos, tgt = Vector((0.9, 0.7, 1.65)), Vector((7.0, 5.6, 1.7))
    elif shot == "joint":  # стык: шов задней стены (у «now» щель 1.5 см) и низ стены у пола
        cam.data.lens = 35
        sx = -T + (W + 2 * T) * 2 / 6 + 0.0075
        pos, tgt = Vector((sx, 4.2, 1.0)), Vector((sx, D, 0.55))
    else:  # крупно: марш у задней стены и галерея
        cam.data.lens = 28
        pos, tgt = Vector((3.2, 2.6, 1.6)), Vector((6.2, 6.4, 1.3))
    cam.location = pos
    cam.rotation_euler = (tgt - pos).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = cam


def main():
    sc = reset()
    M = materials(MODE)
    good = MODE != "now"
    obs = {"now": build_now, "kit": build_kit, "cast": build_cast, "hybrid": build_hybrid}[MODE](M)
    pr = props(M, good)
    db = debris(M, MODE != "now")
    lights(sc)
    camera(SHOT)
    n_shell = sum(tris(o) for o in obs if o.type == 'MESH')
    n_props = sum(tris(o) for o in pr + db if o.type == 'MESH')
    print("STATS", MODE, "shell_objects", len(obs), "shell_tris", n_shell, "props_tris", n_props)
    sc.render.filepath = os.path.join(OUT, "%s_%s.png" % (MODE, SHOT))
    bpy.ops.render.render(write_still=True)


main()
