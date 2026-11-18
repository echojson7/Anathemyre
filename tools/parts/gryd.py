"""«Гридь» — двуногий мех-стрелок со штурмовой винтовкой (docs/gryd.md): рецепт машины по референсу пользователя.

Облик (18.11): ноги с обратным коленом и гидравликой на каждом суставе, компактный плотный корпус с механикой по
бокам и энергоблоком сзади, голова-блок датчиков с антенной, две руки-манипулятора держат винтовку у правого
плеча. Свой дизайн в этом духе, не копия: светлая пыльно-серая краска щитков, тёмное железо механики.

Кости: chassis — таз (оси бёдер, подшипники рысканья ног, поворотный круг пояса); hull — корпус на поясе
(поворачивается к цели); gun — винтовка в упоре приклада; eye — голова на шее; ua/fa/hand — плечо, предплечье,
кисть каждой руки, ea/eb — цилиндр локтя; ноги — gryd_leg.py. Антенны: ant_l — на голове, ant_r — за спиной.

Оси сетки — как у Вежи: x вправо, y вверх, z вперёд, начало — на земле под серединой. Бинд-поза — «стоит,
винтовка у плеча стволом вперёд».
"""
import math

from kit import Body, add, mul, sub, unit
from rig import Rig
from shape import Frame, band, hexbolt, loft, pin, rbox, rcyl, rlathe, rloft, rpoly, seg_for
import gryd_arm as arm
from gryd_gun import BORE, GRIP_L, GRIP_R, LENGTH, LENS, flash, rifle
from gryd_leg import BRASS, GLASS, HIP_Y, HOSE, IRON, LEGS, MARK, PAINT, RUBBER, STEEL, WORN, leg_parts

LODS = (52000, 26000, 14000, 7000)
WEAR = (WORN, 0.016, 0.7, 0.03)
BELLY = 0.84                 # оси бёдер над землёй в покое (присела, колени вперёд)

WAIST = (0.0, HIP_Y + 0.19, -0.02)       # ось поворота корпуса
GUN = (0.10, HIP_Y + 0.46, 0.17)         # упор приклада (кость gun)
NECK = (-0.02, HIP_Y + 0.71, 0.05)       # ось поворота головы
SHOULDER = 0.335                         # плечи от оси, по x
SH_Y, SH_Z = HIP_Y + 0.555, 0.07
ANTENNAS = (("ant_l", "eye", (-0.08, HIP_Y + 0.91, -0.03), 0.42), ("ant_r", "hull", (-0.14, HIP_Y + 0.69, -0.2), 0.62))


def Y(dy):
    return HIP_Y + dy


# --- таз (кость chassis) ---

def pelvis():
    """Таз: мост между подшипниками рысканья ног, корпуса подшипников, поворотный круг пояса, гидростанция сзади."""
    b, pl, det = Body(), Body(), Body()
    rbox(b, (0.0, Y(0.12), -0.01), (0.34, 0.1, 0.2), 0.03, IRON)
    for leg in LEGS:
        x = leg.corner[0]
        rcyl(b, (x, Y(0.075), 0.0), (0, 1, 0), 0.078, 0.095, IRON, e=0.012)
        rcyl(b, (x, Y(0.165), 0.0), (0, 1, 0), 0.062, 0.022, IRON, e=0.006)
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 8
            hexbolt(det, (x + 0.068 * math.cos(a), Y(0.17), 0.068 * math.sin(a)), (0, 1, 0), 0.005, 0.005, IRON)
    rcyl(b, (0.0, Y(0.16), -0.02), (0, 1, 0), 0.13, 0.03, IRON, e=0.006)          # поворотный круг пояса
    for k in range(16):
        a = 2 * math.pi * (k + 0.5) / 16
        hexbolt(det, (0.118 * math.cos(a), Y(0.19), -0.02 + 0.118 * math.sin(a)), (0, 1, 0), 0.005, 0.005, IRON, washer=False)
    # щитки таза спереди и сзади в краске
    for sz, zc in ((1.0, 0.11), (-1.0, -0.13)):
        f = Frame.look((0.0, Y(0.17), zc), (0, -1.0, sz * 0.35), up=(0, 0, sz))
        rloft(pl, f, [(0.0, 0.13, 0.009, 0.007, 0.0), (0.09, 0.11, 0.009, 0.007, 0.0), (0.14, 0.07, 0.009, 0.007, 0.0)], PAINT, n=3, end=0.007)
        for sx in (-1.0, 1.0):
            hexbolt(det, f.at(0.03, 0.009, sx * 0.09), f.dir(0, 1, 0), 0.0055, 0.005, IRON)
    # гидростанция за тазом: бак-цилиндр поперёк, насос, фильтр, шланги к приводам бёдер
    rcyl(det, (-0.12, Y(0.1), -0.15), (1, 0, 0), 0.042, 0.24, IRON, e=0.008)
    for x in (-0.09, 0.09):
        rcyl(det, (x, Y(0.1), -0.15), (1, 0, 0), 0.046, 0.012, IRON, e=0.003)
    rbox(det, (0.0, Y(0.04), -0.13), (0.1, 0.06, 0.07), 0.014, IRON)
    rcyl(det, (0.07, Y(0.02), -0.16), (0, 1, 0), 0.022, 0.06, BRASS, e=0.004)
    for sx in (-1.0, 1.0):
        det.tube([(sx * 0.03, Y(0.04), -0.17), (sx * 0.08, Y(0.04), -0.2), (sx * 0.15, Y(0.11), -0.16), (sx * 0.16, Y(0.15), -0.1)],
                 0.009, HOSE, seg=12, fillet=0.03)
    return b, pl, det


# --- корпус (кость hull) ---

def _torso_section(t):
    """Сечение корпуса по высоте t (0 — пояс, 1 — плечи): полуширина, перед, зад, срез переднего угла."""
    def sm(a, b, x):
        x = min(max((x - a) / (b - a), 0.0), 1.0)
        return x * x * (3 - 2 * x)
    w = 0.16 + 0.10 * sm(0.0, 0.3, t) - 0.045 * sm(0.82, 1.0, t)
    zf = 0.06 + 0.14 * sm(0.0, 0.35, t) - 0.08 * sm(0.75, 1.0, t)
    zb = -0.15 - 0.08 * sm(0.0, 0.3, t) + 0.03 * sm(0.85, 1.0, t)
    c = 0.05 + 0.03 * sm(0.5, 1.0, t)
    return w, zf, zb, c


def torso_core():
    """Корпус одним телом: сечения по высоте (сужение к поясу, скат груди к шее), прорези жалюзи на груди, швы листов."""
    from solid import solid
    y0, y1 = Y(0.18), Y(0.70)
    rings = []
    N = 16
    for k in range(N + 1):
        t = k / N
        w, zf, zb, c = _torso_section(t)
        poly = [(w, zb + c), (w, zf - c), (w - c, zf), (-(w - c), zf), (-w, zf - c), (-w, zb + c), (-(w - c * 0.6), zb), (w - c * 0.6, zb)]
        ins = 0.0
        if k == 0 or k == N:
            ins = 0.012
        pts = rpoly([(x * (1 - ins / max(w, 0.01)), z) for x, z in poly], 0.03, 4)
        rings.append([(x, y0 + (y1 - y0) * t, z) for x, z in pts])
    # торцы скруглить: ещё по кольцу внутрь
    def shrink(ring, d, dy):
        cx = sum(p[0] for p in ring) / len(ring)
        cz = sum(p[2] for p in ring) / len(ring)
        return [(cx + (p[0] - cx) * (1 - d), p[1] + dy, cz + (p[2] - cz) * (1 - d)) for p in ring]
    rings = [shrink(rings[0], 0.12, 0.004)] + rings + [shrink(rings[-1], 0.12, -0.004)]
    base = Body()
    loft(base, rings, None)
    cuts = Body()
    # жалюзи на груди слева
    for k in range(5):
        rbox(cuts, (-0.1, Y(0.43) + 0.026 * k, 0.185), (0.11, 0.009, 0.05), 0.003, None)
    # швы листов поперёк: неглубокий пояс вокруг корпуса
    for yy in (Y(0.36), Y(0.58)):
        w, zf, zb, c = _torso_section((yy - y0) / (y1 - y0))
        poly = [(w, zb + c), (w, zf - c), (w - c, zf), (-(w - c), zf), (-w, zf - c), (-w, zb + c), (-(w - c * 0.6), zb), (w - c * 0.6, zb)]
        outer = rpoly([(x * 1.2, z * 1.2) for x, z in poly], 0.03, 4)
        inner = rpoly([(x - math.copysign(0.004, x), z - math.copysign(0.004, z)) for x, z in poly], 0.026, 4)
        band(cuts, outer, inner, yy - 0.0025, yy + 0.0025, None)
    t = solid([("add", base), ("cut", cuts)], bevel=0.003, bevel_angle=35.0, segments=2)
    out = Body()
    out.faces = [(p, PAINT, s) for p, _, s in t.faces]
    return out


def torso_parts():
    """Навесное на корпусе: гнёзда плеч с наплечниками, грудной лист справа (упор приклада), энергоблок сзади
    с рёбрами и выхлопами, магазины слева, поручни, шея, разъёмы, шланги к плечам."""
    plate, det, iron = Body(), Body(), Body()
    # гнёзда плеч: обойма, крышка с болтами, наплечник
    for sx in (-1.0, 1.0):
        o = (sx * 0.235, SH_Y, SH_Z)
        rcyl(iron, o, (sx, 0, 0), 0.09, 0.06, IRON, e=0.01)
        rcyl(iron, (sx * 0.29, SH_Y, SH_Z), (sx, 0, 0), 0.08, 0.008, IRON, e=0.003)
        for k in range(10):
            a = 2 * math.pi * (k + 0.5) / 10
            hexbolt(det, (sx * 0.297, SH_Y + 0.071 * math.cos(a), SH_Z + 0.071 * math.sin(a)), (sx, 0, 0), 0.005, 0.005, IRON, washer=False)
        # наплечник: изогнутый лист над гнездом по дуге
        rings = []
        for k in range(7):
            a = math.radians(-20 + 125 * k / 6)
            r0, r1 = 0.108, 0.121
            cy, cz = SH_Y + 0.0, SH_Z
            ring = []
            for x, rr in ((0.2, r0), (0.345, r0), (0.35, r1), (0.195, r1)):
                ring.append((sx * x, cy + rr * math.sin(a + 0.6), cz + rr * math.cos(a + 0.6)))
            rings.append(ring if sx > 0 else list(reversed(ring)))
        loft(plate, rings, PAINT, smooth=True)
    # грудной лист справа: упор приклада, рифлёная накладка
    f = Frame.look((0.06, Y(0.5), 0.19), (1, 0, 0), up=(0, 1, 0.3))
    rloft(plate, f, [(0.0, 0.012, 0.11, 0.008, 0.0), (0.14, 0.012, 0.1, 0.008, 0.0)], PAINT, n=3, end=0.008)
    for k in range(5):
        rbox(det, f.at(0.03 + 0.02 * k, 0.0, 0.016), (0.008, 0.16, 0.008), 0.003, RUBBER, rot=f.m)
    for x in (0.01, 0.13):
        for y in (-0.08, 0.08):
            hexbolt(det, f.at(x, y, 0.014), f.dir(0, 0, 1), 0.005, 0.005, IRON)
    # знаки: номер трафаретом на груди слева под жалюзи
    marks = Body()
    for dx, dy, w, h in ((-0.14, 0.0, 0.008, 0.04), (-0.122, 0.0, 0.008, 0.04), (-0.098, 0.0, 0.026, 0.008), (-0.098, 0.016, 0.026, 0.008),
                         (-0.098, -0.016, 0.026, 0.008), (-0.09, -0.008, 0.008, 0.016), (-0.106, 0.008, 0.008, 0.016)):
        rbox(marks, (dx, Y(0.33) + dy, 0.184), (w, h, 0.004), 0.0015, MARK, rot=((1, 0, 0), (0, 0.97, -0.24), (0, 0.24, 0.97)))
    # энергоблок сзади: короб, решётка рёбер, два выхлопа вверх, кожух
    eb = (0.0, Y(0.47), -0.31)
    rbox(iron, eb, (0.38, 0.32, 0.14), 0.025, IRON)
    for k in range(9):
        rbox(det, add(eb, (0.0, -0.12 + 0.03 * k, -0.07)), (0.28, 0.01, 0.02), 0.004, IRON)
    for sx in (-1.0, 1.0):
        rbox(plate, add(eb, (sx * 0.192, 0.0, 0.0)), (0.02, 0.3, 0.15), 0.008, PAINT)
        base = add(eb, (sx * 0.12, 0.17, 0.02))
        rlathe(det, base, (0, 1, 0), [(0.034, 0.0), (0.034, 0.02), (0.024, 0.03), (0.024, 0.16), (0.027, 0.17), (0.027, 0.19)], IRON)
        rcyl(det, add(base, (0.0, 0.05, 0.0)), (0, 1, 0), 0.032, 0.09, IRON, e=0.004)    # теплозащита
        for k in range(4):
            rcyl(det, add(base, (0.0, 0.06 + 0.022 * k, 0.0)), (0, 1, 0), 0.035, 0.006, IRON, e=0.002)
    rcyl(det, add(eb, (-0.12, -0.2, 0.0)), (1, 0, 0), 0.036, 0.24, IRON, e=0.008)       # аккумулятор под блоком
    for x in (-0.1, 0.1):
        rcyl(det, add(eb, (x, -0.2, 0.0)), (1, 0, 0), 0.04, 0.012, IRON, e=0.003)
    for sx in (-1.0, 1.0):   # ручки на блоке
        det.tube([add(eb, (sx * 0.09, 0.1, -0.07)), add(eb, (sx * 0.09, 0.1, -0.1)), add(eb, (sx * 0.09, -0.02, -0.1)),
                  add(eb, (sx * 0.09, -0.02, -0.07))], 0.007, STEEL, seg=10, fillet=0.015)
    # магазины в держателе на левом боку
    for k in range(2):
        z = -0.05 + 0.075 * k
        rbox(iron, (-0.285, Y(0.38), z), (0.05, 0.16, 0.06), 0.009, "gryd_gun")
        rbox(det, (-0.285, Y(0.3), z), (0.054, 0.02, 0.064), 0.006, "gryd_iron")
    rbox(det, (-0.264, Y(0.4), -0.012), (0.016, 0.05, 0.17), 0.004, IRON)
    rbox(det, (-0.29, Y(0.43), -0.012), (0.06, 0.012, 0.17), 0.004, IRON)
    # поручни на боках и сверху, шея, разъёмы
    for sx in (-1.0, 1.0):
        det.tube([(sx * 0.17, Y(0.665), -0.06), (sx * 0.17, Y(0.715), -0.06), (sx * 0.17, Y(0.715), -0.15), (sx * 0.17, Y(0.665), -0.15)],
                 0.007, STEEL, seg=10, fillet=0.015)
    rcyl(iron, (NECK[0], Y(0.67), NECK[2]), (0, 1, 0), 0.07, 0.03, IRON, e=0.008)
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        hexbolt(det, (NECK[0] + 0.058 * math.cos(a), Y(0.7), NECK[2] + 0.058 * math.sin(a)), (0, 1, 0), 0.0045, 0.004, IRON, washer=False)
    # шланги от энергоблока к плечам и к тазу
    for sx in (-1.0, 1.0):
        det.tube([add(eb, (sx * 0.16, 0.12, 0.06)), (sx * 0.2, Y(0.64), -0.12), (sx * 0.24, Y(0.66), 0.0), (sx * 0.26, SH_Y + 0.06, SH_Z - 0.02)],
                 0.009, HOSE, seg=12, fillet=0.05)
        det.tube([add(eb, (sx * 0.15, -0.14, 0.06)), (sx * 0.19, Y(0.26), -0.18), (sx * 0.17, Y(0.2), -0.1)], 0.008, HOSE, seg=12, fillet=0.04)
    # пояс: кольцо и гофра под корпусом
    rcyl(iron, (0.0, Y(0.175), -0.02), (0, 1, 0), 0.15, 0.03, IRON, e=0.008)
    prof = []
    for k in range(9):
        prof.append((0.125 if k % 2 == 0 else 0.138, 0.012 * k))
    rlathe(det, (0.0, Y(0.2), -0.02), (0, 1, 0), [(0.11, -0.004)] + prof + [(0.11, 0.1)], RUBBER)
    # правый бок: редуктор с рёбрами, два малых цилиндра, разъёмы
    gb = (0.28, Y(0.36), -0.04)
    rbox(iron, gb, (0.05, 0.13, 0.17), 0.016, IRON)
    for k in range(6):
        rbox(det, add(gb, (0.028, -0.05 + 0.02 * k, 0.0)), (0.012, 0.008, 0.15), 0.003, IRON)
    rcyl(det, add(gb, (0.03, 0.0, 0.06)), (1, 0, 0), 0.03, 0.02, IRON, e=0.004)
    for dz in (-0.05, 0.03):
        a, b2 = add(gb, (0.0, 0.08, dz)), (0.25, Y(0.52), dz - 0.01)
        rcyl(det, a, unit(sub(b2, a)), 0.012, 0.06, IRON, e=0.003)
        rcyl(det, add(a, mul(unit(sub(b2, a)), 0.06)), unit(sub(b2, a)), 0.006, 0.07, STEEL, e=0.002)
    # бронелисты на боках поверх корпуса
    for sx in (-1.0, 1.0):
        f = Frame.look((sx * 0.268, Y(0.5), -0.17), (0, 0, 1), up=(0, 1, 0))
        rloft(plate, f, [(0.0, 0.006, 0.07, 0.005, 0.0), (0.12, 0.006, 0.075, 0.005, 0.0), (0.2, 0.006, 0.06, 0.005, 0.0)], PAINT, n=3, end=0.006)
        for x in (0.02, 0.18):
            for y in (-0.05, 0.05):
                hexbolt(det, f.at(x, y, sx * 0.007), (sx, 0, 0), 0.005, 0.005, IRON)
    # брюшной блок под грудью: короб датчиков с объективом и решёткой
    bb = (-0.02, Y(0.25), 0.15)
    rbox(iron, bb, (0.2, 0.08, 0.08), 0.018, IRON)
    rcyl(det, add(bb, (0.06, 0.0, 0.03)), (0, 0, 1), 0.022, 0.025, IRON, e=0.004)
    rcyl(det, add(bb, (0.06, 0.0, 0.052)), (0, 0, 1), 0.016, 0.004, GLASS, e=0.001)
    for k in range(5):
        rbox(det, add(bb, (-0.06 + 0.018 * k, 0.0, 0.042)), (0.008, 0.05, 0.008), 0.003, IRON)
    # жгут кабелей по груди
    det.tube([(0.2, Y(0.62), 0.12), (0.12, Y(0.6), 0.2), (-0.05, Y(0.6), 0.205), (-0.2, Y(0.55), 0.16)], 0.008, HOSE, seg=12, fillet=0.05)
    det.tube([(0.2, Y(0.6), 0.12), (0.12, Y(0.585), 0.198), (-0.05, Y(0.585), 0.203), (-0.2, Y(0.535), 0.16)], 0.006, "gryd_mark", seg=10, fillet=0.05)
    for x in (0.1, -0.1):
        rbox(det, (x, Y(0.593), 0.205), (0.02, 0.03, 0.018), 0.004, IRON)
    return plate, det, iron, marks


# --- голова (кость eye) ---

def head():
    """Голова на шее-стойке: шарнир наклона, корпус датчиков со скатом, главный объектив в бленде, два малых,
    дальномер сбоку, козырёк, боковые датчики, основание антенны."""
    b, sh, gl, det = Body(), Body(), Body(), Body()
    # шея: стакан, шток, вилка наклона
    rcyl(b, (NECK[0], Y(0.67), NECK[2]), (0, 1, 0), 0.05, 0.06, IRON, e=0.008)
    rcyl(b, (NECK[0], Y(0.72), NECK[2]), (0, 1, 0), 0.032, 0.07, STEEL, e=0.004)
    for sx in (-1.0, 1.0):
        rbox(b, (NECK[0] + sx * 0.05, Y(0.8), NECK[2]), (0.016, 0.07, 0.06), 0.008, IRON)
    pin(b, (NECK[0], Y(0.81), NECK[2]), (1, 0, 0), 0.016, 0.065, STEEL)
    rbox(b, (NECK[0], Y(0.775), NECK[2]), (0.116, 0.02, 0.07), 0.008, IRON)
    hc = (NECK[0], Y(0.855), NECK[2] + 0.04)
    rloft(sh, Frame.look(hc, (0, 0, 1)), [(-0.13, 0.095, 0.048, 0.03, 0.006), (-0.05, 0.125, 0.062, 0.034, 0.0),
                                           (0.07, 0.13, 0.064, 0.034, 0.0), (0.13, 0.108, 0.05, 0.03, -0.01)], PAINT, n=4, end=0.016)
    rloft(b, Frame.look(add(hc, (0.0, -0.05, -0.02)), (0, 0, 1)), [(-0.09, 0.08, 0.02, 0.012, 0.0), (0.11, 0.09, 0.022, 0.014, 0.0)],
          IRON, n=3, end=0.008)
    # главный объектив справа: бленда, кольца, стекло, огонёк
    lc = add(hc, (0.045, -0.006, 0.15))
    rcyl(b, add(lc, (0, 0, -0.05)), (0, 0, 1), 0.05, 0.05, IRON, e=0.007)
    rlathe(b, add(lc, (0, 0, -0.002)), (0, 0, 1), [(0.044, 0.0), (0.048, 0.004), (0.048, 0.03), (0.052, 0.034), (0.052, 0.04),
                                                  (0.046, 0.044)], IRON)
    for k in range(3):
        rcyl(det, add(lc, (0, 0, -0.04 + 0.012 * k)), (0, 0, 1), 0.053, 0.005, IRON, e=0.0015)
    rlathe(gl, add(lc, (0, 0, 0.004)), (0, 0, 1), [(0.04, 0.0), (0.038, 0.008), (0.028, 0.016), (0.01, 0.02)], GLASS)
    rcyl(gl, add(lc, (0, 0, 0.0215)), (0, 0, 1), 0.007, 0.004, LENS)
    for dx, dy, r in ((-0.055, 0.018, 0.018), (-0.055, -0.024, 0.013), (-0.015, -0.03, 0.008)):
        p = add(hc, (dx, dy, 0.14))
        rcyl(b, add(p, (0, 0, -0.03)), (0, 0, 1), r + 0.007, 0.04, IRON, e=0.003)
        rcyl(gl, add(p, (0, 0, 0.009)), (0, 0, 1), r, 0.004, LENS if r < 0.014 else GLASS, e=0.001)
    # козырёк над объективами
    rloft(sh, Frame.look(add(hc, (0.0, 0.06, 0.1)), (0, -0.28, 1.0)), [(0.0, 0.118, 0.006, 0.005, 0.0), (0.085, 0.112, 0.006, 0.005, 0.0)],
          PAINT, n=3, end=0.006)
    # дальномер на левом боку, датчики-уши
    rcyl(det, add(hc, (-0.16, 0.0, -0.05)), (0, 0, 1), 0.024, 0.15, IRON, e=0.005)
    rcyl(gl, add(hc, (-0.16, 0.0, 0.1)), (0, 0, 1), 0.017, 0.004, GLASS, e=0.001)
    for z in (-0.02, 0.06):
        rbox(det, add(hc, (-0.142, 0.0, z)), (0.03, 0.02, 0.02), 0.005, IRON)
    p = add(hc, (0.13, 0.0, -0.02))
    rcyl(b, p, (1, 0, 0), 0.03, 0.03, IRON, e=0.006)
    rcyl(b, add(p, (0.03, 0.0, 0.0)), (1, 0, 0), 0.022, 0.012, IRON, e=0.003)
    # основание антенны сзади сверху, болты, шланг шеи
    rlathe(det, (-0.08, Y(0.905), -0.03), (0, 1, 0), [(0.017, 0.0), (0.017, 0.012), (0.011, 0.02), (0.012, 0.03), (0.008, 0.04)], IRON)
    for x in (-0.07, 0.03):
        for z in (-0.05, 0.1):
            hexbolt(det, (NECK[0] + x, Y(0.918), NECK[2] + 0.04 + z), (0, 1, 0), 0.0045, 0.004, IRON, washer=False)
    det.tube([(NECK[0] - 0.035, Y(0.70), NECK[2] - 0.04), (NECK[0] - 0.04, Y(0.75), NECK[2] - 0.07), (NECK[0] - 0.03, Y(0.82), NECK[2] - 0.08)],
             0.007, HOSE, seg=10, fillet=0.02)
    return b, sh, gl, det


def antenna(o, length, lean):
    """Антенна-хлыст: пружина у основания, тонкий прут, наконечник."""
    b = Body()
    rcyl(b, o, (0, 1, 0), 0.008, 0.035, STEEL, e=0.002)
    top = add(o, (lean * length, length, 0.0))
    b.tube([add(o, (0, 0.03, 0)), top], 0.0028, STEEL, seg=8)
    rcyl(b, top, unit((lean, 1.0, 0.0)), 0.005, 0.012, RUBBER, e=0.002)
    return b


# --- руки ---

def arm_points():
    """Плечо, локоть и запястье каждой руки в бинде: кисти на рукоятях, локти — вниз и наружу."""
    out = {}
    for side, name, grip, palm, axis, pole in ((1.0, "r", GRIP_R, (1.0, 0.0, 0.0), (0.0, -1.0, -0.32), (0.45, -0.4, -0.1)),
                                               (-1.0, "l", GRIP_L, (-1.0, 0.0, 0.0), (0.0, -1.0, 0.08), (-0.3, -0.5, -0.05))):
        S = (side * SHOULDER, SH_Y, SH_Z)
        gc = add(GUN, grip)
        _, W = arm.hand_frame(gc, axis, palm)
        E = arm.arm_ik(S, W, add(S, pole))
        out[name] = {"S": S, "E": E, "W": W, "grip": gc, "axis": axis, "palm": palm, "side": side, "pole": add(S, pole)}
    return out


def make():
    rig = Rig("gryd")
    rig.bone("root", None, (0.0, 0.0, 0.0))
    rig.bone("chassis", "root", (0.0, HIP_Y, 0.0))
    rig.bone("hull", "chassis", WAIST)
    rig.bone("gun", "hull", GUN)
    rig.bone("eye", "hull", NECK)
    pb, pp, pd = pelvis()
    rig.add(pb, "chassis", name="таз")
    rig.add(pp, "chassis", wear=WEAR, edge=0.05, name="щитки таза")
    rig.add(pd, "chassis", name="таз: навесное")
    rig.add(torso_core(), "hull", wear=WEAR, edge=0.05, name="корпус")
    plate, det, iron, marks = torso_parts()
    rig.add(plate, "hull", wear=WEAR, edge=0.05, name="корпус: щитки")
    rig.add(det, "hull", name="корпус: навесное")
    rig.add(iron, "hull", name="корпус: железо")
    rig.add(marks, "hull", name="знаки")
    for body, worn in rifle(GUN):
        rig.add(body, "gun", wear=WEAR if worn else None, edge=0.05 if worn else 0.12, name="винтовка")
    rig.bone("flash", "gun", (GUN[0], GUN[1] + BORE, GUN[2] + LENGTH))
    rig.add(flash(GUN), "flash", ao=False, name="вспышка")
    hb, hs, hg, hd = head()
    rig.add(hb, "eye", name="голова")
    rig.add(hs, "eye", wear=WEAR, edge=0.05, name="голова: кожух")
    rig.add(hg, "eye", name="голова: стёкла")
    rig.add(hd, "eye", name="голова: навесное")
    for bone, parent, o, length in ANTENNAS:
        rig.bone(bone, parent, o)
        rig.add(antenna(o, length, -0.06), bone, name=bone)
    # руки
    for n, A in arm_points().items():
        S, E, W, side = A["S"], A["E"], A["W"], A["side"]
        ea, eb = arm.cyl_points(S, E, W)
        rig.bone("ua_" + n, "hull", S)
        rig.bone("fa_" + n, "ua_" + n, E)
        rig.bone("hand_" + n, "fa_" + n, W)
        rig.bone("ea_" + n, "ua_" + n, ea)
        rig.bone("eb_" + n, "fa_" + n, eb)
        ub, us, ud = arm.upper_parts(S, E, W, side)
        rig.add(ub, "ua_" + n, name="плечо " + n)
        rig.add(us, "ua_" + n, wear=WEAR, edge=0.05, name="плечо: щиток " + n)
        rig.add(ud, "ua_" + n, name="плечо: навесное " + n)
        fb, fs, fd = arm.fore_parts(S, E, W, side)
        rig.add(fb, "fa_" + n, name="предплечье " + n)
        rig.add(fs, "fa_" + n, wear=WEAR, edge=0.05, name="предплечье: кожух " + n)
        rig.add(fd, "fa_" + n, name="предплечье: навесное " + n)
        kb, kd = arm.hand(A["grip"], A["axis"], A["palm"])
        rig.add(kb, "hand_" + n, name="кисть " + n)
        rig.add(kd, "hand_" + n, name="кисть: шарниры " + n)
        c, r = arm_cylinder(n)
        g, s, _ = __import__("gryd_leg").piston(ea, eb, c, r, rc=0.02, rr=0.01)
        rig.add(g, "ea_" + n, name="цилиндр локтя " + n)
        rig.add(s, "eb_" + n, name="шток локтя " + n)
    # ноги
    for leg in LEGS:
        L = leg.points()
        m = ((0.0, 0.0, -1.0), (0.0, 1.0, 0.0), (1.0, 0.0, 0.0))   # rot_y(−90°): +x рецепта → +z сетки
        o = (leg.corner[0], 0.0, leg.corner[1])

        def w(p):
            return (o[0] + m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2], p[1], o[2] + m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2])
        n = leg.name
        rig.bone("hip_" + n, "chassis", w((0.0, HIP_Y, 0.0)))
        rig.bone("thigh_" + n, "hip_" + n, w(L["P"]))
        rig.bone("shin_" + n, "thigh_" + n, w(L["K"]))
        rig.bone("tarsus_" + n, "shin_" + n, w(L["A"]))
        rig.bone("foot_" + n, "tarsus_" + n, w(L["F"]))
        rig.bone("ka_" + n, "thigh_" + n, w(L["ka"]))
        rig.bone("kb_" + n, "shin_" + n, w(L["kb"]))
        rig.bone("ma_" + n, "shin_" + n, w(L["ma"]))
        rig.bone("mb_" + n, "tarsus_" + n, w(L["mb"]))
        for bone, body, wear in leg_parts(leg):
            rig.add(body.placed(m, o), bone + "_" + n, wear=wear, edge=0.05 if wear else 0.12, name=bone + " " + n)
    return rig


def arm_cylinder(n):
    """Длина гильзы и штока цилиндра локтя по ходу руки (gryd_pose.ARM_RANGE): шток не выходит из гильзы."""
    import gryd_pose
    lo, hi = gryd_pose.elbow_range(n)
    c = lo - 0.03
    assert 2 * c >= hi + 0.03, ("ход цилиндра локтя", n, lo, hi)
    return c, c
