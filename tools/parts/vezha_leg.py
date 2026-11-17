"""Нога «Вежи» (docs/vezha.md): паучья. Бедро от поворотного узла идёт вверх-наружу, колено выше рамы, длинная
голень спускается к отвесной стойке голеностопа, под ней — широкая лапа-опора на шаровой опоре. Бедро поднимает
гидроцилиндр снизу, колено — большой гидроцилиндр поверх бедра на рог голени (как у экскаватора).

Два вида: передняя (front) — короче, бедро в бронещеках с жалюзи, голень под щитком; задняя (rear) — длиннее, бедро
открытое, с карманами облегчения. Левая нога пары — зеркало правой.

Оси рецепта ноги: ось поворота узла — ось y в начале координат, нога уходит в +x, z — вбок. Звено описано в своих
осях (Link): x — вдоль звена от сустава к суставу, y — спинка (у бедра сверху), z — вбок. Стойка голеностопа отвесна
в осях рамы (нога решается двумя длинами L1, L2 до голеностопа над лапой).
"""
import math

from kit import Body, add, mat_mul, mat_vec, mul, rot_y, rot_z, sub, unit

PAINT, WORN, IRON, STEEL = "vezha_paint", "vezha_worn", "vezha_iron", "vezha_steel"
RUBBER, GLASS, EYE, MARK = "rubber_black", "vezha_glass", "vezha_eye", "vezha_mark"
BRASS, CANVAS, OLIVE = "vezha_brass", "vezha_canvas", "vezha_olive"

# --- размеры, м ---
U_PITCH = 0.17                 # ось качания бедра — на столько дальше оси поворота
HIP_Y = 0.82                   # оси бёдер над землёй в бинде (стоит)
FOOT_H = 0.065                 # ось шаровой опоры над подошвой
L3 = 0.13                      # стойка голеностопа: от оси голеностопа до шаровой опоры
REACH = 1.0                    # бинд: лапа на столько дальше оси качания
LINKS = {"front": (0.54, 0.98), "rear": (0.62, 1.00)}   # бедро, голень (между осями)
THIGH_MIN, THIGH_MAX = math.radians(-40.0), math.radians(95.0)   # ход бедра к горизонту
# Проушины гидроцилиндров: подъём бедра — под осью качания на узле и под бедром; колено — поверх бедра и на роге
LIFT_A, LIFT_B = (-0.02, -0.13), (0.33, -0.08)
KNEE_A, KNEE_B = (0.075, 0.125), (-0.125, 0.07)
DECK = 0.092                   # палубы рамы над и под осями бёдер (между ними — барабан узла)

WEAR_LEG = (WORN, 0.03, 0.9, 0.12)   # щитки ног бьются сильнее корпуса


def sphere(b, c, r, mat, seg=12, rings=6):
    prof = [(r * math.sin(math.pi * (k + 0.5) / rings * (1 - 1e-3) + 5e-4), -r * math.cos(math.pi * (k + 0.5) / rings))
            for k in range(rings)]
    prof = [(r * 0.25, -r * 0.97)] + prof + [(r * 0.25, r * 0.97)]
    b.lathe(c, (0.0, 1.0, 0.0), [(pr, h) for pr, h in prof], mat, seg=seg)


def bolt(b, p, axis, r=0.011, h=0.008, mat=IRON):
    b.lathe(p, axis, [(r, 0.0), (r, h * 0.7), (r * 0.7, h)], mat, seg=6, smooth=False)


class Link:
    """Оси звена в плоскости ноги: начало в суставе, x — под углом a к горизонту."""

    def __init__(self, o, a):
        self.o, self.a, self.m = o, a, rot_z(a)

    def at(self, x, y, z=0.0):
        return add(self.o, mat_vec(self.m, (x, y, z)))

    def dir(self, x, y, z=0.0):
        return mat_vec(self.m, (x, y, z))

    def end(self, length):
        return self.at(length, 0.0)


def two_link(a_len, b_len, x, y, bend=1.0):
    """Углы первого и второго звена к горизонту для конца в (x, y); bend 1 — колено вверх."""
    d = min(max(math.hypot(x, y), abs(a_len - b_len) + 1e-4), a_len + b_len - 1e-4)
    a = math.atan2(y, x) + bend * math.acos(max(-1.0, min(1.0, (a_len * a_len + d * d - b_len * b_len) / (2 * a_len * d))))
    return a, math.atan2(y - a_len * math.sin(a), x - a_len * math.cos(a))


def leg_points(kind):
    """Суставы ноги вида kind в бинде и проушины цилиндров (оси ноги)."""
    l1, l2 = LINKS[kind]
    P = (U_PITCH, HIP_Y, 0.0)
    a, s = two_link(l1, l2, REACH, FOOT_H + L3 - HIP_Y)
    th = Link(P, a)
    K = th.end(l1)
    sh = Link(K, s)
    A = sh.end(l2)
    F = (A[0], A[1] - L3, 0.0)
    return {"kind": kind, "l1": l1, "l2": l2, "P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh,
            "ta": Link(A, -0.5 * math.pi), "la": (P[0] + LIFT_A[0], P[1] + LIFT_A[1], 0.0), "lb": th.at(*LIFT_B),
            "ka": th.at(*KNEE_A), "kb": sh.at(*KNEE_B)}


def _rect(link, x, y0, y1, z0, z1):
    return [link.at(x, y0, z0), link.at(x, y1, z0), link.at(x, y1, z1), link.at(x, y0, z1)]


def _oct(link, x, hy, hz, c):
    """Сечение балки: прямоугольник 2hy × 2hz со снятыми на c углами."""
    pts = [(hy, hz - c), (hy - c, hz), (-hy + c, hz), (-hy, hz - c), (-hy, -hz + c), (-hy + c, -hz), (hy - c, -hz), (hy, -hz + c)]
    return [link.at(x, y, z) for y, z in pts]


def _disc(b, link, x, y, z0, z1, r, mat, seg=18):
    """Цилиндр вдоль z ноги от z0 до z1 с осью в точке (x, y) звена."""
    b.lathe(link.at(x, y, z0), (0.0, 0.0, 1.0), [(r, 0.0), (r, z1 - z0)], mat, seg=seg)


def _cap(b, link, x, s, z, r, mat=STEEL, n=0, rb=0.0, y=0.0):
    """Крышка оси на боковой грани (s — знак стороны) и n болтов вокруг на радиусе rb."""
    b.lathe(link.at(x, y, s * z), (0.0, 0.0, s), [(r, 0.0), (r, 0.008), (r * 0.7, 0.014)], mat, seg=12)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        bolt(b, add(link.at(x, y, s * z), (rb * math.cos(a), rb * math.sin(a), 0.0)), (0.0, 0.0, s), r=0.007, h=0.006)


def _clamp(b, link, x, y, z, r):
    """Хомут шланга: скоба поперёк звена."""
    b.box(link.at(x, y, z), (0.018, 2 * r + 0.008, 2 * r + 0.012), STEEL, bevel=0.002, rot=link.m)


# --- поворотный узел (кость hip): барабан между палубами рамы, вилка оси качания, проушина цилиндра подъёма ---

def hip_parts(L):
    from solid import solid
    H = HIP_Y
    P = L["P"]
    hp, hp0 = Link(P, 0.0), Link((P[0], 0.0, 0.0), 0.0)
    core = Body()
    core.lathe((0.0, H - DECK + 0.004, 0.0), (0, 1, 0), [(0.088, 0.0), (0.088, 2 * DECK - 0.008)], None, seg=20)
    adds = Body()
    for s in (-1.0, 1.0):   # щёки вилки: от барабана к оси качания и вниз к проушине цилиндра
        z0, z1 = s * 0.061, s * 0.086
        lo, hi = min(z0, z1), max(z0, z1)
        adds.prism([(0.03, 0.07), (P[0] - 0.01, 0.075), (P[0] + 0.06, 0.02), (P[0] + 0.045, -0.08), (P[0] + 0.015, -0.165),
                    (P[0] - 0.06, -0.165), (0.03, -0.075)], "z", lo, hi, None, plane=("x", "y"))
        _disc(adds, hp0, 0.0, 0.0, lo, hi, 0.072, None, seg=18)
        _disc(adds, hp0, LIFT_A[0], LIFT_A[1], lo, hi, 0.04, None, seg=16)
    t = solid([("add", core), ("add", adds.placed(None, (0.0, H, 0.0)))], bevel=0.005, bevel_angle=30.0)
    h = Body()
    h.faces = [(p, IRON, s) for p, _, s in t.faces]
    # венец барабана: зубья под шестерню мотора на раме
    for k in range(24):
        a = 2 * math.pi * (k + 0.5) / 24
        h.box((0.094 * math.cos(a), H + 0.058, 0.094 * math.sin(a)), (0.016, 0.024, 0.012), IRON, bevel=0.0, rot=rot_y(-a))
    # крышки сверху и снизу (над и под палубами) с болтами; поворотный штуцер магистрали сверху
    for y, s in ((H + DECK + 0.026, 1.0), (H - DECK - 0.026, -1.0)):
        h.lathe((0.0, y, 0.0), (0, s, 0), [(0.074, 0.0), (0.074, 0.016), (0.058, 0.024), (0.03, 0.026)], IRON, seg=20)
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 8
            bolt(h, (0.064 * math.cos(a), y + s * 0.016, 0.064 * math.sin(a)), (0, s, 0), r=0.0075, h=0.006)
    h.lathe((0.0, H + DECK + 0.05, 0.0), (0, 1, 0), [(0.028, 0.0), (0.028, 0.03), (0.02, 0.036), (0.02, 0.05), (0.012, 0.056)],
            STEEL, seg=10)
    # ось качания: крышки с болтами на обеих щеках; палец проушины цилиндра
    for s in (-1.0, 1.0):
        _cap(h, hp, 0.0, s, 0.086, 0.046, n=6, rb=0.058)
        _cap(h, hp, LIFT_A[0], s, 0.086, 0.024, y=LIFT_A[1])
    # палец проушины цилиндра поперёк вилки, проставки между щекой и проушиной
    _disc(h, hp, LIFT_A[0], LIFT_A[1], -0.061, 0.061, 0.013, STEEL, seg=10)
    for s in (-1.0, 1.0):
        _disc(h, hp, LIFT_A[0], LIFT_A[1], s * 0.042 - 0.019, s * 0.042 + 0.019, 0.02, IRON, seg=12)
    # шланги от штуцера к цилиндрам: по верху вилки к проушинам
    sw = (0.0, H + DECK + 0.07, 0.0)
    for s, end in ((1.0, (P[0] - 0.05, H - 0.12, 0.1)), (-1.0, (P[0] + 0.02, H + 0.09, -0.1))):
        h.tube([add(sw, (0.0, -0.01, s * 0.02)), add(sw, (0.05, -0.02, s * 0.05)), (P[0] - 0.06, H + 0.07, s * 0.1), end], 0.0085,
               RUBBER, seg=6, fillet=0.03)
    return [(h, None)]


# --- бедро (кость thigh): коробчатая балка, на колене вилка; у передней — бронещёки ---

def thigh_parts(L):
    from solid import solid
    th, l1, front = L["th"], L["l1"], L["kind"] == "front"
    hh = lambda x: 0.074 + (0.058 - 0.074) * x / l1
    core = Body()
    core.loft([_oct(th, x, hh(x), 0.058, 0.014) for x in (0.0, l1 * 0.5, l1 - 0.07)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.055, 0.055, 0.074, None, seg=18)
    for s in (-1.0, 1.0):   # вилка колена
        lo, hi = sorted((s * 0.05, s * 0.07))
        adds.loft([_rect(th, x, -hh(x), hh(x), lo, hi) for x in (l1 - 0.12, l1)], None)
        _disc(adds, th, l1, 0.0, lo, hi, 0.068, None, seg=18)
    # кронштейны проушин: под бедром (цилиндр подъёма) и сверху (цилиндр колена)
    for (x, y) in (LIFT_B, KNEE_A):   # по две щеки, проушина цилиндра — между ними
        sy = 1.0 if y > 0 else -1.0
        for s in (-1.0, 1.0):
            lo, hi = sorted((s * 0.024, s * 0.037))
            adds.loft([_rect(th, x + dx, sy * (hh(x) - 0.01), y, lo, hi) for dx in (-0.05, 0.05)], None)
            _disc(adds, th, x, y, lo, hi, 0.03, None, seg=14)
    cuts = Body()
    if not front:   # задняя: карманы облегчения в боках
        for s in (-1.0, 1.0):
            for x0, x1 in ((0.12, 0.27), (0.31, 0.44)):
                cuts.loft([_rect(th, x, -hh(x) + 0.022, hh(x) - 0.022, s * 0.058 - 0.009, s * 0.058 + 0.02) for x in (x0, x1)], None)
    cuts.lathe(th.at(l1, 0.0, -0.08), (0, 0, 1), [(0.03, 0.0), (0.03, 0.16)], None, seg=14)   # гнездо пальца колена
    ops = [("add", core), ("add", adds)] + ([("cut", cuts)] if cuts.faces else [])
    t = solid(ops, bevel=0.0045, bevel_angle=30.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _disc(b, th, 0.0, 0.0, s * 0.0505 - 0.004, s * 0.0505 + 0.004, 0.05, STEEL, seg=14)    # втулки качания
        _disc(b, th, l1, 0.0, s * 0.078 - 0.008, s * 0.078 + 0.008, 0.032, STEEL, seg=12)     # палец колена
        _cap(b, th, l1, s, 0.086, 0.036, n=5, rb=0.05)
        for (x, y) in (LIFT_B, KNEE_A):
            _disc(b, th, x, y, s * 0.04 - 0.004, s * 0.04 + 0.004, 0.016, STEEL, seg=10)
    # магистраль по боку бедра в хомутах (к цилиндру колена)
    side = -1.0
    path = [th.at(0.06, -0.03, side * 0.069), th.at(0.2, -0.035, side * 0.069), th.at(l1 - 0.18, -0.03, side * 0.069),
            th.at(l1 - 0.13, 0.04, side * 0.069), th.at(KNEE_A[0] + 0.17, KNEE_A[1] - 0.005, side * 0.035)]
    b.tube(path, 0.008, RUBBER, seg=6, fillet=0.02)
    for x in (0.13, 0.30):
        _clamp(b, th, x, -0.033, side * 0.069, 0.008)
    plates, marks = Body(), Body()
    if front:
        # бронещёки: трапеция на шести стойках с зазором, жалюзи, полосы знака у колена
        for s in (-1.0, 1.0):
            z = s * 0.086
            pts = lambda x0, x1, y0, y1: [(x0, y0), (x1, y0 + 0.012), (x1, y1 - 0.01), (x0 + 0.03, y1)]
            prof = pts(0.05, l1 - 0.14, -0.088, 0.1)
            ring = lambda dz: [th.at(x, y, z + dz) for x, y in prof]
            plates.loft([ring(-0.006), ring(0.006)], PAINT)
            for x, y in ((0.09, -0.065), (0.09, 0.075), (0.24, -0.07), (0.24, 0.075), (l1 - 0.18, -0.06), (l1 - 0.18, 0.065)):
                b.lathe(th.at(x, y, s * 0.062), (0, 0, s), [(0.009, 0.0), (0.009, 0.018)], IRON, seg=6, smooth=False)
                bolt(plates, th.at(x, y, z + s * 0.006), (0, 0, s), r=0.0075, h=0.006)
            for k in range(5):   # жалюзи: рёбра над щелями
                x = 0.13 + 0.03 * k
                plates.box(th.at(x, 0.01, z + s * 0.009), (0.008, 0.09, 0.008), PAINT, bevel=0.002, rot=th.m)
            for k in range(3):
                x = l1 - 0.29 + 0.03 * k
                marks.box(th.at(x, 0.0, z + s * 0.0068), (0.014, 0.12, 0.002), MARK, bevel=0.0, rot=mat_mul(th.m, rot_z(0.45)))
    else:
        # задняя: узкая накладка по спинке и световой отражатель
        ring = lambda x, w: [th.at(x, hh(x) - 0.004, -w), th.at(x, hh(x) + 0.012, -w * 0.6), th.at(x, hh(x) + 0.012, w * 0.6),
                             th.at(x, hh(x) - 0.004, w)]
        plates.loft([ring(0.1, 0.035), ring(0.14, 0.05), ring(KNEE_A[0] - 0.0, 0.05)], PAINT)
        for s in (-1.0, 1.0):
            plates.box(th.at(l1 - 0.2, 0.0, s * 0.0595), (0.03, 0.05, 0.004), MARK, bevel=0.0, rot=th.m)
    return [(b, None), (plates, WEAR_LEG), (marks, None)]


# --- голень (кость shin): длинная гранёная балка, сверху рог под цилиндр колена, снизу вилка голеностопа ---

def shin_parts(L):
    from solid import solid
    sh, l2, front = L["sh"], L["l2"], L["kind"] == "front"
    hy = lambda x: 0.074 + (0.044 - 0.074) * max(x, 0.0) / l2
    hz = lambda x: 0.047 + (0.033 - 0.047) * max(x, 0.0) / l2
    core = Body()
    core.loft([_oct(sh, x, hy(x), hz(x), 0.014) for x in (0.0, 0.3 * l2, l2 - 0.06)], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.047, 0.047, 0.072, None, seg=18)
    # рог: клин от оси колена назад-вверх до проушины
    adds.loft([_rect(sh, 0.02, -0.04, 0.06, -0.037, 0.037), _rect(sh, -0.075, 0.02, 0.085, -0.037, 0.037)], None)
    for s in (-1.0, 1.0):   # две щеки рога, проушина штока — между ними
        lo, hi = sorted((s * 0.024, s * 0.037))
        adds.loft([_rect(sh, -0.07, 0.025, 0.08, lo, hi), _rect(sh, KNEE_B[0], KNEE_B[1] - 0.03, KNEE_B[1] + 0.02, lo, hi)], None)
        _disc(adds, sh, KNEE_B[0], KNEE_B[1], lo, hi, 0.032, None, seg=14)
    for s in (-1.0, 1.0):   # вилка голеностопа
        lo, hi = sorted((s * 0.03, s * 0.047))
        adds.loft([_rect(sh, x, -hy(x), hy(x), lo, hi) for x in (l2 - 0.11, l2)], None)
        _disc(adds, sh, l2, 0.0, lo, hi, 0.05, None, seg=16)
    cuts = Body()
    for s in (-1.0, 1.0):   # продольные выборки по бокам: двутавр, а не брусок
        cuts.loft([_rect(sh, x, -hy(x) + 0.024, hy(x) - 0.024, s * hz(x) - 0.008, s * hz(x) + 0.02) for x in (0.14, l2 - 0.2)], None)
    cuts.lathe(sh.at(0.0, 0.0, -0.06), (0, 0, 1), [(0.03, 0.0), (0.03, 0.12)], None, seg=14)
    cuts.lathe(sh.at(KNEE_B[0], KNEE_B[1], -0.05), (0, 0, 1), [(0.014, 0.0), (0.014, 0.1)], None, seg=10)
    t = solid([("add", core), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=30.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _disc(b, sh, KNEE_B[0], KNEE_B[1], s * 0.04 - 0.004, s * 0.04 + 0.004, 0.016, STEEL, seg=10)
        _disc(b, sh, l2, 0.0, s * 0.053 - 0.006, s * 0.053 + 0.006, 0.026, STEEL, seg=12)
    # поворотный привод голеностопа: стакан на одной щеке вилки, плоская крышка на другой
    b.lathe(sh.at(l2, 0.0, -0.047), (0, 0, -1), [(0.052, 0.0), (0.052, 0.01), (0.046, 0.014), (0.046, 0.042), (0.036, 0.05),
                                                 (0.016, 0.05)], IRON, seg=18)
    for k in range(6):
        a = 2 * math.pi * (k + 0.5) / 6
        bolt(b, add(sh.at(l2, 0.0, -0.057), (0.04 * math.cos(a), 0.04 * math.sin(a), 0.0)), (0, 0, -1), r=0.0065, h=0.005)
    _cap(b, sh, l2, 1.0, 0.047, 0.04, mat=IRON, n=5, rb=0.032)
    # магистраль к приводу голеностопа: по нижней грани голени в хомутах
    y = -hy(0.5 * l2) - 0.012
    path = [sh.at(0.09, -0.05, -0.03), sh.at(0.2, y - 0.004, -0.02), sh.at(l2 - 0.2, -hy(l2 - 0.2) - 0.012, -0.02),
            sh.at(l2 - 0.07, -0.04, -0.058), sh.at(l2 - 0.02, -0.02, -0.07)]
    b.tube(path, 0.0075, RUBBER, seg=6, fillet=0.025)
    for x in (0.3, 0.55, l2 - 0.25):
        b.box(sh.at(x, -hy(x) - 0.012, -0.02), (0.02, 0.026, 0.03), STEEL, bevel=0.002, rot=sh.m)
    guard = Body()
    if front:
        # щиток голени: по наружной спинке, с отгибами на бока, на болтах
        ring = lambda x, w: [sh.at(x, hy(x) - 0.018, -w), sh.at(x, hy(x) + 0.008, -w), sh.at(x, hy(x) + 0.018, -w * 0.5),
                             sh.at(x, hy(x) + 0.018, w * 0.5), sh.at(x, hy(x) + 0.008, w), sh.at(x, hy(x) - 0.018, w)]
        guard.loft([ring(0.07, 0.036), ring(0.11, hz(0.11) + 0.008), ring(0.42 * l2, hz(0.42 * l2) + 0.008),
                    ring(0.46 * l2, 0.03)], PAINT)
        for x in (0.15, 0.36 * l2):
            for s in (-1.0, 1.0):
                bolt(guard, sh.at(x, hy(x) + 0.004, s * (hz(x) + 0.009)), (0, 0, s), r=0.0065, h=0.005)
        for k in range(2):   # шеврон тёмной краской
            for s in (-1.0, 1.0):
                b.box(sh.at(0.3 * l2 + 0.035 * k, hy(0.3 * l2) + 0.0195, s * 0.012), (0.012, 0.002, 0.032), MARK, bevel=0.0,
                      rot=mat_mul(sh.m, rot_y(s * 0.7)))
    else:
        # задняя: два ребра жёсткости по спинке и табличка
        for s in (-1.0, 1.0):
            guard.loft([_rect(sh, x, hy(x) - 0.004, hy(x) + 0.014, s * 0.016 - 0.004, s * 0.016 + 0.004) for x in (0.1, l2 - 0.2)],
                       PAINT)
        guard.box(sh.at(0.4, hy(0.4) + 0.006, 0.0), (0.12, 0.008, 0.026), PAINT, bevel=0.002, rot=sh.m)
    return [(b, None), (guard, WEAR_LEG)]


# --- стойка голеностопа (кость tarsus, отвесна): ступица в вилке, корпус демпфера, шаровая опора внизу ---

def tarsus_parts(L):
    A, F = L["A"], L["F"]
    ta = Link(A, -0.5 * math.pi)
    b = Body()
    _disc(b, ta, 0.0, 0.0, -0.029, 0.029, 0.045, IRON, seg=18)
    b.loft([_rect(ta, x, -0.036, 0.036, -0.027, 0.027) for x in (0.0, 0.06)], IRON)
    b.lathe(add(A, (0.0, -0.056, 0.0)), (0, -1, 0), [(0.042, 0.0), (0.046, 0.008), (0.046, 0.05), (0.036, 0.058),
                                                   (0.026, 0.06)], IRON, seg=16)
    for k in range(4):   # стяжки корпуса
        a = 2 * math.pi * (k + 0.5) / 4
        b.box(add(A, (0.05 * math.cos(a), -0.08, 0.05 * math.sin(a))), (0.008, 0.05, 0.008), STEEL, bevel=0.0)
    sphere(b, F, 0.036, STEEL, seg=14, rings=7)
    return [(b, None)]


# --- лапа-опора (кость foot): широкая плита с косынками, резина снизу, когти наружу, чашка шаровой опоры ---

def foot_parts(L):
    F = L["F"]
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    H = FOOT_H
    f = Body()
    # плита: гранёная, сверху сужается
    plan = lambda e: [(0.2 - e, -0.1 + e), (0.2 - e, 0.1 - e), (0.12 - e, 0.13 - e), (-0.1 + e, 0.13 - e), (-0.14 + e, 0.09 - e),
                      (-0.14 + e, -0.09 + e), (-0.1 + e, -0.13 + e), (0.12 - e, -0.13 + e)]
    f.loft([[at(x, -H + 0.014, z) for x, z in plan(0.0)], [at(x, -H + 0.036, z) for x, z in plan(0.0)],
            [at(x, -H + 0.046, z) for x, z in plan(0.012)]], IRON)
    # резина подошвы: четыре подушки
    for sx, sz in ((0.085, 0.06), (0.085, -0.06), (-0.06, 0.06), (-0.06, -0.06)):
        f.box(at(sx, -H + 0.007, sz), (0.12, 0.016, 0.1), RUBBER, bevel=0.004)
    # чашка шаровой опоры с косынками к краям плиты
    f.lathe(at(0.0, -H + 0.046), (0, 1, 0), [(0.075, 0.0), (0.062, 0.012), (0.05, 0.036), (0.044, 0.05), (0.032, 0.054)], IRON, seg=20)
    for k in range(6):
        a = 2 * math.pi * k / 6
        d = (math.cos(a), 0.0, math.sin(a))
        f.loft([[at(0.055 * d[0] - 0.006 * d[2], -H + 0.046, 0.055 * d[2] + 0.006 * d[0]),
                 at(0.055 * d[0] + 0.006 * d[2], -H + 0.046, 0.055 * d[2] - 0.006 * d[0]),
                 at(0.05 * d[0] + 0.006 * d[2], -H + 0.08, 0.05 * d[2] - 0.006 * d[0]),
                 at(0.05 * d[0] - 0.006 * d[2], -H + 0.08, 0.05 * d[2] + 0.006 * d[0])],
                [at(0.115 * d[0] - 0.006 * d[2], -H + 0.046, 0.115 * d[2] + 0.006 * d[0]),
                 at(0.115 * d[0] + 0.006 * d[2], -H + 0.046, 0.115 * d[2] - 0.006 * d[0]),
                 at(0.115 * d[0] + 0.006 * d[2], -H + 0.05, 0.115 * d[2] - 0.006 * d[0]),
                 at(0.115 * d[0] - 0.006 * d[2], -H + 0.05, 0.115 * d[2] + 0.006 * d[0])]], IRON)
    # болты по краю плиты
    for x, z in ((0.17, 0.08), (0.17, -0.08), (0.05, 0.11), (0.05, -0.11), (-0.08, 0.11), (-0.08, -0.11), (-0.12, 0.0)):
        bolt(f, at(x, -H + 0.046, z), (0, 1, 0), r=0.008, h=0.006)
    # когти: три стальных клина наружу-вниз
    for z in (-0.07, 0.0, 0.07):
        f.loft([[at(0.195, -H + 0.04, z - 0.02), at(0.195, -H + 0.04, z + 0.02), at(0.195, -H + 0.012, z + 0.02),
                 at(0.195, -H + 0.012, z - 0.02)],
                [at(0.235, -H + 0.024, z - 0.012), at(0.235, -H + 0.024, z + 0.012), at(0.235, -H + 0.004, z + 0.012),
                 at(0.235, -H + 0.004, z - 0.012)],
                [at(0.258, -H + 0.004, z - 0.004), at(0.258, -H + 0.004, z + 0.004), at(0.258, -H - 0.008, z + 0.004),
                 at(0.258, -H - 0.008, z - 0.004)]], STEEL)
    # пыльник шаровой: резиновая манжета от чашки к шару
    f.lathe(at(0.0, -H + 0.098), (0, 1, 0), [(0.034, 0.0), (0.04, 0.006), (0.034, 0.012), (0.04, 0.018), (0.03, 0.024)],
            RUBBER, seg=14)
    return [(f, None)]


# --- гидроцилиндр: гильза с днищем и штуцерами, гайка сальника, хромированный шток с проушиной ---

def ram(a, b_, c_len, r_len, big, small):
    """Гильза от a к b_ длиной c_len и шток от b_ к a длиной r_len. Возвращает (тело гильзы, тело штока)."""
    d = unit(sub(b_, a))
    cyl, rod = Body(), Body()
    cyl.lathe(add(a, (0, 0, -0.022)), (0, 0, 1), [(big * 0.85, 0.0), (big * 0.85, 0.044)], IRON, seg=14)   # проушина днища
    cyl.lathe(add(a, mul(d, big * 0.5)), d, [(big * 0.6, 0.0), (big + 0.006, 0.012), (big + 0.006, 0.03), (big, 0.034),
                                             (big, c_len - 0.05), (big + 0.008, c_len - 0.046), (big + 0.008, c_len - 0.016),
                                             (small + 0.008, c_len - 0.01), (small + 0.008, c_len)], IRON, seg=16)
    # штуцеры у днища и у сальника, с шестигранниками
    side = (0.0, 0.0, 1.0)   # штуцеры вбок, к магистрали
    for t in (0.07, c_len - 0.07):
        o = add(add(a, mul(d, t)), mul(side, big - 0.004))
        cyl.lathe(o, side, [(0.011, 0.0), (0.011, 0.018), (0.014, 0.02), (0.014, 0.03), (0.008, 0.034)], STEEL, seg=6, smooth=False)
    rod.lathe(add(b_, (0, 0, -0.02)), (0, 0, 1), [(small * 1.25, 0.0), (small * 1.25, 0.04)], IRON, seg=12)   # проушина штока
    rod.lathe(add(b_, mul(d, -small * 1.1)), mul(d, -1.0), [(small * 0.9, 0.0), (small * 0.9, 0.012)], IRON, seg=10)
    rod.lathe(add(b_, mul(d, -r_len)), d, [(small, 0.0), (small, r_len - small * 1.1)], STEEL, seg=12)
    return cyl, rod


def stroke(points, pin_a, pin_b, joint, angles):
    """Длины гильзы и штока по ходу сустава: шток не выходит из гильзы и не торчит сзади. pin_b вертится вокруг
    joint на углы angles (от бинда)."""
    d = []
    for q in angles:
        v = sub(pin_b, joint)
        d.append(math.dist(pin_a, add(joint, mat_vec(rot_z(q), v))))
    lo, hi = min(d), max(d)
    c = r = min(lo - 0.03, (hi + 0.06) * 0.5)
    assert c + r >= hi + 0.04, ("ход цилиндра", lo, hi, c, r)
    return c, r


def joint_ranges(L):
    """Углы бедра (к горизонту) и колена (голень к бедру) от бинда по рабочим позам: стоит, привстала, присела, шаг
    вперёд-назад, лёжа (pack)."""
    l1, l2 = L["l1"], L["l2"]
    a0 = L["th"].a
    q0 = L["sh"].a - a0
    th, kn = [], []
    for h, reaches in ((0.21, (0.85, 1.0)), (0.4, (0.8, 1.15)), (0.58, (0.72, 1.28)), (0.82, (0.72, 1.28)), (1.0, (0.72, 1.28))):
        for reach in reaches:
            a, s = two_link(l1, l2, reach, FOOT_H + L3 - h)
            th.append(a - a0)
            kn.append((s - a) - q0)
    return th, kn


def leg_parts(L):
    """Части ноги в осях ноги: {кость: [(тело, износ)]} и цилиндры {кость: тело}. Кости без приставки ноги."""
    th_q, kn_q = joint_ranges(L)
    lc, lr = stroke(L, L["la"], L["lb"], L["P"], th_q)
    pa, pb = ram(L["la"], L["lb"], lc, lr, 0.032, 0.017)
    # цилиндр колена: проушина гильзы на бедре, шток — на роге голени, вертится вокруг колена
    kc, kr = stroke(L, L["ka"], L["kb"], L["K"], kn_q)
    ka, kb = ram(L["ka"], L["kb"], kc, kr, 0.036, 0.019)
    parts = {"hip": hip_parts(L), "thigh": thigh_parts(L), "shin": shin_parts(L), "tarsus": tarsus_parts(L),
             "foot": foot_parts(L)}
    return parts, {"pa": pa, "pb": pb, "ka": ka, "kb": kb}
