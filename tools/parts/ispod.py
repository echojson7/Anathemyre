"""Исподь — подземная станция с выходом на поверхность (docs/ispod.md): оголовок входа, лестница в три марша,
платформа с путём и тоннелем, технический коридор от платформы до тоннеля.

Весь бетон — одно замкнутое тело: глыба, из которой вырезаны пустоты, плюс лестница, минус сколы (solid.py).
Здесь раскладка бетона и коллайдер, мелкие детали (трубы, кабели, перила, рельсы) — в ispod_props.py.

Оси сеток: x — восток, z — север, y — вверх от земли (0 — площадка, `prefab ispod` в data/scene.txt).
"""
import math

from kit import Body, rnd, seg_for, uniform
from plan import Box, Prism

F = -9.0                      # пол платформы и коридора
T = 0.04                      # верх лестницы и отмостки: чуть выше земли, чтобы земля не спорила с бетоном
N = 18                        # ступеней в марше
RUN = 0.30                    # проступь
RISE = (T - F) / (3 * N)      # подступёнок (~16.7 см)
L1, L2 = F + N * RISE, F + 2 * N * RISE   # площадки между маршами
HEAD = 2.5                    # просвет над ступенями по вертикали
BED = F - 1.1                 # путь: дно лотка и тоннеля
HALL = (-4.0, 7.3, 0.0, 30.0)  # зал платформы: x от, до, z от, до
HALL_H = 5.5
EDGE = 2.2                    # край платформы
TUN_X, TUN_R = 4.75, 2.55     # ось и радиус свода тоннеля
SPRING = F + 1.35             # пята свода
TUN_N, TUN_S = 66.0, -14.0    # концы тоннеля
ST_X0, ST_X1 = -4.0, -2.2     # лестница у западной стены зала
ST_Z = 4.0                    # подножие первого марша
Z1, Z2, Z3 = ST_Z + N * RUN, 11.0, 18.0   # конец первого марша, начала второго и третьего
Z2E, Z3E = Z2 + N * RUN, Z3 + N * RUN      # концы второго и третьего
PAV = (-4.45, -1.75, 13.5, 24.3)           # оголовок: x от, до, z от, до
COR_X = -7.3                  # ось технического коридора
COR_Z0, COR_ZN, COR_Z1 = 1.0, 5.6, 38.4   # коридор: начало, конец высокой части, конец
LEG_Z = (37.0, 38.4)          # ход от коридора к тоннелю
HOLE = (-4.2, -2.0, 18.4, 24.28)           # проём земли над лестницей (штамп hole в data/terrain.txt)


def flight(z0, y0, n=N):
    """Ломаная марша в плоскости (z, y): подступёнок, проступь, …"""
    out = []
    for k in range(n):
        out += [(z0 + RUN * k, y0 + RISE * k), (z0 + RUN * k, y0 + RISE * (k + 1))]
    out.append((z0 + RUN * n, y0 + RISE * n))
    return out


def steps_down(x0, n=6):
    """Ступени вниз на восток от пола F до пути BED в плоскости (x, y): первая грань — подступёнок у x0."""
    r = (F - BED) / n
    out = []
    for k in range(n):
        out += [(x0 + RUN * k, F - r * k), (x0 + RUN * k, F - r * (k + 1))]
    return out


def arch(r, y_low):
    """Свод тоннеля в плоскости (x, y): полукруг над пятой и полоса до y_low под ней."""
    pts = [(TUN_X + r, y_low)]
    n = seg_for(r, math.pi)  # полукруг по допуску хорды (docs/parts.md, «Гладкость»)
    for k in range(n + 1):
        a = math.pi * k / n
        pts.append((TUN_X + r * math.cos(a), SPRING + r * math.sin(a)))
    pts.append((TUN_X - r, y_low))
    return pts


def ceil_y(z):
    """Потолок лестничной шахты над вторым и третьим маршами."""
    k = RISE / RUN
    if z <= Z2E:
        return (F + HALL_H) + (z - Z2) * (L2 + HEAD - (F + HALL_H)) / (Z2E - Z2)
    if z <= Z3:
        return L2 + HEAD
    return min(L2 + HEAD + (z - Z3) * k, 2.45)


def concrete_ops():
    """Операции бетона по порядку (без сколов): (операция, тело[, тело для коллайдера или False — не участвует])."""
    hx0, hx1, hz0, hz1 = HALL
    k = RISE / RUN
    z_flat = Z3 + (2.45 - (L2 + HEAD)) / k      # где потолок шахты выходит под козырёк
    ops = [
        ("add", Box(-10.2, 9.3, -12.1, -1.5, -16.0, 68.0)),
        # Оголовок: низкая плита над вторым маршем, наклонная крыша над третьим, козырёк над входом
        ("add", Prism("x", PAV[0], PAV[1], [(PAV[2], -1.6), (PAV[2], 0.45), (18.6, 0.45),
                                            (18.6 + (3.0 - 0.45) / k, 3.0), (PAV[3], 3.0), (PAV[3], -1.6)])),
        ("add", Box(-5.0, -1.2, -1.6, T, PAV[3] - 0.05, 26.6)),                        # отмостка у входа
        # Пустоты
        ("cut", Box(hx0, hx1, F, F + HALL_H, hz0, hz1)),                                # зал
        ("cut", Box(EDGE, hx1, BED, F + 0.1, hz0, hz1)),                                # лоток пути
        ("cut", Box(EDGE, hx1, BED, SPRING + 0.05, hz1 - 0.1, TUN_N)),                  # тоннель на север
        ("cut", Prism("z", hz1 - 0.1, TUN_N, arch(TUN_R, SPRING - 0.05)), Box(TUN_X - 1.9, TUN_X + 1.9, SPRING, SPRING + 1.7, hz1 - 0.1, TUN_N)),
        ("cut", Box(EDGE, hx1, BED, SPRING + 0.05, TUN_S, hz0 + 0.1)),                  # тоннель на юг
        ("cut", Prism("z", TUN_S, hz0 + 0.1, arch(TUN_R, SPRING - 0.05)), Box(TUN_X - 1.9, TUN_X + 1.9, SPRING, SPRING + 1.7, TUN_S, hz0 + 0.1)),
        # Технический коридор: высокая часть с уступом слева, дальше ниже и уже
        ("cut", Box(COR_X - 1.1, COR_X + 1.1, F + 0.95, F + 3.1, COR_Z0, COR_ZN)),
        ("cut", Box(COR_X - 0.45, COR_X + 1.1, F, F + 1.0, COR_Z0, COR_ZN)),
        ("cut", Box(COR_X - 0.9, COR_X + 0.9, F, F + 2.6, COR_ZN - 0.05, COR_Z1)),
        ("cut", Box(COR_X + 1.0, COR_X + 1.55, F + 0.9, F + 1.8, 3.0, 3.9)),            # ниша справа
        ("cut", Box(COR_X + 1.05, hx0 + 0.05, F, F + 2.1, 1.6, 2.7)),                   # дверь с платформы
        # Ход к тоннелю: коридор на восток, шесть ступеней вниз, площадка у пути под низкой перемычкой
        ("cut", Box(COR_X + 0.85, 0.4, F, F + 2.4, LEG_Z[0], LEG_Z[1])),
        ("cut", Prism("z", LEG_Z[0], LEG_Z[1], [(-1.4, F + 0.5)] + steps_down(-1.4)[1:] +
                      [(EDGE + 0.05, BED), (EDGE + 0.05, F + 1.1), (0.4, F + 1.1), (0.4, F + 0.5)])),
        # Сход с северного конца платформы на путь
        ("cut", Prism("z", 28.6, hz1, [(0.4, F + 0.05)] + steps_down(0.4)[1:] + [(EDGE + 0.05, BED), (EDGE + 0.05, F + 0.05)])),
        # Лестница: первый марш в зале (три нижние ступени шире), косая стенка, дальше — глухой угол зала
        ("add", Prism("x", ST_X0 - 0.05, ST_X1, [(ST_Z, F - 0.05)] + flight(ST_Z, F)[1:] + [(Z2 + 0.05, L1), (Z2 + 0.05, F - 0.05)])),
        ("add", Prism("x", ST_X0 - 0.05, -0.9, [(ST_Z, F - 0.05)] + flight(ST_Z, F, 3)[1:] + [(ST_Z + 3 * RUN, F - 0.05)])),
        ("add", Prism("x", ST_X1, ST_X1 + 0.4, [(ST_Z + 3 * RUN, F - 0.05), (ST_Z + 3 * RUN, F + 3 * RISE + 0.95),
                                                (Z1, L1 + 0.95), (Z2 + 0.05, L1 + 0.95), (Z2 + 0.05, F - 0.05)])),
        ("add", Box(ST_X0 - 0.05, ST_X1 + 0.4, F - 0.05, F + HALL_H + 0.05, Z2, hz1 + 0.05)),
    ]
    # Балки под потолком зала
    for z in (6.0, 12.0, 18.0, 24.0):
        ops.append(("add", Box(hx0 - 0.05, hx1 + 0.05, F + 4.85, F + HALL_H + 0.05, z - 0.3, z + 0.3)))
    # Кольца обделки тоннеля: выступают на 12 см каждые 3 м
    for z in [33.0 + 3.0 * i for i in range(11)] + [-3.0 - 3.0 * i for i in range(4)]:
        ring = arch(TUN_R + 0.05, BED - 0.05) + list(reversed(arch(TUN_R - 0.12, BED - 0.05)))
        ops.append(("add", Prism("z", z - 0.17, z + 0.17, ring), False))
    # Шахта второго и третьего маршей: низ — ступени, верх — потолок на HEAD выше, наверху выходит под козырёк
    shaft = [(Z2 - 0.05, L1)] + flight(Z2, L1) + [(Z3, L2)] + flight(Z3, L2)[1:] + [
        (PAV[3] + 0.05, T), (PAV[3] + 0.05, 2.45), (z_flat, 2.45), (Z3, L2 + HEAD), (Z2E, L2 + HEAD),
        (Z2 - 0.05, F + HALL_H)]
    ops.append(("cut", Prism("x", ST_X0, ST_X1, shaft)))
    return ops


def collider_ops():
    """Те же операции для коллайдера: свод тоннеля — коробкой, кольца обделки не участвуют."""
    return [(o[0], o[2] if len(o) > 2 else o[1]) for o in concrete_ops() if len(o) < 3 or o[2] is not False]


def chip_list():
    """Сколы: (середина, радиус, зерно). Кромки ступеней, край платформы, уступ коридора, проёмы, оголовок."""
    out = []
    n = 0

    def put(c, r):
        nonlocal n
        out.append((c, r, 1000 + n))
        n += 1
    s = 41
    for fl, (z0, y0) in enumerate(((ST_Z, F), (Z2, L1), (Z3, L2))):
        for k in range(N):
            for j in range(2):
                if rnd(s, n) < 0.25:
                    n += 1
                    continue
                put((uniform(s, n, ST_X0 + 0.1, ST_X1 - 0.1), y0 + RISE * (k + 1) + uniform(s + 1, n, -0.01, 0.01),
                     z0 + RUN * k + uniform(s + 2, n, -0.01, 0.01)), uniform(s + 3, n, 0.015, 0.045))
    for k in range(46):   # край платформы
        put((EDGE + uniform(s, n, -0.02, 0.02), F + uniform(s + 1, n, -0.02, 0.01), uniform(s + 2, n, 0.5, 28.2)),
            uniform(s + 3, n, 0.02, 0.07))
    for k in range(22):   # кромка уступа в коридоре
        put((COR_X - 0.45 + uniform(s, n, -0.01, 0.02), F + 0.95 + uniform(s + 1, n, -0.02, 0.01),
             uniform(s + 2, n, COR_Z0 + 0.2, COR_ZN - 0.2)), uniform(s + 3, n, 0.015, 0.045))
    for k in range(20):   # кромка низкого потолка и углы сужения коридора
        put((COR_X + uniform(s, n, -0.9, 0.9), F + 2.6 + uniform(s + 1, n, -0.01, 0.02), COR_ZN - 0.05), uniform(s + 3, n, 0.015, 0.05))
    for k in range(14):
        put((COR_X + (0.9 if rnd(s, n) < 0.5 else -0.9), F + uniform(s + 1, n, 0.1, 2.5), COR_ZN - 0.05), uniform(s + 3, n, 0.02, 0.06))
    for k in range(12):   # проём двери с платформы
        put((HALL[0], F + uniform(s + 1, n, 0.1, 2.1), 1.6 if rnd(s, n) < 0.5 else 2.7), uniform(s + 3, n, 0.02, 0.06))
    for k in range(16):   # верх косой стенки первого марша
        z = uniform(s, n, ST_Z + 1.2, Z1)
        put((ST_X1 + (0.4 if rnd(s + 1, n) < 0.6 else 0.0), F + 3 * RISE + 0.95 + (z - ST_Z - 3 * RUN) * RISE / RUN, z),
            uniform(s + 3, n, 0.02, 0.06))
    for k in range(18):   # портал тоннеля и кромки балок
        a = uniform(s, n, 0.1, math.pi - 0.1)
        put((TUN_X + TUN_R * math.cos(a), SPRING + TUN_R * math.sin(a), HALL[3]), uniform(s + 3, n, 0.03, 0.08))
    for k in range(16):
        put((uniform(s, n, -1.5, 7.0), F + 4.85, (6.0, 12.0, 18.0, 24.0)[k % 4] + (0.3 if rnd(s + 1, n) < 0.5 else -0.3)),
            uniform(s + 3, n, 0.02, 0.06))
    for k in range(26):   # оголовок снаружи: углы стен, край козырька, вход
        side = PAV[0] if rnd(s, n) < 0.5 else PAV[1]
        r = rnd(s + 1, n)
        if r < 0.4:
            put((side, uniform(s + 2, n, 0.1, 2.9), PAV[3]), uniform(s + 3, n, 0.03, 0.10))
        elif r < 0.7:
            put((uniform(s + 2, n, PAV[0], PAV[1]), 3.0, PAV[3]), uniform(s + 3, n, 0.03, 0.09))
        else:
            z = uniform(s + 2, n, 19.0, 23.0)
            put((side, 0.45 + (z - 18.6) * RISE / RUN, z), uniform(s + 3, n, 0.03, 0.09))
    for k in range(10):   # вход шахты изнутри: косяки и перемычка
        put((ST_X0 if rnd(s, n) < 0.5 else ST_X1, uniform(s + 1, n, 0.2, 2.4), PAV[3]), uniform(s + 3, n, 0.02, 0.07))
    return out


def chip_bodies():
    out = []
    for c, r, seed in chip_list():
        b = Body()
        b.blob(c, r, None, seed, squash=(0.8 + 0.6 * rnd(seed, 90), 0.7 + 0.6 * rnd(seed, 91), 0.8 + 0.6 * rnd(seed, 92)))
        out.append(("cut", b))
    return out


def material_of(c, n, chips):
    """Материал грани бетона по её середине c и нормали n."""
    if max(abs(n[0]), abs(n[1]), abs(n[2])) < 0.985:
        for cc, r, _ in chips:
            d = (c[0] - cc[0]) ** 2 + (c[1] - cc[1]) ** 2 + (c[2] - cc[2]) ** 2
            if d < (r * 1.9) ** 2:
                return "concrete_break"
    in_tunnel = c[2] > HALL[3] + 0.05 or c[2] < HALL[2] - 0.05
    if in_tunnel and c[1] > SPRING and abs(c[0] - TUN_X) < TUN_R + 0.3:
        return "concrete"            # свод тоннеля — одним материалом, без шва по наклону
    if n[1] > 0.8:
        return "concrete" if c[1] > 0.2 else "concrete_floor"   # крыша оголовка — как стены
    if n[1] < -0.8:
        return "concrete_soffit"
    return "concrete"


def shell():
    """Бетон станции одним телом."""
    from solid import solid
    chips = chip_list()
    body = solid([(o[0], o[1].body()) for o in concrete_ops()] + chip_bodies(), bevel=0.012)
    out = Body()
    for pts, _, _ in body.faces:
        a, b, c = pts
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        l = (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) ** 0.5
        if l < 1e-12:
            mat = "concrete"
        else:
            mid = ((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3)
            mat = material_of(mid, (n[0] / l, n[1] / l, n[2] / l), chips)
        out.faces.append((pts, mat, mat != "concrete_break"))
    return out
