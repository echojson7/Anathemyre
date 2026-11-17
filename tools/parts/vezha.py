"""«Вежа» — шагающая турель (docs/vezha.md): рецепт машины. Литой гранёный корпус в пустынной краске на поворотном
кольце, тяжёлая железная рама с баками по бортам и насосной станцией на корме, четыре паучьи ноги (vezha_leg.py:
передние в бронещеках, задние длиннее и открытые), ствол, голова-прибор и антенны.

Оси сетки — как у людей: x вправо, y вверх, z вперёд (ствол смотрит в +z), начало — на земле под серединой.
Бинд-поза — «стоит». Ноги смотрят по диагоналям наружу, колени выше рамы; левая нога пары — зеркало правой.
Лёжа рама на брюхе, лапы на земле, колени торчат вверх.
"""
import math

from kit import Body, add, cross, mat_mul, mat_vec, mul, rot_x, rot_y, rot_z, sub, unit
from rig import Rig
from vezha_leg import (BRASS, CANVAS, DECK, EYE, GLASS, HIP_Y, IRON, MARK, OLIVE, PAINT, RUBBER, STEEL, WORN, bolt, leg_parts,
                       leg_points)

# --- размеры, м ---
BELLY = 0.21                   # бронеплита дна ниже осей бёдер
RING_Y = HIP_Y + 0.14          # разъём поворотного кольца: ниже — рама, выше — корпус
Y0 = HIP_Y + 0.19              # низ корпуса
TOP = Y0 + 0.44                # крыша
GUN_Y = Y0 + 0.19              # ось ствола и цапф
GUN_Z = 0.47                   # цапфы — на лобовой грани
EYE_AT = (0.0, TOP + 0.02, 0.185)   # ось головы-прибора на крыше
# Ноги: имя, вид (vezha_leg.LINKS), ось поворота (x, z) правой ноги пары и поворот рецепта вокруг y; левая — отражение
# правой через x = 0
LEGS = (("fr", "front", 0.40, 0.44, -50.0, False), ("fl", "front", 0.40, 0.44, -50.0, True),
        ("rl", "rear", 0.40, -0.44, 50.0, True), ("rr", "rear", 0.40, -0.44, 50.0, False))
ANTENNAS = (("ant_l", -1.0, 0.16, 0.92), ("ant_r", 1.0, 0.05, 0.62))   # кость, сторона, наклон назад, длина штыря

LODS = (45000, 26000, 16000)  # треугольников в LOD 1…3 (meshpack --prune --lods): первый держит ошибку ~2 см; ноги из
# сотен отдельных тел (болты, пальцы, хомуты) упрощаются ступенькой — ниже ~40 тыс. ошибка сразу растёт до 7–12 см

WEAR = (WORN, 0.035, 0.80, 0.05)       # краска: сколы у кромки на 3.5 см, редкие на плоскости


def frame(n, up=(0.0, 1.0, 0.0)):
    """Матрица (столбцы — вправо, вверх, наружу) для детали на грани с нормалью n."""
    n = unit(n)
    r = unit(cross(up, n))
    u = cross(n, r)
    return ((r[0], u[0], n[0]), (r[1], u[1], n[1]), (r[2], u[2], n[2]))


def sweep_rect(b, path, w, h, mat, up=(0.0, 1.0, 0.0)):
    """Прямоугольный короб w × h вдоль ломаной (лоток ленты, скоба)."""
    rings = []
    for i, p in enumerate(path):
        if i == 0:
            t = unit(sub(path[1], p))
        elif i == len(path) - 1:
            t = unit(sub(p, path[i - 1]))
        else:
            t = unit(add(unit(sub(path[i + 1], p)), unit(sub(p, path[i - 1]))))
        s = unit(cross(up, t))
        u = cross(t, s)
        rings.append([add(p, add(mul(s, sx * w / 2), mul(u, sy * h / 2))) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    b.loft(rings, mat)


# --- корпус ---

def oct_ring(y, s, zc=-0.01):
    """Восьмиугольник корпуса на высоте y, сжатый в s раз к оси."""
    hx, zb, xf, zf, xr, zr = 0.43, 0.19, 0.15, 0.47, 0.13, 0.50
    pts = [(hx, -zb), (hx, zb), (xf, zf), (-xf, zf), (-hx, zb), (-hx, -zb), (-xr, -zr), (xr, -zr)]
    return [(x * s, y, zc + (z - zc) * s) for x, z in pts]


FACES = {  # середина грани пояса и её нормаль наружу (на высоте Y0 + 0.15)
    "right": ((0.43, 0.0), (1.0, 0.0)), "left": ((-0.43, 0.0), (-1.0, 0.0)),
    "fr": ((0.29, 0.33), (0.7071, 0.7071)), "fl": ((-0.29, 0.33), (-0.7071, 0.7071)),
    "rr": ((0.28, -0.345), (0.7186, -0.6954)), "rl": ((-0.28, -0.345), (-0.7186, -0.6954)),
}


def hull_shell():
    """Литой корпус одним телом: пояс, скаты, корма, щёки цапф; вырезы — гнездо маски, жалюзи, швы люков."""
    from solid import solid
    base = Body()
    base.loft([oct_ring(Y0, 0.80), oct_ring(Y0 + 0.06, 1.0), oct_ring(Y0 + 0.24, 1.0), oct_ring(Y0 + 0.44, 0.60)], None)
    adds = Body()
    adds.box((0.0, Y0 + 0.16, -0.58), (0.42, 0.20, 0.22), None, bevel=0.0)               # корма: моторный короб
    for s in (-1.0, 1.0):
        adds.box((s * 0.185, GUN_Y, GUN_Z + 0.035), (0.07, 0.25, 0.17), None, bevel=0.0)  # щёки цапф
    cuts = Body()
    cuts.lathe((-0.15, GUN_Y, GUN_Z + 0.02), (1.0, 0.0, 0.0), [(0.135, 0.0), (0.135, 0.30)], None, seg=20)  # гнездо маски
    cuts.box((0.0, GUN_Y, GUN_Z + 0.12), (0.30, 0.30, 0.12), None, bevel=0.0)
    for k in range(5):                                                                    # жалюзи кормы
        cuts.box((0.0, Y0 + 0.095 + k * 0.034, -0.70), (0.32, 0.014, 0.05), None, bevel=0.0, rot=rot_x(-0.5))
    # швы съёмных листов: тонкие канавки
    g = 0.006
    for s in (-1.0, 1.0):
        x = s * 0.432
        for z in (-0.13, 0.13):
            cuts.box((x, Y0 + 0.15, z), (0.012, 0.15, g), None, bevel=0.0)
        for y in (Y0 + 0.075, Y0 + 0.225):
            cuts.box((x, y, 0.0), (0.012, g, 0.266), None, bevel=0.0)
    out = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.007, bevel_angle=28.0)
    out.faces = [(p, PAINT, s) for p, _, s in out.faces]
    return out


def hull_fittings():
    """Навесное корпуса: люк, перископ, глаз, фара, антенны, поручни, рымы, болты, короб ленты, ящик, выхлоп, знаки."""
    b = Body()
    top = TOP
    # люк: кольцо на болтах, крышка, ручка, петля
    hc = (0.0, top, -0.06)
    b.lathe(hc, (0, 1, 0), [(0.205, 0.0), (0.205, 0.022), (0.178, 0.022), (0.178, 0.012)], IRON, seg=24)
    b.lathe(add(hc, (0, 0.012, 0)), (0, 1, 0), [(0.172, 0.0), (0.172, 0.022), (0.15, 0.04), (0.06, 0.048)], PAINT, seg=24)
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        bolt(b, add(hc, (0.191 * math.cos(a), 0.022, 0.191 * math.sin(a))), (0, 1, 0))
    b.tube([add(hc, (-0.05, 0.05, 0.07)), add(hc, (-0.05, 0.085, 0.07)), add(hc, (0.05, 0.085, 0.07)),
            add(hc, (0.05, 0.05, 0.07))], 0.008, IRON, seg=6, fillet=0.015)
    b.box(add(hc, (0.0, 0.03, -0.19)), (0.10, 0.035, 0.05), IRON, bevel=0.006)
    b.lathe((EYE_AT[0], top, EYE_AT[2]), (0, 1, 0), [(0.062, 0.0), (0.062, 0.012), (0.05, 0.02)], IRON, seg=16)   # гнездо головы
    # глаз (прибор) на правой передней грани и фара на левой
    for name, mat_glass, r in (("fr", GLASS, 0.058), ("fl", GLASS, 0.07)):
        (cx, cz), (nx, nz) = FACES[name]
        n = (nx, 0.0, nz)
        o = (cx, Y0 + 0.155, cz)
        b.lathe(o, n, [(r + 0.02, 0.0), (r + 0.02, 0.02), (r + 0.012, 0.03), (r + 0.012, 0.085), (r + 0.004, 0.095)], IRON, seg=18)
        b.lathe(add(o, mul(n, 0.07)), n, [(r, 0.0), (r, 0.012), (r * 0.7, 0.02)], mat_glass, seg=18)
        m = frame(n)
        b.box(add(o, add(mul(n, 0.06), (0.0, r + 0.03, 0.0))), (2 * r + 0.07, 0.012, 0.11), PAINT, bevel=0.004, rot=m)  # козырёк
        if name == "fr":
            b.lathe(add(o, add(mul(n, 0.012), mat_vec(m, (r + 0.045, -0.02, 0.0)))), n, [(0.014, 0.0), (0.014, 0.02), (0.009, 0.026)], EYE, seg=10)
            for k in range(4):
                a = math.pi / 4 + k * math.pi / 2
                bolt(b, add(o, add(mul(n, 0.02), mat_vec(m, ((r + 0.011) * math.cos(a), (r + 0.011) * math.sin(a), 0.0)))), n, r=0.007)
    # основания антенн на задних скатах (штыри — свои кости)
    for _, side, lean, _len in ANTENNAS:
        o, ax = antenna_base(side, lean)
        b.lathe(o, ax, [(0.03, 0.0), (0.03, 0.02), (0.018, 0.03), (0.02, 0.045), (0.016, 0.06), (0.019, 0.075),
                        (0.015, 0.09), (0.01, 0.1)], IRON, seg=10)
    # поручни на боковых скатах
    for s in (-1.0, 1.0):
        x0, y0 = s * 0.375, Y0 + 0.31
        b.tube([(x0 - s * 0.03, y0 - 0.035, -0.15), (x0 + s * 0.012, y0 + 0.012, -0.15), (x0 + s * 0.012, y0 + 0.012, 0.15),
                (x0 - s * 0.03, y0 - 0.035, 0.15)], 0.011, IRON, seg=8, fillet=0.02)
    # рымы на скатах диагональных граней
    for name in ("fr", "fl", "rr", "rl"):
        (cx, cz), (nx, nz) = FACES[name]
        up = unit((nx * 0.75, 0.66, nz * 0.75))  # нормаль ската
        o = (cx * 0.80, Y0 + 0.345, -0.01 + (cz + 0.01) * 0.80)
        b.box(add(o, mul(up, 0.004)), (0.07, 0.05, 0.01), IRON, bevel=0.003, rot=frame(up, (nz, 0.0, -nx)))
        b.torus(add(o, mul(up, 0.034)), (nz, 0.0, -nx), 0.026, 0.008, IRON, seg=12, tseg=6)
    # болты по низу пояса
    for name, half in (("right", 0.16), ("left", 0.16), ("fr", 0.17), ("fl", 0.17), ("rr", 0.18), ("rl", 0.18)):
        (cx, cz), (nx, nz) = FACES[name]
        n, t = (nx, 0.0, nz), (-nz, 0.0, nx)
        cnt = 5
        for k in range(cnt):
            d = -half + 2 * half * k / (cnt - 1)
            bolt(b, add((cx, Y0 + 0.085, cz), add(mul(t, d), mul(n, -0.001))), n, r=0.0085, h=0.007)
    # короб ленты слева и лоток к казённику
    dc = (-0.432, Y0 + 0.15, -0.01)
    b.lathe(dc, (-1, 0, 0), [(0.05, 0.0), (0.158, 0.0), (0.158, 0.012), (0.15, 0.02), (0.15, 0.10), (0.158, 0.108),
                             (0.158, 0.12), (0.13, 0.128), (0.045, 0.128)], IRON, seg=22)
    b.lathe(add(dc, (-0.128, 0, 0)), (-1, 0, 0), [(0.04, 0.0), (0.04, 0.018), (0.022, 0.03)], STEEL, seg=10)
    for k in range(6):
        a = math.pi / 6 + k * math.pi / 3
        bolt(b, add(dc, (-0.128, 0.105 * math.cos(a), 0.105 * math.sin(a))), (-1, 0, 0), r=0.008)
    sweep_rect(b, [(-0.50, Y0 + 0.225, 0.075), (-0.487, Y0 + 0.235, 0.21), (-0.40, Y0 + 0.24, 0.345),
                   (-0.285, Y0 + 0.235, 0.455), (-0.235, GUN_Y + 0.04, 0.50)], 0.036, 0.085, IRON)
    # ящик справа: на двух полосах
    bc = (0.482, Y0 + 0.15, 0.0)
    b.box(bc, (0.095, 0.125, 0.25), PAINT, bevel=0.008)
    for z in (-0.075, 0.075):
        b.box(add(bc, (0.004, 0.0, z)), (0.105, 0.135, 0.022), IRON, bevel=0.003)
    b.box(add(bc, (0.05, 0.02, 0.0)), (0.012, 0.035, 0.05), STEEL, bevel=0.003)
    # выхлоп: два колена на корме
    for s in (-1.0, 1.0):
        x = s * 0.115
        b.tube([(x, Y0 + 0.24, -0.60), (x, Y0 + 0.31, -0.60), (x, Y0 + 0.36, -0.68)], 0.026, IRON, seg=10, fillet=0.03)
        b.lathe((x, Y0 + 0.255, -0.60), (0, 1, 0), [(0.036, 0.0), (0.036, 0.016), (0.028, 0.02)], IRON, seg=10)
    # знаки: глиф и счёт на задних гранях (тёмная краска, полоски 2 мм над листом)
    strokes = {"rl": [(-0.07, 0.0, 0.016, 0.13, 0.0), (0.0, 0.035, 0.11, 0.016, 0.0), (0.045, -0.02, 0.016, 0.09, 0.5),
                      (-0.02, -0.05, 0.07, 0.016, -0.4), (0.075, 0.045, 0.03, 0.03, 0.785)],
               "rr": [(-0.08 + 0.04 * k, 0.0, 0.016, 0.085, 0.0) for k in range(4)] + [(-0.02, 0.0, 0.2, 0.014, 0.35)]}
    for name, ss in strokes.items():
        (cx, cz), (nx, nz) = FACES[name]
        n = (nx, 0.0, nz)
        m = frame(n)
        for dx, dy, w, h, a in ss:
            o = add((cx, Y0 + 0.155, cz), add(mat_vec(m, (dx, dy, 0.0)), mul(n, 0.0016)))
            b.box(o, (w, h, 0.003), MARK, bevel=0.0, rot=mat_mul(m, rot_z(a)))
    return b


def hull_extras():
    """Мелочь корпуса: лента в окне лотка, накладные листы, дымовые мортирки, буксирный трос, скатка, смотровые
    блоки, проводка в трубках, заклёпки по поясу скатов."""
    b = Body()
    top = TOP
    # патроны ленты в открытом окне лотка (лоток — hull_fittings)
    path = [(-0.487, Y0 + 0.235, 0.21), (-0.40, Y0 + 0.24, 0.345), (-0.285, Y0 + 0.235, 0.455)]
    for seg in range(2):
        p0, p1 = path[seg], path[seg + 1]
        t = unit(sub(p1, p0))
        side = unit(cross((0.0, 1.0, 0.0), t))
        n = 5
        for k in range(n):
            c = add(add(p0, mul(sub(p1, p0), (k + 0.5) / n)), (0.0, 0.047, 0.0))
            b.lathe(add(c, mul(side, -0.016)), side, [(0.0062, 0.0), (0.0062, 0.024), (0.004, 0.03), (0.0035, 0.036)], BRASS, seg=6)
            b.box(add(c, (0.0, -0.004, 0.0)), (0.016, 0.004, 0.03), IRON, bevel=0.0, rot=frame(side, (0.0, 1.0, 0.0)))
    # накладные листы на бортах и передних гранях: краска, болты по углам
    for name, du, w, h in (("right", -0.02, 0.2, 0.1), ("fl", 0.0, 0.13, 0.05), ("rr", 0.0, 0.2, 0.05)):
        (cx, cz), (nx, nz) = FACES[name]
        n = (nx, 0.0, nz)
        m = frame(n)
        dy = 0.085 if name != "right" else 0.0
        o = add((cx, Y0 + 0.15 - dy, cz), add(mat_vec(m, (du, 0.0, 0.0)), mul(n, 0.004)))
        if name == "right":
            continue   # там ящик
        b.box(o, (w, h, 0.008), PAINT, bevel=0.003, rot=m)
        for su in (-1.0, 1.0):
            for sv in (-1.0, 1.0):
                bolt(b, add(o, add(mat_vec(m, (su * (w / 2 - 0.014), sv * (h / 2 - 0.012), 0.0)), mul(n, 0.004))), n, r=0.006, h=0.005)
    # дымовые мортирки: по три трубы на задних скатах
    for name, s in (("rl", -1.0), ("rr", 1.0)):
        (cx, cz), (nx, nz) = FACES[name]
        out = unit((nx, 0.55, nz))
        t = (-nz, 0.0, nx)
        base = (cx * 0.86, Y0 + 0.30, -0.01 + (cz + 0.01) * 0.86)
        b.box(add(base, mul(out, 0.012)), (0.16, 0.03, 0.05), IRON, bevel=0.004, rot=frame(unit((nx, 0.0, nz))))
        for k in (-1, 0, 1):
            ax = unit(add(out, mul(t, 0.22 * k)))
            o = add(base, add(mul(t, 0.05 * k), mul(out, 0.02)))
            b.lathe(o, ax, [(0.021, 0.0), (0.021, 0.085), (0.0235, 0.088), (0.0235, 0.1), (0.017, 0.1)], IRON, seg=10)
            b.lathe(add(o, mul(ax, 0.092)), ax, [(0.016, 0.0), (0.016, 0.006)], RUBBER, seg=10)
    # буксирный трос кольцами на левой задней грани
    (cx, cz), (nx, nz) = FACES["rl"]
    n = (nx, 0.0, nz)
    m = frame(n)
    o = add((cx, Y0 + 0.07, cz), mat_vec(m, (0.09, 0.0, 0.0)))
    for k in range(3):
        b.torus(add(o, mul(n, 0.014 + 0.017 * k)), n, 0.052 - 0.003 * k, 0.0085, STEEL, seg=16, tseg=6)
    for su in (-1.0, 1.0):
        b.box(add(o, add(mat_vec(m, (su * 0.052, 0.0, 0.0)), mul(n, 0.03))), (0.016, 0.03, 0.062), IRON, bevel=0.003, rot=m)
    # скатка брезента на корме: валик, два ремня
    rc = (0.0, Y0 + 0.285, -0.615)
    b.lathe(add(rc, (-0.2, 0, 0)), (1, 0, 0), [(0.03, 0.0), (0.05, 0.012), (0.052, 0.2), (0.05, 0.388), (0.03, 0.4)], CANVAS, seg=12)
    for x in (-0.11, 0.11):
        b.lathe(add(rc, (x - 0.011, 0, 0)), (1, 0, 0), [(0.055, 0.0), (0.055, 0.022)], RUBBER, seg=12)
        b.box(add(rc, (x, 0.056, 0.0)), (0.026, 0.008, 0.03), STEEL, bevel=0.002)
    # смотровые блоки на лобовом скате
    for sx in (-1.0, 1.0):
        o = (sx * 0.085, top - 0.055, 0.345)
        m = rot_x(-0.72)
        b.box(o, (0.075, 0.03, 0.045), IRON, bevel=0.005, rot=m)
        b.box(add(o, mat_vec(m, (0.0, 0.002, 0.024))), (0.058, 0.016, 0.004), GLASS, bevel=0.0, rot=m)
    # проводка в трубке по правому скату к фаре и по корме к антеннам
    b.tube([(0.33, top - 0.105, 0.2), (0.345, top - 0.12, 0.0), (0.33, top - 0.105, -0.2), (0.2, top - 0.03, -0.23)], 0.006, STEEL,
           seg=6, fillet=0.03)
    for z in (0.12, -0.12):
        b.box((0.343, top - 0.117, z), (0.02, 0.012, 0.02), IRON, bevel=0.003, rot=rot_z(-0.75))
    b.tube([(-0.19, top - 0.01, -0.2), (-0.1, top + 0.005, -0.26), (0.1, top + 0.005, -0.26), (0.19, top - 0.01, -0.2)], 0.006,
           RUBBER, seg=6, fillet=0.03)
    # заклёпки по верхнему ребру пояса: по четыре на грань
    for name, half in (("right", 0.15), ("left", 0.15), ("fr", 0.15), ("fl", 0.15), ("rr", 0.16), ("rl", 0.16)):
        (cx, cz), (nx, nz) = FACES[name]
        n, t = (nx, 0.0, nz), (-nz, 0.0, nx)
        for k in range(4):
            d = -half + 2 * half * k / 3
            bolt(b, add((cx, Y0 + 0.218, cz), add(mul(t, d), mul(n, -0.001))), n, r=0.006, h=0.005)
    return b


def slope_at(name, f, du=0.0):
    """Точка ската над гранью пояса name на доле f от пояса к крыше (du — вбок вдоль грани), нормаль и ось вбок."""
    (cx, cz), (nx, nz) = SLOPES[name]
    k = 1.0 - 0.4 * f
    n = unit((nx * 0.75, 0.66, nz * 0.75))
    t = (-nz, 0.0, nx)
    return add((cx * k, Y0 + 0.24 + 0.2 * f, -0.01 + (cz + 0.01) * k), mul(t, du)), n, t


SLOPES = dict(FACES, front=((0.0, 0.47), (0.0, 1.0)))


def hull_panels():
    """Второй слой корпуса: накладная броня на передних скатах, лючки на бортовых, отбойник на лобовом, лом в
    зажимах, знаки на щёках."""
    b, paint = Body(), Body()
    # накладные листы на передних диагональных скатах: на четырёх стойках с болтами, с зазором
    for name in ("fr", "fl"):
        o, n, t = slope_at(name, 0.2)
        m = tuple(zip(cross(t, n), t, n))   # столбцы: вдоль ската, вбок, наружу
        paint.box(add(o, mul(n, 0.016)), (0.07, 0.25, 0.008), PAINT, bevel=0.003, rot=m)
        for su in (-1.0, 1.0):
            for sv in (-1.0, 1.0):
                c = add(o, mat_vec(m, (su * 0.022, sv * 0.105, 0.0)))
                b.lathe(c, n, [(0.011, 0.0), (0.011, 0.012)], IRON, seg=6, smooth=False)
                bolt(b, add(c, mul(n, 0.02)), n, r=0.007, h=0.005)
    # лючки на бортовых скатах: крышка в краске, две петли, защёлка
    for name in ("right", "left"):
        o, n, t = slope_at(name, 0.74)
        up = unit(cross(t, n))
        up = up if up[1] > 0 else mul(up, -1.0)
        m = tuple(zip(t, up, n))
        paint.box(add(o, mul(n, 0.004)), (0.17, 0.06, 0.008), PAINT, bevel=0.003, rot=m)
        for d in (-0.055, 0.055):
            b.lathe(add(o, add(mul(up, 0.034), add(mul(t, d - 0.014), mul(n, 0.006)))), t, [(0.007, 0.0), (0.007, 0.028)], IRON, seg=6)
        b.box(add(o, add(mul(up, -0.03), mul(n, 0.008))), (0.03, 0.014, 0.012), STEEL, bevel=0.003, rot=m)
    # отбойник на лобовом скате: уголок перед смотровыми блоками, болты под ним
    o, n, t = slope_at("front", 0.34)
    up = unit(cross(t, n))
    up = up if up[1] > 0 else mul(up, -1.0)
    m = tuple(zip(t, up, n))
    for sx in (-1.0, 1.0):
        b.box(add(o, add(mul(t, sx * 0.062), mul(n, 0.012))), (0.13, 0.008, 0.026), IRON, bevel=0.002,
              rot=mat_mul(m, rot_z(sx * 0.22)))
    for k in range(4):
        c, _, _ = slope_at("front", 0.12, du=-0.105 + 0.07 * k)
        bolt(b, c, n, r=0.007, h=0.005)
    # лом в двух зажимах по низу левого ската
    o, n, t = slope_at("left", 0.1)
    b.tube([add(o, add(mul(t, -0.17), mul(n, 0.016))), add(o, add(mul(t, 0.15), mul(n, 0.016)))], 0.008, STEEL, seg=6)
    b.box(add(o, add(mul(t, 0.165), mul(n, 0.016))), (0.012, 0.03, 0.02), STEEL, bevel=0.003, rot=tuple(zip(n, t, cross(n, t))))
    for d in (-0.1, 0.08):
        b.box(add(o, add(mul(t, d), mul(n, 0.012))), (0.022, 0.03, 0.024), IRON, bevel=0.003, rot=tuple(zip(t, cross(n, t), n)))
    # знаки на щёках цапф: две косые полосы
    marks = Body()
    for s in (-1.0, 1.0):
        for k in range(2):
            marks.box((s * 0.2212, GUN_Y - 0.07 + 0.035 * k, GUN_Z + 0.035), (0.003, 0.016, 0.12), MARK, bevel=0.0,
                      rot=rot_x(s * 0.5))
    return b, paint, marks


def hull_ring():
    """Верхняя половина поворотного кольца (ходит с корпусом)."""
    b = Body()
    b.lathe((0.0, RING_Y + 0.003, 0.0), (0, 1, 0), [(0.25, 0.0), (0.345, 0.0), (0.345, 0.03), (0.325, 0.037), (0.25, 0.037)], IRON, seg=32)
    return b


# --- орудие (кость gun: начало в цапфах) ---

def gun():
    from solid import solid
    b = Body()
    c = (0.0, GUN_Y, GUN_Z + 0.02)
    # маска-барабан на цапфах
    b.lathe(add(c, (-0.142, 0, 0)), (1, 0, 0), [(0.04, 0.0), (0.105, 0.0), (0.122, 0.014), (0.122, 0.27), (0.105, 0.284),
                                               (0.04, 0.284)], IRON, seg=22)
    for s in (-1.0, 1.0):  # цапфы наружу сквозь щёки
        b.lathe(add(c, (s * 0.142, 0, 0)), (s, 0, 0), [(0.045, 0.0), (0.045, 0.085), (0.032, 0.092)], STEEL, seg=14)
    # люлька и казённая муфта
    b.lathe(add(c, (0, 0, 0.07)), (0, 0, 1), [(0.085, 0.0), (0.085, 0.05), (0.066, 0.07), (0.066, 0.2), (0.058, 0.215),
                                             (0.045, 0.22)], IRON, seg=16)
    for k in range(6):
        a = k * math.pi / 3
        bolt(b, add(c, (0.074 * math.cos(a), 0.074 * math.sin(a), 0.12)), (0, 0, 1), r=0.008)
    # ствол
    z0, z1 = c[2] + 0.2, c[2] + 1.10
    b.tube([(0, GUN_Y, z0), (0, GUN_Y, z1)], 0.027, STEEL, seg=12, radii=lambda i, n: 0.03 if i == 0 else 0.0245)
    # кожух с отверстиями
    jz0, jz1 = c[2] + 0.235, c[2] + 0.68
    jac = Body()
    jac.lathe((0, GUN_Y, jz0), (0, 0, 1), [(0.036, 0.0), (0.051, 0.0), (0.051, jz1 - jz0), (0.036, jz1 - jz0)], None, seg=16)
    holes = Body()
    rows = 6
    for i in range(rows):
        z = jz0 + 0.05 + i * (jz1 - jz0 - 0.1) / (rows - 1)
        for k in range(4):
            a = k * math.pi / 2 + (math.pi / 4 if i % 2 else 0.0)
            d = (math.cos(a), math.sin(a), 0.0)
            holes.lathe(add((0, GUN_Y, z), mul(d, 0.02)), d, [(0.0125, 0.0), (0.0125, 0.045)], None, seg=8)
    j = solid([("add", jac), ("cut", holes)], bevel=0.0)
    b.faces += [(p, IRON, True) for p, _, _ in j.faces]
    for z in (jz0 - 0.004, jz1 - 0.012):
        b.lathe((0, GUN_Y, z), (0, 0, 1), [(0.03, 0.0), (0.057, 0.0), (0.057, 0.016), (0.03, 0.016)], IRON, seg=16)
    # дульный тормоз: два окна
    mz = z1 - 0.01
    mb = Body()
    mb.lathe((0, GUN_Y, mz), (0, 0, 1), [(0.026, 0.0), (0.045, 0.012), (0.045, 0.15), (0.036, 0.165), (0.018, 0.165)], None, seg=14)
    mc = Body()
    for dz in (0.05, 0.11):
        mc.box((0, GUN_Y, mz + dz), (0.2, 0.034, 0.032), None, bevel=0.0)
    mc.lathe((0, GUN_Y, mz + 0.02), (0, 0, 1), [(0.0175, 0.0), (0.0175, 0.2)], None, seg=10)
    m = solid([("add", mb), ("cut", mc)], bevel=0.0)
    b.faces += [(p, IRON, True) for p, _, _ in m.faces]
    # хомут ствола перед кожухом, мушка на тормозе, рёбра охлаждения у муфты
    b.lathe((0, GUN_Y, jz1 + 0.07), (0, 0, 1), [(0.027, 0.0), (0.034, 0.004), (0.034, 0.02), (0.027, 0.024)], IRON, seg=12)
    b.box((0, GUN_Y + 0.052, mz + 0.03), (0.008, 0.022, 0.02), IRON, bevel=0.002)
    for k in range(5):
        b.lathe((0, GUN_Y, c[2] + 0.085 + 0.022 * k), (0, 0, 1), [(0.066, 0.0), (0.079, 0.002), (0.079, 0.008), (0.066, 0.01)], IRON, seg=16)
    b.tube([(0.075, GUN_Y + 0.08, c[2] + 0.03), (0.11, GUN_Y + 0.02, c[2] - 0.02), (0.125, GUN_Y - 0.03, c[2] - 0.06)], 0.006, RUBBER,
           seg=6, fillet=0.02)
    # откатник под стволом и прицел на маске
    b.tube([(0, GUN_Y - 0.078, c[2] + 0.09), (0, GUN_Y - 0.078, c[2] + 0.43)], 0.021, IRON, seg=10)
    b.tube([(0, GUN_Y - 0.078, c[2] + 0.43), (0, GUN_Y - 0.078, c[2] + 0.56)], 0.011, STEEL, seg=8)
    b.box((0, GUN_Y - 0.052, c[2] + 0.56), (0.03, 0.075, 0.022), IRON, bevel=0.004)
    b.box((0.075, GUN_Y + 0.105, c[2] + 0.07), (0.05, 0.05, 0.13), IRON, bevel=0.006)
    b.lathe((0.075, GUN_Y + 0.105, c[2] + 0.135), (0, 0, 1), [(0.019, 0.0), (0.019, 0.05), (0.024, 0.06), (0.024, 0.075)], IRON, seg=10)
    b.lathe((0.075, GUN_Y + 0.105, c[2] + 0.2), (0, 0, 1), [(0.018, 0.0), (0.018, 0.006)], GLASS, seg=10)
    return b


# --- рама (кость chassis): тяжёлый короб, по углам — уши палуб с барабанами ног, по бортам баки, сзади насосная ---

def leg_corner(name):
    """Ось поворота ноги name в осях сетки (x, z)."""
    for n, _kind, x, z, _deg, mirror in LEGS:
        if n == name:
            return (-x if mirror else x, z)
    raise KeyError(name)


def _plan(e):
    """План короба рамы, сжатый на e: у углов скошен к барабанам ног."""
    return [(0.30 - e, 0.30), (0.17 - e * 0.5, 0.50 - e), (-0.17 + e * 0.5, 0.50 - e), (-0.30 + e, 0.30), (-0.30 + e, -0.30),
            (-0.19 + e * 0.5, -0.52 + e), (0.19 - e * 0.5, -0.52 + e), (0.30 - e, -0.30)]


def _corner_axes(name):
    """Ось ноги name, направление от неё к коробу (n) и поперёк (t), в плане."""
    x, z = leg_corner(name)
    sx, sz = (1.0 if x > 0 else -1.0), (1.0 if z > 0 else -1.0)
    n = (-0.838 * sx, -0.545 * sz)
    return (x, z), n, (-n[1], n[0])


def _ear(b, name, y0, y1, r=0.115):
    """Палуба уха: круг у оси ноги и перемычка к коробу, толщиной от y0 до y1."""
    (x, z), n, t = _corner_axes(name)
    b.lathe((x, y0, z), (0, 1, 0), [(r, 0.0), (r, y1 - y0)], None, seg=24)
    q = [(x + t[0] * s * 0.105 + n[0] * d, z + t[1] * s * 0.105 + n[1] * d) for s, d in ((-1, 0.0), (1, 0.0), (1, 0.24), (-1, 0.24))]
    b.loft([[(px, y0, pz) for px, pz in q], [(px, y1, pz) for px, pz in q]], None)


def chassis():
    from solid import solid
    H = HIP_Y
    core = Body()
    core.loft([[(x, y, z) for x, z in _plan(e)] for y, e in ((H - 0.125, 0.03), (H - 0.097, 0.0), (H + 0.068, 0.0),
                                                              (H + 0.092, 0.018))], None)
    adds = Body()
    for name, *_ in LEGS:
        (x, z), n, t = _corner_axes(name)
        _ear(adds, name, H + DECK, H + DECK + 0.026)
        _ear(adds, name, H - DECK - 0.026, H - DECK)
        c = (x + n[0] * 0.135, z + n[1] * 0.135)   # стенка уха между палубами, за барабаном
        q = [(c[0] + t[0] * s * 0.11 + n[0] * d, c[1] + t[1] * s * 0.11 + n[1] * d) for s, d in ((-1, 0.0), (1, 0.0), (1, 0.03), (-1, 0.03))]
        adds.loft([[(px, H - DECK, pz) for px, pz in q], [(px, H + DECK, pz) for px, pz in q]], None)
    cuts = Body()
    for s in (-1.0, 1.0):   # ниши лючков на бортах короба (за баками видны снизу)
        cuts.box((s * 0.302, H - 0.02, 0.0), (0.02, 0.1, 0.2), None, bevel=0.0)
    out = solid([("add", core), ("add", adds), ("cut", cuts)], bevel=0.006, bevel_angle=30.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in out.faces]
    paint = Body()
    marks = Body()
    # уши: болты по краю палуб, мотор поворота с шестернёй к венцу барабана, клапанный блок и шланг к штуцеру ноги
    for name, *_ in LEGS:
        (x, z), n, t = _corner_axes(name)
        for y, s in ((H + DECK + 0.026, 1.0), (H - DECK - 0.026, -1.0)):
            for k in range(7):
                a = math.pi * (k + 0.5) / 7 + math.atan2(t[1], t[0])
                bolt(b, (x + 0.1 * math.cos(a), y, z + 0.1 * math.sin(a)), (0, s, 0), r=0.007, h=0.006)
        ca, sa = math.cos(0.95), math.sin(0.95)
        md = (n[0] * ca - n[1] * sa, n[0] * sa + n[1] * ca)   # от оси ноги к мотору
        mo = (x + md[0] * 0.128, z + md[1] * 0.128)
        b.lathe((mo[0], H + 0.046, mo[1]), (0, 1, 0), [(0.026, 0.0), (0.026, 0.024)], STEEL, seg=12)   # шестерня
        b.lathe((mo[0], H + DECK + 0.026, mo[1]), (0, 1, 0), [(0.036, 0.0), (0.036, 0.008), (0.03, 0.012), (0.03, 0.07),
                                                             (0.024, 0.078), (0.012, 0.08)], IRON, seg=14)
        for k in range(6):   # рёбра стакана мотора
            a = 2 * math.pi * k / 6
            b.box((mo[0] + 0.032 * math.cos(a), H + DECK + 0.062, mo[1] + 0.032 * math.sin(a)), (0.012, 0.05, 0.012), IRON,
                  bevel=0.0, rot=rot_y(-a))
        b.tube([(mo[0], H + DECK + 0.106, mo[1]), (mo[0] + n[0] * 0.05, H + DECK + 0.11, mo[1] + n[1] * 0.05),
                (x + n[0] * 0.2, H + 0.075, z + n[1] * 0.2)], 0.006, RUBBER, seg=6, fillet=0.02)
        vb = (x + n[0] * 0.19 - md[0] * 0.0 + t[0] * 0.0, z + n[1] * 0.19)
        mv = ((t[0], 0.0, n[0]), (0.0, 1.0, 0.0), (t[1], 0.0, n[1]))
        b.box((vb[0], H + DECK + 0.05, vb[1]), (0.1, 0.046, 0.06), IRON, bevel=0.006, rot=mv)
        for s in (-1.0, 1.0):
            o = (vb[0] + t[0] * s * 0.03, H + DECK + 0.075, vb[1] + t[1] * s * 0.03)
            b.lathe(o, (0, 1, 0), [(0.011, 0.0), (0.011, 0.012), (0.014, 0.014), (0.014, 0.024)], STEEL, seg=6, smooth=False)
        b.tube([(vb[0] + t[0] * 0.03, H + DECK + 0.098, vb[1] + t[1] * 0.03),
                (vb[0] + t[0] * 0.03 - n[0] * 0.06, H + DECK + 0.14, vb[1] + t[1] * 0.03 - n[1] * 0.06),
                (x + t[0] * 0.01, H + DECK + 0.13, z + t[1] * 0.01), (x, H + DECK + 0.106, z)], 0.009, RUBBER, seg=7, fillet=0.03)
        for s in (-1.0, 1.0):   # две магистрали от блока вниз к бортовой разводке
            b.tube([(vb[0] + t[0] * s * 0.03 + n[0] * 0.035, H + DECK + 0.05, vb[1] + t[1] * s * 0.03 + n[1] * 0.035),
                    (vb[0] + t[0] * s * 0.03 + n[0] * 0.06, H + DECK + 0.02, vb[1] + t[1] * s * 0.03 + n[1] * 0.06),
                    (x + n[0] * 0.27 + t[0] * s * 0.02, H + 0.05, z + n[1] * 0.27 + t[1] * s * 0.02)], 0.0075, RUBBER, seg=6,
                   fillet=0.02)
    # верх: нижняя половина поворотного кольца на болтах
    b.lathe((0.0, H + 0.092, 0.0), (0, 1, 0), [(0.2, 0.0), (0.36, 0.0), (0.36, 0.03), (0.345, 0.042), (0.2, 0.042)], IRON, seg=32)
    for k in range(16):
        a = 2 * math.pi * (k + 0.5) / 16
        bolt(b, (0.325 * math.cos(a), H + 0.092, 0.325 * math.sin(a)), (0, -1, 0), r=0.01)
    # бортовая разводка: по две магистрали вдоль верхних кромок короба от насосной к ушам, в скобах
    for s in (-1.0, 1.0):
        for k, dy in enumerate((0.0, 0.024)):
            b.tube([(s * 0.21, H + 0.05 + dy, -0.47), (s * 0.29, H + 0.05 + dy, -0.3 + 0.02 * k), (s * 0.29, H + 0.05 + dy, 0.26 - 0.02 * k),
                    (s * 0.2, H + 0.05 + dy, 0.44)], 0.0085, RUBBER, seg=6, fillet=0.05)
        for z in (-0.2, 0.0, 0.2):
            b.box((s * 0.296, H + 0.062, z), (0.018, 0.06, 0.03), STEEL, bevel=0.003)
    # слева — бак гидравлики (латунь) в двух лентах на ложементах; мерное стекло, заливная горловина, шильд
    tc = (-0.41, H - 0.015, 0.0)
    b.lathe(add(tc, (0.0, 0.0, -0.27)), (0, 0, 1), [(0.03, 0.0), (0.066, 0.008), (0.09, 0.025), (0.1, 0.045), (0.1, 0.495),
                                                  (0.09, 0.515), (0.066, 0.532), (0.03, 0.54)], BRASS, seg=24)
    for z in (-0.14, 0.14):
        b.lathe(add(tc, (0.0, 0.0, z - 0.012)), (0, 0, 1), [(0.104, 0.0), (0.104, 0.024)], IRON, seg=24)
        b.box(add(tc, (0.075, -0.07, z)), (0.07, 0.05, 0.024), IRON, bevel=0.004)
        b.box(add(tc, (0.0, 0.106, z)), (0.03, 0.012, 0.03), STEEL, bevel=0.002)
    b.box(add(tc, (-0.098, 0.0, 0.03)), (0.012, 0.06, 0.15), IRON, bevel=0.003)
    b.box(add(tc, (-0.104, 0.0, 0.03)), (0.004, 0.034, 0.12), GLASS, bevel=0.0)
    for dz in (-0.062, 0.062):
        bolt(b, add(tc, (-0.104, 0.0, 0.03 + dz)), (-1, 0, 0), r=0.006, h=0.005)
    b.lathe(add(tc, (0.0, 0.09, -0.07)), (0, 1, 0), [(0.03, 0.0), (0.03, 0.03), (0.036, 0.034), (0.036, 0.046), (0.02, 0.05)],
            STEEL, seg=12)
    b.box(add(tc, (-0.07, 0.07, 0.07)), (0.004, 0.05, 0.1), IRON, bevel=0.0, rot=rot_z(0.78))
    for k in range(3):
        marks.box(add(tc, (-0.0725, 0.0675 + 0.0 * k, 0.04 + 0.025 * k)), (0.002, 0.03, 0.014), MARK, bevel=0.0, rot=rot_z(0.78))
    b.tube([add(tc, (0.03, -0.09, -0.2)), add(tc, (0.08, -0.1, -0.32)), (-0.2, H - 0.06, -0.47)], 0.012, RUBBER, seg=8, fillet=0.04)
    # справа — топливный бак (олива) в лентах с пряжками, сверху скатка брезента в ремнях, внизу сливной кран
    fc = (0.405, H - 0.03, 0.0)
    b.lathe(add(fc, (0.0, 0.0, -0.235)), (0, 0, 1), [(0.04, 0.0), (0.075, 0.006), (0.085, 0.02), (0.085, 0.45), (0.075, 0.464),
                                                   (0.04, 0.47)], OLIVE, seg=22)
    for z in (-0.11, 0.11):
        b.lathe(add(fc, (0.0, 0.0, z - 0.011)), (0, 0, 1), [(0.088, 0.0), (0.088, 0.022)], STEEL, seg=22)
        b.box(add(fc, (0.09, 0.0, z)), (0.012, 0.03, 0.03), STEEL, bevel=0.003)
        b.box(add(fc, (-0.07, -0.06, z)), (0.06, 0.04, 0.024), IRON, bevel=0.004)
    b.lathe(add(fc, (0.0, 0.08, 0.15)), (0, 1, 0), [(0.026, 0.0), (0.026, 0.025), (0.032, 0.028), (0.032, 0.04)], IRON, seg=12)
    b.lathe(add(fc, (0.03, -0.083, -0.15)), (0, -1, 0), [(0.012, 0.0), (0.012, 0.03), (0.016, 0.034)], STEEL, seg=8)
    rc = add(fc, (-0.01, 0.125, 0.0))
    b.lathe(add(rc, (0.0, 0.0, -0.2)), (0, 0, 1), [(0.025, 0.0), (0.042, 0.012), (0.044, 0.2), (0.042, 0.388), (0.025, 0.4)], CANVAS,
            seg=14)
    for z in (-0.11, 0.11):
        b.lathe(add(rc, (0.0, 0.0, z - 0.011)), (0, 0, 1), [(0.047, 0.0), (0.047, 0.022)], RUBBER, seg=14)
    for k in range(3):   # знак на баке
        marks.box(add(fc, (0.0854, 0.01, -0.02 + 0.022 * k)), (0.002, 0.05, 0.012), MARK, bevel=0.0)
    # нос: бронелист в краске на стойках, буксирная проушина снизу, отражатели
    gl = [[(x, y, z) for x, z in ((-0.2 + e, 0.53 - d), (0.2 - e, 0.53 - d))] for y, d, e in ((H - 0.12, 0.0, 0.0), (H + 0.08, 0.06, 0.02))]
    ring = lambda dz: [add(gl[0][0], (0, 0, dz)), add(gl[0][1], (0, 0, dz)), add(gl[1][1], (0, 0, dz)), add(gl[1][0], (0, 0, dz))]
    paint.loft([ring(-0.012), ring(0.0)], PAINT)
    for x, y, zz in ((-0.15, H - 0.08, 0.523), (0.15, H - 0.08, 0.523), (-0.13, H + 0.05, 0.483), (0.13, H + 0.05, 0.483),
                     (0.0, H - 0.08, 0.523), (0.0, H + 0.05, 0.483)):
        bolt(paint, (x, y, zz), unit((0.0, 0.29, 1.0)), r=0.008, h=0.006)
    for s in (-1.0, 1.0):
        b.box((s * 0.17, H - 0.03, 0.52), (0.03, 0.03, 0.02), GLASS, bevel=0.004)
    for k in range(4):
        marks.box((-0.06 + 0.04 * k, H - 0.02, 0.5132 - 0.002), (0.018, 0.08, 0.002), MARK, bevel=0.0,
                  rot=mat_mul(rot_x(-0.29), rot_z(0.5)))
    b.box((0.0, H - 0.13, 0.515), (0.08, 0.04, 0.06), IRON, bevel=0.006)
    b.torus((0.0, H - 0.145, 0.555), (1.0, 0.0, 0.0), 0.03, 0.01, STEEL, seg=12, tseg=6)
    # корма: насосная станция — короб с радиатором, крышка в краске, насос с магистралями, глушитель и выхлоп
    pc = (0.0, H - 0.02, -0.6)
    b.box(pc, (0.4, 0.18, 0.16), IRON, bevel=0.008)
    paint.box(add(pc, (0.0, 0.098, 0.0)), (0.41, 0.018, 0.17), PAINT, bevel=0.005)
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            bolt(paint, add(pc, (sx * 0.18, 0.107, sz * 0.06)), (0, 1, 0), r=0.007, h=0.005)
    for i in range(13):
        b.box(add(pc, (-0.15 + i * 0.025, 0.0, -0.083)), (0.008, 0.14, 0.012), IRON, bevel=0.0)
    for s in (-1.0, 1.0):
        b.box(add(pc, (s * 0.165, 0.0, -0.086)), (0.025, 0.16, 0.012), IRON, bevel=0.003)
        for k in range(4):   # жалюзи боковых заборников
            b.box(add(pc, (s * 0.203, -0.05 + 0.033 * k, 0.0)), (0.008, 0.012, 0.11), IRON, bevel=0.0, rot=rot_z(s * 0.4))
    b.lathe(add(pc, (-0.12, -0.03, 0.08)), (0, 0, 1), [(0.05, 0.0), (0.055, 0.01), (0.055, 0.06), (0.04, 0.075), (0.02, 0.08)],
            IRON, seg=14)
    b.lathe((-0.13, H + 0.115, -0.6), (1, 0, 0), [(0.02, 0.0), (0.04, 0.012), (0.04, 0.2), (0.02, 0.212)], IRON, seg=14)
    for x in (-0.08, 0.02):
        b.lathe((x, H + 0.115, -0.6), (1, 0, 0), [(0.043, 0.0), (0.043, 0.016)], STEEL, seg=14)
    b.tube([(0.08, H + 0.115, -0.6), (0.14, H + 0.115, -0.6), (0.17, H + 0.13, -0.63), (0.17, H + 0.17, -0.68)], 0.02, IRON,
           seg=10, fillet=0.03)
    b.lathe((0.17, H + 0.165, -0.675), (unit((0.0, 0.7, -0.7))), [(0.026, 0.0), (0.026, 0.03), (0.022, 0.034)], IRON, seg=10)
    lamp = (-0.17, H + 0.03, -0.685)
    b.box(lamp, (0.05, 0.035, 0.02), IRON, bevel=0.004)
    b.box(add(lamp, (0.0, 0.0, -0.012)), (0.036, 0.022, 0.006), EYE, bevel=0.0)
    # дно: бронеплита на болтах, полозья, сливная пробка
    bp = [[(x, y, z) for x, z in ((0.26 - e, 0.42 - e), (-0.26 + e, 0.42 - e), (-0.26 + e, -0.44 + e), (0.26 - e, -0.44 + e))]
          for y, e in ((H - 0.125, 0.0), (H - 0.16, 0.03), (H - 0.19, 0.07))]
    b.loft(bp, IRON)
    for s in (-1.0, 1.0):
        b.box((s * 0.12, H - 0.198, -0.01), (0.03, 0.016, 0.7), STEEL, bevel=0.004)
        for z in (-0.28, -0.08, 0.12, 0.32):
            bolt(b, (s * 0.2, H - 0.19, z), (0, -1, 0), r=0.007, h=0.005)
    b.lathe((0.0, H - 0.19, -0.2), (0, -1, 0), [(0.02, 0.0), (0.02, 0.01), (0.012, 0.016)], STEEL, seg=6, smooth=False)
    return b, paint, marks


# --- голова-прибор (кость eye: ось y в EYE_AT) и антенны (кости ant_*) ---

def eye_head():
    b = Body()
    o = EYE_AT
    b.lathe(o, (0, 1, 0), [(0.034, 0.0), (0.034, 0.03), (0.044, 0.036), (0.044, 0.046)], IRON, seg=14)   # шея
    c = add(o, (0.0, 0.094, 0.012))
    b.box(c, (0.17, 0.092, 0.12), PAINT, bevel=0.01)
    b.box(add(c, (0.0, 0.052, 0.012)), (0.19, 0.012, 0.165), PAINT, bevel=0.004)                           # козырёк
    lens = add(c, (-0.036, 0.0, 0.06))
    b.lathe(lens, (0, 0, 1), [(0.04, 0.0), (0.04, 0.016), (0.034, 0.022)], IRON, seg=16)
    b.lathe(add(lens, (0, 0, 0.012)), (0, 0, 1), [(0.03, 0.0), (0.03, 0.006), (0.02, 0.012)], GLASS, seg=16)
    lamp = add(c, (0.046, 0.006, 0.06))
    b.lathe(lamp, (0, 0, 1), [(0.027, 0.0), (0.027, 0.012), (0.023, 0.016)], IRON, seg=12)
    b.lathe(add(lamp, (0, 0, 0.008)), (0, 0, 1), [(0.019, 0.0), (0.019, 0.007), (0.012, 0.012)], EYE, seg=12)
    b.box(add(c, (0.046, -0.03, 0.062)), (0.05, 0.012, 0.006), GLASS, bevel=0.0)                           # щель дальномера
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            bolt(b, add(c, (sx * 0.074, sy * 0.034, 0.06)), (0, 0, 1), r=0.006, h=0.005)
        b.lathe(add(c, (sx * 0.085, -0.005, -0.01)), (sx, 0, 0), [(0.024, 0.0), (0.024, 0.014), (0.016, 0.02)], IRON, seg=10)  # уши
    b.box(add(c, (0.0, -0.01, -0.068)), (0.09, 0.05, 0.02), IRON, bevel=0.004)                              # разъём сзади
    b.tube([add(c, (0.02, -0.02, -0.076)), add(c, (0.02, -0.06, -0.1)), add(o, (0.02, 0.012, -0.05))], 0.007, RUBBER, seg=6,
           fillet=0.02)
    return b


def antenna_base(side, lean):
    """Основание антенны на заднем скате и её ось."""
    return (side * 0.19, TOP - 0.01, -0.235), unit((side * 0.04, 1.0, -lean))


def antenna(side, lean, length):
    o, ax = antenna_base(side, lean)
    b = Body()
    b.tube([add(o, mul(ax, 0.1)), add(o, mul(ax, length))], 0.0065, IRON, seg=6,
           radii=lambda i, n: 0.0065 if i == 0 else 0.004)
    for k in range(3):
        b.torus(add(o, mul(ax, 0.105 + 0.014 * k)), ax, 0.009, 0.004, IRON, seg=8, tseg=5)
    return b


def make():
    """Собрать машину: Rig с костями и частями."""
    rig = Rig("vezha")
    rig.bone("root", None, (0.0, 0.0, 0.0))
    rig.bone("chassis", "root", (0.0, HIP_Y, 0.0))
    rig.bone("hull", "chassis", (0.0, RING_Y, 0.0))
    rig.bone("gun", "hull", (0.0, GUN_Y, GUN_Z + 0.02))
    rig.bone("eye", "hull", EYE_AT)
    frame_b, frame_paint, frame_marks = chassis()
    rig.add(frame_b, "chassis", edge=0.2, name="рама")
    rig.add(frame_paint, "chassis", wear=WEAR, edge=0.05, name="рама: краска")
    rig.add(frame_marks, "chassis", name="рама: знаки")
    rig.add(hull_shell(), "hull", wear=WEAR, edge=0.065, name="корпус")
    rig.add(hull_fittings(), "hull", wear=WEAR, name="навесное")
    rig.add(hull_extras(), "hull", wear=WEAR, edge=0.06, name="мелочь")
    panels, plates, marks = hull_panels()
    rig.add(panels, "hull", name="второй слой")
    rig.add(plates, "hull", wear=WEAR, edge=0.05, name="накладные листы")
    rig.add(marks, "hull", name="знаки щёк")
    rig.add(hull_ring(), "hull", name="кольцо")
    rig.add(gun(), "gun", name="орудие")
    rig.add(eye_head(), "eye", wear=WEAR, edge=0.05, name="голова")
    for bone, side, lean, length in ANTENNAS:
        o, ax = antenna_base(side, lean)
        rig.bone(bone, "hull", add(o, mul(ax, 0.1)))
        rig.add(antenna(side, lean, length), bone, name=bone)
    kinds = {}
    for kind in ("front", "rear"):
        L = leg_points(kind)
        parts, rams = leg_parts(L)
        heads = {"hip": (0.0, HIP_Y, 0.0), "thigh": L["P"], "shin": L["K"], "tarsus": L["A"], "foot": L["F"],
                 "pa": L["la"], "pb": L["lb"], "ka": L["ka"], "kb": L["kb"], "ma": L["ma"], "mb": L["mb"]}
        kinds[kind] = (parts, heads, rams)
    parent = {"hip": "chassis", "thigh": "hip", "shin": "thigh", "tarsus": "shin", "foot": "tarsus", "pa": "hip", "pb": "thigh",
              "ka": "thigh", "kb": "shin", "ma": "shin", "mb": "tarsus"}
    for name, kind, x, z, deg, mirror in LEGS:
        parts, heads, extra = kinds[kind]
        m = rot_y(math.radians(deg))
        o = (x, 0.0, z)
        flip = lambda p: (-p[0], p[1], p[2]) if mirror else p
        for bone, head in heads.items():
            rig.bone(bone + "_" + name, "chassis" if bone == "hip" else parent[bone] + "_" + name, flip(add(mat_vec(m, head), o)))
        for bone, bodies in list(parts.items()) + [(k, [(v, None)]) for k, v in extra.items()]:
            for body, wear in bodies:
                body = body.placed(m, o)
                if mirror:
                    body = mirrored(body)
                rig.add(body, bone + "_" + name, wear=wear, edge=0.08 if wear else 0.3, name=bone + " " + name)
    return rig


def mirrored(b):
    """Отражение тела через плоскость x = 0 (обход граней разворачивается, чтобы нормали смотрели наружу)."""
    out = Body()
    out.faces = [(tuple(reversed([(-p[0], p[1], p[2]) for p in pts])), m, s) for pts, m, s in b.faces]
    return out
