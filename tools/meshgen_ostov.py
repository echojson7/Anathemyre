"""Остов — закрытое бетонное здание для проверки света (docs/ostov.md): толстые стены, плита-крыша на балках,
галерея с лестницей, тёмная комната, тамбур у входа. Свет внутрь — только через зазоры: дверь, щели в стенах,
полоса просветов под крышей на западе, круглый световой колодец, шов между плитами крыши и пролом.

Весь бетон — плиты с отверстиями `panel` из tools/meshgen_concrete.py (неровные рёбра, излом, арматура).
Здесь только раскладка: размеры, зазоры, части здания.

Начало координат всех сеток Остова — середина пятна на высоте площадки (`prefab ostov` в data/scene.txt), детали
стоят строкой `part <сетка> 0 0 0`. x — восток, z — север, y — вверх. Одно зерно — те же байты: целочисленный
хеш и арифметика double, тригонометрия только в раскладке.
"""
import math

import meshgen_concrete as mc
from meshgen_concrete import (
    CircleHole, M_FLOOR, M_SOFFIT, M_WALL, NZ, PX, PY, PZ, Part, RaggedHole, RectHole, SlantCut, add,
    box, collar, hash3, panel, rot_x, rot_y, sc, sub, wall_x, wall_z,
)


# Раскладка (м): пятно, стены, уровни
X0, X1, Z0, Z1 = -15.0, 15.0, -10.0, 12.0   # снаружи по стенам
WT = 0.6                                     # толщина наружных стен
PL = 0.4                                     # цоколь шире стен
OV = 0.8                                     # свес крыши
FY = 0.25                                    # верх пола
RY0, RY1 = 7.0, 8.0                          # плита крыши: низ, верх
BEAM_D, BEAM_B = 0.8, 0.5                    # балки под крышей: высота, ширина
GY = 4.0                                     # верх пола галереи
GT = 0.4                                     # толщина плиты галереи
GZ = 8.6                                     # край галереи (галерея — от него до северной стены)
ST_N, ST_RUN = 21, 0.28                      # лестница: ступеней, проступь
ST_X0, ST_X1 = 12.9, X1 - WT                 # лестница у восточной стены
ST_Z0 = GZ - ST_N * ST_RUN                   # подножие марша
JOINT = 2.5                                  # шов между плитами крыши по x, ширина 0.1
OCULUS = (7.5, 0.5, 1.25)                    # световой колодец: x, z, радиус
RUPTURE = (-9.5, -4.0, 2.8, 3.2)             # пролом: середина x, z, полуоси
DOOR = (1.8, 4.2, 3.2)                       # вход в южной стене: x от, до, высота над полом
ROOM = (9.0, -4.0)                           # тёмная комната в юго-восточном углу: x запада, z севера


# --- части здания ---

def floor():
    m = Part()
    # Цоколь шире стен на PL: снаружи — ступень-отлив по периметру
    box(m, X0 - PL, X1 + PL, 0.0, FY, Z0 - PL, Z1 + PL, hide=("bottom",), seed=101, top=M_FLOOR, bump=0.006,
        chip=0.025, cell=0.6)
    # Порог у входа: плита на песке перед дверью
    box(m, DOOR[0] - 0.6, DOOR[1] + 0.6, -0.05, FY - 0.08, Z0 - 2.0, Z0 - PL, hide=("bottom", "z1"), seed=102,
        top=M_FLOOR, bump=0.006, chip=0.03)
    return m


def walls():
    m = Part()
    h = RY0 - FY
    # Юг: вход и три узкие щели
    slits = [RectHole(x - X0 - 0.09, x - X0 + 0.09, 0.95, 4.55) for x in (-3.9, -2.5, -1.1)]
    door = RectHole(DOOR[0] - X0, DOOR[1] - X0, -1.0, DOOR[2])
    wall_x(m, X0, X1, Z0, Z0 + WT, FY, RY0, holes=[door] + slits, seed=201)
    # Север: два окошка на уровне галереи
    wins = [RectHole(x - X0 - 0.3, x - X0 + 0.3, 4.95, 5.55) for x in (-6.0, 3.0)]
    wall_x(m, X0, X1, Z1 - WT, Z1, FY, RY0, holes=wins, seed=202)
    # Запад: глухая до 5.4, выше — простенки под крышу, между ними просветы (полоса солнца после полудня)
    zi0, zi1 = Z0 + WT, Z1 - WT
    n = 8
    pw = 0.7
    step = (zi1 - zi0 - pw) / n
    gaps = [RectHole(pw + step * k, step * (k + 1), h - 1.0, h + 1.0) for k in range(n)]
    wall_z(m, zi0, zi1, X0, X0 + WT, FY, RY0, holes=gaps, hide=("z0", "z1", "bottom", "top"), seed=203)
    # Восток: две высокие щели (утреннее солнце)
    eslits = [RectHole(z - zi0 - 0.1, z - zi0 + 0.1, 1.0, 5.0) for z in (-2.0, 1.0)]
    wall_z(m, zi0, zi1, X1 - WT, X1, FY, RY0, holes=eslits, hide=("z0", "z1", "bottom", "top"), seed=204)
    # Контрфорсы снаружи восточной стены: клин 1.8 м у земли, 0.4 наверху, от песка
    for k, z in enumerate((-6.5, 4.0, 9.0)):
        panel(m, (X1, 0.0, z + 0.5), PX, PY, NZ, 1.8, 6.25, 1.0, holes=[SlantCut(1.8, -1.4 / 6.25)],
              sides=("hide", "chip", "hide", "chip"), seed=210 + k, extra_v=(0.25, 0.6, 1.0))
    # Пилястры на юге и севере — по осям балок: ритм фасада и тени от солнца
    for k, x in enumerate((-10.0, -5.0, 0.0, 5.0, 10.0)):
        box(m, x - 0.25, x + 0.25, FY, RY0, Z0 - 0.35, Z0, hide=("top", "bottom", "z1"), seed=230 + k)
        box(m, x - 0.25, x + 0.25, FY, RY0, Z1, Z1 + 0.35, hide=("top", "bottom", "z0"), seed=240 + k)
    # Навес над входом
    box(m, DOOR[0] - 0.5, DOOR[1] + 0.5, 3.55, 3.85, Z0 - 1.5, Z0, hide=("z1",), seed=220, top=M_WALL)
    return m


def roof():
    m = Part()
    xw0, xj = X0 - OV, JOINT - 0.05
    xe0, xe1 = JOINT + 0.05, X1 + OV
    z0, z1 = Z0 - OV, Z1 + OV
    rup = RaggedHole(RUPTURE[0] - xw0, RUPTURE[1] - z0, RUPTURE[2], RUPTURE[3], seed=301)
    panel(m, (xw0, RY0, z0), PX, PZ, PY, xj - xw0, z1 - z0, RY1 - RY0, holes=[rup], cell=0.3, seed=302,
          mats=(M_FLOOR, M_SOFFIT, M_WALL), soffit=0.05, taper=0.35, rebar=0.55, bump=0.015)
    ocu = CircleHole(OCULUS[0] - xe0, OCULUS[1] - z0, OCULUS[2])
    panel(m, (xe0, RY0, z0), PX, PZ, PY, xe1 - xe0, z1 - z0, RY1 - RY0, holes=[ocu], cell=0.3, seed=303,
          mats=(M_FLOOR, M_SOFFIT, M_WALL), soffit=0.05, bump=0.015)
    # Обечайка колодца: кольцо ниже плиты
    collar(m, OCULUS[0], OCULUS[1], OCULUS[2], OCULUS[2] + 0.22, RY0 - 0.35, RY0 + 0.02)
    return m


def frame():
    """Балки под крышей (с юга на север) и колонны под ними; балка над проломом сломана."""
    m = Part()
    zi0, zi1 = Z0 + WT - 0.3, Z1 - WT + 0.3
    yb = RY0 - BEAM_D
    for k, x in enumerate((-10.0, -5.0, 0.0, 5.0, 10.0)):
        common = dict(seed=401 + k, bump=0.01, cell=0.4)
        if x == -10.0:
            # Через пролом: два обломка с рваными концами и арматурой
            box(m, x - BEAM_B / 2, x + BEAM_B / 2, yb, RY0, zi0, -6.9, hide=("top", "z0"), side_z1="broken",
                rebar=0.9, **common)
            box(m, x - BEAM_B / 2, x + BEAM_B / 2, yb, RY0, -1.2, zi1, hide=("top", "z1"), side_z0="broken",
                rebar=0.9, **common)
        else:
            box(m, x - BEAM_B / 2, x + BEAM_B / 2, yb, RY0, zi0, zi1, hide=("top", "z0", "z1"), **common)
    for k, x in enumerate((-5.0, 0.0, 5.0)):
        box(m, x - 0.35, x + 0.35, FY, yb, 0.65, 1.35, hide=("top", "bottom"), seed=420 + k, cell=0.4)
        # Подушка под балкой
        box(m, x - 0.45, x + 0.45, yb - 0.3, yb, 0.55, 1.45, hide=("top",), seed=430 + k)
    return m


def gallery():
    """Галерея вдоль северной стены, марш вдоль восточной, парапет с проломом, колонны под краем."""
    m = Part()
    xi0, xi1, zi1 = X0 + WT, X1 - WT, Z1 - WT
    box(m, xi0, xi1, GY - GT, GY, GZ, zi1, hide=("x0", "x1", "z1"), seed=501, top=M_FLOOR, soffit=0.03, cell=0.4)
    for k, x in enumerate((-9.0, -3.0, 3.0, 9.0)):
        box(m, x - 0.25, x + 0.25, FY, GY - GT, GZ, GZ + 0.5, hide=("top", "bottom"), seed=510 + k)
    # Парапет: от западной стены до марша, в середине обрушен кусок
    py0, py1 = GY, GY + 1.0
    box(m, xi0, -4.2, py0, py1, GZ, GZ + 0.2, hide=("x0", "bottom"), side_x1="broken", seed=520, rebar=0.8)
    box(m, -2.6, ST_X0, py0, py1, GZ, GZ + 0.2, hide=("bottom",), side_x0="broken", seed=521, rebar=0.8)
    # Марш: ступени до верха галереи
    rise = (GY - FY) / ST_N
    for k in range(ST_N):
        z0 = ST_Z0 + ST_RUN * k
        top = FY + rise * (k + 1)
        hide = ("bottom", "x1") + (("z1",) if k < ST_N - 1 else ())
        box(m, ST_X0, ST_X1, FY, top, z0, z0 + ST_RUN, hide=hide, seed=530 + k, top=M_FLOOR, bump=0.004,
            chip=0.02, chip_w=0.05, cell=0.5)
    return m


def rooms():
    """Тёмная комната в юго-восточном углу (вход с запада), тамбур напротив входа."""
    m = Part()
    rx, rz = ROOM
    zi0, xi1 = Z0 + WT, X1 - WT
    hr = 3.4
    door = RectHole(-7.5 - zi0, -6.4 - zi0, -1.0, 2.3)
    wall_z(m, zi0, rz - 0.3, rx, rx + 0.3, FY, hr, holes=[door], hide=("z0", "z1", "bottom", "top"), seed=601)
    wall_x(m, rx, xi1, rz - 0.3, rz, FY, hr, hide=("x1", "bottom", "top"), seed=602)
    box(m, rx - 0.15, xi1, hr, hr + 0.3, zi0, rz + 0.15, hide=("x1", "z0"), seed=603, top=M_FLOOR)
    # Тамбур: стена напротив входа, солнце из двери упирается в неё
    wall_x(m, DOOR[0] - 1.3, DOOR[1] + 1.3, -7.0, -6.7, FY, 3.6, hide=("bottom",), seed=604)
    return m


def debris():
    """Обломки под проломом: куски плиты, обломок балки, щебень. Всё из зерна."""
    m = Part()
    cx, cz = RUPTURE[0], RUPTURE[1]

    def chunk(c, size, yaw, tilt, seed, broken=True):
        sx, sy, sz = size
        U = rot_y(rot_x(PX, tilt), yaw)
        V = rot_y(rot_x(PZ, tilt), yaw)
        W = rot_y(rot_x(PY, tilt), yaw)
        O = sub(c, add(add(sc(U, sx / 2), sc(V, sz / 2)), sc(W, sy / 2)))
        sides = ("broken" if broken and hash3(seed, k, 1, 9) < 0.5 else "chip" for k in range(4))
        t = min(sx, sy, sz)
        panel(m, O, U, V, W, sx, sz, sy, sides=tuple(sides), seed=seed, cell=0.35, chip=min(0.04, 0.12 * t),
              bump=min(0.02, 0.08 * t), brk=min(0.18, 0.25 * t), mats=(M_FLOOR, M_SOFFIT, M_WALL),
              rebar=0.5 if t > 0.35 else 0.0, coll=sy > 0.25)

    # Крупные куски плиты: лежат косо, один опёрт на другой
    chunk((cx - 0.6, FY + 0.45, cz + 0.4), (2.4, 0.5, 1.8), 0.4, 0.32, 701)
    chunk((cx + 1.1, FY + 0.3, cz - 0.9), (1.7, 0.45, 1.4), -0.9, -0.18, 702)
    chunk((cx + 0.2, FY + 0.95, cz - 0.2), (1.3, 0.4, 1.1), 1.9, 0.55, 703)
    # Обломок балки: одним концом на куче
    chunk((cx - 0.4, FY + 0.75, cz - 1.9), (0.5, 0.8, 3.2), 0.25, -0.35, 704, broken=True)
    # Щебень вокруг
    for k in range(34):
        a = 2 * math.pi * hash3(k, 0, 0, 710)
        r = 0.6 + 2.6 * math.sqrt(hash3(k, 1, 0, 710))
        h = hash3(k, 2, 0, 710)
        s = 0.12 + 0.45 * h * h
        p = (cx + r * math.cos(a), FY + s * 0.35, cz + r * math.sin(a))
        size = (s * (0.8 + 0.6 * hash3(k, 3, 0, 710)), s * 0.7, s * (0.8 + 0.6 * hash3(k, 4, 0, 710)))
        chunk(p, size, 6.28 * hash3(k, 5, 0, 710), 0.5 * (hash3(k, 6, 0, 710) - 0.5), 720 + k)
    return m


PARTS = {
    "ostov_floor": floor,
    "ostov_walls": walls,
    "ostov_roof": roof,
    "ostov_frame": frame,
    "ostov_gallery": gallery,
    "ostov_rooms": rooms,
    "ostov_debris": debris,
}
mc.FLOOR_Y = FY  # стены у пола зала темнее (ao_of)
MESHES = PARTS
