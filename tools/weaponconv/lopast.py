# ЛОПАСТЬ: меч из лопатки турбины мёртвой машины. Перо — крыловой профиль, закручен по длине;
# полка лопатки стала гардой, хвостовик-ёлочка — рукоятью; у передней кромки отверстия охлаждения, у полки тусклые знаки
# blender -b -P lopast.py -- out.png [сэмплы]
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *

OUT, SAMPLES = args()
reset()
M_BLADE = mat_steel('blade', (0.16, 0.155, 0.15), 0.38, 0.5, scale=6, bump=0.012, grain=150)
M_EDGE = mat_steel('edge', (0.26, 0.26, 0.26), 0.28, 0.25, scale=6, bump=0.008, grain=150)
M_DARK = mat_steel('dark', (0.05, 0.05, 0.052), 0.5, 0.5, scale=8, bump=0.02, grain=200)
M_CORD = mat_plain('cord', (0.012, 0.011, 0.01), 0.7)
M_HOLE = mat_plain('hole', (0.004, 0.004, 0.004), 0.9)
M_GLYPH = mat_plain('glyph', (0.12, 0.2, 0.14), 0.6)
bs = M_GLYPH.node_tree.nodes['Principled BSDF']
bs.inputs['Emission Color'].default_value = (0.45, 0.85, 0.55, 1); bs.inputs['Emission Strength'].default_value = 0.12

L = 0.74          # перо от полки до кончика
TW = math.radians(20)

def chord(x):
    return lerp(0.088, 0.068, x / L)

def top_z(x):
    # передняя кромка (сверху) почти прямая, у кончика косой срез вниз: остриё уходит к задней кромке
    c = chord(x); z = c * 0.5
    t = (x - (L - 0.11)) / 0.11
    if t > 0:
        z -= c * 0.86 * t ** 1.25
    return z

def bot_z(x):
    # задняя кромка — режущая, у кончика чуть поднимается к острию
    c = chord(x); z = -c * 0.5
    t = (x - (L - 0.05)) / 0.05
    if t > 0:
        z += c * 0.06 * t * t
    return z

def thick(x):
    return lerp(0.2, 0.13, x / L)

def surf(x, s, side):
    # точка профиля: s 0 — передняя кромка (обух), 1 — задняя (лезвие); side +1 — к камере (-Y), -1 — от неё
    zt, zb = top_z(x), bot_z(x)
    c = zt - zb
    # толщина как у крылового профиля, от полной хорды пера, чтобы срез у кончика не раздувал толщину
    cf = chord(x)
    sa = (0.5 * chord(x) - zt) / cf + s * c / cf
    th = thick(x) * cf
    h = 5 * th * (0.2969 * math.sqrt(max(sa, 0)) - 0.126 * sa - 0.3516 * sa ** 2 + 0.2843 * sa ** 3 - 0.1036 * sa ** 4)
    h = max(h, 0.0004)
    camber = 0.07 * cf * math.sin(math.pi * min(sa, 1))
    y = -camber - side * h
    z = zt - s * c
    # закрутка вокруг середины хорды
    a = TW * x / L
    zc = 0.0
    yr = y * math.cos(a) - (z - zc) * math.sin(a)
    zr = y * math.sin(a) + (z - zc) * math.cos(a) + zc
    # перо слегка изогнуто по длине вниз, к лезвию, как лопатка в колесе
    return yr, zr + 0.026 * (x / L) ** 2 - 0.0

NS = 24
def blade_sec(x):
    ss = [(i / NS) ** 1.6 for i in range(NS + 1)]
    front = [(*surf(x, s, 1), 1 if s > 0.84 else 0) for s in ss]
    back = [(*surf(x, s, -1), 1 if s > 0.84 else 0) for s in reversed(ss[1:-1])]
    return front + back

xs = [0.0] + [L * (i / 60) for i in range(1, 60)] + [L - 0.0015]
bl = loft('blade', xs, blade_sec, [M_BLADE, M_EDGE], smooth_shade=True)

# ---------------------------------------------------------------- полка лопатки — гарда: косой параллелограмм
def plat_sec(x):
    k = x / 0.02
    hy, z0, z1 = 0.026, -0.072 - 0.006 * k, 0.07 - 0.006 * k
    return [(-hy, z1, 0), (hy, z1, 0), (hy, z0, 0), (-hy, z0, 0)]
pl = loft('platform', [-0.02 + 0.02 * i / 4 for i in range(5)], lambda x: plat_sec(x + 0.02), [M_DARK])
bevel(pl, 0.002, 2)

# ---------------------------------------------------------------- хвостовик-ёлочка: рукоять с зубцами сверху и снизу
GL = 0.26
def saw(u):
    f = u - math.floor(u)
    return f if f < 0.75 else (1 - f) * 3
def root_sec(x):
    d = -0.02 - x            # от полки вниз по рукояти
    teeth = 0.0042 * saw(d / 0.022)
    # к торцу ёлочка расширяется замком: три лопасти, каждая шире прежней
    e = d - (GL - 0.06)
    lobe = 0.0 if e < 0 else 0.0035 * (1 + e / 0.02) * saw(e / 0.02)
    hz = 0.0165 + (teeth if e < 0 else 0) + lobe
    hy = 0.0135
    return [(-hy, hz, 0), (hy, hz, 0), (hy, -hz, 0), (-hy, -hz, 0)]
rt = loft('root', [-0.02 - GL * i / 160 for i in range(161)][::-1], root_sec, [M_DARK])
bevel(rt, 0.0012, 2)

# шнур кольцами поверх ёлочки, между пачками видны зубцы
for c0 in (0.045, 0.105, 0.165):
    for k in range(9):
        x = -0.02 - c0 - k * 0.0052
        bpy.ops.mesh.primitive_torus_add(major_radius=0.0215, minor_radius=0.0029, location=(x, 0, 0),
                                         rotation=(0, math.radians(90), 0), major_segments=24, minor_segments=8)
        t = bpy.context.active_object; t.scale = (1, 0.72, 1.0)
        t.data.materials.append(M_CORD)
        for p in t.data.polygons: p.use_smooth = True

# ---------------------------------------------------------------- отверстия охлаждения у передней кромки
for i in range(11):
    x = 0.05 + i * 0.052
    if x > L - 0.13: break
    y, z = surf(x, 0.1, 1)
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.0026, depth=0.003, location=(x, y + 0.0009, z),
                                        rotation=(math.radians(90), 0, 0))
    bpy.context.active_object.data.materials.append(M_HOLE)

# ---------------------------------------------------------------- знаки: пиксельные глифы 5×5, стёрты местами
rnd = random.Random(7)
PX = 0.0028
for g in range(4):
    gx = 0.06 + g * 0.03
    cells = [(i, j) for i in range(5) for j in range(5)]
    on = set(c for c in cells if rnd.random() < 0.45)
    on |= {(2, j) for j in range(5) if rnd.random() < 0.7}       # у каждого знака своя ось
    for i, j in on:
        if rnd.random() < 0.35: continue                           # стёрто
        x = gx + i * PX
        y, z = surf(x, 0.42 + j * 0.055, 1)
        bpy.ops.mesh.primitive_plane_add(size=PX * 0.86, location=(x, y - 0.0003, z), rotation=(math.radians(90), 0, 0))
        bpy.context.active_object.data.materials.append(M_GLYPH)

export_weapon('lopast', list(bpy.context.scene.objects), (-0.075, 0.0))
if os.environ.get('VIEW') == '34':
    # вид три четверти: видно крыловой профиль и закрутку
    import mathutils
    R = mathutils.Matrix.Rotation(math.radians(-28), 4, 'Z') @ mathutils.Matrix.Rotation(math.radians(-40), 4, 'X')
    for o in list(bpy.context.scene.objects):
        o.matrix_world = R @ o.matrix_world
stage(0.22, 0.0, 1.16)
render(OUT, SAMPLES)
