"""Пилот гибрида (docs/bake.md): одна секция Исподи — марш с площадкой у стены, — двумя способами рядом.

«Было» — как строится Исподь сейчас (docs/ispod.md): весь бетон секции одним телом solid(), узор формулой.
«Стало» — гибрид (docs/assets_rooms.md, решение 18.11): марш с площадкой и косой стенкой — литой узел,
запечённый с детальной модели (bake.py), стены, пол, потолок, балки, пилястры, плинтус и карниз — детали набора
с нахлёстом 3 см, сетками фабрики (kit.py).

Описание одно: те же размеры марша, что у первого марша Исподи (ispod.py: 18 ступеней 0.30 × 0.167 м, ширина
1.8 м, три нижние ступени шире, косая стенка). Оси — игры: x — восток, y — вверх, z — север; 0 — верх пола
секции, западная стена — x < 0, северная — z > Z_N. Секция открыта на восток и юг.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "parts"))
from ispod import N, RISE, RUN, flight  # noqa: E402
from kit import Body  # noqa: E402
from plan import Box, Prism  # noqa: E402

OV = 0.03                 # нахлёст деталей набора и литого узла: никогда встык (docs/decisions.md, 18.11)
WALL = 0.4                # толщина стен
SLAB = 0.3                # толщина пола и потолка
W = 7.0                   # секция: от западной стены на восток
Z_N = 9.0                 # внутренняя грань северной стены
H = 5.5                   # высота зала (как у Исподи)
ST_Z = 1.6                # подножие марша
ST_W = 1.8                # ширина марша
L1 = N * RISE             # верх площадки
Z1 = ST_Z + N * RUN       # конец марша, дальше площадка до стены
DOOR = (0.2, 1.6, L1, L1 + 2.2, 0.25)   # ниша двери над площадкой в северной стене: x от, до, y от, до, глубина
BEAMS = (3.0, 6.2)        # балки под потолком поперёк зала, по z
PIL = 2.0                 # ритм пилястр
LAMPS = ((3.2, 2.6), (3.2, 6.6))   # трубчатые лампы под потолком: x, z


def march_ops(ov=OV):
    """Литой узел: марш, площадка, три широкие нижние ступени и косая стенка — одно тело. ov — заход в стену
    и пол: у литого узла набора он прячет стык, у «было» не нужен (там всё одно тело)."""
    k3 = ST_Z + 3 * RUN
    return [
        ("add", Prism("x", -ov, ST_W, [(ST_Z, -ov)] + flight(ST_Z, 0.0)[1:] + [(Z_N + ov, L1), (Z_N + ov, -ov)])),
        ("add", Prism("x", -ov, ST_W + 1.3, [(ST_Z, -ov)] + flight(ST_Z, 0.0, 3)[1:] + [(k3, -ov)])),
        ("add", Prism("x", ST_W, ST_W + 0.4, [(k3, -ov), (k3, 3 * RISE + 0.95), (Z1, L1 + 0.95),
                                              (Z_N + ov, L1 + 0.95), (Z_N + ov, -ov)])),
    ]


def march_chips():
    """Сколы литого узла (только детальная модель): кромки проступей, верх косой стенки, край площадки.
    (центр, радиус, зерно); радиусы 1.5–4 см — чтобы их видела карта нормалей, а не силуэт."""
    from kit import rnd
    out = []
    s = 0
    for k in range(N):
        z, y = ST_Z + RUN * k, RISE * (k + 1)
        for j in range(2):
            s += 1
            if rnd(s, 1) < 0.55:
                x = 0.15 + (ST_W - 0.3) * rnd(s, 2)
                out.append(((x, y + 0.004, z - 0.004), 0.015 + 0.025 * rnd(s, 3), s))
    for k in range(14):
        s += 1
        t = rnd(s, 4)
        z = ST_Z + 3 * RUN + t * (Z_N - ST_Z - 3 * RUN)
        top = min(3 * RISE + 0.95 + (z - ST_Z - 3 * RUN) * RISE / RUN, L1 + 0.95)
        x = ST_W + (0.0 if rnd(s, 5) < 0.5 else 0.4)
        out.append(((x, top, z), 0.02 + 0.02 * rnd(s, 6), s))
    for k in range(5):
        s += 1
        out.append(((0.2 + 1.4 * rnd(s, 7), L1, Z1), 0.02 + 0.02 * rnd(s, 8), s))
    return out


def room_ops():
    """Оболочка секции «было» одним телом: пол, западная и северная стены, потолок, балки, ниша двери."""
    ops = [("add", Box(-WALL, W, -SLAB, H + SLAB, 0.0, Z_N + WALL)),
           ("cut", Box(0.0, W + 1.0, 0.0, H, -1.0, Z_N))]
    ops += march_ops(0.0)
    for z in BEAMS:
        ops.append(("add", Box(-0.05, W, H - 0.65, H + 0.05, z - 0.3, z + 0.3)))
    x0, x1, y0, y1, d = DOOR
    ops.append(("cut", Box(x0, x1, y0, y1, Z_N - 0.1, Z_N + d)))
    return ops


def collider_ops():
    """Коллайдер у обеих секций один: та же раскладка (plan.colliders)."""
    return room_ops()


def kit_parts():
    """Детали набора секции «стало»: [(имя, Body)] — каждая замкнута, соседи заходят друг в друга на OV."""
    out = []

    def box(name, x0, x1, y0, y1, z0, z1, mat, bevel=0.015):
        b = Body()
        b.box(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), mat, bevel=bevel)
        out.append((name, b))

    box("floor", -WALL, W, -SLAB, 0.0, 0.0, Z_N + WALL, "concrete_floor")
    box("ceiling", -WALL, W, H, H + SLAB, 0.0, Z_N + WALL, "concrete_soffit")
    # Щиты стен по 2 м, нахлёст на соседа OV; заходят в пол и потолок
    n = int(Z_N / PIL + 0.999)
    for i in range(n):
        z0, z1 = max(0.0, i * PIL - OV), min(Z_N + WALL, (i + 1) * PIL + OV)
        box("wall_w%d" % i, -WALL, 0.0, -OV, H + OV, z0, z1, "concrete")
    for i in range(int(W / PIL + 0.999)):
        x0, x1 = max(-WALL, i * PIL - OV), min(W, (i + 1) * PIL + OV)
        if x0 < DOOR[1] and x1 > DOOR[0]:
            continue   # щит с нишей — ниже, литым solid() не нужен: собирается из трёх брусков
        box("wall_n%d" % i, x0, x1, -OV, H + OV, Z_N, Z_N + WALL, "concrete")
    # Щит с нишей двери: бруски слева, справа, над нишей и задник ниши, каждый заходит в соседей
    dx0, dx1, dy0, dy1, d = DOOR
    x1 = PIL + OV
    box("wall_n0_l", -WALL, dx0 + OV, -OV, H + OV, Z_N, Z_N + WALL, "concrete")
    box("wall_n0_r", dx1 - OV, x1, -OV, H + OV, Z_N, Z_N + WALL, "concrete")
    box("wall_n0_t", dx0 - OV, dx1 + OV, dy1 - OV, H + OV, Z_N, Z_N + WALL, "concrete")
    box("wall_n0_b", dx0 - OV, dx1 + OV, -OV, dy0 + OV, Z_N, Z_N + WALL, "concrete")
    box("wall_n0_back", dx0 - OV, dx1 + OV, dy0 - OV, dy1 + OV, Z_N + d, Z_N + WALL, "concrete")
    # Пилястры по ритму 2 м — на каждом стыке щитов; западная стена — и над маршем (стоят на ступенях)
    for i in range(1, n):
        z = i * PIL
        y0 = -OV if z < ST_Z else min(L1, max(0.0, (z - ST_Z) / RUN * RISE)) - OV
        box("pil_w%d" % i, -0.02, 0.12, y0, H + OV, z - 0.14, z + 0.14, "concrete")
    for i in range(1, int(W / PIL + 0.999)):
        x = i * PIL
        if ST_W - 0.1 < x < ST_W + 0.5:
            continue   # стык за косой стенкой
        box("pil_n%d" % i, x - 0.14, x + 0.14, -OV, H + OV, Z_N - 0.12, Z_N + 0.02, "concrete")
    # Плинтус там, где стена стоит на полу, и карниз по всей длине стен под потолком
    box("plinth_w", -0.02, 0.06, -OV, 0.18, 0.0, ST_Z + OV, "concrete")
    box("plinth_n", ST_W + 0.4 - OV, W, -OV, 0.18, Z_N - 0.06, Z_N + 0.02, "concrete")
    box("cornice_w", -0.02, 0.07, H - 0.25, H + OV, 0.0, Z_N + 0.02, "concrete")
    box("cornice_n", -0.02, W, H - 0.25, H + OV, Z_N - 0.07, Z_N + 0.02, "concrete")
    # Балки под потолком: заходят в западную стену и в потолок
    for k, z in enumerate(BEAMS):
        box("beam%d" % k, -OV, W, H - 0.65, H + OV, z - 0.3, z + 0.3, "concrete", bevel=0.02)
    return out


def fill():
    """Общее наполнение обеих секций: полотно двери в нише, трубчатые лампы на подвесах."""
    b = Body()
    dx0, dx1, dy0, dy1, d = DOOR
    b.box(((dx0 + dx1) / 2, (dy0 + dy1) / 2 - 0.01, Z_N + d - 0.02), (dx1 - dx0 - 0.12, dy1 - dy0 - 0.06, 0.05),
          "steel_paint", bevel=0.006)
    for x, z in LAMPS:
        b.tube([(x, H - 0.55, z - 0.6), (x, H - 0.55, z + 0.6)], 0.03, "lamp_glass", seg=16)
        for dz in (-0.45, 0.45):
            b.tube([(x, H - 0.53, z + dz), (x, H + 0.02, z + dz)], 0.004, "steel_galv", seg=8)
    return b


def lamp_lines(dz, lift):
    """Строки light заготовки: трубка 1.2 м вдоль z светит вниз."""
    out = []
    for x, z in LAMPS:
        out.append("light %s %s %s 0 0 0.6 0 -1 0 0.1 1800 4000 0.03" % (_n(x), _n(H - 0.6 + lift), _n(z + dz)))
    return out


def _n(v):
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s
