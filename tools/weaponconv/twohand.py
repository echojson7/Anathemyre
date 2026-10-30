# Двуручное: ДОЛГАЯ ПОЛОСА, ОТВАЛ, БАЛКА (v2: ОТВАЛ и БАЛКА переделаны по скриншотам пользователя) — на одном листе, в одном масштабе
# blender -b -P twohand.py -- out.png [сэмплы]
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *

OUT, SAMPLES = args()
reset()
rnd = random.Random(11)
M_STEEL = mat_steel('steel', (0.075, 0.075, 0.08), 0.42, 0.5, scale=5, bump=0.006, grain=120)
M_EDGE = mat_steel('edge', (0.22, 0.22, 0.225), 0.3, 0.3, scale=5, bump=0.005, grain=120)
M_IRON = mat_steel('iron', (0.06, 0.058, 0.055), 0.6, 0.55, scale=8, bump=0.04, grain=180)
M_RUST = mat_steel('rustish', (0.075, 0.05, 0.035), 0.75, 0.5, scale=10, bump=0.06, grain=220)
M_DARK = mat_steel('dark', (0.035, 0.035, 0.037), 0.5, 0.5, scale=8, bump=0.02, grain=200)
M_LEATHER = mat_plain('leather', (0.011, 0.007, 0.0045), 0.6)
M_RAG = mat_plain('rag', (0.028, 0.025, 0.02), 0.95)
M_WIRE = mat_plain('wire', (0.08, 0.075, 0.07), 0.4, 1.0)
M_HOLE = mat_plain('hole', (0.004, 0.004, 0.004), 0.9)
for m in (M_LEATHER, M_RAG, M_HOLE):
    m.node_tree.nodes['Principled BSDF'].inputs['Specular IOR Level'].default_value = 0.15

def saw(u):
    f = u - math.floor(u)
    return f if f < 0.75 else (1 - f) * 3

def hexnut(x0, x1, r, mat, z=0.0, y=0.0):
    ob = loft('nut', [x0, x1], lambda x: [(yy + y, zz + z, m) for yy, zz, m in ring(r, 6, 0)], [mat])
    bevel(ob, 0.0015, 2)
    return ob

def wrap(x0, x1, r, rz, mat, step=0.016, bulge=0.0015, z=0.0, y=0.0, n=20):
    # обмотка внахлёст: каждый виток чуть толще к краю
    xs = [x0 + (x1 - x0) * i / 160 for i in range(161)]
    def sec(x):
        b = bulge * saw((x - x0) / step) + 0.0006 * math.sin(x * 397.0)
        return [(yy + y, zz + z, m) for yy, zz, m in ring(r + b, n, 0, rz + b)]
    return loft('wrap', xs, sec, [mat], smooth_shade=True)

# ================================================================ ДОЛГАЯ ПОЛОСА: 1.3 м, клинок на полторы руки
def dolgaya():
    BL = 1.0          # от гарды до острия
    RIC = 0.13        # незаточенная пята
    def w(x):         # полуширина клинка
        return lerp(0.026, 0.018, x / BL)
    def sec(x):
        h = w(x)
        # остриё срезано наискось: верхняя кромка сходит вниз за последние 9 см
        t = (x - (BL - 0.09)) / 0.09
        zt = h - (2 * h * 0.92) * max(0.0, t) ** 1.1
        zb = -h + 0.004 * max(0.0, t) ** 2
        zr = (zt + zb) / 2
        T = 0.0034 * (1 - 0.35 * x / BL) * (0.25 + 0.75 * (zt - zb) / (2 * h))
        band = 0.0 if x < RIC else 0.22
        return diamond(zt, zr, zb, max(T, 0.0005), band=band)
    xs = [0.0, RIC - 0.001, RIC] + [RIC + (BL - 0.09 - RIC) * i / 30 for i in range(1, 31)] + \
         [BL - 0.09 + 0.09 * (i / 14) for i in range(1, 14)] + [BL - 0.0008]
    bl = loft('dp_blade', xs, sec, [M_STEEL, M_EDGE])
    bevel(bl, 0.0006)
    # усы перехвата в конце пяты: два шипа из той же полосы
    for sgn in (1, -1):
        pts = lambda x: [(-0.003, sgn * (w(x) + lerp(0.0, 0.009, (x - RIC + 0.016) / 0.016)), 0),
                         (0.003, sgn * (w(x) + lerp(0.0, 0.009, (x - RIC + 0.016) / 0.016)), 0),
                         (0.003, sgn * (w(x) - 0.004), 0), (-0.003, sgn * (w(x) - 0.004), 0)]
        lug = loft('dp_lug', [RIC - 0.016 + 0.016 * i / 6 for i in range(7)],
                   pts, [M_STEEL])
        bevel(lug, 0.0008)
    # кожаная обмотка на пяте под перехват
    wrap(0.012, RIC - 0.028, 0.006, 0.029, M_LEATHER, step=0.012, bulge=0.001, n=16)
    # гарда: прямой брусок
    g = loft('dp_guard', [-0.018, 0.0], lambda x: [(-0.011, 0.075, 0), (0.011, 0.075, 0), (0.011, -0.075, 0), (-0.011, -0.075, 0)], [M_DARK])
    bevel(g, 0.003, 2)
    # рукоять: кожа внахлёст, навершие — две гайки
    loft('dp_core', [-0.31, -0.018], lambda x: ring(0.014, 12, 0, 0.017), [M_DARK])
    wrap(-0.285, -0.022, 0.0155, 0.0185, M_LEATHER, step=0.017, bulge=0.0016)
    hexnut(-0.312, -0.288, 0.024, M_DARK)
    hexnut(-0.338, -0.314, 0.021, M_DARK)

def mat_knurl():
    # накатка корпуса: мелкие шестигранные ячейки рельефом
    m = mat_steel('knurl', (0.09, 0.09, 0.095), 0.5, 0.45, scale=6, bump=0.0, grain=150)
    nt = m.node_tree; bs = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    vo = nt.nodes.new('ShaderNodeTexVoronoi'); vo.feature = 'DISTANCE_TO_EDGE'; vo.inputs['Scale'].default_value = 260
    nt.links.new(tc.outputs['Object'], vo.inputs['Vector'])
    bp = nt.nodes.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = 0.5
    nt.links.new(vo.outputs['Distance'], bp.inputs['Height']); nt.links.new(bp.outputs['Normal'], bs.inputs['Normal'])
    return m
M_KNURL = mat_knurl()
M_PLATE = mat_steel('plate', (0.13, 0.13, 0.135), 0.45, 0.55, scale=3, bump=0.01, grain=120)
M_FORGED = mat_steel('forged', (0.12, 0.118, 0.115), 0.5, 0.75, scale=4, bump=0.02, grain=140)

# ================================================================ ОТВАЛ: широкий тяжёлый клинок, корпус-гарда, 1.65 м
def otval():
    BL, H, T = 1.2, 0.085, 0.0095     # длина клинка, полуширина, полутолщина обуха
    TL = 0.27                          # длина скоса к острию
    def sec(x):
        t = max(0.0, (x - (BL - TL)) / TL)
        zt = H - 0.004 * t                                # обух прямой до острия
        zb = lerp(-H, zt - 0.003, t ** 1.08)               # лезвие уходит косой линией к острию у обуха
        w = zt - zb
        zf = zt - w * 0.42                                 # плоскость, ниже неё широкий спуск
        tt = T * (1 - 0.88 * t)
        zbn = zb + min(0.012, w * 0.15)                    # подводка
        ee = 0.0005
        return [(-tt, zt, 0), (tt, zt, 0), (tt, zf, 0), (0.0018, zbn, 1), (ee, zb, 0),
                (-ee, zb, 1), (-0.0018, zbn, 0), (-tt, zf, 0)]
    xs = [0.0] + [(BL - TL) * i / 30 for i in range(1, 31)] + [BL - TL + TL * i / 30 for i in range(1, 30)] + [BL - 0.001]
    bl = loft('ot_blade', xs, sec, [M_PLATE, M_EDGE]); bevel(bl, 0.0012)
    # корпус-гарда: брусок с накаткой, скруглённые углы, болт с куполом
    g = loft('ot_guard', [-0.066, -0.006], lambda x: [(-0.031, 0.122, 0), (0.031, 0.122, 0), (0.031, -0.122, 0), (-0.031, -0.122, 0)], [M_KNURL])
    bv = g.modifiers.new('bev', 'BEVEL'); bv.width = 0.012; bv.segments = 4
    # прижимная планка между корпусом и клинком, с прорезями
    p = loft('ot_plate', [-0.008, 0.0], lambda x: [(-0.034, 0.118, 0), (0.034, 0.118, 0), (0.034, -0.118, 0), (-0.034, -0.118, 0)], [M_DARK])
    bevel(p, 0.0015)
    for k in range(7):
        z = -0.09 + k * 0.03
        bpy.ops.mesh.primitive_cube_add(size=1, location=(-0.004, -0.0345, z)); c = bpy.context.active_object
        c.scale = (0.009, 0.002, 0.006); c.data.materials.append(M_HOLE)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=20, ring_count=10, radius=0.012, location=(-0.036, -0.031, 0.06))
    b = bpy.context.active_object; b.scale = (1, 0.45, 1); b.data.materials.append(M_DARK)
    bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=0.0145, depth=0.003, location=(-0.036, -0.0315, 0.06), rotation=(math.radians(90), 0, 0))
    bpy.context.active_object.data.materials.append(M_DARK)
    # рукоять: стержень с кольцами, навершие-стакан
    loft('ot_grip', [-0.45, -0.066], lambda x: ring(0.0165, 18, 0), [M_IRON], smooth_shade=True)
    for k in range(9):
        x = -0.43 + k * 0.042
        r = loft('ot_ring', [x, x + 0.006], lambda xx: ring(0.0188, 18, 0), [M_DARK], smooth_shade=True); bevel(r, 0.0012)
    pm = loft('ot_pommel', [-0.5, -0.45], lambda x: ring(0.022 if x < -0.458 else 0.019, 16, 0), [M_DARK], smooth_shade=True)
    bevel(pm, 0.003, 2)

# ================================================================ БАЛКА: длинный клинок с долом и загнутым концом, витая гарда, 1.4 м
def balka():
    BL = 1.0
    H = 0.032
    T = 0.0048
    def drop(x):
        # конец клинка загибается к лезвию: последние 22 см идут дугой вниз
        t = max(0.0, (x - (BL - 0.22)) / 0.22)
        return 0.075 * t ** 2.2
    def sec(x):
        u = x / BL
        hh = lerp(H, H * 0.9, u)
        t = max(0.0, (x - (BL - 0.07)) / 0.07)
        zt = hh - drop(x) - (2 * hh * 0.97 * t ** 1.3)          # обух у кончика скруглён вниз к острию
        zb = -hh - drop(x) - 0.004 * math.sin(math.pi * min(1, t))
        if zt - zb < 0.0015: zt = zb + 0.0015
        w = zt - zb
        tt = T * (1 - 0.45 * u) * min(1.0, w / (2 * hh) + 0.25)
        # дол: от пяты почти до конца, идёт за изгибом
        fd = 0.0 if not (0.03 < x < BL - 0.09) else 0.0022 * min(1, (x - 0.03) / 0.03, (BL - 0.09 - x) / 0.03)
        zf1 = zt - w * 0.22; zf2 = zt - w * 0.42
        zbev = zb + w * 0.3
        e = 0.0005
        f = [(tt, zt, 0), (tt, zf1, 0), (tt - fd, zf1 - 0.002, 0), (tt - fd, zf2 + 0.002, 0), (tt, zf2, 0), (tt, zbev, 1), (e, zb, 0)]
        back = [(-y, z, 0) for y, z, _ in reversed(f)]
        pts = [(-tt, zt, 0)] + f + back[1:-1]
        # материал грани спуска с тыльной стороны
        out = []
        for i, (y, z, m) in enumerate(pts):
            out.append((y, z, m))
        # спуск с лицевой стороны: грань от кромки (-e) к (-tt, zbev)
        for i, (y, z, m) in enumerate(out):
            if abs(y + e) < 1e-9 and abs(z - zb) < 1e-9:
                out[i] = (y, z, 1)
        return out
    xs = [0.0] + [BL * i / 120 for i in range(1, 120)] + [BL - 0.0008]
    bl = loft('bk_blade', xs, sec, [M_FORGED, M_EDGE]); bevel(bl, 0.0006)
    # гарда: витой квадратный пруток
    GH = 0.1
    def gsec(x):
        a = x / GH * math.pi * 7
        s = 0.0085
        return [(s * math.cos(a + k * math.pi / 2) * 1.41, s * math.sin(a + k * math.pi / 2) * 1.41, 0) for k in range(4)]
    gr = loft('bk_guard', [-GH + 2 * GH * i / 120 for i in range(121)], gsec, [M_FORGED]); bevel(gr, 0.0008)
    gr.rotation_euler = (0, math.radians(90), 0); gr.location = (-0.008, 0, 0)
    # муфта на гарде и широкий лопатообразный хвостовик над ней
    c = loft('bk_collar', [-0.016, 0.0], lambda x: [(-0.012, 0.022, 0), (0.012, 0.022, 0), (0.012, -0.022, 0), (-0.012, -0.022, 0)], [M_DARK]); bevel(c, 0.0015)
    def ric(x):
        u = (x + 0.016) / -0.1
        rz = lerp(0.03, 0.014, smooth(u)); ry = lerp(0.008, 0.012, u)
        return ring(ry, 16, 0, rz)
    loft('bk_ricasso', [-0.016 - 0.1 * i / 20 for i in range(21)][::-1], ric, [M_IRON], smooth_shade=True)
    loft('bk_core', [-0.33, -0.11], lambda x: ring(0.0125, 14, 0, 0.0145), [M_DARK])
    wrap(-0.32, -0.115, 0.0135, 0.0155, M_LEATHER, step=0.022, bulge=0.0014)
    # навершие-колесо с отверстием
    bpy.ops.mesh.primitive_cylinder_add(vertices=28, radius=0.027, depth=0.02, location=(-0.355, 0, 0), rotation=(math.radians(90), 0, 0))
    w = bpy.context.active_object; w.data.materials.append(M_DARK); bevel(w, 0.003, 2)
    bpy.ops.mesh.primitive_cylinder_add(vertices=20, radius=0.0095, depth=0.024, location=(-0.355, 0, 0), rotation=(math.radians(90), 0, 0))
    bpy.context.active_object.data.materials.append(M_HOLE)
    loft('bk_neck', [-0.335, -0.318], lambda x: ring(0.011, 12, 0, 0.014), [M_DARK])


# кулак правой руки (x, z) для выгрузки в игру
FIST = {'dolgaya': (-0.07, 0.0), 'otval': (-0.12, 0.0), 'balka': (-0.15, 0.0)}

rows = [(dolgaya, 0.32, -0.6, 0), (otval, 0.0, -0.45, 0), (balka, -0.33, -0.6, 0)]
for fn, z, dx, tilt in rows:
    before = set(o.name for o in bpy.context.scene.objects)
    fn()
    objs = new_objs(before)
    export_weapon(fn.__name__, objs, FIST[fn.__name__])
    if tilt:
        # двутавр чуть повёрнут к камере верхней полкой, иначе сбоку он читается брусом
        R = mathutils.Matrix.Rotation(math.radians(tilt), 4, 'X')
        for o in objs: o.matrix_world = R @ o.matrix_world
    group_move(objs, dx, z)

stage(0.02, 0.0, 2.1, (1400, 900))
render(OUT, SAMPLES)
