# РЕШЕТО по концепту: модель скриптом, рендер сбоку для сравнения
# blender -b -P resheto.py -- out.png
import bpy, bmesh, math, sys
from mathutils import Vector

OUT = sys.argv[sys.argv.index('--') + 1]
S = 1.5 / 830.0          # м на пиксель концепта
X0, Y0 = 240.0, 215.0     # пята клинка и линия обуха в пикселях концепта

def P(px, py):
    # пиксели концепта -> (вдоль, вверх) в метрах
    return ((px - X0) * S, -(py - Y0) * S)

bpy.ops.wm.read_factory_settings(use_empty=True)
scn = bpy.context.scene

# ---------------------------------------------------------------- контур клинка
def lerp(a, b, t): return a + (b - a) * t

def spline(ctrl, n):
    # Катмулл-Ром по опорным точкам, n точек на участок
    out = []
    c = [ctrl[0]] + ctrl + [ctrl[-1]]
    for i in range(1, len(c) - 2):
        p0, p1, p2, p3 = c[i - 1], c[i], c[i + 1], c[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                   + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(2)))
    out.append(ctrl[-1])
    return out

# Обух опущен на 16 px (клинок ниже, кромка и заточка на месте): у пяты целиком, к острию сдвиг сходит на нет
def SH(px):
    return 16.0 if px <= 630 else 16.0 * max(0.0, (846 - px) / 216.0)

def blade_outline():
    # Точь-в-точь по концепту (столбцы концепта промерены по яркости): обух прямой до 630 px, к острию провисает
    # до 243 и поднимается к острию 846,222; кромка снизу почти прямая, потом дугой к острию;
    # линия заточки волной: у пяты 276, к середине поднимается до 263, к 600 опускается до 276, дальше к острию
    pts = []
    # обух с тремя сколами разного размера у гарды: (где, полуширина, глубина) в пикселях концепта
    pts.append((*P(240, 232), 0.0))
    for cx, w, d in ((285, 3, 5), (318, 6, 10), (362, 4.5, 7)):
        pts += [(*P(cx - w, 232), 0.0), (*P(cx - w * 0.2, 232 + d), 0.0), (*P(cx + w, 232), 0.0)]
    # обух: прямой до 630, прямым скосом вниз до 770,240, дальше вогнутой дугой вверх к острию 846,221
    top = [(600, 216), (630, 217)] + [(lerp(630, 770, t / 6), lerp(217, 240, t / 6)) for t in range(1, 7)]
    top += spline([(770, 240), (790, 238.5), (810, 234), (830, 228), (846, 221)], 4)[1:]
    pts += [(*P(x, y + SH(x)), 0.0) for x, y in top[:-1]]
    # кромка по линии пользователя на концепте: от пяты поднимается к середине (285 у 420–470),
    # провисает к 610–650 (302), дальше вогнутой дугой к острию
    low = spline([(846, 221), (840, 230), (825, 242), (810, 252), (790, 263), (770, 272), (750, 281), (730, 288),
                  (710, 295), (690, 300), (650, 305), (610, 304), (570, 297), (520, 288), (470, 282), (420, 280),
                  (370, 284), (330, 292), (300, 299), (275, 301), (255, 301), (240, 301)], 4)
    # линия заточки: волна у пяты и середины, к острию сходится с обухом у 770
    grind = spline([(846, 221), (825, 229), (800, 236), (770, 241), (740, 251), (700, 261), (660, 270), (630, 275),
                    (600, 277), (575, 276), (550, 275), (515, 272), (480, 269), (450, 265), (415, 262), (390, 263),
                    (365, 265), (330, 268), (290, 270), (240, 272)], 4)
    gx = [p[0] for p in grind]
    def gz(x):
        for k in range(len(grind) - 1):
            a, b = grind[k], grind[k + 1]
            if (a[0] - x) * (b[0] - x) <= 0:
                t = (x - a[0]) / (b[0] - a[0]) if b[0] != a[0] else 0
                return a[1] + (b[1] - a[1]) * t
        return grind[-1][1]
    for i, (x, y) in enumerate(low):
        if i == 0:
            pts.append((*P(x, y), 0.0)); continue
        # заточка — ровная полоса вдоль кромки, повторяет её изгибы; к острию сужается
        band = 20.0 * min(1.0, (846 - x) / 60.0) ** 0.7
        yg = y - max(band, 0.6)
        pts.append((*P(x, y), P(x, yg)))
    return pts

def slot(cx, cy, rx, ry, n=28):
    c = P(cx, cy)
    return [(c[0] + rx * S * math.cos(-2 * math.pi * i / n), c[1] + ry * S * math.sin(-2 * math.pi * i / n)) for i in range(n)]

def build_blade():
    outline = blade_outline()
    holes = []
    T = 0.0042        # полутолщина плоскости
    TE = 0.0005       # полутолщина у кромки
    me = bpy.data.meshes.new('blade')
    bm = bmesh.new()
    # нормаль внутрь контура в 2D
    N = len(outline)
    def inward(i):
        a = Vector(outline[i - 1][:2]); b = Vector(outline[(i + 1) % N][:2])
        d = (b - a).normalized()
        return Vector((d.y, -d.x))   # обход по часовой: внутрь справа
    O_top, O_bot, I_top, I_bot = [], [], [], []
    for i, (x, z, g) in enumerate(outline):
        p = Vector((x, z))
        # g — ширина заточки или прямо точка линии заточки (x, z) над кромкой
        if isinstance(g, tuple):
            q = Vector(g); g = (p - q).length
        else:
            q = p + inward(i) * g
        xo = TE if g > 0.002 else T
        O_top.append(bm.verts.new((x, xo, z))); O_bot.append(bm.verts.new((x, -xo, z)))
        I_top.append(bm.verts.new((q.x, T, q.y))); I_bot.append(bm.verts.new((q.x, -T, q.y)))
    H_top, H_bot = [], []
    for h in holes:
        H_top.append([bm.verts.new((x, T, z)) for x, z in h])
        H_bot.append([bm.verts.new((x, -T, z)) for x, z in h])
    # плоскости: заливка с дырами через рёбра
    for side, I, H in ((1, I_top, H_top), (-1, I_bot, H_bot)):
        edges = [bm.edges.new((I[i], I[(i + 1) % N])) for i in range(N)]
        for ring in H:
            edges += [bm.edges.new((ring[i], ring[(i + 1) % len(ring)])) for i in range(len(ring))]
        res = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=edges)
        for f in res['geom']:
            if isinstance(f, bmesh.types.BMFace):
                f.material_index = 0
    # обод: плоскость -> кромка -> кромка -> плоскость
    for i in range(N):
        j = (i + 1) % N
        gw = lambda o: (o[2] if not isinstance(o[2], tuple) else (Vector(o[:2]) - Vector(o[2])).length)
        g = gw(outline[i]) > 0.002 or gw(outline[j]) > 0.002
        for a, b in ((I_top, O_top), (O_top, O_bot), (O_bot, I_bot)):
            if a is O_top and not g:
                pass
            try:
                f = bm.faces.new((a[i], a[j], b[j], b[i]))
                f.material_index = 1 if g else 0
            except ValueError:
                pass
    for H1, H2 in zip(H_top, H_bot):
        m = len(H1)
        for i in range(m):
            j = (i + 1) % m
            bm.faces.new((H1[i], H1[j], H2[j], H2[i]))
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me)
    ob = bpy.data.objects.new('blade', me)
    scn.collection.objects.link(ob)
    bev = ob.modifiers.new('bev', 'BEVEL'); bev.width = 0.0012; bev.segments = 2; bev.limit_method = 'ANGLE'
    bev.angle_limit = math.radians(35)
    return ob

# ---------------------------------------------------------------- рукоять, гарда, навершие, хомуты
def tube_along(points, r, name, seg=12):
    cu = bpy.data.curves.new(name, 'CURVE'); cu.dimensions = '3D'
    sp = cu.splines.new('POLY'); sp.points.add(len(points) - 1)
    for p, c in zip(sp.points, points): p.co = (c[0], c[1], c[2], 1)
    cu.bevel_depth = r; cu.bevel_resolution = 3; cu.use_fill_caps = True
    ob = bpy.data.objects.new(name, cu); scn.collection.objects.link(ob)
    return ob

def grip_path(n=40):
    # от гарды назад прямо, по оси середины гарды
    pts = []
    for i in range(n + 1):
        t = i / n
        px = lerp(212, 55, t)
        py = 266.5
        x, z = P(px, py)
        pts.append((x, 0.0, z))
    return pts

def box(name, c, size, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=c)
    ob = bpy.context.active_object; ob.name = name; ob.scale = size
    bev = ob.modifiers.new('bev', 'BEVEL'); bev.width = 0.002; bev.segments = 2
    ob.data.materials.append(mat)
    return ob

def mat_steel(name, base, rough, grime):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bs = nt.nodes['Principled BSDF']
    bs.inputs['Metallic'].default_value = 1.0
    tex = nt.nodes.new('ShaderNodeTexNoise'); tex.inputs['Scale'].default_value = 9; tex.inputs['Detail'].default_value = 12; tex.inputs['Roughness'].default_value = 0.62
    tc = nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tc.outputs['Object'], tex.inputs['Vector'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color = (*[c * (1 - grime) for c in base], 1)
    ramp.color_ramp.elements[1].color = (*base, 1)
    nt.links.new(tex.outputs['Fac'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bs.inputs['Base Color'])
    rr = nt.nodes.new('ShaderNodeMapRange'); rr.inputs['To Min'].default_value = rough - 0.15; rr.inputs['To Max'].default_value = rough + 0.2
    nt.links.new(tex.outputs['Fac'], rr.inputs['Value']); nt.links.new(rr.outputs['Result'], bs.inputs['Roughness'])
    bump = nt.nodes.new('ShaderNodeBump'); bump.inputs['Strength'].default_value = 0.08
    n2 = nt.nodes.new('ShaderNodeTexNoise'); n2.inputs['Scale'].default_value = 400
    nt.links.new(tc.outputs['Object'], n2.inputs['Vector'])
    nt.links.new(n2.outputs['Fac'], bump.inputs['Height']); nt.links.new(bump.outputs['Normal'], bs.inputs['Normal'])
    return m

def mat_plain(name, col, rough, metal=0.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    bs = m.node_tree.nodes['Principled BSDF']
    bs.inputs['Base Color'].default_value = (*col, 1); bs.inputs['Roughness'].default_value = rough
    bs.inputs['Metallic'].default_value = metal
    return m

M_BLADE = mat_steel('blade', (0.06, 0.06, 0.064), 0.5, 0.6)
M_EDGE = mat_steel('edge', (0.17, 0.17, 0.175), 0.38, 0.35)
M_DARK = mat_steel('dark', (0.04, 0.04, 0.042), 0.55, 0.4)
M_LEATHER = mat_plain('leather', (0.012, 0.009, 0.007), 0.7)
M_CORD = mat_plain('cord', (0.016, 0.012, 0.009), 0.6)

blade = build_blade()
blade.data.materials.append(M_BLADE); blade.data.materials.append(M_EDGE)
for p in blade.data.polygons: p.use_smooth = False

# Гарда: толстый брусок у пяты, выше обуха и ниже зубцов
gx0, gz0 = P(212, 222.5); gx1, gz1 = P(240, 310.5)
box('guard', ((gx0 + gx1) / 2, 0, (gz0 + gz1) / 2), (gx1 - gx0, 0.03, gz0 - gz1), M_DARK)
# Рукоять: сердечник и спиральная кожаная обмотка поверх изогнутой оси
path = grip_path()
core = tube_along(path, 0.0195, 'core'); core.data.materials.append(M_LEATHER)
# витки: точки по спирали вокруг оси
spiral = []
turns = 11
for i in range(len(path) * 8):
    t = i / (len(path) * 8 - 1)
    k = t * (len(path) - 1); a = int(min(k, len(path) - 2)); f = k - a
    p = Vector(path[a]).lerp(Vector(path[a + 1]), f)
    d = (Vector(path[a + 1]) - Vector(path[a])).normalized()
    side = Vector((0, 1, 0)); up = d.cross(side).normalized()
    ang = t * turns * 2 * math.pi
    spiral.append(tuple(p + (side * math.cos(ang) + up * math.sin(ang)) * 0.0192))

# Навершие: короткий толстый цилиндр с косым срезом
px, pz = P(38, 266.5)
bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.03, depth=0.06, location=(px, 0, pz),
                                    rotation=(0, math.radians(90), 0))
pm = bpy.context.active_object; pm.data.materials.append(M_DARK)
b = pm.modifiers.new('bev', 'BEVEL'); b.width = 0.004; b.segments = 2
# выгрузка в игру: кулак правой руки у гарды, на оси рукояти
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import export_weapon
export_weapon('resheto', list(scn.objects), P(179, 266.5))
if os.environ.get('NORENDER'): sys.exit(0)
# ---------------------------------------------------------------- свет и камера как на концепте: серый фон, мягкий верхний свет
world = bpy.data.worlds.new('w'); scn.world = world; world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.18, 0.18, 0.18, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.25
bpy.ops.object.light_add(type='AREA', location=(0.4, -0.6, 1.0))
L = bpy.context.active_object; L.data.energy = 45; L.data.size = 1.5
L.rotation_euler = (math.radians(-30), 0, 0)
L.rotation_euler = Vector((0.0, 0.6, -1.0)).to_track_quat('-Z', 'Y').to_euler()
bpy.ops.object.light_add(type='AREA', location=(0.4, -0.8, -0.4))
L2 = bpy.context.active_object; L2.data.energy = 8; L2.data.size = 2
L2.rotation_euler = Vector((0.0, 0.8, 0.4)).to_track_quat('-Z', 'Y').to_euler()
# фон-плоскость позади
bpy.ops.mesh.primitive_plane_add(size=6, location=(0.4, 0.3, 0), rotation=(math.radians(90), 0, 0))
bg = bpy.context.active_object; bg.data.materials.append(mat_plain('bg', (0.3, 0.3, 0.3), 0.9))

cx = P(445, 250)[0]
bpy.ops.object.camera_add(location=(cx, -3.0, -0.05))
cam = bpy.context.active_object
cam.rotation_euler = Vector((0, 1, 0)).to_track_quat('-Z', 'Z').to_euler()
cam.data.type = 'ORTHO'; cam.data.ortho_scale = 1.62
scn.camera = cam
scn.render.engine = 'CYCLES'; scn.cycles.samples = int(sys.argv[sys.argv.index('--') + 2]) if len(sys.argv) > sys.argv.index('--') + 2 else 48
scn.cycles.device = 'CPU'; scn.cycles.use_denoising = False
scn.render.resolution_x = 886; scn.render.resolution_y = 498
scn.view_settings.view_transform = 'Standard'
scn.render.filepath = OUT
for o in scn.objects: print('MAT', o.name, o.type, [m.name for m in o.data.materials] if hasattr(o.data,'materials') else None)
bpy.ops.render.render(write_still=True)
