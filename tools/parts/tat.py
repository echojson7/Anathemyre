"""«Тать» — скрытный шагающий снайпер (docs/tat.md): рецепт машины по ролику-концепту.

Принцип концепта: четыре одинаковые длинные ноги растут из-под углов широкой плоской крыши, а плотное тело
висит под крышей между ними. Сверху вниз: крыша в тёмной краске (люк, рёбра радиатора, купола датчиков, окна
излучателей морока, антенны); под ней рама из чёрного железа с кулаками ног; ниже — киль с баками-капсулами по
бокам, спереди голова с янтарным визором (кость eye), справа на станке длинная винтовка (кости turret, gun),
под килем короб с лентой, лента заходит в винтовку слева; спереди слева — дырчатый фартук, сзади — ранец
радиатора; по швам — красные кабели. Брюхо — полозья под коробом: на них машина ложится.

Оси сетки — как у Вежи: x вправо, y вверх, z вперёд, начало — на земле под серединой. Бинд-поза — «стоит».
"""
import math

from kit import Body, add, mat_vec, mul, rot_x, rot_y, rot_z, unit
from rig import Rig
from tat_leg import BRASS, CABLE, GLASS, HIP_Y, IRON, LEGS, LENS, MARK, PAINT, RUBBER, STEEL, WORN, leg_parts
from vezha import frame
from vezha_leg import bolt, sphere

# --- размеры, м (высоты — от осей бёдер) ---
BELLY = 0.37                 # оси бёдер над землёй, когда машина лежит брюхом на полозьях
# Крыша: сечения по z — полуширина; углы срезаны там, где из-под крыши выходят ноги
ROOF = ((-0.40, 0.13), (-0.31, 0.25), (0.19, 0.25), (0.31, 0.13))
ROOF_Y = (0.065, 0.092, 0.125)   # низ, кромка, верх
TOP = ROOF_Y[2]
TURRET = (0.19, HIP_Y - 0.075, 0.07)    # ось станка: под рамой справа
GUN = (0.19, HIP_Y - 0.175, 0.08)       # цапфы винтовки
HEAD = (-0.04, HIP_Y - 0.03, 0.2)        # ось поворота головы: под передней кромкой крыши
BARREL = 1.12                # дульный тормоз — на столько впереди цапф
FEED = (0.157, HIP_Y - 0.162, 0.11)      # окно подачи слева на коробе затвора (лента входит сюда)
AMMO = (0.04, HIP_Y - 0.3, 0.07)         # короб с лентой под килем
ANTENNAS = (("ant_l", (-0.15, -0.3), 0.08, 0.5), ("ant_r", (0.12, -0.33), 0.16, 0.2))

LODS = (40000, 22000, 12000, 6000)
WEAR = (WORN, 0.016, 0.7, 0.025)


def Y(dy):
    return HIP_Y + dy


def roof_w(z):
    """Полуширина крыши в точке z."""
    for (z0, w0), (z1, w1) in zip(ROOF, ROOF[1:]):
        if z0 <= z <= z1:
            return w0 + (w1 - w0) * (z - z0) / (z1 - z0)
    return 0.0


def _cylz(b, c, r, z0, z1, mat, seg=16, cap=0.0):
    """Капсула вдоль z: цилиндр от z0 до z1 со скруглёнными торцами длиной cap."""
    prof = [(r * 0.3, 0.0)]
    for k in range(1, 4):
        a = math.pi / 2 * k / 4
        prof.append((r * math.sin(a), cap * (1 - math.cos(a))))
    prof += [(r, cap), (r, z1 - z0 - cap)]
    for k in range(3, 0, -1):
        a = math.pi / 2 * k / 4
        prof.append((r * math.sin(a), z1 - z0 - cap * (1 - math.cos(a))))
    prof.append((r * 0.3, z1 - z0))
    b.lathe((c[0], c[1], z0), (0, 0, 1), prof, mat, seg=seg)


def _perforated(base, holes, axis, r, mat, bevel=0.003):
    """Лист с отверстиями: base минус цилиндры радиуса r вдоль axis в точках holes."""
    from solid import solid
    cuts = Body()
    for p in holes:
        cuts.lathe(add(p, mul(axis, -0.04)), axis, [(r, 0.0), (r, 0.08)], None, seg=8)
    t = solid([("add", base), ("cut", cuts)], bevel=bevel, bevel_angle=30.0)
    out = Body()
    out.faces = [(p, mat, s) for p, _, s in t.faces]
    return out


# --- крыша (кость chassis) ---

def roof():
    """Крыша одним телом: плита с фаской, срезанные углы над ногами, швы листов, щели жалюзи на корме."""
    from solid import solid
    yb, ye, yt = (Y(v) for v in ROOF_Y)
    ring = lambda z, w: [(-w * 0.96, yb, z), (w * 0.96, yb, z), (w, ye, z), (w * 0.88, yt, z), (-w * 0.88, yt, z), (-w, ye, z)]
    base = Body()
    base.loft([ring(z, w) for z, w in ROOF], None)
    adds = Body()   # нос: скат над головой
    adds.loft([[(-0.12, yb, 0.3), (0.12, yb, 0.3), (0.11, ye, 0.345), (-0.11, ye, 0.345)], [(-0.1, yb, 0.27), (0.1, yb, 0.27), (0.09, yt - 0.01, 0.27),
               (-0.09, yt - 0.01, 0.27)]], None)
    cuts = Body()
    g = 0.005
    for z in (-0.22, 0.02, 0.2):   # швы поперёк
        cuts.box((0.0, yt, z), (0.6, 0.016, g), None, bevel=0.0)
    for s in (-1.0, 1.0):
        cuts.box((s * 0.15, yt, -0.04), (g, 0.016, 0.52), None, bevel=0.0)
    for k in range(5):   # жалюзи на корме
        cuts.box((0.0, ye + 0.012, -0.36 - k * 0.012), (0.18, 0.012, 0.005), None, bevel=0.0, rot=rot_x(-0.6))
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.006, bevel_angle=25.0)
    out = Body()
    out.faces = [(p, PAINT, s) for p, _, s in t.faces]
    return out


def roof_top():
    """Навесное на крыше: люк, рёбра радиатора, купола датчиков, окна излучателей морока, рымы, болты, знаки."""
    b = Body()
    yt = Y(TOP)
    # люк слева спереди
    hc = (-0.075, yt, 0.1)
    b.box(add(hc, (0.0, 0.006, 0.0)), (0.12, 0.012, 0.15), PAINT, bevel=0.004)
    b.box(add(hc, (0.0, 0.014, 0.045)), (0.05, 0.008, 0.014), IRON, bevel=0.002)
    b.box(add(hc, (0.0, 0.007, -0.083)), (0.09, 0.014, 0.016), IRON, bevel=0.003)
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            bolt(b, add(hc, (sx * 0.05, 0.012, sz * 0.064)), (0, 1, 0), r=0.005, h=0.004)
    # радиатор посередине кормы: рамка и частые рёбра поперёк
    rc = (0.0, yt, -0.15)
    b.box(add(rc, (0.0, 0.01, 0.0)), (0.26, 0.02, 0.18), IRON, bevel=0.004)
    for k in range(13):
        b.box(add(rc, (-0.114 + 0.019 * k, 0.036, 0.0)), (0.005, 0.032, 0.16), IRON, bevel=0.0)
    for sx in (-1.0, 1.0):
        b.box(add(rc, (sx * 0.132, 0.03, 0.0)), (0.012, 0.042, 0.18), IRON, bevel=0.003)
    for z in (-0.06, 0.06):
        b.box(add(rc, (0.0, 0.054, z)), (0.27, 0.006, 0.014), STEEL, bevel=0.0)
    # купола датчиков справа спереди (крупный и малый)
    for x, z, r in ((0.1, 0.12, 0.034), (0.15, 0.05, 0.022)):
        b.lathe((x, yt - 0.002, z), (0, 1, 0), [(r + 0.008, 0.0), (r + 0.008, 0.012)], IRON, seg=18)
        sphere(b, (x, yt + 0.01, z), r, GLASS, seg=18, rings=6)
        for k in range(6):
            a = 2 * math.pi * (k + 0.5) / 6
            bolt(b, (x + (r + 0.004) * math.cos(a), yt + 0.012, z + (r + 0.004) * math.sin(a)), (0, 1, 0), r=0.003, h=0.003)
    # окна излучателей морока: по кромке крыши, шестигранные
    for s in (-1.0, 1.0):
        for z in (-0.24, -0.06, 0.12):
            n = unit((s * 0.7, 1.0, 0.0))
            o = (s * (roof_w(z) * 0.94), Y(0.112), z)
            b.lathe(o, n, [(0.02, -0.004), (0.02, 0.006), (0.015, 0.01)], IRON, seg=6, smooth=False)
            b.lathe(add(o, mul(n, 0.008)), n, [(0.012, 0.0), (0.012, 0.004)], GLASS, seg=6, smooth=False)
    # рымы и болты по кромке
    for p in ((0.19, -0.27), (-0.19, -0.27), (-0.19, 0.16), (0.19, 0.16)):
        b.box((p[0], yt + 0.004, p[1]), (0.032, 0.008, 0.024), IRON, bevel=0.002)
        b.torus((p[0], yt + 0.02, p[1]), (1, 0, 0), 0.013, 0.0042, IRON, seg=10, tseg=5)
    for s in (-1.0, 1.0):
        for k in range(9):
            z = -0.28 + 0.058 * k
            bolt(b, (s * 0.238, Y(0.103), z), (s, 0.6, 0.0), r=0.005, h=0.004)
    for _, (x, z), lean, _len in ANTENNAS:
        o = (x, yt, z)
        b.lathe(o, (0, 1, 0), [(0.022, 0.0), (0.022, 0.014), (0.013, 0.022), (0.015, 0.034), (0.01, 0.05)], IRON, seg=10)
    # знаки: номер трафаретом на левой половине кормы
    marks = Body()
    n = (0.0, 1.0, 0.0)
    m = frame(n, up=(0.0, 0.0, 1.0))
    o = (-0.17, yt, -0.12)
    for dx, dy, w, h in ((-0.03, 0.0, 0.008, 0.04), (-0.012, 0.0, 0.008, 0.04), (0.012, 0.0, 0.026, 0.008),
                         (0.012, 0.016, 0.008, 0.008), (0.012, -0.016, 0.026, 0.008)):
        marks.box(add(o, add(mat_vec(m, (dx, dy, 0.0)), mul(n, 0.0016))), (w, h, 0.003), MARK, bevel=0.0, rot=m)
    return b, marks


def eaves():
    """Бортовые щитки под кромкой крыши между ногами: гранёный лист с отгибом внизу, рёбра, болты."""
    out = Body()
    for s in (-1.0, 1.0):
        z0, z1 = -0.17, 0.13
        prof = [(0.236, 0.07), (0.25, 0.07), (0.262, -0.035), (0.248, -0.05), (0.236, -0.05), (0.248, -0.032)]
        rings = [[(s * x, Y(y), z) for x, y in prof] for z in (z0, z0 + 0.02, z1 - 0.02, z1)]
        rings[0] = [(s * (x - 0.004), Y(y + (0.02 if y < 0 else 0.0)), z0) for x, y in prof]
        rings[3] = [(s * (x - 0.004), Y(y + (0.02 if y < 0 else 0.0)), z1) for x, y in prof]
        out.loft(rings if s > 0 else [list(reversed(r)) for r in rings], PAINT)
        for z in (-0.09, 0.0, 0.09):
            out.box((s * 0.262, Y(0.01), z), (0.008, 0.08, 0.012), PAINT, bevel=0.002, rot=rot_z(-s * 0.11))
            bolt(out, (s * 0.261, Y(0.05), z + 0.03), (s, 0.1, 0.0), r=0.005, h=0.004)
    return out


# --- рама и киль (кость chassis) ---

def frame_body():
    """Рама под крышей одним телом: короб, вилки кулаков ног, карманы поворота кулаков, окна по бокам."""
    from solid import solid
    base = Body()
    base.box((0.0, Y(0.0), -0.03), (0.32, 0.15, 0.52), None, bevel=0.0)
    adds = Body()
    for leg in LEGS:
        cx, cz = leg.corner
        for s in (-1.0, 1.0):   # вилка над и под кулаком
            y = Y(s * 0.072)
            adds.lathe((cx, y - 0.011, cz), (0, 1, 0), [(0.056, 0.0), (0.056, 0.022)], None, seg=22)
            adds.box(((cx + math.copysign(0.15, cx)) * 0.5, y, cz), (abs(cx) - 0.1, 0.022, 0.08), None, bevel=0.0)
    cuts = Body()
    for leg in LEGS:
        cx, cz = leg.corner
        cuts.lathe((cx, Y(-0.0615), cz), (0, 1, 0), [(0.058, 0.0), (0.058, 0.123)], None, seg=24)
    for s in (-1.0, 1.0):
        for z0, z1 in ((-0.17, -0.06), (-0.02, 0.09)):
            cuts.box((s * 0.16, Y(-0.01), (z0 + z1) / 2), (0.03, 0.08, z1 - z0), None, bevel=0.0)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.005, bevel_angle=30.0)
    out = Body()
    out.faces = [(p, IRON, s) for p, _, s in t.faces]
    return out


def keel():
    """Киль под рамой: гранёный короб, сужается книзу; швы листов, гнездо станка справа."""
    from solid import solid
    sec = lambda z, w, yb: [(-w * 0.7, Y(yb), z), (w * 0.7, Y(yb), z), (w, Y(yb + 0.06), z), (w, Y(-0.06), z), (-w, Y(-0.06), z),
                            (-w, Y(yb + 0.06), z)]
    base = Body()
    base.loft([sec(-0.27, 0.11, -0.21), sec(-0.22, 0.145, -0.27), sec(0.14, 0.145, -0.27), sec(0.19, 0.12, -0.23)], None)
    cuts = Body()
    for z in (-0.1, 0.05):
        cuts.box((0.0, Y(-0.17), z), (0.3, 0.2, 0.005), None, bevel=0.0)
    t = solid([("add", base), ("cut", cuts)], bevel=0.005, bevel_angle=25.0)
    out = Body()
    out.faces = [(p, PAINT, s) for p, _, s in t.faces]
    return out


def keel_details():
    """Баки-капсулы по бокам киля, ранец радиатора, батарея и полозья брюха, короб ленты, лента, проводка."""
    b = Body()
    straps = Body()
    # баки: слева два во всю длину, справа — только позади винтовки
    for s, z0, z1 in ((-1.0, -0.22, 0.12), (1.0, -0.25, -0.12)):
        for y, r in ((-0.105, 0.05), (-0.2, 0.04)):
            c = (s * (0.145 + r * 0.75), Y(y), 0.0)
            _cylz(b, c, r, z0, z1, IRON, seg=18, cap=min(0.028, (z1 - z0) * 0.2))
            for z in (z0 + 0.06, z1 - 0.06):
                straps.lathe((c[0], c[1], z - 0.008), (0, 0, 1), [(r + 0.004, 0.0), (r + 0.004, 0.016)], PAINT, seg=18)
            b.lathe((c[0], c[1], z1), (0, 0, 1), [(r * 0.35, 0.0), (r * 0.35, 0.012), (r * 0.22, 0.02)], STEEL, seg=10)
        b.tube([(s * 0.18, Y(-0.1), z0), (s * 0.17, Y(-0.08), z0 - 0.03), (s * 0.12, Y(-0.07), z0 - 0.04)], 0.007, STEEL, seg=6, fillet=0.02)
        b.lathe((s * 0.175, Y(-0.055), (z0 + z1) / 2), (0, 1, 0), [(0.008, 0.0), (0.008, 0.016)], STEEL, seg=8)
        b.box((s * 0.175, Y(-0.036), (z0 + z1) / 2), (0.006, 0.006, 0.03), RUBBER, bevel=0.001)
    # ранец радиатора на корме
    c = (0.0, Y(-0.14), -0.31)
    b.box(c, (0.18, 0.16, 0.08), IRON, bevel=0.008)
    for s in (-1.0, 1.0):
        for k in range(7):
            b.box(add(c, (s * 0.098, -0.06 + 0.02 * k, 0.0)), (0.016, 0.006, 0.07), IRON, bevel=0.0)
    for k in range(5):
        b.box(add(c, (0.0, -0.05 + 0.025 * k, -0.042)), (0.13, 0.008, 0.006), STEEL, bevel=0.0)
    # батарея под кормой, полозья брюха
    yb = HIP_Y - BELLY
    bc = (0.0, Y(-0.3), -0.15)
    b.box(bc, (0.17, 0.07, 0.2), IRON, bevel=0.005)
    for k in range(7):
        b.box(add(bc, (0.0, 0.0, -0.09 + 0.03 * k)), (0.18, 0.05, 0.006), IRON, bevel=0.0)
    for s in (-1.0, 1.0):
        b.loft([[(s * 0.075 - 0.011, yb + 0.03, z), (s * 0.075 + 0.011, yb + 0.03, z), (s * 0.075 + 0.009, yb, z * 0.97 + 0.003),
                 (s * 0.075 - 0.009, yb, z * 0.97 + 0.003)] for z in (-0.27, -0.23, 0.13, 0.17)], STEEL)
        for z in (-0.18, 0.08):
            b.box((s * 0.075, yb + 0.042, z), (0.026, 0.026, 0.026), IRON, bevel=0.003)
    # короб с лентой под килем: крышка, рёбра, защёлки, ручка
    ac = AMMO
    b.box(ac, (0.15, 0.1, 0.19), PAINT, bevel=0.006)
    b.box(add(ac, (0.0, 0.055, 0.0)), (0.154, 0.01, 0.194), IRON, bevel=0.003)
    for k in range(3):
        b.box(add(ac, (-0.071, -0.02 + 0.02 * k, 0.0)), (0.006, 0.006, 0.14), IRON, bevel=0.0)
    for z in (-0.06, 0.06):
        b.box(add(ac, (0.071, 0.03, z)), (0.008, 0.02, 0.014), STEEL, bevel=0.001)
    b.tube([add(ac, (-0.04, -0.046, -0.04)), add(ac, (-0.04, -0.062, -0.02)), add(ac, (-0.04, -0.062, 0.02)), add(ac, (-0.04, -0.046, 0.04))],
           0.004, STEEL, seg=6, fillet=0.008)
    # лента: патроны поперёк пути (вдоль ствола), из крышки короба вверх и вправо в окно подачи
    p0 = add(ac, (0.05, 0.06, 0.04))
    p1 = (FEED[0] - 0.012, FEED[1], FEED[2])
    n = 10
    for k in range(n):
        t = k / (n - 1)
        x = p0[0] + (p1[0] - p0[0]) * (1 - (1 - t) ** 2)
        y = p0[1] + (p1[1] - p0[1]) * t * t + 0.02 * math.sin(math.pi * t)
        p = (x + 0.012 * math.sin(math.pi * t), y, p1[2])
        b.lathe(add(p, (0.0, 0.0, -0.036)), (0, 0, 1), [(0.007, 0.0), (0.007, 0.05), (0.0058, 0.054), (0.0044, 0.064), (0.0015, 0.072)],
                BRASS if k % 4 else STEEL, seg=8)
        b.box(add(p, (0.0, 0.0, -0.013)), (0.017, 0.017, 0.011), IRON, bevel=0.001)
        b.box(add(p, (0.0, 0.0, 0.013)), (0.017, 0.017, 0.011), IRON, bevel=0.001)
    # проводка: красные жгуты по бокам киля из рамы вниз
    for s in (-1.0, 1.0):
        for k, mat in enumerate((CABLE, RUBBER, CABLE)):
            z = -0.2 + 0.016 * k
            b.tube([(s * 0.14, Y(-0.05), z), (s * 0.158, Y(-0.16), z + 0.02), (s * 0.135, Y(-0.26), z + 0.06), (s * 0.06, Y(-0.282), z + 0.08)],
                   0.006, mat, seg=6, fillet=0.03)
    for k, mat in enumerate((CABLE, CABLE, RUBBER)):   # к голове
        x = HEAD[0] - 0.03 + 0.03 * k
        b.tube([(x, Y(-0.06), 0.12), (x, Y(-0.09), 0.17), (x * 0.8 - 0.01, HEAD[1] - 0.04, HEAD[2])], 0.006, mat, seg=6, fillet=0.02)
    return b, straps


def apron():
    """Дырчатый фартук спереди слева под головой: изогнутый лист на двух кронштейнах."""
    rings = []
    for k in range(7):
        x = -0.17 + 0.03 * k
        bow = 0.02 * math.sin(math.pi * k / 6)
        rings.append([(x, Y(-0.31), 0.205 + bow), (x, Y(-0.31), 0.217 + bow), (x, Y(-0.12), 0.217 + bow + 0.02), (x, Y(-0.12), 0.205 + bow + 0.02)])
    base = Body()
    base.loft(rings, None)
    holes = []
    for i in range(4):
        for j in range(6):
            x = -0.15 + 0.028 * i * 1.0 + (0.014 if j % 2 else 0.0)
            if x > 0.0:
                continue
            y = -0.29 + 0.03 * j
            t = (y + 0.31) / 0.19
            holes.append((x, Y(y), 0.211 + 0.02 * math.sin(math.pi * (x + 0.17) / 0.18) + 0.02 * t))
    out = _perforated(base, holes, (0.0, 0.0, 1.0), 0.0085, PAINT)
    for x in (-0.13, -0.03):
        out.box((x, Y(-0.14), 0.195), (0.02, 0.03, 0.03), IRON, bevel=0.003)
        out.lathe((x - 0.012, Y(-0.14), 0.195), (1, 0, 0), [(0.007, 0.0), (0.007, 0.024)], STEEL, seg=8)
    return out


# --- станок (кость turret) и винтовка (кость gun) ---

def turret_body():
    """Станок: поворотный узел под рамой, вилка вниз к цапфам, привод возвышения сзади, кабель."""
    tx, ty, tz = TURRET
    gx, gy, gz = GUN
    b = Body()
    b.lathe((tx, ty + 0.0, tz), (0, -1, 0), [(0.04, 0.0), (0.046, 0.006), (0.046, 0.018), (0.036, 0.024)], IRON, seg=20)
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        bolt(b, (tx + 0.04 * math.cos(a), ty - 0.019, tz + 0.04 * math.sin(a)), (0, -1, 0), r=0.004, h=0.003)
    for s in (-1.0, 1.0):   # щёки вилки и крышки цапф
        b.loft([[(gx + s * 0.04 - 0.006, ty - 0.022, z), (gx + s * 0.04 + 0.006, ty - 0.022, z), (gx + s * 0.04 + 0.006, gy - 0.02, z * 0.4 + gz * 0.6),
                 (gx + s * 0.04 - 0.006, gy - 0.02, z * 0.4 + gz * 0.6)] for z in (tz - 0.035, tz + 0.035)], IRON)
        b.lathe((gx + s * 0.046, gy, gz), (s, 0, 0), [(0.02, 0.0), (0.02, 0.008), (0.013, 0.013)], STEEL, seg=12)
    b.lathe((tx, ty - 0.03, tz - 0.06), (0, -0.5, -1), [(0.014, 0.0), (0.014, 0.05), (0.01, 0.056)], IRON, seg=10)
    return b


def rifle():
    """Винтовка: короб затвора с крышкой и окном подачи слева, буфер сзади, ствол в дырчатом кожухе, тонкий
    длинный ствол, дульный тормоз со щелями."""
    from solid import solid
    gx, gy, gz = GUN
    b = Body()
    sec = lambda z, w, h: [(gx - w, gy - h, z), (gx + w, gy - h, z), (gx + w, gy + h * 0.7, z), (gx + w * 0.7, gy + h, z),
                           (gx - w * 0.7, gy + h, z), (gx - w, gy + h * 0.7, z)]
    b.loft([sec(gz - 0.11, 0.024, 0.03), sec(gz - 0.08, 0.03, 0.036), sec(gz + 0.17, 0.03, 0.036), sec(gz + 0.2, 0.024, 0.03)], IRON)
    b.box((gx, gy + 0.04, gz + 0.03), (0.05, 0.012, 0.15), IRON, bevel=0.003)          # крышка подачи
    for k in range(5):
        b.box((gx, gy + 0.048, gz - 0.03 + 0.03 * k), (0.046, 0.005, 0.006), IRON, bevel=0.0)
    b.lathe((gx + 0.026, gy + 0.04, gz - 0.045), (-1, 0, 0), [(0.006, 0.0), (0.006, 0.052)], STEEL, seg=8)   # ось крышки
    b.box((FEED[0] + 0.002, FEED[1], FEED[2]), (0.012, 0.03, 0.05), IRON, bevel=0.002)   # окно подачи слева
    b.box((gx + 0.031, gy - 0.004, gz + 0.07), (0.006, 0.022, 0.06), STEEL, bevel=0.001)   # окно выброса справа
    b.lathe((gx, gy - 0.004, gz - 0.11), (0, 0, -1), [(0.018, 0.0), (0.018, 0.06), (0.014, 0.066)], IRON, seg=12)   # буфер
    b.tube([(gx + 0.034, gy + 0.01, gz + 0.13), (gx + 0.05, gy + 0.012, gz + 0.13), (gx + 0.056, gy + 0.028, gz + 0.13)], 0.0055, STEEL, seg=6,
           fillet=0.008)   # рукоять заряжания
    # кожух ствола с рядами отверстий
    z0 = gz + 0.2
    jacket = Body()
    jacket.lathe((gx, gy, z0), (0, 0, 1), [(0.024, 0.0), (0.024, 0.26)], None, seg=16)
    cuts = Body()
    cuts.lathe((gx, gy, z0 - 0.01), (0, 0, 1), [(0.016, 0.0), (0.016, 0.28)], None, seg=12)
    for i in range(6):
        for j in range(6):
            a = 2 * math.pi * (j + 0.5 * (i % 2)) / 6
            d = (math.cos(a), math.sin(a), 0.0)
            cuts.lathe(add((gx, gy, z0 + 0.04 + 0.036 * i), mul(d, 0.012)), d, [(0.0065, 0.0), (0.0065, 0.03)], None, seg=6)
    t = solid([("add", jacket), ("cut", cuts)], bevel=0.0)
    b.faces += [(p, IRON, s) for p, _, s in t.faces]
    for z in (z0, z0 + 0.25):
        b.lathe((gx, gy, z), (0, 0, 1), [(0.027, 0.0), (0.027, 0.014)], IRON, seg=16)
    # ствол: тонкий, ступенями, муфта
    b.lathe((gx, gy, z0 - 0.01), (0, 0, 1), [(0.0145, 0.0), (0.0145, 0.42), (0.0125, 0.44), (0.0125, gz + BARREL - z0 + 0.01)], IRON, seg=12)
    b.lathe((gx, gy, gz + 0.72), (0, 0, 1), [(0.019, 0.0), (0.019, 0.03), (0.015, 0.036)], IRON, seg=12)
    # дульный тормоз: цилиндр со сквозными щелями по бокам и вырезами спереди
    mz = gz + BARREL
    brake = Body()
    brake.lathe((gx, gy, mz), (0, 0, 1), [(0.016, 0.0), (0.022, 0.008), (0.022, 0.1), (0.019, 0.106)], None, seg=16)
    bc = Body()
    bc.lathe((gx, gy, mz + 0.02), (0, 0, 1), [(0.008, 0.0), (0.008, 0.1)], None, seg=10)
    for k in range(3):
        bc.box((gx, gy, mz + 0.03 + 0.022 * k), (0.06, 0.014, 0.011), None, bevel=0.0)
    for s in (-1.0, 1.0):
        bc.box((gx, gy + s * 0.024, mz + 0.098), (0.012, 0.02, 0.03), None, bevel=0.0)
    t = solid([("add", brake), ("cut", bc)], bevel=0.0015, bevel_angle=30.0)
    b.faces += [(p, IRON, s) for p, _, s in t.faces]
    b.tube([(gx + 0.024, gy - 0.03, gz - 0.06), (gx + 0.03, gy - 0.04, gz + 0.02), (gx + 0.024, gy - 0.032, gz + 0.1)], 0.005, CABLE, seg=6,
           fillet=0.02)
    return b


# --- голова (кость eye) ---

def head():
    """Голова: гранёный короб, янтарный визор с решёткой под козырьком, щёки с отверстиями, нижний объектив."""
    from solid import solid
    hx, hy, hz = HEAD
    c = (hx, hy - 0.035, hz + 0.075)
    sec = lambda z, w, h: [(c[0] - w, c[1] - h, c[2] + z), (c[0] + w, c[1] - h, c[2] + z), (c[0] + w * 1.04, c[1] + h * 0.3, c[2] + z),
                           (c[0] + w * 0.78, c[1] + h, c[2] + z), (c[0] - w * 0.78, c[1] + h, c[2] + z), (c[0] - w * 1.04, c[1] + h * 0.3, c[2] + z)]
    base = Body()
    base.loft([sec(-0.07, 0.07, 0.045), sec(-0.02, 0.09, 0.056), sec(0.05, 0.09, 0.056), sec(0.075, 0.08, 0.05)], None)
    cuts = Body()
    cuts.box(add(c, (0.0, 0.006, 0.08)), (0.13, 0.034, 0.03), None, bevel=0.0)   # гнездо визора
    for z in (-0.03, 0.02):
        cuts.box(add(c, (0.0, 0.058, z)), (0.2, 0.012, 0.004), None, bevel=0.0)
    t = solid([("add", base), ("cut", cuts)], bevel=0.005, bevel_angle=25.0)
    shell_ = Body()
    shell_.faces = [(p, PAINT, s) for p, _, s in t.faces]
    b = Body()
    b.box(add(c, (0.0, 0.006, 0.068)), (0.128, 0.032, 0.006), LENS, bevel=0.0)    # визор
    for k in range(4):
        b.box(add(c, (0.0, -0.007 + 0.009 * k, 0.074)), (0.13, 0.0025, 0.004), IRON, bevel=0.0)
    for x in (-0.033, 0.0, 0.033):
        b.box(add(c, (x, 0.006, 0.074)), (0.003, 0.03, 0.004), IRON, bevel=0.0)
    b.box(add(c, (0.0, 0.05, 0.075)), (0.17, 0.008, 0.05), PAINT, bevel=0.002, rot=rot_x(0.25))   # козырёк
    for s in (-1.0, 1.0):   # щёки с отверстиями
        b.box(add(c, (s * 0.098, -0.004, 0.0)), (0.01, 0.08, 0.12), IRON, bevel=0.003)
        for i in range(3):
            for j in range(2):
                b.lathe(add(c, (s * 0.104, -0.02 + 0.03 * j, -0.035 + 0.035 * i)), (s, 0, 0), [(0.007, 0.0), (0.007, 0.003)], RUBBER, seg=8)
    o = add(c, (-0.035, -0.05, 0.055))   # нижний объектив и малый датчик
    b.lathe(o, (0, -0.4, 1), [(0.016, 0.0), (0.016, 0.014), (0.012, 0.02)], IRON, seg=12)
    b.lathe(add(o, (0.0, -0.008, 0.018)), (0, -0.4, 1), [(0.01, 0.0), (0.01, 0.003)], GLASS, seg=12)
    b.lathe(add(c, (0.04, -0.05, 0.06)), (0, -0.4, 1), [(0.008, 0.0), (0.008, 0.01)], LENS, seg=8)
    b.box(add(c, (0.0, 0.0, -0.08)), (0.08, 0.05, 0.014), IRON, bevel=0.003)   # разъёмы сзади
    for x in (-0.025, 0.0, 0.025):
        b.lathe(add(c, (x, 0.0, -0.087)), (0, 0, -1), [(0.008, 0.0), (0.008, 0.01)], STEEL, seg=8)
    b.lathe((hx, hy + 0.02, hz), (0, -1, 0), [(0.03, 0.0), (0.03, 0.02), (0.04, 0.024), (0.04, 0.032)], IRON, seg=16)   # поворотный узел
    return shell_, b


def antenna_base(x, z, lean):
    o = (x, Y(TOP) + 0.05, z)
    return o, unit((0.03 * (1 if x > 0 else -1), 1.0, -lean))


def antenna(x, z, lean, length):
    o, ax = antenna_base(x, z, lean)
    b = Body()
    b.tube([o, add(o, mul(ax, length))], 0.005, IRON, seg=6, radii=lambda i, n: 0.005 if i == 0 else 0.0025)
    if length < 0.3:   # короткий штырь с шаром
        b.lathe(add(o, mul(ax, length)), ax, [(0.007, 0.0), (0.011, 0.01), (0.007, 0.02)], IRON, seg=10)
    for k in range(2):
        b.torus(add(o, mul(ax, 0.008 + 0.012 * k)), ax, 0.008, 0.0035, IRON, seg=8, tseg=5)
    return b


def make():
    """Собрать машину: Rig с костями и частями."""
    rig = Rig("tat")
    rig.bone("root", None, (0.0, 0.0, 0.0))
    rig.bone("chassis", "root", (0.0, HIP_Y, 0.0))
    rig.bone("turret", "chassis", TURRET)
    rig.bone("gun", "turret", GUN)
    rig.bone("eye", "chassis", HEAD)
    rig.add(roof(), "chassis", wear=WEAR, edge=0.06, name="крыша")
    top, marks = roof_top()
    rig.add(top, "chassis", wear=WEAR, name="крыша: навесное")
    rig.add(marks, "chassis", name="знаки")
    rig.add(frame_body(), "chassis", name="рама")
    rig.add(keel(), "chassis", wear=WEAR, edge=0.05, name="киль")
    kd, straps = keel_details()
    rig.add(kd, "chassis", name="киль: навесное")
    rig.add(straps, "chassis", wear=WEAR, name="ленты баков")
    rig.add(apron(), "chassis", wear=WEAR, edge=0.05, name="фартук")
    rig.add(eaves(), "chassis", wear=WEAR, edge=0.05, name="бортовые щитки")
    rig.add(turret_body(), "turret", name="станок")
    rig.add(rifle(), "gun", name="винтовка")
    hs, hb = head()
    rig.add(hs, "eye", wear=WEAR, edge=0.05, name="голова")
    rig.add(hb, "eye", name="голова: визор")
    for bone, (x, z), lean, length in ANTENNAS:
        o, ax = antenna_base(x, z, lean)
        rig.bone(bone, "chassis", o)
        rig.add(antenna(x, z, lean, length), bone, name=bone)
    for leg in LEGS:
        L, parts = leg_parts(leg)
        m = rot_y(math.radians(leg.deg))
        o = (leg.corner[0], 0.0, leg.corner[1])
        w = lambda p: add(mat_vec(m, p), o)
        n = leg.name
        rig.bone("hip_" + n, "chassis", w((0.0, HIP_Y, 0.0)))
        rig.bone("thigh_" + n, "hip_" + n, w(L["P"]))
        rig.bone("shin_" + n, "thigh_" + n, w(L["K"]))
        rig.bone("tarsus_" + n, "shin_" + n, w(L["A"]))
        rig.bone("foot_" + n, "tarsus_" + n, w(L["F"]))
        for bone, bodies in parts.items():
            for body, wear in bodies:
                rig.add(body.placed(m, o), bone + "_" + n, wear=wear, edge=0.05 if wear else 0.12, name=bone + " " + n)
    return rig
