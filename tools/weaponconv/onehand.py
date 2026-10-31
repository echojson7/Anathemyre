# Одноручное: ЗАТОЧКА, РЕЗАК, РЕССОРА, ПОЛОСА, СЕКАЧ, ТЁСЛО (АРМАТУРА убрана пользователем 2026-10-30) — на одном листе, в одном масштабе
# Каждая вещь строится от торца рукояти (x = 0) к острию (+X), лезвие вниз (-Z), к камере -Y
# blender -b -P onehand.py -- out.png [сэмплы]
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *
import mathutils

OUT, SAMPLES = args()
reset()
rnd = random.Random(5)
M_STEEL = mat_steel('steel', (0.075, 0.075, 0.08), 0.42, 0.5, scale=5, bump=0.006, grain=120)
M_EDGE = mat_steel('edge', (0.22, 0.22, 0.225), 0.3, 0.3, scale=5, bump=0.005, grain=120)
M_IRON = mat_steel('iron', (0.06, 0.058, 0.055), 0.6, 0.55, scale=8, bump=0.04, grain=180)
M_RUST = mat_steel('rustish', (0.075, 0.05, 0.035), 0.75, 0.5, scale=10, bump=0.06, grain=220)
M_DARK = mat_steel('dark', (0.035, 0.035, 0.037), 0.5, 0.5, scale=8, bump=0.02, grain=200)
M_LEATHER = mat_plain('leather', (0.011, 0.007, 0.0045), 0.6)
M_RAG = mat_plain('rag', (0.028, 0.025, 0.02), 0.95)
M_WIRE = mat_plain('wire', (0.08, 0.075, 0.07), 0.4, 1.0)
M_HOLE = mat_plain('hole', (0.004, 0.004, 0.004), 0.9)
M_TAPE = mat_plain('tape', (0.006, 0.007, 0.01), 0.3)
M_TEXT = mat_plain('textolite', (0.035, 0.03, 0.016), 0.45)
M_WOOD = mat_plain('wood', (0.016, 0.01, 0.006), 0.7)
M_CORD = mat_plain('cord', (0.012, 0.011, 0.01), 0.7)
M_CONC = mat_steel('concrete', (0.16, 0.155, 0.145), 0.95, 0.55, scale=30, bump=0.4, grain=90)
M_CONC.node_tree.nodes['Principled BSDF'].inputs['Metallic'].default_value = 0.0
for m in (M_LEATHER, M_RAG, M_HOLE, M_TEXT, M_WOOD, M_CORD):
    m.node_tree.nodes['Principled BSDF'].inputs['Specular IOR Level'].default_value = 0.15

def saw(u):
    f = u - math.floor(u)
    return f if f < 0.75 else (1 - f) * 3

def mat_file():
    # насечка напильника: косые полосы рельефом
    m = mat_steel('file', (0.06, 0.06, 0.065), 0.5, 0.5, scale=5, bump=0.0, grain=120)
    nt = m.node_tree; bs = nt.nodes['Principled BSDF']
    tc = nt.nodes.new('ShaderNodeTexCoord')
    mp = nt.nodes.new('ShaderNodeMapping'); mp.inputs['Rotation'].default_value = (0, math.radians(35), 0)
    wv = nt.nodes.new('ShaderNodeTexWave'); wv.inputs['Scale'].default_value = 260
    wv.wave_profile = 'SAW'; wv.bands_direction = 'X'
    nt.links.new(tc.outputs['Object'], mp.inputs['Vector']); nt.links.new(mp.outputs['Vector'], wv.inputs['Vector'])
    bp = nt.nodes.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = 0.6
    nt.links.new(wv.outputs['Fac'], bp.inputs['Height']); nt.links.new(bp.outputs['Normal'], bs.inputs['Normal'])
    return m
M_FILE = mat_file()

def wrap(x0, x1, r, rz, mat, step=0.016, bulge=0.0015, z=0.0, y=0.0, n=20):
    xs = [x0 + (x1 - x0) * i / 120 for i in range(121)]
    def sec(x):
        b = bulge * saw((x - x0) / step) + 0.0004 * math.sin(x * 397.0)
        return [(yy + y, zz + z, m) for yy, zz, m in ring(r + b, n, 0, rz + b)]
    return loft('wrap', xs, sec, [mat], smooth_shade=True)

def hole(x, z, y, r, depth=0.004):
    bpy.ops.mesh.primitive_cylinder_add(vertices=14, radius=r, depth=depth, location=(x, y, z), rotation=(math.radians(90), 0, 0))
    bpy.context.active_object.data.materials.append(M_HOLE)

def rivet(x, z, y, r, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=14, ring_count=7, radius=r, location=(x, y, z))
    o = bpy.context.active_object; o.scale = (1, 0.4, 1); o.data.materials.append(mat)

def hexnut_y(x, z, y, r, mat):
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=r, depth=r * 0.8, location=(x, y, z), rotation=(math.radians(90), 0, 0))
    o = bpy.context.active_object; o.data.materials.append(mat); bevel(o, r * 0.08, 1)

# ================================================================ ЗАТОЧКА 0.33: напильник, сточенный в жало
def zatochka():
    X0, L = 0.105, 0.33
    def sec(x):
        u = (x - X0) / (L - X0)
        h = 0.0095 * (1 - u ** 1.6) + 0.0004
        T = 0.0042 * (1 - u ** 1.4) + 0.0003
        return [(0, h, 0), (T, 0, 0), (0, -h, 0), (-T, 0, 0)]
    xs = [X0 + (L - X0) * (i / 40) for i in range(41)]
    bl = loft('za_spike', xs, sec, [M_FILE]); bevel(bl, 0.0004)
    # хвостовик, загнутый в кольцо под палец
    loft('za_tang', [0.0, X0], lambda x: [(-0.0025, 0.006, 0), (0.0025, 0.006, 0), (0.0025, -0.006, 0), (-0.0025, -0.006, 0)], [M_DARK])
    ring_pts = [(-0.019 + 0.018 * math.cos(a), 0, 0.018 * math.sin(a)) for a in [2 * math.pi * i / 40 for i in range(41)]]
    tube('za_ring', ring_pts, 0.0034, M_DARK)
    wrap(0.006, X0 - 0.004, 0.0105, 0.0135, M_TAPE, step=0.019, bulge=0.0007, n=16)

# ================================================================ РЕЗАК 0.41: нож из полотна машинной пилы
def rezak():
    X0, L = 0.12, 0.41
    H = 0.032
    def zt(x):
        return H * 0.5 + (0.0016 * saw((x - X0) / 0.0045) if x < L - 0.06 else 0.0)
    def zb(x):
        # танто: от прямой кромки косым срезом к острию у обуха
        t = (x - (L - 0.05)) / 0.05
        return -H * 0.5 + (H * 0.9 * t ** 1.05 if t > 0 else 0.0)
    def sec(x):
        top = zt(x) if x < L - 0.05 else lerp(H * 0.5, zb(x) + 0.0015, ((x - (L - 0.05)) / 0.05) ** 4)
        return edge_sec(top, zb(x), 0.0011, 0.009 if x > X0 + 0.012 else 0.0)
    xs = [X0 + (L - X0) * i / 260 for i in range(261)]
    xs[-1] = L - 0.0008
    bl = loft('re_blade', xs, sec, [M_STEEL, M_EDGE]); bevel(bl, 0.0003)
    hole(X0 + 0.03, 0.004, -0.0012, 0.0035)
    # рукоять: хвостовик в накладках из текстолита, две заклёпки, третью заменил болт
    loft('re_scales', [0.0, X0], lambda x: [(-0.0052, 0.015, 0), (0.0052, 0.015, 0), (0.0052, -0.017 + 0.003 * math.sin(x * 40), 0), (-0.0052, -0.017 + 0.003 * math.sin(x * 40), 0)], [M_TEXT])
    bevel(bpy.data.objects['re_scales'], 0.0025, 3)
    rivet(0.018, 0.0, -0.0053, 0.0042, M_WIRE)
    rivet(0.06, 0.0, -0.0053, 0.0042, M_WIRE)
    hexnut_y(0.1, 0.0, -0.0068, 0.0055, M_DARK)

# ================================================================ РЕССОРА 0.9: лист рессоры
def ressora():
    X0, L = 0.16, 0.9
    def mid(x):
        return 0.075 * ((x - X0) / (L - X0)) ** 2           # лист выгнут вверх: выпуклое лезвие снизу
    def sec(x):
        u = (x - X0) / (L - X0)
        hh = lerp(0.0225, 0.019, u)
        t = (x - (L - 0.09)) / 0.09
        top = mid(x) + hh - (2 * hh * 0.9 * t ** 1.4 if t > 0 else 0)
        bot = mid(x) - hh + (0.004 * t * t if t > 0 else 0)
        T = 0.0034 * (1 - 0.3 * u) * (0.3 + 0.7 * min(1, (top - bot) / (2 * hh)))
        return edge_sec(top, bot, T, 0.015 if x > X0 + 0.02 else 0.0)
    xs = [0.02] + [X0 + (L - X0) * i / 80 for i in range(81)]
    xs[-1] = L - 0.001
    bl = loft('rs_blade', xs, lambda x: sec(max(x, X0)) if x >= X0 else edge_sec(0.0225, -0.0225, 0.0034, 0.0), [M_STEEL, M_EDGE])
    bevel(bl, 0.0006)
    # отверстие центрового болта посередине листа
    xm = X0 + 0.42
    hole(xm, mid(xm) + 0.004, -0.0036, 0.0045)
    # рукоять: гарда из обрезка листа, кожа внахлёст на деревянных щёчках, навершие-шайба
    g = loft('rs_guard', [X0 - 0.014, X0], lambda x: [(-0.0065, 0.04, 0), (0.0065, 0.04, 0), (0.0065, -0.036, 0), (-0.0065, -0.036, 0)], [M_DARK])
    bevel(g, 0.0025, 2)
    loft('rs_core', [0.02, X0 - 0.014], lambda x: ring(0.011, 14, 0, 0.0155), [M_WOOD])
    wrap(0.026, X0 - 0.016, 0.0118, 0.0165 + 0.0015 * math.sin(math.pi * 0.5), M_LEATHER, step=0.016, bulge=0.0013)
    pm = loft('rs_pommel', [-0.004, 0.026], lambda x: ring(0.017 if 0.004 < x < 0.02 else 0.0145, 16, 0, 0.02 if 0.004 < x < 0.02 else 0.017), [M_DARK], smooth_shade=True)
    bevel(pm, 0.0025, 2)

# ================================================================ ПОЛОСА 0.94: прямой обоюдоострый, к острию листом
def polosa():
    X0, L = 0.17, 0.94
    def hw(x):
        u = (x - X0) / (L - X0)
        w = lerp(0.019, 0.017, u) + 0.006 * math.sin(math.pi * min(u / 0.85, 1)) ** 2
        t = (u - 0.8) / 0.2
        return w * (1 - t ** 1.5) + 0.0004 if t > 0 else w
    def sec(x):
        h = hw(x); T = 0.0028 * (0.4 + 0.6 * h / 0.025)
        return diamond(h, 0.0, -h, T, band=0.2)
    xs = [X0 + (L - X0) * (i / 70) for i in range(71)]
    xs[-1] = L - 0.0008
    bl = loft('po_blade', xs, sec, [M_STEEL, M_EDGE]); bevel(bl, 0.0005)
    hole(X0 + 0.03, 0.0085, -0.0024, 0.0032); hole(X0 + 0.03, -0.0085, -0.0024, 0.0032)
    # гарда из обрезка уголка: полка к камере
    loft('po_guard', [X0 - 0.014, X0], lambda x: [(-0.004, 0.045, 0), (0.004, 0.045, 0), (0.004, -0.045, 0), (-0.004, -0.045, 0)], [M_DARK])
    loft('po_guard2', [X0 - 0.014, X0 - 0.010], lambda x: [(-0.018, 0.045, 0), (-0.004, 0.045, 0), (-0.004, -0.045, 0), (-0.018, -0.045, 0)], [M_DARK])
    for n in ('po_guard', 'po_guard2'): bevel(bpy.data.objects[n], 0.001)
    loft('po_core', [0.02, X0 - 0.014], lambda x: ring(0.0115, 12, 0, 0.0135), [M_DARK])
    wrap(0.03, X0 - 0.016, 0.0125, 0.0148, M_LEATHER, step=0.015, bulge=0.0012)
    loft('po_nut', [0.0, 0.028], lambda x: ring(0.019, 6, 0), [M_DARK]); bevel(bpy.data.objects['po_nut'], 0.0015, 2)

# ================================================================ СЕКАЧ 1.0: тесак из толстого листа, носок-клюв
def sekach():
    X0, L = 0.27, 1.0
    def top(x):
        u = (x - X0) / (L - X0)
        n = 0.0022 * saw((x - X0) / 0.011) if 0.04 < (x - X0) < 0.2 else 0.0     # зазубрины на обухе у пяты
        return 0.022 - n
    def bot(x):
        u = (x - X0) / (L - X0)
        z = -0.042 - 0.045 * u ** 1.3
        t = (x - (L - 0.045)) / 0.045
        if t > 0: z -= 0.02 * t ** 2           # нижний угол вытянут вперёд клювом
        return z
    def sec(x):
        x = min(x, L)
        T = 0.0033
        # торец срезан прямо: последний срез сходит к кромке только у самого края
        return edge_sec(top(x), bot(x), T, 0.022 if x > X0 + 0.015 else 0.0)
    xs = [X0 - 0.12, X0 - 0.0005] + [X0 + (L - X0) * i / 180 for i in range(181)]
    bl = loft('se_blade', xs, lambda x: sec(max(x, X0)) if x >= X0 else edge_sec(0.012, -0.012, 0.003, 0.0), [M_IRON, M_EDGE])
    bevel(bl, 0.0008)
    hole(L - 0.05, 0.004, -0.0034, 0.0075)
    # рукоять: деревянные накладки на хвостовике, три заклёпки, стальной упор у клинка, к торцу расширяется крюком
    def grip(x):
        u = (x - 0.012) / (X0 - 0.03 - 0.012)
        rz = 0.0175 + 0.0025 * math.sin(math.pi * u) + 0.007 * smooth((0.12 - u) / 0.12)   # брюшко и крюк на торце
        zc = -0.003 - 0.005 * smooth((0.12 - u) / 0.12)
        return [(yy, zz + zc, m) for yy, zz, m in ring(0.0118, 18, 0, rz)]
    gx = [0.012 + (X0 - 0.03 - 0.012) * i / 60 for i in range(61)]
    loft('se_grip', gx, grip, [M_WOOD], smooth_shade=True)
    bo = loft('se_bolster', [X0 - 0.032, X0], lambda x: [(-0.0085, 0.024, 0), (0.0085, 0.024, 0), (0.0085, -0.034, 0), (-0.0085, -0.034, 0)], [M_DARK])
    bevel(bo, 0.003, 2)
    cap = loft('se_cap', [0.0, 0.013], lambda x: [(yy, zz - 0.008, m) for yy, zz, m in ring(0.0105, 14, 0, 0.0245)], [M_DARK], smooth_shade=True)
    bevel(cap, 0.002, 2)
    for x in (0.06, 0.13, 0.2):
        rivet(x, -0.002, -0.0117, 0.0045, M_WIRE)
    hole(0.03, -0.012, -0.0118, 0.0035, depth=0.003)

# ================================================================ ТЁСЛО 0.86: топор с длинной бородой
def teslo():
    L = 0.86
    loft('te_haft', [0.0] + [L * i / 20 for i in range(1, 21)], lambda x: ring(0.0125 + 0.0015 * smooth(x / 0.1), 12, 0, 0.0165 + 0.002 * smooth(x / 0.1)), [M_WOOD], smooth_shade=True)
    wrap(0.012, 0.11, 0.0148, 0.0195, M_CORD, step=0.006, bulge=0.0009)
    # втулка из трубы
    S0, S1 = 0.735, 0.835
    loft('te_sleeve', [S0, S1], lambda x: ring(0.022, 16, 0, 0.024), [M_IRON], smooth_shade=True)
    # щека-пластина: полумесяц лезвия, борода назад к рукояти
    def zt(x):
        if x < S0 + 0.01: return lerp(-0.13, -0.022, smooth((x - 0.6) / (S0 + 0.01 - 0.6)) ** 0.7)
        if x > S1 - 0.01: return lerp(-0.022, -0.06, ((x - (S1 - 0.01)) / 0.075) ** 1.5)
        return -0.022
    def zb(x):
        u = (x - 0.6) / 0.31
        return -0.135 - 0.015 * math.sin(math.pi * u) + 0.06 * max(0, u - 0.82) / 0.18
    def sec(x):
        a, b = zt(x), zb(x)
        if a - b < 0.004: a = b + 0.004
        return edge_sec(a, b, 0.0045, min(0.022, (a - b) * 0.5))
    xs = [0.6 + 0.305 * i / 70 for i in range(71)]
    hd = loft('te_head', xs, sec, [M_IRON, M_EDGE]); bevel(hd, 0.0008)
    # сквозной болт и старая приварка
    hexnut_y(0.785, 0.0, -0.026, 0.0065, M_DARK)
    tube('te_weld', [(S0 + 0.004, -0.0048, -0.023), (S1 - 0.004, -0.0048, -0.023)], 0.0025, M_RUST)

# ---------------------------------------------------------------- лист: две колонки
# кулак правой руки (x, z) для выгрузки в игру
FIST = {'zatochka': (0.055, 0.0), 'rezak': (0.06, 0.0), 'ressora': (0.09, 0.0), 'polosa': (0.1, 0.0), 'sekach': (0.17, -0.005), 'teslo': (0.07, 0.0)}

cells = [(zatochka, -1.0, 0.42), (ressora, 0.12, 0.42),
         (rezak, -1.0, 0.14), (polosa, 0.12, 0.14),
         (teslo, -1.0, -0.2), (sekach, 0.12, -0.14)]
for fn, dx, z in cells:
    before = set(o.name for o in bpy.context.scene.objects)
    fn()
    export_weapon(fn.__name__, new_objs(before), FIST[fn.__name__])
    group_move(new_objs(before), dx, z)

stage(0.05, 0.0, 2.3, (1600, 1000))
render(OUT, SAMPLES)
