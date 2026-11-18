"""Детали Исподи (docs/ispod.md): трубы, проводка, перила, светильники, путь. Всё — рецепты kit.py: каждое
тело замкнуто, расстановка — правилами (хомут через полтора метра, крюк под каждым пролётом кабеля), не руками.
Стекло светильников светится само (материал вида glow), а свет на стены дают лампы b.light (огни 6е,
docs/lamps.md): их строки в заготовке пишет lights.py.
"""
import math

from ispod import (BED, COR_X, COR_Z1, EDGE, F, HALL, HALL_H, L1, L2, LEG_Z, N, PAV, RISE, RUN, SPRING, ST_X0, ST_X1,
                   ST_Z, T, TUN_N, TUN_R, TUN_S, TUN_X, Z1, Z2, Z2E, Z3, Z3E)
from kit import Body, add, mat_mul, mul, rnd, rot_x, rot_y, rot_z, sag, uniform, unit, cross

IRON, PAINT, GALV, RUBBER, WOOD, RAIL = "iron_cast", "steel_paint", "steel_galv", "rubber_black", "wood_tar", "rust_metal"
LAMP, RED = "lamp_glass", "lamp_red"
# Лампы (docs/lamps.md): поток, лм, и цвет, К. Старые и пыльные: трубка 36 Вт ~1800 лм, колба 60 Вт ~600 лм,
# колба 40 Вт в сетке тоннеля ~400 лм
TUBE_LM, TUBE_K = 1800.0, 4000.0
BULB_LM, BULB_K = 600.0, 2700.0
CAGE_LM, CAGE_K = 400.0, 2900.0
# Табло над порталом светит красным (docs/lamps.md, «Цветные лампы»): цвет и яркость — материала lamp_red
# (data/materials.txt: 1.0 0.07 0.03 × 1500 кд/м², яркость Y ≈ 397 кд/м²), поток — с лица 0.8 × 0.2 м:
# Φ = π · L · A ≈ 200 лм
SIGN_RGB = (1.0, 0.07, 0.03)
SIGN_LM = 200.0
SLOPE = RISE / RUN
ANG = math.atan2(RISE, RUN)


# --- рецепты мелочи ---

def hub(b, o, ax, r, mat=IRON):
    """Раструб чугунной трубы."""
    b.lathe(o, ax, [(r + 0.002, 0), (r + 0.024, 0.012), (r + 0.03, 0.03), (r + 0.03, 0.11), (r + 0.018, 0.135),
                    (r + 0.002, 0.15)], mat)


def flange(b, o, ax, r, mat, bolts=6):
    """Фланец с болтами."""
    b.lathe(o, ax, [(r + 0.04, 0), (r + 0.04, 0.022), (r + 0.006, 0.022), (r + 0.006, 0.03)], mat, seg=20)
    ax = unit(ax)
    up = (0.0, 1.0, 0.0) if abs(ax[1]) < 0.9 else (1.0, 0.0, 0.0)
    n = unit(cross(up, ax))
    t = cross(ax, n)
    for k in range(bolts):
        a = 2 * math.pi * k / bolts
        p = add(add(o, mul(add(mul(n, math.cos(a)), mul(t, math.sin(a))), r + 0.025)), mul(ax, 0.02))
        b.lathe(p, ax, [(0.009, 0), (0.009, 0.012)], GALV, seg=6)


def coupling(b, o, ax, r, mat, length=0.086):
    """Муфта на трубе."""
    b.lathe(o, ax, [(r + 0.002, 0), (r + 0.011, 0.006), (r + 0.011, length - 0.006), (r + 0.002, length)], mat, seg=14)


def bulb(b, o, down=(0.0, -1.0, 0.0)):
    """Лампа в патроне: патрон на потолке или стене, колба светится."""
    b.lathe(o, down, [(0.07, 0), (0.07, 0.03), (0.035, 0.05), (0.035, 0.08)], GALV, seg=14)
    b.lathe(add(o, mul(down, 0.08)), down, [(0.02, 0), (0.045, 0.03), (0.05, 0.06), (0.035, 0.1), (0.01, 0.115)], LAMP, seg=14)
    b.light(add(o, mul(down, 0.14)), (0.0, 0.0, 0.0), down, 0.5, BULB_LM, BULB_K, 0.05)


def tube_lamp(b, c, tilt=0.0, wall=-1.0):
    """Лампа дневного света на стене: короб вдоль z (tilt — наклон по маршу), трубка светится. wall — куда стена по x."""
    b.box(c, (0.07, 0.11, 1.25), GALV, bevel=0.006, rot=rot_x(-tilt))
    d = (0.0, math.sin(tilt) * 0.6, math.cos(tilt) * 0.6)
    o = add(c, (-wall * 0.055, 0.0, 0.0))
    b.tube([add(o, mul(d, -1.0)), add(o, d)], 0.017, LAMP, seg=8)
    b.light(o, d, (-wall, 0.0, 0.0), 0.1, TUBE_LM, TUBE_K, 0.017)
    for s in (-1.0, 1.0):
        b.box(add(o, mul(d, s * 1.02)), (0.05, 0.05, 0.03), GALV, bevel=0.004, rot=rot_x(-tilt))


def stair_y(z):
    """Высота ступеней лестницы над точкой z (линия носков)."""
    if z < ST_Z:
        return F
    if z < Z1:
        return F + (z - ST_Z) * SLOPE
    if z < Z2:
        return L1
    if z < Z2E:
        return L1 + (z - Z2) * SLOPE
    if z < Z3:
        return L2
    if z < Z3E:
        return L2 + (z - Z3) * SLOPE
    return T


# --- технический коридор (первый снимок) ---

def tech():
    b = Body()

    def c(x, along, h):
        """Точка коридора: x от оси, along — вдоль от начала сужения (как на снимке: 0 — у ближнего торца + 1.6), h — над полом."""
        return (COR_X + x, F + h, 2.6 + along)
    up, dn, fw, lt = (0.0, 1.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (-1.0, 0.0, 0.0)

    # 1. Чугунный стояк на уступе слева с косым отводом, хомут на деревянной подкладке
    b.tube([c(-0.80, 1.6, 3.1), c(-0.80, 1.6, 0.95)], 0.08, IRON, seg=16)
    b.tube([c(-0.80, -1.6, 2.92), c(-0.80, 0.8, 2.92), c(-0.80, 1.55, 2.35)], 0.08, IRON, seg=16, fillet=0.25)
    hub(b, c(-0.80, 1.6, 2.12), dn, 0.08)
    hub(b, c(-0.80, 1.6, 1.25), dn, 0.08)
    hub(b, c(-0.80, 0.25, 2.92), fw, 0.08)
    flange(b, c(-0.80, 1.6, 0.95), up, 0.08, IRON)
    b.lathe(c(-0.80, 1.6, 1.62), up, [(0.085, 0), (0.085, 0.045)], GALV, seg=16)
    b.box(c(-0.97, 1.6, 1.645), (0.26, 0.006, 0.04), GALV, bevel=0.001)
    b.box(c(-1.05, 1.6, 1.64), (0.10, 0.11, 0.14), WOOD, bevel=0.006)
    b.lathe(c(-0.80, -0.6, 2.92), fw, [(0.085, 0), (0.085, 0.04)], GALV, seg=16)
    b.tube([c(-0.80, -0.58, 3.0), c(-0.80, -0.58, 3.1)], 0.006, GALV, seg=6)

    # 2. Уголок вдоль левой стены на кронштейнах
    prof = [(-1.1, 2.50), (-1.03, 2.50), (-1.03, 2.508), (-1.092, 2.508), (-1.092, 2.58), (-1.1, 2.58)]
    b.prism([(COR_X + x, F + y) for x, y in prof], "z", 2.6 - 1.55, 2.6 + 2.95, GALV, plane=("x", "y"))
    for a in (-1.0, 0.2, 1.4, 2.6):
        b.box(c(-1.085, a, 2.46), (0.03, 0.09, 0.05), GALV, bevel=0.002)

    # 3. Труба под низким потолком: муфты и подвесы по всей длине коридора
    end = COR_Z1 - 2.6 + 0.1
    b.tube([c(-0.55, 2.3, 3.1), c(-0.55, 2.3, 2.42), c(-0.55, end, 2.42)], 0.045, PAINT, seg=14, fillet=0.12)
    a = 3.6
    while a < end - 0.5:
        coupling(b, c(-0.55, a, 2.42), fw, 0.045, PAINT)
        b.lathe(c(-0.55, a + 0.8, 2.42), fw, [(0.05, 0), (0.05, 0.022)], GALV, seg=14)
        b.tube([c(-0.55, a + 0.811, 2.47), c(-0.55, a + 0.811, 2.6)], 0.005, GALV, seg=6)
        a += 2.2

    # 4. Стойка-труба справа с оголовком и тонкой трубой до потолка
    b.tube([c(0.80, 2.2, 0.0), c(0.80, 2.2, 1.72)], 0.075, IRON, seg=16)
    flange(b, c(0.80, 2.2, 0.0), up, 0.075, IRON)
    b.lathe(c(0.80, 2.2, 1.66), up, [(0.078, 0), (0.10, 0.012), (0.10, 0.075), (0.085, 0.09), (0.04, 0.09)], IRON, seg=18)
    b.tube([c(0.80, 2.2, 1.74), c(0.80, 2.2, 3.1)], 0.036, IRON, seg=12)
    coupling(b, c(0.80, 2.2, 2.38), up, 0.036, IRON, 0.076)
    b.lathe(c(0.80, 2.2, 2.75), up, [(0.041, 0), (0.041, 0.03)], GALV, seg=12)
    b.box(c(0.95, 2.2, 2.765), (0.3, 0.005, 0.03), GALV, bevel=0.001)

    # 5. Тонкие трубы внизу справа с вентилем — узел повторяется дальше по коридору
    def valve_group(a0):
        b.tube([c(0.9, a0 + 0.7, 0.9), c(0.62, a0 + 0.7, 0.9), c(0.62, a0 + 0.7, 0.3), c(0.62, a0 + 3.1, 0.3),
                c(0.62, a0 + 3.1, 0.0)], 0.028, PAINT, seg=10, fillet=0.07)
        b.tube([c(0.9, a0, 1.15), c(0.72, a0, 1.15), c(0.72, a0 + 1.7, 1.15), c(0.72, a0 + 1.7, 0.62),
                c(0.9, a0 + 1.7, 0.62)], 0.022, PAINT, seg=10, fillet=0.06)
        for o, ax in ((c(0.62, a0 + 0.7, 0.62), up), (c(0.62, a0 + 1.5, 0.3), fw), (c(0.72, a0 + 0.8, 1.15), fw)):
            b.lathe(o, ax, [(0.03, 0), (0.037, 0.005), (0.037, 0.05), (0.03, 0.055)], PAINT, seg=6)
        b.lathe(c(0.62, a0 + 2.1, 0.3), fw, [(0.03, 0), (0.05, 0.02), (0.05, 0.08), (0.03, 0.10)], IRON, seg=12)
        b.tube([c(0.62, a0 + 2.15, 0.3), c(0.62, a0 + 2.15, 0.43)], 0.012, IRON, seg=8)
        b.lathe(c(0.62, a0 + 2.15, 0.43), up, [(0.055, 0), (0.055, 0.008)], IRON, seg=8)
        flange(b, c(0.9, a0 + 0.7, 0.9), lt, 0.028, PAINT, bolts=4)
    valve_group(3.5)
    valve_group(19.3)

    # 6. Три трубки проводки по правой стене, коробка, скобы
    for k in range(3):
        h = 2.2 + 0.06 * k
        b.tube([c(1.085, -1.55, h), c(1.085, 2.45 + 0.06 * k, h), c(1.085, 2.45 + 0.06 * k, 3.1)], 0.011, GALV, seg=8, fillet=0.08)
    for a in (-0.9, 0.0, 1.9):
        b.box(c(1.088, a, 2.26), (0.012, 0.2, 0.025), GALV, bevel=0.001)
    b.box(c(1.06, 0.9, 2.26), (0.08, 0.26, 0.2), GALV, bevel=0.006)
    b.box(c(1.018, 0.9, 2.26), (0.006, 0.23, 0.17), GALV, bevel=0.002)

    # 7. Гофра провисает справа
    b.tube(sag(c(1.0, 0.3, 3.05), c(1.02, 1.5, 1.95), 0.25, 90), 0.02, RUBBER, seg=8,
           radii=lambda i, n: 0.02 + (0.004 if i % 2 else 0.0))

    # 8. Кабели на крюках по левой стене узкой части
    a0 = 3.2
    k = 0
    while a0 + 1.7 < end:
        for j in range(3):
            h = 1.85 + 0.09 * j
            b.tube(sag(c(-0.875, a0, h), c(-0.875, a0 + 1.7, h), 0.03 + 0.02 * j + 0.02 * rnd(7, k * 3 + j), 6), 0.009, RUBBER, seg=6)
        b.box(c(-0.885, a0, 1.95), (0.03, 0.32, 0.03), GALV, bevel=0.002)
        a0 += 1.7
        k += 1
    b.box(c(-0.885, a0, 1.95), (0.03, 0.32, 0.03), GALV, bevel=0.002)

    # 9. Рамы из стоек через 3.2 м
    a = 5.0
    while a < end - 2.5:
        for x in (-0.84, 0.84):
            b.box(c(x, a, 1.3), (0.045, 2.6, 0.045), IRON, bevel=0.004)
        b.box(c(0.0, a, 2.28), (1.7, 0.045, 0.045), IRON, bevel=0.004)
        a += 3.2

    # 10. Труба по полу слева, лампы в патронах
    b.tube([c(-0.78, 3.3, 0.045), c(-0.78, 9.5, 0.045), c(-0.5, 9.9, 0.045)], 0.04, PAINT, seg=10, fillet=0.2)
    bulb(b, c(0.55, 0.9, 3.1))
    for a in (7.2, 14.6, 22.0, 29.4):
        bulb(b, c(0.0, a, 2.6))

    # 11. Щит на правой стене и второй стояк слева в глубине
    b.box(c(0.82, 16.6, 1.35), (0.16, 0.9, 0.6), PAINT, bevel=0.008)
    b.box(c(0.735, 16.6, 1.35), (0.012, 0.82, 0.52), PAINT, bevel=0.003)
    b.lathe(c(0.725, 16.42, 1.35), lt, [(0.012, 0), (0.012, 0.03)], GALV, seg=8)
    for k in range(4):
        b.tube([c(0.82, 16.6 - 0.21 + 0.14 * k, 1.8), c(0.82, 16.6 - 0.21 + 0.14 * k, 2.6)], 0.011, GALV, seg=8)
    b.tube([c(-0.74, 24.4, 2.6), c(-0.74, 24.4, 0.0)], 0.07, IRON, seg=16)
    hub(b, c(-0.74, 24.4, 1.9), dn, 0.07)
    hub(b, c(-0.74, 24.4, 0.75), dn, 0.07)
    flange(b, c(-0.74, 24.4, 0.0), up, 0.07, IRON)
    b.lathe(c(-0.74, 24.4, 1.3), up, [(0.075, 0), (0.075, 0.04)], GALV, seg=16)
    b.box(c(-0.83, 24.4, 1.32), (0.14, 0.006, 0.04), GALV, bevel=0.001)

    # 12. Мусор на полу справа: обломки досок
    for k in range(22):
        s = uniform(11, k, 0.03, 0.12)
        rot = mat_mul(rot_y(uniform(12, k, 0, 3)), rot_x(uniform(13, k, -0.3, 0.3)))
        b.box(c(uniform(14, k, 0.45, 0.8), uniform(15, k, 3.2, 13.0), s * 0.3), (s, s * 0.5, s * uniform(16, k, 1, 3)),
              WOOD, bevel=0.004, rot=rot)

    # 13. Ход к тоннелю: лампа, трубки по потолку, поручень у ступеней
    zl = (LEG_Z[0] + LEG_Z[1]) / 2
    bulb(b, (-3.2, F + 2.4, zl))
    for k in range(2):
        b.tube([(COR_X + 0.9, F + 2.3 - 0.07 * k, LEG_Z[1] - 0.03), (0.38, F + 2.3 - 0.07 * k, LEG_Z[1] - 0.03)], 0.011, GALV, seg=8)
    for x in (-5.0, -3.0, -1.0):
        b.box((x, F + 2.26, LEG_Z[1] - 0.012), (0.025, 0.2, 0.012), GALV, bevel=0.001)
    rail = [(-1.6, F + 0.95, LEG_Z[0] + 0.07), (-1.4, F + 0.95, LEG_Z[0] + 0.07), (0.1, BED + 0.95, LEG_Z[0] + 0.07),
            (0.5, BED + 0.95, LEG_Z[0] + 0.07)]
    b.tube(rail, 0.019, PAINT, seg=8, fillet=0.1)
    for x, y in ((-1.3, F + 0.88), (-0.4, F + 0.22), (0.35, BED + 0.9)):
        b.tube([(x, y, LEG_Z[0]), (x, y, LEG_Z[0] + 0.07), (x, y + 0.045, LEG_Z[0] + 0.07)], 0.007, GALV, seg=6, fillet=0.02)
    # Стальная дверь в тоннель, распахнута к стене хода
    door_frame(b, EDGE - 0.02, LEG_Z[0], LEG_Z[1], BED, BED + 2.2, axis="x")
    return b


def door_frame(b, at, lo, hi, y0, y1, axis):
    """Рама проёма из уголка: at — плоскость стены, lo..hi — проём вдоль стены, y0..y1 — по высоте."""
    w = 0.06
    for u in (lo + w / 2, hi - w / 2):
        size, p = ((0.05, y1 - y0, w), (at, (y0 + y1) / 2, u)) if axis == "x" else ((w, y1 - y0, 0.05), (u, (y0 + y1) / 2, at))
        b.box(p, size, PAINT, bevel=0.004)
    size, p = ((0.05, w, hi - lo), (at, y1 - w / 2, (lo + hi) / 2)) if axis == "x" else ((hi - lo, w, 0.05), ((lo + hi) / 2, y1 - w / 2, at))
    b.box(p, size, PAINT, bevel=0.004)


# --- зал платформы и лестница (второй снимок) ---

def hall():
    b = Body()
    hx0, hx1, hz0, hz1 = HALL

    # Поручень на западной стене вдоль всей лестницы, от платформы до выхода
    x = ST_X0 + 0.07
    hp = [(x, F + 0.95, ST_Z - 0.5), (x, F + 0.95 + RISE, ST_Z + 0.2), (x, L1 + 0.95, Z1), (x, L1 + 0.95, Z2 + 0.1),
          (x, L2 + 0.95, Z2E), (x, L2 + 0.95, Z3 + 0.1), (x, T + 0.95, Z3E), (x, T + 0.95, Z3E + 0.7)]
    b.tube(hp, 0.021, PAINT, seg=10, fillet=0.15)
    z = ST_Z + 0.6
    while z < Z3E + 0.5:
        y = stair_y(z) + 0.95 + (RISE * 0.3 if z < Z1 else 0.0)
        b.tube([(ST_X0, y - 0.05, z), (x, y - 0.05, z), (x, y - 0.012, z)], 0.007, GALV, seg=6, fillet=0.02)
        b.lathe((ST_X0, y - 0.05, z), (1.0, 0.0, 0.0), [(0.028, 0), (0.028, 0.005)], GALV, seg=10)
        z += 1.2
    # Второй поручень на восточной стене шахты (второй и третий марши)
    x = ST_X1 - 0.07
    hp = [(x, L1 + 0.95, Z2 - 0.3), (x, L1 + 0.95, Z2 + 0.1), (x, L2 + 0.95, Z2E), (x, L2 + 0.95, Z3 + 0.1),
          (x, T + 0.95, Z3E), (x, T + 0.95, Z3E + 0.7)]
    b.tube(hp, 0.021, PAINT, seg=10, fillet=0.15)
    z = Z2 + 0.5
    while z < Z3E + 0.5:
        y = stair_y(z) + 0.95
        b.tube([(ST_X1, y - 0.05, z), (x, y - 0.05, z), (x, y - 0.012, z)], 0.007, GALV, seg=6, fillet=0.02)
        z += 1.2

    # Ограждение первого марша со стороны зала: стойки, поручень, две тяги
    xs = ST_X1 - 0.12
    posts = [ST_Z + RUN * (0.5 + 2 * k) for k in range(9)]
    for z in posts:
        y = stair_y(z + RUN * 0.5)
        b.tube([(xs, y, z), (xs, y + 0.98, z)], 0.016, PAINT, seg=8)
        b.lathe((xs, y, z), (0.0, 1.0, 0.0), [(0.04, 0), (0.04, 0.006)], GALV, seg=10)
    for dy, r in ((0.98, 0.02), (0.62, 0.011), (0.3, 0.011)):
        b.tube([(xs, stair_y(posts[0] + RUN * 0.5) + dy, posts[0]), (xs, stair_y(posts[-1] + RUN * 0.5) + dy, posts[-1])], r, PAINT, seg=8)

    # Лампы дневного света на западной стене: зал, марши, площадки
    xl = hx0 + 0.04
    for z in (0.9, 3.3):
        tube_lamp(b, (xl, F + 2.55, z))
    for z in (5.4, 7.8):
        tube_lamp(b, (xl, stair_y(z) + 2.3, z), ANG)
    tube_lamp(b, (xl, L1 + 2.3, 10.2))
    for z in (12.6, 15.0):
        tube_lamp(b, (xl, stair_y(z) + 2.1, z), ANG)
    tube_lamp(b, (xl, L2 + 2.1, 17.2))
    for z in (19.6, 22.0):
        tube_lamp(b, (xl, stair_y(z) + 2.1, z), ANG)
    # Трубка проводки вдоль стены над лампами, коробки
    cd = [(xl - 0.025, F + 2.38, hz0 + 0.05), (xl - 0.025, F + 2.38, ST_Z - 0.4), (xl - 0.025, L1 + 2.13, Z1 - 0.4),
          (xl - 0.025, L1 + 2.13, Z2 + 0.1), (xl - 0.025, L2 + 1.93, Z2E), (xl - 0.025, L2 + 1.93, Z3 + 0.1),
          (xl - 0.025, T + 1.93, Z3E), (xl - 0.025, T + 1.93, PAV[3] - 0.4)]
    b.tube(cd, 0.011, GALV, seg=8, fillet=0.2)
    for z in (2.1, 6.6, 10.9, 16.6, 20.8):
        y = (F + 2.38) if z < ST_Z else stair_y(z) + (2.13 if z < Z2 else 1.93)
        b.box((xl - 0.015, y, z), (0.04, 0.09, 0.09), GALV, bevel=0.004)
    # Лампы на восточной стене зала
    for z in (3.0, 9.0, 15.0, 21.0, 27.0):
        tube_lamp(b, (hx1 - 0.04, F + 2.75, z), 0.0, wall=1.0)

    # Кабельный лоток под потолком на подвесах, кабели в нём
    yt = F + 4.30
    for xr in (0.55, 1.05):
        b.prism([(xr, yt), (xr + 0.004, yt), (xr + 0.004, yt + 0.08), (xr, yt + 0.08)], "z", hz0 + 0.05, hz1 - 0.05, GALV, plane=("x", "y"))
    for k in range(99):
        b.box((0.8, yt + 0.005, hz0 + 0.2 + 0.3 * k), (0.5, 0.012, 0.03), GALV, bevel=0)
    for k in range(12):
        z = hz0 + 1.5 + 2.4 * k
        top = F + 4.85 if any(abs(z - q) < 0.35 for q in (6.0, 12.0, 18.0, 24.0)) else F + HALL_H
        for xr in (0.53, 1.07):
            b.tube([(xr, yt, z), (xr, top, z)], 0.005, GALV, seg=6)
        b.box((0.8, yt - 0.008, z), (0.6, 0.012, 0.035), GALV, bevel=0)
    for k in range(5):
        xc = 0.63 + 0.085 * k
        pts = [(xc + uniform(21, k * 40 + j, -0.01, 0.01), yt + 0.025 + uniform(22, k * 40 + j, 0, 0.012), hz0 + 0.06 + 0.9 * j)
               for j in range(34)]
        b.tube(pts, 0.012 + 0.004 * (k % 2), RUBBER, seg=6)
    # Пучок кабелей уходит из лотка к порталу тоннеля
    for k in range(4):
        b.tube([(1.3 + 0.05 * k, yt + 0.06, 22.0), (2.4 + 0.05 * k, yt + 0.2, 25.0), (2.7 + 0.05 * k, yt + 0.2, 29.3),
                (2.7 + 0.05 * k, F + 3.2, hz1 - 0.02)], 0.014, RUBBER, seg=6, fillet=0.6)

    # Табло над порталом, табличка и дверь технического коридора
    b.box((TUN_X, F + 4.45, hz1 - 0.07), (0.9, 0.3, 0.12), GALV, bevel=0.01)
    b.box((TUN_X, F + 4.45, hz1 - 0.14), (0.8, 0.2, 0.02), RED, bevel=0)
    b.light((TUN_X, F + 4.45, hz1 - 0.17), (0.3, 0.0, 0.0), (0.0, 0.0, -1.0), 0.0, SIGN_LM, 0, 0.1, rgb=SIGN_RGB)
    b.box((hx0 + 0.01, F + 1.7, 1.15), (0.01, 0.42, 0.3), PAINT, bevel=0.002)
    door_frame(b, hx0 + 0.02, 1.6, 2.7, F, F + 2.1, axis="x")
    b.box((hx0 + 0.06, F + 1.03, 2.75 + 0.52), (0.045, 2.02, 1.04), PAINT, bevel=0.006)        # створка распахнута к стене
    for y in (0.3, 1.75):
        b.lathe((hx0 + 0.085, F + y, 2.73), (0.0, 1.0, 0.0), [(0.014, 0), (0.014, 0.12)], GALV, seg=8)
    b.tube([(hx0 + 0.085, F + 1.0, 3.6), (hx0 + 0.13, F + 1.0, 3.6), (hx0 + 0.13, F + 1.0, 3.72)], 0.009, GALV, seg=6, fillet=0.02)

    # Решётка у схода на путь
    zf = 28.52
    for xp in (0.32, 2.12):
        b.tube([(xp, F, zf), (xp, F + 1.1, zf)], 0.022, PAINT, seg=8)
    for y in (0.12, 1.07):
        b.tube([(0.32, F + y, zf), (2.12, F + y, zf)], 0.016, PAINT, seg=8)
    for k in range(1, 14):
        b.tube([(0.32 + 1.8 * k / 14, F + 0.12, zf), (0.32 + 1.8 * k / 14, F + 1.07, zf)], 0.006, PAINT, seg=6)
    # Лампа под козырьком оголовка
    bulb(b, ((ST_X0 + ST_X1) / 2, 2.45, PAV[3] - 0.6))
    return b


# --- путь и тоннель ---

def track():
    b = Body()
    z0, z1 = TUN_S + 0.5, TUN_N - 0.5
    # Рельсы: профиль вытяжкой; шпалы
    prof = [(-0.075, 0), (0.075, 0), (0.075, 0.012), (0.012, 0.03), (0.012, 0.125), (0.036, 0.135), (0.036, 0.17),
            (-0.036, 0.17), (-0.036, 0.135), (-0.012, 0.125), (-0.012, 0.03), (-0.075, 0.012)]
    for x in (TUN_X - 0.72, TUN_X + 0.72):
        b.prism([(x + u, BED + 0.16 + v) for u, v in prof], "z", z0, z1, RAIL, plane=("x", "y"))
    k = 0
    z = z0 + 0.3
    while z < z1:
        rot = rot_y(uniform(31, k, -0.02, 0.02))
        b.box((TUN_X + uniform(32, k, -0.03, 0.03), BED + 0.08, z), (2.6, 0.16, 0.24), WOOD, bevel=0.01, rot=rot)
        for x in (TUN_X - 0.72, TUN_X + 0.72):     # подкладки с костылями
            b.box((x, BED + 0.166, z), (0.3, 0.012, 0.16), RAIL, bevel=0)
            for sx, sz in ((-0.1, -0.05), (0.1, 0.05)):
                b.lathe((x + sx, BED + 0.17, z + sz), (0.0, 1.0, 0.0), [(0.012, 0), (0.012, 0.02)], RAIL, seg=4)
        z += 0.6
        k += 1

    # Кабели на кронштейнах по восточной стене зала и тоннеля
    xw = HALL[1]
    zs = []
    z = z0 + 0.4
    while z < z1:
        zs.append(z)
        z += 2.0
    for j in range(4):
        h = BED + 1.75 + 0.14 * j
        pts = []
        for i, z in enumerate(zs[:-1]):
            span = sag((xw - 0.09, h, z), (xw - 0.09, h, zs[i + 1]), 0.025 + 0.02 * rnd(33, i * 4 + j), 4)
            pts += span[:-1]
        pts.append((xw - 0.09, h, zs[-1]))
        b.tube(pts, 0.016 - 0.002 * (j % 2), RUBBER, seg=6)
    for z in zs:
        b.box((xw - 0.075, BED + 1.95, z), (0.15, 0.03, 0.04), GALV, bevel=0.003)
        b.box((xw - 0.012, BED + 1.9, z), (0.024, 0.75, 0.05), GALV, bevel=0.003)
        b.box((xw - 0.075, BED + 1.67, z), (0.15, 0.03, 0.04), GALV, bevel=0.003)

    # Лампы тоннеля на западной стене и водоотводная труба вдоль пути
    xl = EDGE
    for z in [33.0 + 6.0 * i for i in range(6)] + [-4.5, -10.5]:
        b.box((xl + 0.03, BED + 2.6, z), (0.06, 0.16, 0.16), GALV, bevel=0.006)
        b.lathe((xl + 0.06, BED + 2.6, z), (1.0, 0.0, 0.0), [(0.03, 0), (0.05, 0.03), (0.05, 0.1), (0.03, 0.13)], LAMP, seg=10)
        b.light((xl + 0.13, BED + 2.6, z), (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 0.1, CAGE_LM, CAGE_K, 0.05)
        for a in range(4):       # решётка лампы
            t = math.pi / 4 + math.pi / 2 * a
            dy, dz = 0.062 * math.cos(t), 0.062 * math.sin(t)
            b.tube([(xl + 0.06, BED + 2.6 + dy, z + dz), (xl + 0.2, BED + 2.6 + dy, z + dz)], 0.003, GALV, seg=4)
        b.lathe((xl + 0.2, BED + 2.6, z), (1.0, 0.0, 0.0), [(0.066, 0), (0.066, 0.006)], GALV, seg=10)
    b.tube([(xl + 0.14, BED + 0.09, HALL[3] + 0.3), (xl + 0.14, BED + 0.09, z1 - 0.2)], 0.06, IRON, seg=12)
    z = HALL[3] + 2.0
    while z < z1 - 1:
        hub(b, (xl + 0.14, BED + 0.09, z), (0.0, 0.0, 1.0), 0.06)
        z += 3.0

    # Затворы в концах тоннеля: стальной щит с рёбрами и штурвалом
    for zg, face in ((TUN_N - 0.09, -1.0), (TUN_S + 0.09, 1.0)):
        b.box((TUN_X, BED + 2.3, zg), (2 * TUN_R - 0.1, 4.6, 0.14), PAINT, bevel=0.012)
        zf = zg + face * 0.1
        for y in (0.5, 1.6, 2.7, 3.8):
            b.box((TUN_X, BED + y, zf), (2 * TUN_R - 0.3, 0.14, 0.08), PAINT, bevel=0.008)
        for dx in (-1.6, 0.0, 1.6):
            b.box((TUN_X + dx, BED + 2.3, zf - face * 0.012), (0.12, 4.4, 0.05), PAINT, bevel=0.008)
        b.lathe((TUN_X + 0.8, BED + 2.15, zf), (0.0, 0.0, face), [(0.05, 0), (0.05, 0.14)], IRON, seg=10)
        wheel = (TUN_X + 0.8, BED + 2.15, zf + face * 0.14)
        for a in range(4):
            t = math.pi / 4 * a
            d = (0.24 * math.cos(t), 0.24 * math.sin(t), 0.0)
            b.tube([add(wheel, mul(d, -1.0)), add(wheel, d)], 0.012, IRON, seg=6)
        ring = [add(wheel, (0.25 * math.cos(2 * math.pi * i / 20), 0.25 * math.sin(2 * math.pi * i / 20), 0.0)) for i in range(21)]
        for i in range(20):      # обод штурвала отрезками (замкнутая петля трубой не делается)
            b.tube([ring[i], ring[i + 1]], 0.016, IRON, seg=6)
        for k in range(10):       # болты по краю
            b.lathe((TUN_X - 2.3 + 4.6 * (k % 5) / 4, BED + (0.25 if k < 5 else 4.35), zf - face * 0.03), (0.0, 0.0, face),
                    [(0.022, 0), (0.022, 0.03)], GALV, seg=6)
    return b
