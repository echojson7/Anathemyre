"""«Тать» — скрытный шагающий снайпер (docs/tat.md): рецепт машины. Низкий гранёный корпус из плоских граней в
тёмной краске, малая башенка с длинной винтовкой на правом плече, прибор на складной мачте слева сзади, четыре
разные ноги (tat_leg.py).

Оси сетки — как у Вежи: x вправо, y вверх, z вперёд, начало — на земле под серединой. Бинд-поза — «стоит»,
мачта поднята, сошки сложены вдоль ствола.
"""
import math

from kit import Body, add, mat_mul, mat_vec, mul, rot_x, rot_y, rot_z, sub, unit
from rig import Rig
from tat_leg import GLASS, HIP_Y, IRON, LEGS, LENS, MARK, NET, PAINT, RUBBER, STEEL, WORN, leg_parts
from vezha import frame, sweep_rect
from vezha_leg import bolt

# --- размеры, м ---
BELLY = 0.15                 # оси бёдер над землёй, когда машина лежит брюхом на полозьях
# Сечения корпуса: z, низ, скула, верх (от осей бёдер), полуширина низа, скулы и верха
HULL = ((-0.53, -0.05, 0.03, 0.10, 0.15, 0.19, 0.11),
        (-0.43, -0.10, 0.035, 0.155, 0.19, 0.265, 0.17),
        (0.08, -0.10, 0.035, 0.17, 0.19, 0.27, 0.17),
        (0.33, -0.08, 0.025, 0.105, 0.15, 0.215, 0.12),
        (0.47, -0.04, 0.012, 0.045, 0.09, 0.125, 0.065))
DECK = HIP_Y + 0.17          # палуба посередине
TURRET = (0.12, DECK, 0.0)   # ось башенки: смещена вправо
GUN = (0.12, DECK + 0.078, 0.10)   # цапфы винтовки
BIPOD_Z = 0.62               # шарнир сошек по стволу от цапф
MAST = (-0.13, HIP_Y + 0.157, -0.30)   # шарнир мачты на палубе
MAST_H = 0.30                # высота мачты до оси головы
ANTENNAS = (("ant_l", (-0.205, -0.43), 0.06, 0.24), ("ant_r", (0.175, -0.445), 0.14, 0.66))

LODS = (24000, 14000, 8500, 5000)
WEAR = (WORN, 0.022, 0.75, 0.04)


def deck_y(z):
    """Высота палубы над осями бёдер в точке z (между сечениями — по прямой)."""
    for a, b in zip(HULL, HULL[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0])
            return a[3] + (b[3] - a[3]) * t
    return HULL[0][3] if z < HULL[0][0] else HULL[-1][3]


def hull_ring(z, yb, yc, yt, wb, wc, wt):
    y = HIP_Y
    return [(-wb, y + yb, z), (wb, y + yb, z), (wc, y + yc, z), (wt, y + yt, z), (-wt, y + yt, z), (-wc, y + yc, z)]


# --- корпус (кость chassis) ---

def hull_shell():
    """Корпус одним телом: гранёный клин, брюшной киль, карманы бёдер, ниша башенки; вырезы — швы, щели жалюзи."""
    from solid import solid
    base = Body()
    base.loft([hull_ring(*r) for r in HULL], None)
    adds = Body()
    # киль и полозья: на них машина лежит
    adds.loft([[(-0.10, HIP_Y - 0.09, z), (0.10, HIP_Y - 0.09, z), (0.07, HIP_Y - BELLY + 0.01, z * 0.96), (-0.07, HIP_Y - BELLY + 0.01, z * 0.96)]
               for z in (-0.40, 0.25)], None)
    # кронштейны бёдер: вилка над и под кулаком
    for leg in LEGS:
        cx, cz = leg.corner
        m = rot_y(math.radians(leg.deg))
        h = {"stilt": 0.10, "brace": 0.13, "ski": 0.11, "spike": 0.12}[leg.kind]
        for s in (-1.0, 1.0):
            y = HIP_Y + s * (h / 2 + 0.013)
            adds.box(add(mat_vec(m, (-0.07, 0.0, 0.0)), (cx, y, cz)), (0.17, 0.022, 0.1), None, bevel=0.0, rot=m)
            adds.lathe((cx, y - 0.011, cz), (0, 1, 0), [(0.064, 0.0), (0.064, 0.022)], None, seg=18)
    cuts = Body()
    for leg in LEGS:   # карман: шея бедра ходит в нём на всём повороте
        cx, cz = leg.corner
        h = {"stilt": 0.10, "brace": 0.13, "ski": 0.11, "spike": 0.12}[leg.kind]
        cuts.lathe((cx, HIP_Y - h / 2 - 0.002, cz), (0, 1, 0), [(leg.u + 0.07, 0.0), (leg.u + 0.07, h + 0.004)], None, seg=24)
    # ниша кольца башенки, щели жалюзи на корме, швы листов
    cuts.lathe((TURRET[0], DECK - 0.012, TURRET[2]), (0, 1, 0), [(0.125, 0.0), (0.125, 0.05)], None, seg=28)
    for k in range(6):
        cuts.box((0.0, HIP_Y + 0.075 + k * 0.012, -0.535), (0.2, 0.006, 0.05), None, bevel=0.0)
    g = 0.005
    for z in (-0.20, 0.20):
        cuts.box((0.0, DECK + 0.004, z), (0.4, 0.012, g), None, bevel=0.0)
    for s in (-1.0, 1.0):
        for z in (-0.08, 0.10):
            cuts.box((s * 0.25, HIP_Y + 0.09, z), (0.08, 0.13, g), None, bevel=0.0, rot=rot_z(s * 0.48))
    out = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.006, bevel_angle=25.0)
    out.faces = [(p, PAINT, s) for p, _, s in out.faces]
    return out


def hull_fittings():
    """Навесное корпуса: кольцо башенки, люк, жалюзи, скатка маскировочной сети, ящик, рымы, поручень, проводка,
    шарнир мачты, основания антенн, нос с прибором, болты, полозья брюха."""
    b = Body()
    # нижняя половина кольца башенки и болты
    tx, ty, tz = TURRET
    b.lathe((tx, ty - 0.012, tz), (0, 1, 0), [(0.10, 0.0), (0.122, 0.0), (0.122, 0.022), (0.114, 0.03), (0.10, 0.03)], IRON, seg=28)
    for k in range(12):
        a = 2 * math.pi * (k + 0.5) / 12
        bolt(b, (tx + 0.111 * math.cos(a), ty + 0.018, tz + 0.111 * math.sin(a)), (0, 1, 0), r=0.006, h=0.005)
    # люк на левой половине палубы
    hc = (-0.09, HIP_Y + deck_y(-0.02), -0.02)
    b.box(add(hc, (0.0, 0.006, 0.0)), (0.16, 0.012, 0.22), PAINT, bevel=0.004)
    b.box(add(hc, (0.0, 0.014, 0.08)), (0.05, 0.008, 0.016), IRON, bevel=0.002)
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            bolt(b, add(hc, (sx * 0.068, 0.012, sz * 0.096)), (0, 1, 0), r=0.006, h=0.004)
    b.box(add(hc, (0.0, 0.006, -0.118)), (0.11, 0.016, 0.018), IRON, bevel=0.003)
    # нос: прибор в литом гнезде, щиток над ним
    nz = 0.47
    no = (0.0, HIP_Y + 0.0, nz - 0.01)
    b.lathe(no, (0, 0, 1), [(0.034, 0.0), (0.034, 0.018), (0.028, 0.026)], IRON, seg=14)
    b.lathe(add(no, (0, 0, 0.02)), (0, 0, 1), [(0.024, 0.0), (0.024, 0.004), (0.016, 0.008)], GLASS, seg=14)
    b.box((0.0, HIP_Y + 0.032, nz - 0.02), (0.11, 0.008, 0.06), IRON, bevel=0.002)
    # жалюзи кормы: косые пластины над щелями
    for k in range(4):
        b.box((0.0, HIP_Y + 0.07 + k * 0.022, -0.55), (0.22, 0.004, 0.03), IRON, bevel=0.0, rot=rot_x(0.6))
    # скатка маскировочной сети на корме справа: ком в ремнях
    rc = (0.11, HIP_Y + deck_y(-0.33) + 0.05, -0.335)
    for k in range(9):
        x = -0.12 + 0.03 * k
        b.blob(add(rc, (x, 0.003 * math.sin(k * 1.7), 0.004 * math.cos(k * 2.3))), 0.05, NET, 900 + k, squash=(0.75, 0.95, 1.0))
    for x in (-0.07, 0.07):
        b.torus(add(rc, (x, 0.0, 0.0)), (1, 0, 0), 0.054, 0.006, RUBBER, seg=14, tseg=5)
        b.box(add(rc, (x, -0.052, 0.0)), (0.02, 0.008, 0.05), IRON, bevel=0.002)
    # ящик слева у кормы, рымы, поручень по правому борту
    bc = (-0.07, HIP_Y + deck_y(-0.42) + 0.035, -0.43)
    b.box(bc, (0.15, 0.07, 0.09), PAINT, bevel=0.006)
    b.box(add(bc, (0.0, 0.036, 0.0)), (0.12, 0.006, 0.06), IRON, bevel=0.002)
    b.box(add(bc, (0.0, 0.0, 0.047)), (0.03, 0.02, 0.008), STEEL, bevel=0.002)
    for p in ((0.2, -0.38), (-0.2, 0.15), (0.13, 0.3), (-0.13, 0.3)):
        y = HIP_Y + deck_y(p[1])
        b.box((p[0], y + 0.004, p[1]), (0.04, 0.008, 0.03), IRON, bevel=0.002)
        b.torus((p[0], y + 0.022, p[1]), (1, 0, 0), 0.016, 0.005, IRON, seg=10, tseg=5)
    b.tube([(0.235, HIP_Y + 0.115, -0.30), (0.262, HIP_Y + 0.135, -0.27), (0.262, HIP_Y + 0.135, -0.03), (0.235, HIP_Y + 0.115, 0.0)],
           0.008, IRON, seg=6, fillet=0.02)
    # шарнир мачты: проушины и упор
    mx, my, mz = MAST
    for s in (-1.0, 1.0):
        b.box((mx + s * 0.032, my + 0.03, mz), (0.012, 0.06, 0.05), IRON, bevel=0.003)
    b.box((mx, my + 0.006, mz - 0.11), (0.05, 0.012, 0.04), RUBBER, bevel=0.003)   # упор сложенной мачты
    # основания антенн
    for _, (x, z), lean, _len in ANTENNAS:
        o = (x, HIP_Y + deck_y(z), z)
        b.lathe(o, (0, 1, 0), [(0.024, 0.0), (0.024, 0.014), (0.014, 0.022), (0.016, 0.034), (0.011, 0.05)], IRON, seg=10)
    # проводка: кабель из-под башенки к мачте и в корму
    b.tube([(tx - 0.08, DECK + 0.004, tz - 0.08), (-0.02, DECK + 0.006, -0.18), (mx + 0.03, DECK - 0.004, mz + 0.02)], 0.007, RUBBER, seg=6,
           fillet=0.04)
    # болты по скуле
    for s in (-1.0, 1.0):
        for k in range(7):
            z = -0.36 + 0.105 * k
            yc = HIP_Y + 0.035 if z < 0.08 else HIP_Y + 0.035 - 0.01 * (z - 0.08) / 0.25
            wc = 0.265 if z < 0.08 else 0.27 - 0.055 * (z - 0.08) / 0.25
            bolt(b, (s * (wc - 0.002), yc + 0.004, z), (s, 0.4, 0.0), r=0.007, h=0.005)
    # знаки: светлый трафарет на бортах (глиф и номер)
    marks = Body()
    for s in (-1.0, 1.0):
        n = unit((s * 0.86, 0.5, 0.0))
        m = frame(n)
        o = (s * 0.215, HIP_Y + 0.1, 0.24 if s > 0 else -0.28)
        strokes = [(-0.03, 0.0, 0.01, 0.06, 0.0), (0.0, 0.02, 0.06, 0.01, 0.0), (0.02, -0.01, 0.01, 0.05, 0.6)] if s > 0 else \
                  [(-0.04 + 0.02 * k, 0.0, 0.008, 0.045, 0.0) for k in range(3)] + [(0.02, 0.0, 0.03, 0.03, 0.785)]
        for dx, dy, w, h, a in strokes:
            marks.box(add(o, add(mat_vec(m, (dx, dy, 0.0)), mul(n, 0.0016))), (w, h, 0.003), MARK, bevel=0.0,
                      rot=mat_mul(m, rot_z(a)))
    return b, marks


def hull_skirts():
    """Бортовые листы над ногами: тонкие гранёные щитки на кронштейнах (прячут кулаки бёдер сверху)."""
    b = Body()
    for s in (-1.0, 1.0):
        for z0, z1 in ((-0.25, -0.02), (0.05, 0.24)):
            y0, y1 = HIP_Y + 0.12, HIP_Y + 0.155
            pts = [[(s * 0.2, y1, z), (s * 0.275, y0 + 0.012, z), (s * 0.29, y0 - 0.005, z), (s * 0.28, y0 - 0.008, z),
                    (s * 0.265, y0 + 0.004, z), (s * 0.19, y1 - 0.01, z)] for z in (z0, z1)]
            b.loft(pts if s > 0 else [list(reversed(r)) for r in pts], PAINT)
    return b


def chassis_details():
    """Железо рамы под корпусом: кулаки бёдер прикрыты, брюхо — лючки, шланги к ногам, полозья."""
    b = Body()
    for s in (-1.0, 1.0):
        b.loft([[(s * 0.07 - 0.012, HIP_Y - BELLY + 0.012, z), (s * 0.07 + 0.012, HIP_Y - BELLY + 0.012, z), (s * 0.07 + 0.009, HIP_Y - BELLY, z * 0.97),
                 (s * 0.07 - 0.009, HIP_Y - BELLY, z * 0.97)] for z in (-0.38, -0.32, 0.18, 0.23)], STEEL)
    b.box((0.0, HIP_Y - 0.095, 0.0), (0.12, 0.008, 0.18), IRON, bevel=0.002)
    for sx in (-1.0, 1.0):
        for z in (-0.07, 0.07):
            bolt(b, (sx * 0.05, HIP_Y - 0.099, z), (0, -1, 0), r=0.006, h=0.004)
    for leg in LEGS:
        cx, cz = leg.corner
        b.tube([(cx * 0.4, HIP_Y - 0.08, cz * 0.6), (cx * 0.8, HIP_Y - 0.07, cz * 0.85), (cx, HIP_Y - 0.082, cz)], 0.007, RUBBER, seg=6,
               fillet=0.03)
    return b


# --- башенка (кость turret) и винтовка (кость gun), сошки (кость bipod) ---

def turret_body():
    from solid import solid
    tx, ty, tz = TURRET
    sec = lambda z, w, h, y0: [(tx - w, ty + y0, z), (tx + w, ty + y0, z), (tx + w * 1.05, ty + y0 + h * 0.45, z), (tx + w * 0.7, ty + y0 + h, z),
                               (tx - w * 0.7, ty + y0 + h, z), (tx - w * 1.05, ty + y0 + h * 0.45, z)]
    base = Body()
    base.loft([sec(tz - 0.17, 0.07, 0.06, 0.012), sec(tz - 0.12, 0.09, 0.1, 0.012), sec(tz + 0.05, 0.095, 0.105, 0.012),
               sec(tz + 0.14, 0.07, 0.07, 0.012)], None)
    adds = Body()
    for s in (-1.0, 1.0):   # щёки цапф
        adds.box((GUN[0] + s * 0.062, GUN[1], GUN[2] - 0.01), (0.022, 0.07, 0.1), None, bevel=0.0)
    cuts = Body()
    cuts.box((GUN[0], GUN[1] + 0.01, GUN[2] + 0.02), (0.1, 0.12, 0.16), None, bevel=0.0)   # проём винтовки
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.005, bevel_angle=25.0)
    b = Body()
    b.faces = [(p, PAINT, s) for p, _, s in t.faces]
    fit = Body()
    fit.lathe((tx, ty, tz), (0, 1, 0), [(0.1, 0.0), (0.1, 0.014), (0.092, 0.018)], IRON, seg=28)   # верх кольца
    for s in (-1.0, 1.0):
        fit.lathe((GUN[0] + s * 0.073, GUN[1], GUN[2]), (s, 0, 0), [(0.026, 0.0), (0.026, 0.01), (0.018, 0.016)], IRON, seg=12)
    # короб приводов сзади, кабель в винтовку, прибор-дальномер слева
    fit.box((tx, ty + 0.06, tz - 0.17), (0.12, 0.06, 0.04), IRON, bevel=0.004)
    for k in range(4):
        fit.box((tx - 0.045 + 0.03 * k, ty + 0.06, tz - 0.192), (0.008, 0.05, 0.006), IRON, bevel=0.0)
    rf = (tx - 0.085, ty + 0.085, tz + 0.04)
    fit.box(rf, (0.035, 0.035, 0.1), IRON, bevel=0.004)
    fit.lathe(add(rf, (0, 0, 0.05)), (0, 0, 1), [(0.013, 0.0), (0.013, 0.01)], GLASS, seg=10)
    fit.tube([(tx + 0.03, ty + 0.06, tz - 0.15), (tx + 0.05, ty + 0.1, tz - 0.08), (GUN[0] + 0.02, GUN[1] + 0.02, GUN[2] - 0.13)], 0.006, RUBBER,
             seg=6, fillet=0.02)
    for sx in (-1.0, 1.0):
        bolt(fit, (tx + sx * 0.05, ty + 0.117, tz - 0.06), (0, 1, 0), r=0.006, h=0.004)
    return b, fit


def rifle():
    """Винтовка: короб затворной части, гранёный ствол, глушитель, магазин, прицел с бледно-зелёным объективом."""
    b = Body()
    gx, gy, gz = GUN
    # короб: от цапф назад и вперёд, гранёный
    sec = lambda z, w, h: [(gx - w, gy - h, z), (gx + w, gy - h, z), (gx + w, gy + h * 0.6, z), (gx + w * 0.6, gy + h, z),
                           (gx - w * 0.6, gy + h, z), (gx - w, gy + h * 0.6, z)]
    b.loft([sec(gz - 0.2, 0.026, 0.032), sec(gz - 0.12, 0.034, 0.042), sec(gz + 0.12, 0.034, 0.042), sec(gz + 0.17, 0.026, 0.032)], IRON)
    b.lathe((gx - 0.04, gy, gz), (1, 0, 0), [(0.016, 0.0), (0.016, 0.08)], STEEL, seg=10)   # ось цапф
    # ствол: восьмигранный, ступенями, муфта и глушитель
    b.lathe((gx, gy, gz + 0.16), (0, 0, 1), [(0.024, 0.0), (0.024, 0.06), (0.019, 0.07), (0.016, 0.42), (0.014, 0.62)], IRON, seg=8,
            smooth=False)
    b.lathe((gx, gy, gz + 0.16), (0, 0, 1), [(0.03, 0.0), (0.03, 0.05), (0.024, 0.056)], IRON, seg=16)
    sz = gz + 0.76
    b.lathe((gx, gy, sz), (0, 0, 1), [(0.016, 0.0), (0.028, 0.012), (0.028, 0.3), (0.024, 0.315), (0.008, 0.32)], IRON, seg=18)
    for k in range(6):
        b.lathe((gx, gy, sz + 0.035 + 0.045 * k), (0, 0, 1), [(0.029, 0.0), (0.029, 0.006)], STEEL, seg=18)
    b.lathe((gx, gy, sz + 0.316), (0, 0, 1), [(0.007, 0.0), (0.007, 0.006)], RUBBER, seg=8)
    # магазин под коробом, рукоять заряжания справа, кабель
    b.box((gx, gy - 0.07, gz + 0.03), (0.04, 0.07, 0.08), IRON, bevel=0.004, rot=rot_x(-0.18))
    b.box((gx, gy - 0.105, gz + 0.04), (0.044, 0.01, 0.086), STEEL, bevel=0.002)
    b.tube([(gx + 0.034, gy + 0.01, gz - 0.04), (gx + 0.06, gy + 0.012, gz - 0.04), (gx + 0.066, gy + 0.03, gz - 0.04)], 0.006, STEEL, seg=6,
           fillet=0.008)
    # прицел: длинная труба на двух кольцах, объектив светится
    sy = gy + 0.07
    b.lathe((gx, sy, gz - 0.14), (0, 0, 1), [(0.016, 0.0), (0.02, 0.02), (0.02, 0.05), (0.016, 0.07), (0.016, 0.24), (0.026, 0.28), (0.026, 0.32)],
            IRON, seg=14)
    b.lathe((gx, sy, gz + 0.18), (0, 0, 1), [(0.022, 0.0), (0.022, 0.004), (0.016, 0.006)], LENS, seg=14)
    b.lathe((gx, sy, gz - 0.141), (0, 0, -1), [(0.012, 0.0), (0.012, 0.004)], GLASS, seg=10)
    for z in (gz - 0.05, gz + 0.08):
        b.box((gx, sy - 0.03, z), (0.022, 0.03, 0.022), IRON, bevel=0.003)
        b.torus((gx, sy, z), (0, 0, 1), 0.02, 0.005, IRON, seg=14, tseg=5)
    b.lathe((gx + 0.02, sy, gz + 0.0), (1, 0, 0), [(0.008, 0.0), (0.008, 0.014), (0.011, 0.016), (0.011, 0.022)], IRON, seg=8)
    # хомут сошек на стволе
    b.lathe((gx, gy, gz + BIPOD_Z - 0.015), (0, 0, 1), [(0.022, 0.0), (0.022, 0.03)], IRON, seg=12)
    b.box((gx, gy - 0.028, gz + BIPOD_Z), (0.036, 0.016, 0.03), IRON, bevel=0.003)
    return b


def bipod_hinge():
    return (GUN[0], GUN[1] - 0.032, GUN[2] + BIPOD_Z)


def bipod():
    """Сошки: две ноги веером, в бинде сложены вперёд вдоль ствола."""
    b = Body()
    h = bipod_hinge()
    b.lathe(add(h, (-0.025, 0.0, 0.0)), (1, 0, 0), [(0.01, 0.0), (0.01, 0.05)], STEEL, seg=8)
    for s in (-1.0, 1.0):
        d = unit((s * 0.12, -0.08, 1.0))
        o = add(h, (s * 0.018, -0.004, 0.0))
        e = add(o, mul(d, 0.44))  # TAT_BIPOD_LEG в src/view/tat_move.cpp
        b.tube([o, add(o, mul(d, 0.24))], 0.009, IRON, seg=8)
        b.tube([add(o, mul(d, 0.23)), e], 0.006, STEEL, seg=6)
        b.lathe(add(o, mul(d, 0.23)), d, [(0.011, 0.0), (0.011, 0.016)], IRON, seg=8)
        b.lathe(e, d, [(0.012, 0.0), (0.012, 0.01), (0.006, 0.016)], RUBBER, seg=8)
    return b


# --- мачта (кость mast) и прибор (кость eye) ---

def mast():
    b = Body()
    mx, my, mz = MAST
    o = (mx, my + 0.03, mz)
    b.lathe((mx - 0.026, my + 0.03, mz), (1, 0, 0), [(0.02, 0.0), (0.02, 0.052)], IRON, seg=12)
    b.lathe(o, (0, 1, 0), [(0.02, 0.0), (0.02, 0.15), (0.016, 0.16), (0.014, MAST_H - 0.04)], IRON, seg=12)
    b.lathe(add(o, (0, 0.15, 0)), (0, 1, 0), [(0.022, 0.0), (0.022, 0.014)], STEEL, seg=12)
    b.tube([add(o, (0.02, 0.0, 0.0)), add(o, (0.024, 0.12, 0.0)), add(o, (0.018, MAST_H - 0.06, 0.0))], 0.005, RUBBER, seg=6)
    for y in (0.07, 0.2):
        b.box(add(o, (0.02, y, 0.0)), (0.012, 0.01, 0.012), STEEL, bevel=0.002)
    return b


def eye_at():
    return (MAST[0], MAST[1] + MAST_H, MAST[2])


def eye_head():
    """Прибор: плоская гранёная голова — щиток, три объектива (один светится), уши, разъём сзади."""
    b = Body()
    o = eye_at()
    b.lathe(add(o, (0, -0.03, 0)), (0, 1, 0), [(0.02, 0.0), (0.02, 0.03), (0.028, 0.034), (0.028, 0.042)], IRON, seg=12)
    c = add(o, (0.0, 0.045, 0.01))
    sec = lambda z, w, h: [(c[0] - w, c[1] - h, c[2] + z), (c[0] + w, c[1] - h, c[2] + z), (c[0] + w * 1.06, c[1] + h * 0.2, c[2] + z),
                           (c[0] + w * 0.8, c[1] + h, c[2] + z), (c[0] - w * 0.8, c[1] + h, c[2] + z), (c[0] - w * 1.06, c[1] + h * 0.2, c[2] + z)]
    b.loft([sec(-0.06, 0.07, 0.028), sec(0.03, 0.09, 0.034), sec(0.06, 0.085, 0.03)], PAINT)
    b.box(add(c, (0.0, 0.0, 0.062)), (0.15, 0.03, 0.006), GLASS, bevel=0.0)   # щиток
    for x, r, mat in ((-0.045, 0.016, LENS), (0.0, 0.011, GLASS), (0.042, 0.013, GLASS)):
        b.lathe(add(c, (x, -0.002, 0.06)), (0, 0, 1), [(r + 0.006, 0.0), (r + 0.006, 0.01), (r, 0.012)], IRON, seg=12)
        b.lathe(add(c, (x, -0.002, 0.068)), (0, 0, 1), [(r, 0.0), (r, 0.003), (r * 0.6, 0.006)], mat, seg=12)
    for s in (-1.0, 1.0):
        b.box(add(c, (s * 0.098, 0.0, 0.0)), (0.014, 0.04, 0.07), IRON, bevel=0.003)
        b.lathe(add(c, (s * 0.105, 0.005, -0.01)), (s, 0, 0), [(0.012, 0.0), (0.012, 0.012), (0.007, 0.016)], STEEL, seg=8)
    b.box(add(c, (0.0, 0.0, -0.07)), (0.06, 0.03, 0.014), IRON, bevel=0.003)
    return b


def antenna_base(x, z, lean):
    o = (x, HIP_Y + deck_y(z) + 0.05, z)
    return o, unit((0.03 * (1 if x > 0 else -1), 1.0, -lean))


def antenna(x, z, lean, length):
    o, ax = antenna_base(x, z, lean)
    b = Body()
    b.tube([o, add(o, mul(ax, length))], 0.005, IRON, seg=6, radii=lambda i, n: 0.005 if i == 0 else 0.003)
    if length < 0.3:   # короткий штырь с шаром
        b.lathe(add(o, mul(ax, length)), ax, [(0.008, 0.0), (0.012, 0.01), (0.008, 0.022)], IRON, seg=10)
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
    rig.bone("bipod", "gun", bipod_hinge())
    rig.bone("mast", "chassis", (MAST[0], MAST[1] + 0.03, MAST[2]))
    rig.bone("eye", "mast", eye_at())
    rig.add(hull_shell(), "chassis", wear=WEAR, edge=0.06, name="корпус")
    fit, marks = hull_fittings()
    rig.add(fit, "chassis", wear=WEAR, name="навесное")
    rig.add(marks, "chassis", name="знаки")
    rig.add(hull_skirts(), "chassis", wear=WEAR, edge=0.05, name="бортовые листы")
    rig.add(chassis_details(), "chassis", name="брюхо")
    tb, tf = turret_body()
    rig.add(tb, "turret", wear=WEAR, edge=0.05, name="башенка")
    rig.add(tf, "turret", name="башенка: железо")
    rig.add(rifle(), "gun", name="винтовка")
    rig.add(bipod(), "bipod", name="сошки")
    rig.add(mast(), "mast", name="мачта")
    rig.add(eye_head(), "eye", wear=WEAR, edge=0.05, name="прибор")
    for bone, (x, z), lean, length in ANTENNAS:
        o, ax = antenna_base(x, z, lean)
        rig.bone(bone, "chassis", o)
        rig.add(antenna(x, z, lean, length), bone, name=bone)
    for leg in LEGS:
        L, parts, pist = leg_parts(leg)
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
        if pist:
            pa, pb, cyl, rod = pist
            rig.bone("pa_" + n, "hip_" + n, w(pa))
            rig.bone("pb_" + n, "thigh_" + n, w(pb))
            rig.add(cyl.placed(m, o), "pa_" + n, name="pa " + n)
            rig.add(rod.placed(m, o), "pb_" + n, name="pb " + n)
    return rig
