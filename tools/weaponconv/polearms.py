# Древковое: БАГОР, КОСА, ТРУБА — на одном листе, в одном масштабе
# blender -b -P polearms.py -- out.png [сэмплы]
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *
import mathutils

OUT, SAMPLES = args()
reset()
M_STEEL = mat_steel('steel', (0.075, 0.075, 0.08), 0.42, 0.5, scale=5, bump=0.006, grain=120)
M_EDGE = mat_steel('edge', (0.22, 0.22, 0.225), 0.3, 0.3, scale=5, bump=0.005, grain=120)
M_IRON = mat_steel('iron', (0.06, 0.058, 0.055), 0.6, 0.55, scale=8, bump=0.04, grain=180)
M_RUST = mat_steel('rustish', (0.075, 0.05, 0.035), 0.75, 0.5, scale=10, bump=0.06, grain=220)
M_DARK = mat_steel('dark', (0.035, 0.035, 0.037), 0.5, 0.5, scale=8, bump=0.02, grain=200)
M_PIPE = mat_steel('pipe', (0.05, 0.048, 0.045), 0.55, 0.6, scale=4, bump=0.03, grain=160)
M_WOOD = mat_plain('wood', (0.016, 0.01, 0.006), 0.7)
M_RAG = mat_plain('rag', (0.028, 0.025, 0.02), 0.95)
M_WIRE = mat_plain('wire', (0.08, 0.075, 0.07), 0.4, 1.0)
M_HOLE = mat_plain('hole', (0.004, 0.004, 0.004), 0.9)
for m in (M_WOOD, M_RAG, M_HOLE):
    m.node_tree.nodes['Principled BSDF'].inputs['Specular IOR Level'].default_value = 0.15

def saw(u):
    f = u - math.floor(u)
    return f if f < 0.75 else (1 - f) * 3

def wrap(x0, x1, r, rz, mat, step=0.016, bulge=0.0015, n=20):
    xs = [x0 + (x1 - x0) * i / 120 for i in range(121)]
    return loft('wrap', xs, lambda x: ring(r + bulge * saw((x - x0) / step), n, 0, rz + bulge * saw((x - x0) / step)),
                [mat], smooth_shade=True)

def band(x, r, w, mat):
    o = loft('band', [x, x + w], lambda xx: ring(r, 20, 0), [mat], smooth_shade=True); bevel(o, 0.001)

# ================================================================ БАГОР 2.0: пика и крюк из одного прута, деревянное древко
def bagor():
    L = 2.0
    S0, S1 = 1.66, 1.8          # втулка
    loft('ba_haft', [0.0, S0 + 0.02], lambda x: ring(0.018, 14, 0), [M_WOOD], smooth_shade=True)
    band(0.04, 0.0195, 0.035, M_IRON); band(0.95, 0.0195, 0.03, M_IRON)
    loft('ba_sock', [S0 + i * (S1 - S0) / 8 for i in range(9)],
         lambda x: ring(lerp(0.0215, 0.013, ((x - S0) / (S1 - S0)) ** 1.5), 8, 0, phase=math.pi / 8), [M_IRON])
    bevel(bpy.data.objects['ba_sock'], 0.001)
    for x in (S0 + 0.03, S0 + 0.08):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=6, radius=0.0055, location=(x, -0.02, 0))
        o = bpy.context.active_object; o.scale = (1, 0.45, 1); o.data.materials.append(M_DARK)
    # пика: четырёхгранная
    def pike(x):
        u = max(0.0, (x - S1) / (L - S1))
        h = 0.012 * (1 - u ** 1.3) + 0.0004
        return [(0, h, 0), (h, 0, 0), (0, -h, 0), (-h, 0, 0)]
    pk = loft('ba_pike', [S1 - 0.005] + [S1 + (L - S1) * (i / 30) for i in range(1, 31)], pike, [M_STEEL]); bevel(pk, 0.0006)
    # крюк: отходит от втулки вверх и загибается назад, к древку
    pts = []
    for i in range(41):
        t = i / 40
        a = math.pi * 1.15 * t
        pts.append((S1 - 0.01 + 0.055 * math.sin(a) - 0.03 * t, 0, 0.012 + 0.055 * (1 - math.cos(a)) * 0.95))
    tube('ba_hook', pts, 0.0085, M_IRON, res=2)
    tube('ba_hook_tip', [pts[-3], (pts[-1][0] - 0.02, 0, pts[-1][2] - 0.012)], 0.005, M_IRON, res=2)
    # кольцо на торце
    rp = [(-0.028 + 0.026 * math.cos(a), 0, 0.026 * math.sin(a)) for a in [2 * math.pi * i / 40 for i in range(41)]]
    tube('ba_ring', rp, 0.004, M_DARK, res=2)
    loft('ba_eye', [-0.004, 0.004], lambda x: ring(0.006, 10, 0), [M_DARK])

# ================================================================ КОСА 2.84: недораспрямлённое полотно косы на трубе
def kosa():
    L = 2.84
    B0 = 2.12                   # пятка полотна
    loft('ko_shaft', [0.0, B0 + 0.06], lambda x: ring(0.0145, 16, 0), [M_PIPE], smooth_shade=True)
    wrap(0.25, 0.55, 0.0158, 0.0158, M_RAG, step=0.03, bulge=0.0022)
    wrap(0.85, 1.1, 0.0158, 0.0158, M_RAG, step=0.03, bulge=0.0022)
    loft('ko_cap', [-0.012, 0.0], lambda x: ring(0.017, 16, 0), [M_DARK]); bevel(bpy.data.objects['ko_cap'], 0.002)
    # муфта: пятка косы загнута и примотана проволокой
    wrap(B0 - 0.02, B0 + 0.09, 0.0175, 0.0185, M_WIRE, step=0.005, bulge=0.0009)
    def mid(x):
        u = (x - B0) / (L - B0)
        return 0.02 + 0.055 * u ** 2.2              # полотно выгнуто вперёд-вверх
    def sec(x):
        u = (x - B0) / (L - B0)
        h = lerp(0.062, 0.03, u)
        t = (x - (L - 0.16)) / 0.16
        top = mid(x) + h / 2
        bot = mid(x) - h / 2 + (h * 0.95 * t ** 1.6 if t > 0 else 0)
        if top - bot < 0.0015: bot = top - 0.0015
        return edge_sec(top, bot, 0.0018, min(0.016, (top - bot) * 0.6))
    xs = [B0 + 0.03 + (L - B0 - 0.03) * i / 90 for i in range(91)]
    xs[-1] = L - 0.0008
    bl = loft('ko_blade', xs, sec, [M_STEEL, M_EDGE]); bevel(bl, 0.0004)
    # ребро жёсткости косы по обуху
    tube('ko_rib', [(x, -0.0022, mid(x) + lerp(0.062, 0.03, (x - B0) / (L - B0)) / 2 - 0.004) for x in xs[:-6:3]], 0.0028, M_STEEL, res=2)
    # короткий шип на обухе
    def spur(x):
        u = (x - B0 - 0.1) / 0.05
        hgt = 0.03 * (1 - abs(u * 2 - 1)) if 0 <= u <= 1 else 0.0
        z0 = mid(x) + 0.028
        return [(-0.003, z0 + hgt * (1 - u) + 0.0005, 0), (0.003, z0 + hgt * (1 - u) + 0.0005, 0), (0.003, z0 - 0.004, 0), (-0.003, z0 - 0.004, 0)]
    sp = loft('ko_spur', [B0 + 0.1 + 0.05 * i / 10 for i in range(11)], spur, [M_STEEL]); bevel(sp, 0.0006)

# ================================================================ ТРУБА 2.2: водопроводная труба одним куском (пользователь: короче, без вентиля), конец расплющен в жало
def truba():
    L = 2.2
    R = 0.0167
    F0 = L - 0.36               # отсюда труба расплющена
    loft('tr_pipe', [0.03, F0], lambda x: ring(R, 18, 0), [M_PIPE], smooth_shade=True)
    # расплющенный конец и жало; шов трубы раскрылся трещиной
    def flat(x):
        u = min(1.0, (x - F0) / 0.12)
        ry = lerp(R, 0.0035, smooth(u)); rz = lerp(R, 0.026, smooth(u))
        t = (x - (L - 0.2)) / 0.2
        if t > 0:
            rz *= (1 - t ** 1.2); ry *= (1 - 0.7 * t)
        return ring(ry, 18, 0, max(rz, 0.0006))
    fx = [F0 + (L - F0) * (i / 60) for i in range(61)]; fx[-1] = L - 0.0008
    loft('tr_flat', fx, flat, [M_PIPE], smooth_shade=True)
    tube('tr_crack', [(x, -0.0042, 0.006 + 0.0015 * math.sin(x * 90)) for x in [F0 + 0.08 + 0.17 * i / 20 for i in range(21)]], 0.0011, M_HOLE, res=1)
    # резьбовая заглушка на торце
    loft('tr_cap', [0.0 + 0.035 * i / 20 for i in range(21)], lambda x: ring(0.0205 + (0.001 if int(x / 0.0035) % 2 else 0), 8, 0, phase=math.pi / 8), [M_IRON])
    wrap(0.3, 0.56, 0.0178, 0.0178, M_RAG, step=0.03, bulge=0.0022)
    wrap(0.85, 1.1, 0.0178, 0.0178, M_RAG, step=0.03, bulge=0.0022)

# кулак правой руки (x, z) для выгрузки в игру
FIST = {'bagor': (0.25, 0.0), 'kosa': (0.3, 0.0), 'truba': (0.3, 0.0)}

rows = [(bagor, 0.32, -1.8), (kosa, 0.0, -1.85), (truba, -0.32, -1.85)]
for fn, z, dx in rows:
    before = set(o.name for o in bpy.context.scene.objects)
    fn()
    export_weapon(fn.__name__, new_objs(before), FIST[fn.__name__])
    group_move(new_objs(before), dx, z)

stage(0.0, 0.0, 3.8, (1900, 800))
render(OUT, SAMPLES)
