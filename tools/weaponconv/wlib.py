# Общие части для скриптов оружия: материалы, лофт по сечениям, сцена и рендер сбоку
# Ось вещи — X (к острию +X), вверх — Z, толщина — Y. Камера смотрит с -Y.
import bpy, bmesh, math, sys
from mathutils import Vector

def lerp(a, b, t): return a + (b - a) * t
def smooth(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)

def args():
    a = sys.argv[sys.argv.index('--') + 1:]
    return a[0], (int(a[1]) if len(a) > 1 else 48)

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene

def spline(ctrl, n):
    # Катмулл-Ром по опорным точкам, n точек на участок
    out = []
    c = [ctrl[0]] + list(ctrl) + [ctrl[-1]]
    for i in range(1, len(c) - 2):
        p0, p1, p2, p3 = c[i - 1], c[i], c[i + 1], c[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                   + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(len(p1))))
    out.append(tuple(ctrl[-1]))
    return out

def interp(tab, x):
    # кусочно-линейно по таблице (x, v...)
    if x <= tab[0][0]: return tab[0][1:]
    for a, b in zip(tab, tab[1:]):
        if a[0] <= x <= b[0]:
            t = (x - a[0]) / (b[0] - a[0]) if b[0] != a[0] else 0
            return tuple(lerp(u, v, t) for u, v in zip(a[1:], b[1:]))
    return tab[-1][1:]

# ---------------------------------------------------------------- геометрия
def loft(name, xs, section, mats, cap0=True, cap1=True, smooth_shade=False):
    # section(x) -> [(y, z, mat_index_of_face_after_this_point), ...] одинаковой длины, обход замкнутый
    bm = bmesh.new()
    rings = []
    for x in xs:
        sec = section(x)
        rings.append(([bm.verts.new((x, y, z)) for y, z, _ in sec], [m for _, _, m in sec]))
    n = len(rings[0][0])
    for (ra, ma), (rb, _) in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            try:
                f = bm.faces.new((ra[i], ra[j], rb[j], rb[i])); f.material_index = ma[i]
            except ValueError:
                pass
    for flag, (r, m), rev in ((cap0, rings[0], True), (cap1, rings[-1], False)):
        if flag:
            try:
                f = bm.faces.new(list(reversed(r)) if rev else r); f.material_index = m[0]
            except ValueError:
                pass
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me)
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    for m in mats: me.materials.append(m)
    for p in me.polygons: p.use_smooth = smooth_shade
    return ob

def ring(r, n=16, m=0, rz=None, phase=0.0):
    rz = r if rz is None else rz
    return [(r * math.cos(phase + 2 * math.pi * i / n), rz * math.sin(phase + 2 * math.pi * i / n), m) for i in range(n)]

def bevel(ob, w=0.001, seg=2, angle=35):
    b = ob.modifiers.new('bev', 'BEVEL'); b.width = w; b.segments = seg
    b.limit_method = 'ANGLE'; b.angle_limit = math.radians(angle)
    return b

# ---------------------------------------------------------------- материалы
def mat_steel(name, base, rough, grime, scale=9, bump=0.08, grain=400):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bs = nt.nodes['Principled BSDF']
    bs.inputs['Metallic'].default_value = 1.0
    tc = nt.nodes.new('ShaderNodeTexCoord')
    tex = nt.nodes.new('ShaderNodeTexNoise'); tex.inputs['Scale'].default_value = scale
    tex.inputs['Detail'].default_value = 12; tex.inputs['Roughness'].default_value = 0.62
    nt.links.new(tc.outputs['Object'], tex.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (*[c * (1 - grime) for c in base], 1)
    ramp.color_ramp.elements[1].color = (*base, 1)
    nt.links.new(tex.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bs.inputs['Base Color'])
    rr = nt.nodes.new('ShaderNodeMapRange')
    rr.inputs['To Min'].default_value = rough - 0.15; rr.inputs['To Max'].default_value = rough + 0.2
    nt.links.new(tex.outputs['Fac'], rr.inputs['Value']); nt.links.new(rr.outputs['Result'], bs.inputs['Roughness'])
    bp = nt.nodes.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = bump
    n2 = nt.nodes.new('ShaderNodeTexNoise'); n2.inputs['Scale'].default_value = grain
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    nt.links.new(n2.outputs['Fac'], bp.inputs['Height']); nt.links.new(bp.outputs['Normal'], bs.inputs['Normal'])
    return m

def mat_plain(name, col, rough, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (*col, 1); bs.inputs['Roughness'].default_value = rough
    bs.inputs['Metallic'].default_value = metal
    return m

# ---------------------------------------------------------------- сцена и рендер, как у РЕШЕТА
def stage(center_x, center_z, ortho, res=(886, 498)):
    scn = bpy.context.scene
    world = bpy.data.worlds.new('w'); scn.world = world; world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.18, 0.18, 0.18, 1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.25
    k = ortho / 1.62
    bpy.ops.object.light_add(type='AREA', location=(center_x, -0.6 * k, 1.0 * k))
    L = bpy.context.active_object; L.data.energy = 45 * k * k; L.data.size = 1.5 * k
    L.rotation_euler = Vector((0.0, 0.6, -1.0)).to_track_quat('-Z', 'Y').to_euler()
    bpy.ops.object.light_add(type='AREA', location=(center_x, -0.8 * k, -0.4 * k))
    L2 = bpy.context.active_object; L2.data.energy = 8 * k * k; L2.data.size = 2 * k
    L2.rotation_euler = Vector((0.0, 0.8, 0.4)).to_track_quat('-Z', 'Y').to_euler()
    bpy.ops.mesh.primitive_plane_add(size=6 * k, location=(center_x, 0.3 * k, center_z), rotation=(math.radians(90), 0, 0))
    bpy.context.active_object.data.materials.append(mat_plain('bg', (0.3, 0.3, 0.3), 0.9))
    bpy.ops.object.camera_add(location=(center_x, -3.0 * k, center_z))
    cam = bpy.context.active_object
    cam.rotation_euler = Vector((0, 1, 0)).to_track_quat('-Z', 'Z').to_euler()
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = ortho
    scn.camera = cam
    scn.render.resolution_x, scn.render.resolution_y = res
    scn.view_settings.view_transform = 'Standard'
    return cam

def render(out, samples):
    import os
    if os.environ.get('NORENDER'): return
    scn = bpy.context.scene
    scn.render.engine = 'CYCLES'; scn.cycles.samples = samples
    scn.cycles.device = 'CPU'; scn.cycles.use_denoising = False
    scn.render.filepath = out
    bpy.ops.render.render(write_still=True)

def diamond(zt, zr, zb, T, e=0.0006, band=0.0):
    # сечение обоюдоострого клинка (в метрах): кромки сверху и снизу, ребро на zr;
    # band — доля от кромки до ребра под светлой заточкой (материал 1)
    pts = [(e, zt)]
    if band: pts.append((T * band, lerp(zt, zr, band)))
    pts.append((T, zr))
    if band: pts.append((T * band, lerp(zb, zr, band)))
    pts.append((e, zb))
    out = pts + [(-y, z) for y, z in reversed(pts)]
    n = len(out); res = []
    for i in range(n):
        a, b = out[i], out[(i + 1) % n]
        m = 1 if band and (abs(a[0]) <= e * 1.01 or abs(b[0]) <= e * 1.01) else 0
        res.append((a[0], a[1], m))
    return res

def group_move(objs, dx, dz):
    for o in objs:
        o.location.x += dx; o.location.z += dz

def new_objs(before):
    return [o for o in bpy.context.scene.objects if o.name not in before]

def stage_flat(center_x, center_z, ortho, res):
    # тот же свет, что у stage, для листа с несколькими вещами
    return stage(center_x, center_z, ortho, res)

def tube(name, pts, r, mat, res=3, smooth_shade=True):
    # трубка по ломаной (x, y, z): прут, проволока, кольцо
    cu = bpy.data.curves.new(name, 'CURVE'); cu.dimensions = '3D'
    sp = cu.splines.new('POLY'); sp.points.add(len(pts) - 1)
    for p, c in zip(sp.points, pts): p.co = (c[0], c[1], c[2], 1)
    cu.bevel_depth = r; cu.bevel_resolution = res; cu.use_fill_caps = True
    ob = bpy.data.objects.new(name, cu); bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob

def edge_sec(zt, zb, T, band, e=0.0004, both=True):
    # сечение однолезвийного клинка: обух сверху (zt), лезвие снизу (zb), спуск высотой band с обеих сторон
    # или только с лицевой (-Y), если both=False; грань спуска — материал 1
    zb2 = min(zb + band, zt - 0.0005)
    if both:
        pts = [(-T, zt, 0), (T, zt, 0), (T, zb2, 1), (e, zb, 0), (-e, zb, 1), (-T, zb2, 0)]
    else:
        pts = [(-T, zt, 0), (T, zt, 0), (T, zb, 0), (T - 0.0008, zb, 1), (-T, zb2, 0)]
    return pts

def mat_tape(name, col):
    m = mat_plain(name, col, 0.35)
    return m

# ---------------------------------------------------------------- выгрузка в игру
# Сетка AMSH 1 (src/core/mesh.h): Vertex = pos 3f, nrm 3f, color 4B (альфа — параметр материала), mat, bone, tint, pad.
# Оси игры: острие по +Y, лезвие по +Z, толщина по X, кулак правой руки в начале координат.
# Здесь: острие по +X, лезвие по -Z, толщина по Y. pad — номер части (игра по нему раскладывает ржавчину и обнуляет)
GAME_MAT = {'concrete': 0, 'rust': 1, 'cloth': 3, 'leather': 4, 'glyph': 5, 'metal': 8, 'steel': 15}
MATMAP = {
    'steel': ('steel', (0.24, 0.24, 0.25)), 'blade': ('steel', (0.24, 0.24, 0.25)), 'edge': ('steel', (0.36, 0.36, 0.37)),
    'plate': ('steel', (0.27, 0.27, 0.28)), 'forged': ('steel', (0.22, 0.22, 0.22)), 'file': ('steel', (0.2, 0.2, 0.21)),
    'iron': ('metal', (0.15, 0.15, 0.16)), 'dark': ('metal', (0.11, 0.11, 0.12)), 'shaft': ('metal', (0.13, 0.13, 0.14)),
    'pipe': ('metal', (0.13, 0.13, 0.13)), 'knurl': ('metal', (0.2, 0.2, 0.21)), 'wire': ('metal', (0.33, 0.32, 0.3)),
    'hole': ('metal', (0.03, 0.03, 0.03)), 'rustish': ('rust', (0.3, 0.2, 0.13)),
    'leather': ('leather', (0.13, 0.09, 0.06)), 'textolite': ('leather', (0.16, 0.14, 0.08)), 'wood': ('leather', (0.2, 0.13, 0.07)),
    'rag': ('cloth', (0.17, 0.15, 0.12)), 'cord': ('cloth', (0.08, 0.08, 0.08)), 'tape': ('cloth', (0.05, 0.05, 0.06)),
    'glyph': ('glyph', (0.3, 0.42, 0.33)),
}

def tri_count(objs):
    dg = bpy.context.evaluated_depsgraph_get()
    n = 0
    for ob in objs:
        me = ob.evaluated_get(dg).to_mesh(); me.calc_loop_triangles(); n += len(me.loop_triangles)
        ob.evaluated_get(dg).to_mesh_clear()
    return n

def lower_poly(objs, budget):
    # Бюджет треугольников вещи: кривые в сетки, плоские участки сливаются, остальное прореживается схлопыванием рёбер
    curves = [o for o in objs if o.type == 'CURVE']
    if curves:
        bpy.ops.object.select_all(action='DESELECT')
        for o in curves: o.select_set(True)
        bpy.context.view_layer.objects.active = curves[0]
        bpy.ops.object.convert(target='MESH')
    meshes = [o for o in objs if o.type == 'MESH']
    for o in meshes:
        d = o.modifiers.new('flat', 'DECIMATE'); d.decimate_type = 'DISSOLVE'; d.angle_limit = math.radians(1.5)
    n = tri_count(meshes)
    if n > budget:
        r = budget / n
        for o in meshes:
            d = o.modifiers.new('less', 'DECIMATE'); d.ratio = r; d.use_collapse_triangulate = True
    return meshes

def export_weapon(wid, objs, fist, budget=2400):
    # EXPORT=<папка>: записать <папка>/<wid>.mesh; fist — (x, z) кулака правой руки в осях скрипта
    import os, struct
    out = os.environ.get('EXPORT')
    if not out: return
    objs = lower_poly([o for o in objs if o.type in ('MESH', 'CURVE')], budget)
    dg = bpy.context.evaluated_depsgraph_get()
    fx, fz = fist
    verts, idx, seen = [], [], {}
    part = 0
    for ob in objs:
        if ob.type not in ('MESH', 'CURVE'): continue
        ev = ob.evaluated_get(dg)
        me = ev.to_mesh()
        me.calc_loop_triangles()
        if hasattr(me, 'calc_normals_split'): me.calc_normals_split()
        M = ob.matrix_world
        Nm = M.to_3x3().inverted().transposed()
        names = [(m.name.split('.')[0] if m else '') for m in me.materials] or ['']
        for tri in me.loop_triangles:
            mname = names[min(tri.material_index, len(names) - 1)]
            gm, col = MATMAP.get(mname, ('metal', (0.15, 0.15, 0.16)))
            for k in range(3):
                co = M @ me.vertices[tri.vertices[k]].co
                n = (Nm @ Vector(tri.split_normals[k])).normalized()
                v = (round(co.y, 5), round(co.x - fx, 5), round(-(co.z - fz), 5),
                     round(n.y, 3), round(n.x, 3), round(-n.z, 3), gm, col, part & 255)
                i = seen.get(v)
                if i is None:
                    i = seen[v] = len(verts); verts.append(v)
                idx.append(i)
        ev.to_mesh_clear()
        part += 1
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, wid + '.mesh'), 'wb') as f:
        f.write(b'AMSH' + struct.pack('<3I', 1, len(verts), len(idx)))
        for x, y, z, nx, ny, nz, gm, col, pt in verts:
            c = [max(0, min(255, int(c * 255 + 0.5))) for c in col]
            f.write(struct.pack('<6f8B', x, y, z, nx, ny, nz, c[0], c[1], c[2], 255, GAME_MAT[gm], 255, 0, pt))
        f.write(struct.pack('<%dI' % len(idx), *idx))
    print('EXPORT', wid, 'вершин', len(verts), 'треугольников', len(idx) // 3)
