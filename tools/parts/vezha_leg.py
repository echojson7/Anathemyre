"""Нога «Вежи» (docs/vezha.md): паучья, из трёх звеньев. Бедро от поворотного узла идёт вверх-наружу, колено
выше рамы, голень уходит наружу-вниз ко второму суставу (скакательному), плюсна спускается почти отвесно к
карданному голеностопу, под ним — ступня-опора. Каждый сустав двигает свой гидроцилиндр: бедро — снизу от
узла, колено — поверх бедра на рог голени, скакательный — поверх голени на рог плюсны (как ковш экскаватора).

Два вида: передняя (front) — короче, бедро и голень под бронещитками с жалюзи; задняя (rear) — длиннее, открытая:
голень — ферма с окнами, бедро с карманами облегчения. Левая нога пары — зеркало правой.

Оси рецепта ноги: ось поворота узла — ось y в начале координат, нога уходит в +x, z — вбок. Звено описано в своих
осях (Link): x — вдоль звена от сустава к суставу, y — спинка (поворот x на +90° в плоскости ноги), z — вбок.
Угол плюсны к горизонту задан (META_*), нога решается двумя длинами L1, L2 до скакательного сустава.
"""
import math

from kit import Body, add, mat_mul, mat_vec, mul, rot_x, rot_y, rot_z, sub, unit

PAINT, WORN, IRON, STEEL = "vezha_paint", "vezha_worn", "vezha_iron", "vezha_steel"
RUBBER, GLASS, EYE, MARK = "rubber_black", "vezha_glass", "vezha_eye", "vezha_mark"
BRASS, CANVAS, OLIVE = "vezha_brass", "vezha_canvas", "vezha_olive"

# --- размеры, м ---
U_PITCH = 0.19                 # ось качания бедра — на столько дальше оси поворота
HIP_Y = 0.82                   # оси бёдер над землёй в бинде (стоит)
FOOT_H = 0.14                  # ось кардана голеностопа над подошвой
REACH = 1.0                    # бинд: ось кардана на столько дальше оси качания
LINKS = {"front": (0.56, 0.72, 0.58), "rear": (0.58, 0.74, 0.60)}   # бедро, голень, плюсна (между осями)
META_STAND, META_LIE = math.radians(-80.0), math.radians(-75.0)    # плюсна к горизонту: стоит и лежит
META_H0, META_SPAN = 0.3, 0.45   # плюсна переходит от «лежит» к «стоит» на высотах h0 … h0 + span (как вынос лап)
THIGH_MIN, THIGH_MAX = math.radians(-40.0), math.radians(105.0)   # ход бедра к горизонту
# Проушины гидроцилиндров (в осях своих звеньев): подъём бедра — под осью качания на узле и под бедром;
# колено — поверх бедра и на роге голени; скакательный — поверх голени и на роге плюсны
LIFT_A, LIFT_B = (-0.03, -0.155), (0.29, -0.125)
KNEE_A, KNEE_B = (0.04, 0.17), (-0.13, 0.085)
META_A, META_B = (0.25, 0.135), (-0.11, 0.10)
DECK = 0.092                   # палубы рамы над и под осями бёдер (между ними — барабан узла)

WEAR_LEG = (WORN, 0.03, 0.9, 0.12)   # щитки ног бьются сильнее корпуса


def meta_at(h):
    """Угол плюсны к горизонту при высоте осей бёдер h (то же в mech_leg_meta, src/view/mech_in.h)."""
    t = min(max((h - META_H0) / META_SPAN, 0.0), 1.0)
    t = t * t * (3.0 - 2.0 * t)
    return META_LIE + (META_STAND - META_LIE) * t


def sphere(b, c, r, mat, seg=12, rings=6):
    prof = [(r * math.sin(math.pi * (k + 0.5) / rings * (1 - 1e-3) + 5e-4), -r * math.cos(math.pi * (k + 0.5) / rings))
            for k in range(rings)]
    prof = [(r * 0.25, -r * 0.97)] + prof + [(r * 0.25, r * 0.97)]
    b.lathe(c, (0.0, 1.0, 0.0), [(pr, h) for pr, h in prof], mat, seg=seg)


def bolt(b, p, axis, r=0.011, h=0.008, mat=IRON):
    b.lathe(p, axis, [(r, 0.0), (r, h)], mat, seg=6, smooth=False)


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


def solve(kind, h, reach):
    """Углы бедра, голени и плюсны к горизонту: оси бёдер на высоте h, ось кардана на reach от оси качания."""
    l1, l2, l3 = LINKS[kind]
    m = meta_at(h)
    a, s = two_link(l1, l2, reach - l3 * math.cos(m), FOOT_H - l3 * math.sin(m) - h)
    a = min(max(a, THIGH_MIN), THIGH_MAX)
    j = (reach - l3 * math.cos(m), FOOT_H - l3 * math.sin(m) - h)
    s = math.atan2(j[1] - l1 * math.sin(a), j[0] - l1 * math.cos(a))
    return a, s, m


def leg_points(kind):
    """Суставы ноги вида kind в бинде и проушины цилиндров (оси ноги)."""
    l1, l2, l3 = LINKS[kind]
    P = (U_PITCH, HIP_Y, 0.0)
    a, s, m = solve(kind, HIP_Y, REACH)
    th = Link(P, a)
    K = th.end(l1)
    sh = Link(K, s)
    A = sh.end(l2)
    mt = Link(A, m)
    F = mt.end(l3)
    return {"kind": kind, "l1": l1, "l2": l2, "l3": l3, "P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh, "mt": mt,
            "la": (P[0] + LIFT_A[0], P[1] + LIFT_A[1], 0.0), "lb": th.at(*LIFT_B),
            "ka": th.at(*KNEE_A), "kb": sh.at(*KNEE_B), "ma": sh.at(*META_A), "mb": mt.at(*META_B)}


def _tab(tab, x):
    """Значение по таблице [(x, v)] с линейной вставкой."""
    if x <= tab[0][0]:
        return tab[0][1]
    for (x0, v0), (x1, v1) in zip(tab, tab[1:]):
        if x <= x1:
            return v0 + (v1 - v0) * (x - x0) / (x1 - x0)
    return tab[-1][1]


def _rect(link, x, y0, y1, z0, z1):
    return [link.at(x, y0, z0), link.at(x, y1, z0), link.at(x, y1, z1), link.at(x, y0, z1)]


def _oct(link, x, hy, hz, c, dy=0.0):
    """Сечение балки: прямоугольник 2hy × 2hz со снятыми на c углами (середина сдвинута на dy по спинке)."""
    pts = [(hy, hz - c), (hy - c, hz), (-hy + c, hz), (-hy, hz - c), (-hy, -hz + c), (-hy + c, -hz), (hy - c, -hz), (hy, -hz + c)]
    return [link.at(x, y + dy, z) for y, z in pts]


def _shell(link, x, top, w, yb, t=0.011, c=0.022, s=1.0):
    """Сечение щитка-корыта по спинке (s = 1) или по брюху (s = −1): внутри — крыша на top и бока на ±w до yb,
    толщина t, кромки сняты на c. Кольцо замкнутое: снаружи слева направо, внутри обратно."""
    T, W = top, w
    outer = [(yb, -W - t), (T - c, -W - t), (T + t, -W + c), (T + t, W - c), (T - c, W + t), (yb, W + t)]
    inner = [(yb, W), (T - c, W), (T, W - c), (T, -W + c), (T - c, -W), (yb, -W)]
    return [link.at(x, s * y, z) for y, z in outer + inner]


def _disc(b, link, x, y, z0, z1, r, mat, seg=14):
    """Цилиндр вдоль z ноги от z0 до z1 с осью в точке (x, y) звена."""
    b.lathe(link.at(x, y, z0), (0.0, 0.0, 1.0), [(r, 0.0), (r, z1 - z0)], mat, seg=seg)


def _cap(b, link, x, s, z, r, mat=STEEL, n=0, rb=0.0, y=0.0, nipple=False):
    """Крышка оси на боковой грани (s — знак стороны), n болтов вокруг на радиусе rb, маслёнка в середине."""
    o = link.at(x, y, s * z)
    b.lathe(o, (0.0, 0.0, s), [(r, 0.0), (r, 0.008), (r * 0.62, 0.016)], mat, seg=12)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        bolt(b, add(o, (rb * math.cos(a), rb * math.sin(a), s * 0.008)), (0.0, 0.0, s), r=0.0065, h=0.006)
    if nipple:
        b.lathe(add(o, (0.0, 0.0, s * 0.016)), (0.0, 0.0, s), [(0.006, 0.0), (0.006, 0.008), (0.004, 0.01), (0.004, 0.016),
                                                               (0.0055, 0.018), (0.002, 0.022)], BRASS, seg=6)


def _clamp(b, link, x, y, z, r, wide=0.0):
    """Хомут шланга: скоба поперёк звена (wide — шире, на два шланга)."""
    b.box(link.at(x, y, z), (0.016, 2 * r + 0.008, 2 * r + 0.012 + wide), STEEL, bevel=0.002, rot=link.m)


def _fork(adds, link, x0, x1, hy0, hy1, zi, zo, r):
    """Вилка: две щеки от x0 (высота ±hy0) к оси в x1 (±hy1), между zi и zo по обе стороны, диск r на оси."""
    for s in (-1.0, 1.0):
        lo, hi = sorted((s * zi, s * zo))
        adds.loft([_rect(link, x0, -hy0, hy0, lo, hi), _rect(link, x1, -hy1, hy1, lo, hi)], None)
        _disc(adds, link, x1, 0.0, lo, hi, r, None, seg=16)


def _ears(adds, link, x, y, base, zi, zo, r, span=0.05):
    """Две щеки проушины цилиндра от грани звена (base по спинке) к пальцу (x, y)."""
    sy = 1.0 if y > base else -1.0
    for s in (-1.0, 1.0):
        lo, hi = sorted((s * zi, s * zo))
        adds.loft([_rect(link, x + dx, base - sy * 0.012, y, lo, hi) for dx in (-span, span)], None)
        _disc(adds, link, x, y, lo, hi, r, None, seg=12)


def _iron(t):
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    return b


# --- поворотный узел (кость hip): барабан между палубами рамы, вилка оси качания, проушина цилиндра подъёма ---

def hip_parts(L):
    from solid import solid
    H = HIP_Y
    P = L["P"]
    hp, hp0 = Link(P, 0.0), Link((P[0], 0.0, 0.0), 0.0)
    core = Body()
    core.lathe((0.0, H - DECK + 0.004, 0.0), (0, 1, 0), [(0.09, 0.0), (0.09, 2 * DECK - 0.008)], None, seg=16)
    adds = Body()
    for s in (-1.0, 1.0):   # щёки вилки: от барабана к оси качания и вниз к проушине цилиндра
        z0, z1 = s * 0.062, s * 0.09
        lo, hi = min(z0, z1), max(z0, z1)
        adds.prism([(0.03, 0.08), (P[0] - 0.01, 0.09), (P[0] + 0.075, 0.03), (P[0] + 0.06, -0.09), (P[0] + 0.015, -0.19),
                    (P[0] - 0.08, -0.19), (0.03, -0.08)], "z", lo, hi, None, plane=("x", "y"))
        _disc(adds, hp0, 0.0, 0.0, lo, hi, 0.09, None, seg=16)
        _disc(adds, hp0, LIFT_A[0], LIFT_A[1], lo, hi, 0.042, None, seg=16)
    cuts = Body()
    for s in (-1.0, 1.0):   # выборка в щеке между осью и проушиной
        lo, hi = sorted((s * 0.08, s * 0.1))
        cuts.prism([(P[0] - 0.05, -0.06), (P[0] + 0.02, -0.06), (P[0] + 0.0, -0.11), (P[0] - 0.05, -0.11)], "z", lo, hi, None,
                   plane=("x", "y"))
    t = solid([("add", core), ("add", adds.placed(None, (0.0, H, 0.0))), ("cut", cuts.placed(None, (0.0, H, 0.0)))],
              bevel=0.005, bevel_angle=30.0)
    h = _iron(t)
    # венец барабана: зубья под шестерню мотора на раме
    for k in range(24):
        a = 2 * math.pi * (k + 0.5) / 24
        h.box((0.096 * math.cos(a), H + 0.058, 0.096 * math.sin(a)), (0.016, 0.024, 0.012), IRON, bevel=0.0, rot=rot_y(-a))
    # крышки сверху и снизу (над и под палубами) с болтами; поворотный штуцер магистрали сверху
    for y, s in ((H + DECK + 0.026, 1.0), (H - DECK - 0.026, -1.0)):
        h.lathe((0.0, y, 0.0), (0, s, 0), [(0.076, 0.0), (0.076, 0.016), (0.06, 0.024), (0.03, 0.026)], IRON, seg=20)
        for k in range(8):
            a = 2 * math.pi * (k + 0.5) / 8
            bolt(h, (0.066 * math.cos(a), y + s * 0.016, 0.066 * math.sin(a)), (0, s, 0), r=0.0075, h=0.006)
    h.lathe((0.0, H + DECK + 0.05, 0.0), (0, 1, 0), [(0.028, 0.0), (0.028, 0.03), (0.02, 0.036), (0.02, 0.05), (0.012, 0.056)],
            STEEL, seg=10)
    # ось качания: крышки с болтами и маслёнкой на обеих щеках; палец проушины цилиндра
    for s in (-1.0, 1.0):
        _cap(h, hp, 0.0, s, 0.09, 0.05, n=6, rb=0.064, nipple=s > 0)
        _cap(h, hp, LIFT_A[0], s, 0.09, 0.026, y=LIFT_A[1])
    _disc(h, hp, LIFT_A[0], LIFT_A[1], -0.062, 0.062, 0.014, STEEL, seg=10)
    for s in (-1.0, 1.0):
        _disc(h, hp, LIFT_A[0], LIFT_A[1], s * 0.044 - 0.018, s * 0.044 + 0.018, 0.021, IRON, seg=12)
    # шланги от штуцера к цилиндрам: по верху вилки к проушинам
    sw = (0.0, H + DECK + 0.07, 0.0)
    for s, end in ((1.0, (P[0] - 0.06, H - 0.14, 0.105)), (-1.0, (P[0] + 0.03, H + 0.1, -0.105))):
        h.tube([add(sw, (0.0, -0.01, s * 0.02)), add(sw, (0.05, -0.02, s * 0.05)), (P[0] - 0.06, H + 0.08, s * 0.105), end], 0.0085,
               RUBBER, seg=6, fillet=0.03)
    return [(h, None)]


# --- бедро (кость thigh): коробчатая балка с ребристой спинкой, на колене вилка; у передней — бронещёки ---

TH_HY = ((0.0, 0.09), (0.12, 0.1), (0.3, 0.108), (0.4, 0.096), (0.5, 0.084))   # полувысота бедра по длине
TH_HZ = ((0.0, 0.056), (0.09, 0.056), (0.16, 0.07), (0.38, 0.066), (0.5, 0.058))   # полуширина


def thigh_parts(L):
    from solid import solid
    th, l1, front = L["th"], L["l1"], L["kind"] == "front"
    hy = lambda x: _tab(TH_HY, x * 0.5 / l1)
    hz = lambda x: _tab(TH_HZ, x * 0.5 / l1)
    xs = [0.0, 0.09, 0.16, 0.3 * l1, 0.55 * l1, l1 - 0.11]
    core = Body()
    core.loft([_oct(th, x, hy(x), hz(x), 0.02) for x in xs], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.058, 0.058, 0.085, None, seg=16)
    _fork(adds, th, l1 - 0.17, l1, hy(l1 - 0.17) - 0.004, 0.075, 0.062, 0.088, 0.085)
    _ears(adds, th, LIFT_B[0], LIFT_B[1], -hy(LIFT_B[0]), 0.027, 0.042, 0.034)
    _ears(adds, th, KNEE_A[0], KNEE_A[1], hy(KNEE_A[0]), 0.028, 0.044, 0.036, span=0.045)
    cuts = Body()
    for s in (-1.0, 1.0):
        if front:   # передняя: неглубокая выборка под щеку
            cuts.loft([_rect(th, x, -hy(x) + 0.03, hy(x) - 0.03, s * hz(x) - 0.006, s * hz(x) + 0.02) for x in (0.17, l1 - 0.2)], None)
        else:       # задняя: длинный карман облегчения
            cuts.loft([_rect(th, x, -hy(x) + 0.025, hy(x) - 0.025, s * hz(x) - 0.012, s * hz(x) + 0.02) for x in (0.17, l1 - 0.2)], None)
    cuts.lathe(th.at(l1, 0.0, -0.1), (0, 0, 1), [(0.03, 0.0), (0.03, 0.2)], None, seg=14)   # гнездо пальца колена
    t = solid([("add", core), ("add", adds), ("cut", cuts)], bevel=0.005, bevel_angle=30.0)
    b = _iron(t)
    # рёбра жёсткости поперёк спинки (под щитком у передней, открыты у задней)
    for x in (0.19, 0.5 * (0.19 + 0.55 * l1), 0.55 * l1):
        b.box(th.at(x, hy(x) + 0.004, 0.0), (0.018, 0.024, 2 * hz(x) - 0.03), IRON, bevel=0.003, rot=th.m)
    for s in (-1.0, 1.0):
        _disc(b, th, 0.0, 0.0, s * 0.059 - 0.003, s * 0.059 + 0.003, 0.055, STEEL, seg=14)    # шайбы качания
        _disc(b, th, l1, 0.0, s * 0.08 - 0.012, s * 0.08 + 0.012, 0.034, STEEL, seg=12)      # палец колена
        _cap(b, th, l1, s, 0.088, 0.046, n=6, rb=0.058, nipple=s > 0)
        for (x, y) in (LIFT_B, KNEE_A):
            _disc(b, th, x, y, s * 0.046 - 0.004, s * 0.046 + 0.004, 0.018, STEEL, seg=10)
    # пара магистралей по боку бедра в хомутах (к цилиндрам колена и голени)
    side = -1.0
    for k, dy in enumerate((-0.012, 0.012)):
        zz = side * (hz(0.3) + 0.012)
        path = [th.at(0.08, -0.05 + dy, side * 0.062), th.at(0.17, dy - 0.02, zz), th.at(l1 - 0.2, dy - 0.02, side * (hz(l1 - 0.2) + 0.012)),
                th.at(l1 - 0.12, 0.05 + dy, side * 0.1), th.at(l1 + 0.04, 0.09 + dy, side * 0.1)]
        b.tube(path, 0.0075, RUBBER, seg=6, fillet=0.025)
    for x in (0.2, 0.36):
        _clamp(b, th, x, -0.02, side * (hz(x) + 0.012), 0.0075, wide=0.0)
        b.box(th.at(x, -0.02, side * (hz(x) + 0.004)), (0.02, 0.06, 0.008), STEEL, bevel=0.002, rot=th.m)
    plates, marks = Body(), Body()
    # щиток спинки: корыто из двух листов с зазором, по краю болты
    for x0, x1 in ((0.15, 0.3 * l1 + 0.1), (0.3 * l1 + 0.112, l1 - 0.19)):
        plates.loft([_shell(th, x, hy(x) + 0.016, hz(x) + 0.004, hy(x) * 0.25) for x in (x0, 0.5 * (x0 + x1), x1)], PAINT)
        for x in (x0 + 0.025, x1 - 0.025):
            for s in (-1.0, 1.0):
                bolt(plates, th.at(x, hy(x) * 0.6, s * (hz(x) + 0.015)), (0, 0, s), r=0.0065, h=0.005)
    if front:
        # бронещёки: трапеция на стойках с зазором, жалюзи, полосы знака у колена
        for s in (-1.0, 1.0):
            z = s * (0.095)
            prof = [(0.12, -0.105), (l1 - 0.17, -0.09), (l1 - 0.15, 0.05), (l1 - 0.2, 0.09), (0.16, 0.11), (0.12, 0.08)]
            ring = lambda dz: [th.at(x, y, z + dz) for x, y in prof]
            plates.loft([ring(-0.006), ring(0.006)], PAINT)
            for x, y in ((0.15, -0.08), (0.15, 0.07), (l1 - 0.2, -0.065), (l1 - 0.21, 0.06)):
                b.lathe(th.at(x, y, s * hz(x)), (0, 0, s), [(0.009, 0.0), (0.009, 0.095 - hz(x))], IRON, seg=6, smooth=False)
                bolt(plates, th.at(x, y, z + s * 0.006), (0, 0, s), r=0.0075, h=0.006)
            for k in range(6):   # жалюзи: рёбра над щелями
                x = 0.17 + 0.026 * k
                plates.box(th.at(x, 0.0, z + s * 0.009), (0.008, 0.1, 0.008), PAINT, bevel=0.002, rot=mat_mul(th.m, rot_z(0.12)))
            for k in range(3):
                x = l1 - 0.3 + 0.03 * k
                marks.box(th.at(x, -0.02, z + s * 0.0068), (0.014, 0.12, 0.002), MARK, bevel=0.0, rot=mat_mul(th.m, rot_z(0.45)))
    else:
        for s in (-1.0, 1.0):   # таблички в карманах
            plates.box(th.at(0.25, 0.0, s * (hz(0.25) - 0.011)), (0.06, 0.05, 0.004), MARK, bevel=0.0, rot=th.m)
    return [(b, None), (plates, WEAR_LEG), (marks, None)]


# --- голень (кость shin): гранёная балка-двутавр, сзади колена рог под цилиндр колена, на спинке проушина
# цилиндра плюсны, на конце вилка скакательного сустава. Передняя — под щитком, задняя — ферма с окнами ---

def shin_parts(L):
    from solid import solid
    sh, l2, front = L["sh"], L["l2"], L["kind"] == "front"
    hy = lambda x: _tab(((0.0, 0.086), (0.3 * l2, 0.078), (l2 - 0.1, 0.058)), x)
    hz = lambda x: _tab(((0.0, 0.056), (0.3 * l2, 0.052), (l2 - 0.1, 0.044)), x)
    xs = [0.0, 0.3 * l2, l2 - 0.1]
    core = Body()
    core.loft([_oct(sh, x, hy(x), hz(x), 0.018) for x in xs], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.057, 0.057, 0.08, None, seg=16)
    # рог: клин от оси колена назад-вверх до проушины
    adds.loft([_rect(sh, 0.03, -0.045, 0.07, -0.04, 0.04), _rect(sh, -0.08, 0.02, 0.095, -0.04, 0.04)], None)
    for s in (-1.0, 1.0):   # две щеки рога, проушина штока — между ними
        lo, hi = sorted((s * 0.026, s * 0.04))
        adds.loft([_rect(sh, -0.075, 0.025, 0.09, lo, hi), _rect(sh, KNEE_B[0], KNEE_B[1] - 0.034, KNEE_B[1] + 0.022, lo, hi)], None)
        _disc(adds, sh, KNEE_B[0], KNEE_B[1], lo, hi, 0.034, None, seg=14)
    _ears(adds, sh, META_A[0], META_A[1], hy(META_A[0]), 0.024, 0.038, 0.03)
    _fork(adds, sh, l2 - 0.15, l2, hy(l2 - 0.15) - 0.004, 0.06, 0.047, 0.068, 0.068)
    cuts = Body()
    for s in (-1.0, 1.0):   # продольные выборки по бокам: двутавр, а не брусок
        cuts.loft([_rect(sh, x, -hy(x) + 0.022, hy(x) - 0.022, s * hz(x) - 0.012, s * hz(x) + 0.02) for x in (0.13, l2 - 0.2)], None)
    if not front:           # задняя: окна насквозь через стенку — ферма
        for x0, x1 in ((0.17, 0.3), (0.34, l2 - 0.23)):
            m0, m1 = 0.5 * (x0 + x1), 0.5 * (x1 - x0)
            win = [sh.at(m0 - m1, -hy(x0) + 0.04, 0.0), sh.at(m0, -hy(m0) + 0.035, 0.0), sh.at(m0 + m1, -hy(x1) + 0.04, 0.0),
                   sh.at(m0 + m1, hy(x1) - 0.04, 0.0), sh.at(m0, hy(m0) - 0.035, 0.0), sh.at(m0 - m1, hy(x0) - 0.04, 0.0)]
            cuts.loft([[add(p, mat_vec(sh.m, (0.0, 0.0, z))) for p in win] for z in (-0.08, 0.08)], None)
    cuts.lathe(sh.at(0.0, 0.0, -0.07), (0, 0, 1), [(0.032, 0.0), (0.032, 0.14)], None, seg=14)
    cuts.lathe(sh.at(KNEE_B[0], KNEE_B[1], -0.05), (0, 0, 1), [(0.015, 0.0), (0.015, 0.1)], None, seg=10)
    cuts.lathe(sh.at(l2, 0.0, -0.08), (0, 0, 1), [(0.024, 0.0), (0.024, 0.16)], None, seg=12)
    t = solid([("add", core), ("add", adds), ("cut", cuts)], bevel=0.0045, bevel_angle=30.0)
    b = _iron(t)
    for s in (-1.0, 1.0):
        _disc(b, sh, KNEE_B[0], KNEE_B[1], s * 0.043 - 0.004, s * 0.043 + 0.004, 0.018, STEEL, seg=10)
        _disc(b, sh, META_A[0], META_A[1], s * 0.041 - 0.004, s * 0.041 + 0.004, 0.016, STEEL, seg=10)
        _disc(b, sh, l2, 0.0, s * 0.062 - 0.01, s * 0.062 + 0.01, 0.026, STEEL, seg=12)
        _cap(b, sh, l2, s, 0.068, 0.038, n=5, rb=0.048, nipple=s < 0)
    # магистраль по брюху голени в хомутах: к цилиндру плюсны и к скакательному суставу
    for k, z in enumerate((-0.016, 0.016)):
        path = [sh.at(0.07, -0.07, z * 3.5), sh.at(0.18, -hy(0.18) - 0.012, z), sh.at(l2 - 0.24, -hy(l2 - 0.24) - 0.012, z),
                sh.at(l2 - 0.14, -0.035, z * 4.5), sh.at(l2 - 0.06, 0.03, z * 5.2)]
        b.tube(path, 0.0072, RUBBER, seg=6, fillet=0.025)
    for x in (0.24, 0.42 * l2 + 0.08):
        b.box(sh.at(x, -hy(x) - 0.012, 0.0), (0.018, 0.026, 0.062), STEEL, bevel=0.002, rot=sh.m)
    guard, marks = Body(), Body()
    if front:
        # щиток голени: корыто по спинке от колена до середины, коленная накладка, жалюзи, шеврон
        xs = (0.07, 0.16, 0.42 * l2)
        guard.loft([_shell(sh, x, hy(x) + 0.016, hz(x) + 0.004, hy(x) * 0.15) for x in xs], PAINT)
        guard.loft([_shell(sh, x, hy(x) + 0.016, hz(x) + 0.004, hy(x) * 0.4) for x in (0.42 * l2 + 0.012, l2 - 0.2)], PAINT)
        guard.loft([_rect(sh, x, -0.02, 0.05, -0.05, 0.05) for x in (-0.07, -0.02)], PAINT)   # накладка на рог
        for x in (0.11, 0.42 * l2 - 0.03, l2 - 0.23):
            for s in (-1.0, 1.0):
                bolt(guard, sh.at(x, hy(x) * 0.55, s * (hz(x) + 0.015)), (0, 0, s), r=0.0065, h=0.005)
        for k in range(4):   # жалюзи по бокам у колена
            for s in (-1.0, 1.0):
                guard.box(sh.at(0.2 + 0.024 * k, hy(0.2) * 0.45, s * (hz(0.2) + 0.016)), (0.007, 0.04, 0.006), PAINT, bevel=0.0015,
                          rot=sh.m)
        for k in range(2):   # шеврон тёмной краской
            for s in (-1.0, 1.0):
                marks.box(sh.at(0.62 * l2 + 0.035 * k, hy(0.62 * l2) + 0.0275, s * 0.013), (0.012, 0.002, 0.034), MARK, bevel=0.0,
                          rot=mat_mul(sh.m, rot_y(s * 0.7)))
    else:
        # задняя: рёбра по спинке, накладка у колена, табличка
        for s in (-1.0, 1.0):
            guard.loft([_rect(sh, x, hy(x) - 0.004, hy(x) + 0.014, s * 0.02 - 0.0045, s * 0.02 + 0.0045) for x in (0.1, l2 - 0.2)],
                       PAINT)
        guard.loft([_shell(sh, x, hy(x) + 0.014, hz(x) + 0.004, hy(x) * 0.3) for x in (0.06, 0.14)], PAINT)
        marks.box(sh.at(0.5 * l2, hy(0.5 * l2) + 0.0145, 0.0), (0.1, 0.002, 0.03), MARK, bevel=0.0, rot=sh.m)
    return [(b, None), (guard, WEAR_LEG), (marks, None)]


# --- плюсна (кость tarsus): ступица в вилке голени, рог под шток цилиндра, литой корпус под щитком, ниже —
# амортизатор с пружиной и гофрой, внизу вилка кардана ---

def tarsus_parts(L):
    from solid import solid
    mt, l3, front = L["mt"], L["l3"], L["kind"] == "front"
    hy = lambda x: _tab(((0.0, 0.06), (0.15, 0.064), (0.3, 0.052)), x)
    hz = lambda x: _tab(((0.0, 0.045), (0.15, 0.045), (0.3, 0.04)), x)
    xd = 0.3                      # начало амортизатора
    core = Body()
    core.loft([_oct(mt, x, hy(x), hz(x), 0.016) for x in (0.0, 0.15, xd)], None)
    adds = Body()
    _disc(adds, mt, 0.0, 0.0, -0.045, 0.045, 0.064, None, seg=16)
    # рог: клин от ступицы вверх-наружу до проушины штока
    adds.loft([_rect(mt, 0.04, 0.0, 0.06, -0.034, 0.034), _rect(mt, -0.06, 0.03, 0.085, -0.034, 0.034)], None)
    for s in (-1.0, 1.0):
        lo, hi = sorted((s * 0.022, s * 0.034))
        adds.loft([_rect(mt, -0.055, 0.035, 0.085, lo, hi), _rect(mt, META_B[0], META_B[1] - 0.026, META_B[1] + 0.02, lo, hi)], None)
        _disc(adds, mt, META_B[0], META_B[1], lo, hi, 0.028, None, seg=14)
    # фланец амортизатора
    adds.lathe(mt.at(xd - 0.012, 0.0), mt.dir(1, 0), [(0.06, 0.0), (0.06, 0.016)], None, seg=16)
    cuts = Body()
    for s in (-1.0, 1.0):   # выборки по бокам корпуса
        cuts.loft([_rect(mt, x, -hy(x) + 0.018, hy(x) - 0.018, s * hz(x) - 0.008, s * hz(x) + 0.02) for x in (0.09, xd - 0.04)], None)
    cuts.lathe(mt.at(0.0, 0.0, -0.06), (0, 0, 1), [(0.024, 0.0), (0.024, 0.12)], None, seg=12)
    cuts.lathe(mt.at(META_B[0], META_B[1], -0.05), (0, 0, 1), [(0.013, 0.0), (0.013, 0.1)], None, seg=10)
    t = solid([("add", core), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=30.0)
    b = _iron(t)
    for s in (-1.0, 1.0):
        _disc(b, mt, META_B[0], META_B[1], s * 0.039 - 0.004, s * 0.039 + 0.004, 0.016, STEEL, seg=10)
    # амортизатор: стакан, пружина между тарелками, шток, гофра, внизу — вилка кардана
    ax = mt.dir(1, 0)
    o = mt.at(xd, 0.0)
    at = lambda x: add(o, mul(ax, x))
    b.lathe(at(0.0), ax, [(0.034, 0.0), (0.034, 0.09), (0.04, 0.095), (0.04, 0.11), (0.026, 0.115)], IRON, seg=14)
    b.lathe(at(0.115), ax, [(0.016, 0.0), (0.016, 0.07)], STEEL, seg=10)
    for x, r in ((0.004, 0.058), (0.165, 0.055)):   # тарелки
        b.lathe(at(x), ax, [(0.03, 0.0), (r, 0.004), (r, 0.012), (0.03, 0.016)], STEEL, seg=16)
    n, turns = 40, 5.5
    side = unit(mt.dir(0, 1))
    zax = (0.0, 0.0, 1.0)
    coil = []
    for k in range(n + 1):
        u = k / n
        a = 2 * math.pi * turns * u
        coil.append(add(at(0.02 + u * 0.143), add(mul(side, 0.046 * math.cos(a)), mul(zax, 0.046 * math.sin(a)))))
    b.tube(coil, 0.0068, STEEL, seg=5)
    # гофра на штоке
    b.lathe(at(0.18), ax, [(0.03, 0.0), (0.036, 0.008), (0.028, 0.016), (0.036, 0.024), (0.028, 0.032), (0.034, 0.04),
                           (0.024, 0.046)], RUBBER, seg=12)
    # вилка кардана: перемычка и две щеки к оси (вдоль z) в ступне
    yk = Body()
    y0 = xd + 0.225
    yk.loft([_rect(mt, x, -0.04, 0.04, -0.058, 0.058) for x in (y0, y0 + 0.022)], None)
    for s in (-1.0, 1.0):
        lo, hi = sorted((s * 0.044, s * 0.058))
        yk.loft([_rect(mt, y0 + 0.01, -0.04, 0.04, lo, hi), _rect(mt, l3, -0.028, 0.028, lo, hi)], None)
        _disc(yk, mt, l3, 0.0, lo, hi, 0.03, None, seg=14)
    yk.lathe(mt.at(y0 - 0.012, 0.0), ax, [(0.022, 0.0), (0.022, 0.014)], None, seg=10)
    t = solid([("add", yk)], bevel=0.004, bevel_angle=30.0)
    b.extend(_iron(t))
    for s in (-1.0, 1.0):
        _cap(b, mt, l3, s, 0.058, 0.024, n=4, rb=0.03)
    # магистраль от цилиндра к амортизатору по спинке в хомуте
    b.tube([mt.at(-0.02, 0.07, -0.042), mt.at(0.08, hy(0.08) + 0.01, -0.048), mt.at(0.24, hy(0.24) + 0.008, -0.044),
            mt.at(xd + 0.05, 0.042, -0.02)], 0.0065, RUBBER, seg=6, fillet=0.02)
    _clamp(b, mt, 0.16, hy(0.16) + 0.008, -0.046, 0.0065)
    guard = Body()
    # щиток плюсны снаружи (по спинке): у передней — во всю длину корпуса с рёбрами, у задней — короткий
    x1 = xd - 0.01 if front else 0.2
    guard.loft([_shell(mt, x, hy(x) + 0.014, hz(x) + 0.004, hy(x) * 0.1) for x in (0.06, 0.5 * (0.06 + x1), x1)], PAINT)
    for x in (0.09, x1 - 0.03):
        for s in (-1.0, 1.0):
            bolt(guard, mt.at(x, hy(x) * 0.5, s * (hz(x) + 0.015)), (0, 0, s), r=0.006, h=0.005)
    if front:
        for k in range(3):
            x = 0.12 + 0.05 * k
            guard.box(mt.at(x, hy(x) + 0.03, 0.0), (0.012, 0.012, 0.07), PAINT, bevel=0.003, rot=mt.m)
    return [(b, None), (guard, WEAR_LEG)]


# --- ступня (кость foot): крестовина кардана, своя вилка, литой корпус, стальная подошва с отгибом носка,
# протектор из резиновых плит, рёбра, два амортизатора носка, рым ---

def foot_parts(L):
    F = L["F"]
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    H = FOOT_H
    f = Body()
    # крестовина: кубик с цапфами вдоль z (в вилке плюсны) и вдоль x (в вилке ступни)
    f.box(at(0.0, 0.0), (0.05, 0.05, 0.05), STEEL, bevel=0.006)
    f.lathe(at(0.0, 0.0, -0.06), (0, 0, 1), [(0.016, 0.0), (0.016, 0.12)], STEEL, seg=10)
    f.lathe(at(-0.062, 0.0), (1, 0, 0), [(0.016, 0.0), (0.016, 0.124)], STEEL, seg=10)
    # вилка ступни (щёки поперёк x) и литой корпус: от вилки вниз к подошве, гранёный
    from solid import solid
    c = Body()
    yb = -H + 0.042          # верх подошвы
    for s in (-1.0, 1.0):
        lo, hi = sorted((s * 0.03, s * 0.046))
        c.prism([(-0.03, 0.0), (0.03, 0.0), (0.045, -0.06), (-0.045, -0.06)], "x", lo, hi, None, plane=("z", "y"))
        c.lathe(at(lo, 0.0), (1, 0, 0), [(0.03, 0.0), (0.03, hi - lo)], None, seg=14)
    oc = lambda y, hx, hz, k: [at(x, y, z) for x, z in ((hx, hz - k), (hx - k, hz), (-hx + k, hz), (-hx, hz - k), (-hx, -hz + k),
                                                         (-hx + k, -hz), (hx - k, -hz), (hx, -hz + k))]
    c.loft([oc(-0.05, 0.055, 0.05, 0.016), oc(-0.066, 0.07, 0.062, 0.02), oc(yb + 0.012, 0.12, 0.085, 0.03), oc(yb, 0.125, 0.09, 0.03)],
           None)
    # рёбра: назад и вбок от корпуса к краю подошвы
    for d, w in (((-1.0, 0.0), 0.16), ((0.0, 1.0), 0.13), ((0.0, -1.0), 0.13)):
        n = (-d[1], d[0])
        e = 0.007
        ring = lambda r, y: [at(d[0] * r + n[0] * e, y, d[1] * r + n[1] * e), at(d[0] * r - n[0] * e, y, d[1] * r - n[1] * e)]
        c.loft([ring(0.05, -0.05) + list(reversed(ring(0.05, yb + 0.001))), ring(0.07, -0.062) + list(reversed(ring(w, yb + 0.001)))],
               None)
    cuts = Body()
    cuts.lathe(at(-0.06, 0.0), (1, 0, 0), [(0.017, 0.0), (0.017, 0.12)], None, seg=10)
    t = solid([("add", c), ("cut", cuts)], bevel=0.004, bevel_angle=30.0)
    f.extend(_iron(t))
    for s in (-1.0, 1.0):   # колпачки цапф снаружи вилки ступни
        f.lathe(at(s * 0.046, 0.0), (s, 0, 0), [(0.022, 0.0), (0.022, 0.006), (0.014, 0.01)], STEEL, seg=10)
    # подошва: стальной лист со снятыми углами, сверху тоньше; носок отогнут вверх, пятка — коротким отгибом
    plan = lambda e: [(0.16 - e, -0.1 + e), (0.16 - e, 0.1 - e), (0.12 - e, 0.125 - e), (-0.12 + e, 0.125 - e), (-0.15 + e, 0.095 - e),
                      (-0.15 + e, -0.095 + e), (-0.12 + e, -0.125 + e), (0.12 - e, -0.125 + e)]
    f.loft([[at(x, -H + 0.022, z) for x, z in plan(0.0)], [at(x, yb - 0.006, z) for x, z in plan(0.0)],
            [at(x, yb, z) for x, z in plan(0.006)]], STEEL)
    for x0, x1, y1, w in ((0.15, 0.215, 0.05, 0.095), (-0.14, -0.185, 0.03, 0.08)):
        ring = lambda x, y, k: [at(x, y, -w + k), at(x, y + 0.016, -w + k), at(x, y + 0.016, w - k), at(x, y, w - k)]
        f.loft([ring(x0, -H + 0.024, 0.0), ring(x0 + 0.6 * (x1 - x0), -H + 0.032 + 0.4 * y1, 0.006), ring(x1, -H + 0.024 + y1, 0.02)],
               STEEL)
    # протектор: шесть резиновых плит с канавками между ними, по краю — фаска
    for x in (-0.095, 0.0, 0.095):
        for z in (-0.058, 0.058):
            f.box(at(x, -H + 0.011, z), (0.086, 0.022, 0.104), RUBBER, bevel=0.005)
    # болты по краю подошвы и на рёбрах корпуса
    for x, z in ((0.13, 0.09), (0.13, -0.09), (0.04, 0.108), (0.04, -0.108), (-0.06, 0.108), (-0.06, -0.108), (-0.13, 0.07),
                 (-0.13, -0.07)):
        bolt(f, at(x, yb, z), (0, 1, 0), r=0.0075, h=0.006)
    # амортизаторы носка: от проушин на корпусе вниз-вперёд к уху на отгибе носка, с пружиной
    for z in (-0.062, 0.062):
        a, e = at(0.05, -0.058, z), at(0.17, -H + 0.05, z)
        d = unit(sub(e, a))
        ln = math.dist(a, e)
        f.lathe(a, d, [(0.013, 0.0), (0.013, ln * 0.5), (0.01, ln * 0.52)], IRON, seg=10)
        f.lathe(add(a, mul(d, ln * 0.5)), d, [(0.0065, 0.0), (0.0065, ln * 0.5)], STEEL, seg=8)
        for p in (a, e):
            f.lathe(add(p, (0.0, 0.0, -0.012)), (0, 0, 1), [(0.012, 0.0), (0.012, 0.024)], IRON, seg=10)
        f.lathe(add(a, mul(d, ln * 0.36)), d, [(0.016, 0.0), (0.018, 0.006), (0.015, 0.012), (0.018, 0.018), (0.012, 0.026)], RUBBER,
                seg=8)
    # рым на корпусе сзади
    f.torus(at(-0.085, -0.06, 0.0), (0, 0, 1), 0.016, 0.0045, STEEL, seg=10, tseg=5)
    # шире по плану (высоты те же), крестовина — без растяжки
    k = 1.15
    g = Body()
    for pts, mat, sm in f.faces:
        g.faces.append((tuple((F[0] + (p[0] - F[0]) * k, p[1], F[2] + (p[2] - F[2]) * k) if p[1] < F[1] - 0.04 else p
                              for p in pts), mat, sm))
    return [(g, None)]


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
    # трубка между штуцерами вдоль гильзы
    p0 = add(add(a, mul(d, 0.07)), mul(side, big + 0.024))
    p1 = add(add(a, mul(d, c_len - 0.07)), mul(side, big + 0.024))
    cyl.tube([p0, add(p0, mul(side, 0.008)), add(p1, mul(side, 0.008)), p1], 0.0055, STEEL, seg=6, fillet=0.006)
    rod.lathe(add(b_, (0, 0, -0.02)), (0, 0, 1), [(small * 1.25, 0.0), (small * 1.25, 0.04)], IRON, seg=12)   # проушина штока
    rod.lathe(add(b_, mul(d, -small * 1.1)), mul(d, -1.0), [(small * 0.9, 0.0), (small * 0.9, 0.012)], IRON, seg=10)
    rod.lathe(add(b_, mul(d, -r_len)), d, [(small, 0.0), (small, r_len - small * 1.1)], STEEL, seg=12)
    return cyl, rod


def stroke(pin_a, pin_b, joint, angles):
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


POSE_GRID = ((0.21, (0.95, 1.1, 1.2)), (0.4, (0.8, 1.15)), (0.58, (0.75, 1.15)), (0.74, (0.8, 1.15)), (0.82, (0.75, 1.15)),
             (1.0, (0.75, 1.1)))


def joint_ranges(L):
    """Углы бедра (к горизонту), колена (голень к бедру) и скакательного (плюсна к голени) от бинда по рабочим
    позам: стоит, привстала, присела, шаг вперёд-назад, лёжа (pack)."""
    a0, s0, m0 = L["th"].a, L["sh"].a, L["mt"].a
    th, kn, mt = [], [], []
    for h, reaches in POSE_GRID:
        for reach in reaches:
            a, s, m = solve(L["kind"], h, reach)
            th.append(a - a0)
            kn.append((s - a) - (s0 - a0))
            mt.append((m - s) - (m0 - s0))
    return th, kn, mt


def leg_parts(L):
    """Части ноги в осях ноги: {кость: [(тело, износ)]} и цилиндры {кость: тело}. Кости без приставки ноги."""
    th_q, kn_q, mt_q = joint_ranges(L)
    lc, lr = stroke(L["la"], L["lb"], L["P"], th_q)
    pa, pb = ram(L["la"], L["lb"], lc, lr, 0.04, 0.021)
    # цилиндр колена: проушина гильзы на бедре, шток — на роге голени, вертится вокруг колена
    kc, kr = stroke(L["ka"], L["kb"], L["K"], kn_q)
    ka, kb = ram(L["ka"], L["kb"], kc, kr, 0.04, 0.021)
    # цилиндр плюсны: гильза на спинке голени, шток — на роге плюсны, вертится вокруг скакательного
    mc, mr = stroke(L["ma"], L["mb"], L["A"], mt_q)
    ma, mb = ram(L["ma"], L["mb"], mc, mr, 0.032, 0.017)
    parts = {"hip": hip_parts(L), "thigh": thigh_parts(L), "shin": shin_parts(L), "tarsus": tarsus_parts(L),
             "foot": foot_parts(L)}
    return parts, {"pa": pa, "pb": pb, "ka": ka, "kb": kb, "ma": ma, "mb": mb}
