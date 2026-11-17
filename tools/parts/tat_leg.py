"""Ноги «Тати» (docs/tat.md): четыре одинаковые ноги с двумя суставами, по принципу ролика-концепта и примера
пользователя (17.11): бедро поднимается из-под угла крыши наружу и вверх, колено стоит выше крыши, дальше нога идёт
вниз двумя звеньями — длинная голень до голеностопа и наклонная плюсна до ступни. Ступня — платформа: подошва
с резиной, два носка, пятка, вилка голеностопа.

Строение: кулак поворота под рамой и моторы качания; бедро — балка коробом с приводом сверху и щитком; голень —
две боковые щёки с окнами, распорки, трубопровод между щёк, щиток снаружи; плюсна — коробчатая балка с
амортизатором; два поршня через суставы (колено — по спинке бедра к рогу голени, голеностоп — изнутри), каждый — цилиндр и шток на
своих костях (колено — ka/kb, голеностоп — pa/pb, как у Вежи: src/view/mech.cpp), поза наводит их друг на друга. Нога симметрична поперёк своей плоскости, поэтому
левые и правые ноги — одно и то же тело, повёрнутое на свой угол.

Оси рецепта ноги — как у Вежи (tools/parts/vezha_leg.py): ось поворота бедра — ось y в начале координат, нога
уходит в +x, z — вбок. Звено описано в своих осях (Link): x — вдоль звена, y — спинка, z — вбок.
Плюсна держит постоянный угол TAR к горизонту в осях рамы: нога решается как две длины L1 и L2 до голеностопа.
"""
import math

from kit import Body, add, mul, sub, unit
from vezha_leg import Link, bolt

PAINT, WORN, IRON, STEEL = "tat_paint", "tat_worn", "tat_iron", "vezha_steel"
RUBBER, GLASS, LENS, MARK, CABLE = "rubber_black", "vezha_glass", "tat_lens", "tat_mark", "tat_cable"
BRASS = "vezha_brass"

HIP_Y = 0.62                          # оси бёдер над опорой в бинде (стоит)
WEAR_LEG = (WORN, 0.014, 0.65, 0.025)  # щитки ног бьются сильнее корпуса

# --- размеры ноги, м (числа решения — в MECH_DESC, src/view/mech_common.cpp) ---
U = 0.075                  # ось качания бедра дальше оси поворота
L1, L2, L3 = 0.36, 0.56, 0.24   # бедро, голень, плюсна (между осями суставов)
TAR = math.radians(-100.0)  # плюсна к горизонту: вниз и чуть внутрь
FOOT_H = 0.07              # ось ступни над подошвой
REACH = 0.38               # бинд: вынос оси ступни от оси качания
TH_MIN, TH_MAX = -40.0, 100.0   # ход бедра к горизонту, град

# Поршни (точки в осях звена): колено — по спинке бедра к рогу голени за осью колена (как стрела экскаватора),
# голеностоп — по внутренней грани голени к плюсне. Точки подобраны перебором: ход в пределах длины цилиндра
KNEE_A, KNEE_B = (0.06, 0.066), (-0.05, 0.066)
ANK_A, ANK_B = (0.30, -0.05), (0.14, -0.05)


class Leg:
    """Место ноги на раме: имя, поворот рецепта вокруг y (+x рецепта → (cos, 0, −sin)), ось поворота (x, z)."""

    def __init__(self, name, deg, corner):
        self.name, self.deg, self.corner = name, deg, corner
        self.u, self.l1, self.l2, self.l3, self.foot_h = U, L1, L2, L3, FOOT_H
        self.th_min, self.th_max = math.radians(TH_MIN), math.radians(TH_MAX)
        self.a1, self.a2, self.a3 = solve(self, REACH, FOOT_H - HIP_Y)

    def points(self, a1=None, a2=None):
        """Суставы (оси ноги): ось качания P, колено K, голеностоп A, ось ступни F, звенья и концы поршней.
        Без углов — бинд."""
        a1 = self.a1 if a1 is None else a1
        a2 = self.a2 if a2 is None else a2
        P = (self.u, HIP_Y, 0.0)
        th = Link(P, a1)
        K = th.end(self.l1)
        sh = Link(K, a2)
        A = sh.end(self.l2)
        tr = Link(A, TAR)
        F = tr.end(self.l3)
        return {"P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh, "tr": tr,
                "ka": th.at(*KNEE_A), "kb": sh.at(*KNEE_B), "pa": sh.at(*ANK_A), "pb": tr.at(*ANK_B)}


def solve(leg, x, y):
    """Углы бедра, голени и плюсны к горизонту для оси ступни в (x, y) от оси качания (колено вверх)."""
    la, lb = leg.l1, leg.l2
    tx, ty = x - leg.l3 * math.cos(TAR), y - leg.l3 * math.sin(TAR)
    d = min(max(math.hypot(tx, ty), abs(la - lb) + 1e-4), la + lb - 1e-4)
    a = math.atan2(ty, tx) + math.acos(max(-1.0, min(1.0, (la * la + d * d - lb * lb) / (2 * la * d))))
    a = min(max(a, leg.th_min), leg.th_max)
    s = math.atan2(ty - la * math.sin(a), tx - la * math.cos(a))
    return a, s, TAR


# Ноги по кругу fr, fl, rl, rr: углы на 40° от поперечной оси, рама 0.40 × 0.50 м между осями поворота
LEGS = (
    Leg("fr", -40.0, (0.20, 0.24)),
    Leg("fl", -140.0, (-0.20, 0.24)),
    Leg("rl", 140.0, (-0.20, -0.26)),
    Leg("rr", 40.0, (0.20, -0.26)),
)


def piston_lengths(leg):
    """Длины цилиндров и штоков двух поршней по всему ходу ноги (стоит, лёжа, привстала, шаг, сложена):
    шток не выходит из цилиндра и не торчит за проушину. [(цилиндр, шток) колена, (…) голеностопа]."""
    knee, ank = [], []
    for k in range(23):
        h = 0.36 + 0.44 * k / 22                      # высота осей бёдер 0.36 … 0.80
        for j in range(23):
            x = 0.28 + 0.44 * j / 22                  # вынос ступни 0.28 … 0.72 (выше 0.77 — до 0.66)
            if h > 0.77 and x > 0.66:
                continue
            a, s, _ = solve(leg, x, FOOT_H - h)
            L = leg.points(a, s)
            knee.append(math.dist(L["ka"], L["kb"]))
            ank.append(math.dist(L["pa"], L["pb"]))
    out = []
    for d in (knee, ank):
        lo, hi = min(d), max(d)
        c = r = min(lo - 0.03, (hi + 0.05) * 0.5)
        assert c + r >= hi + 0.035, ("ход поршня", lo, hi, c, r)
        out.append((c, r))
    return out


# --- общие мелочи ---

def _rect(link, x, y0, y1, z0, z1):
    return [link.at(x, y0, z0), link.at(x, y1, z0), link.at(x, y1, z1), link.at(x, y0, z1)]


def _hexr(link, x, w, h, c=0.35):
    """Восьмигранное сечение звена: ширина w (по z), высота h (по y), срез углов долей c."""
    return [link.at(x, -h, -w * (1 - c)), link.at(x, -h * (1 - c), -w), link.at(x, h * (1 - c), -w),
            link.at(x, h, -w * (1 - c)), link.at(x, h, w * (1 - c)), link.at(x, h * (1 - c), w),
            link.at(x, -h * (1 - c), w), link.at(x, -h, w * (1 - c))]


def _disc(b, link, x, y, z0, z1, r, mat, seg=16):
    b.lathe(link.at(x, y, z0), (0.0, 0.0, 1.0), [(r, 0.0), (r, z1 - z0)], mat, seg=seg)


def _cap(b, link, x, s, z, r, mat=STEEL, n=0, rb=0.0):
    """Крышка оси на боковой грани (s — знак стороны) и n болтов вокруг."""
    b.lathe(link.at(x, 0.0, s * z), (0.0, 0.0, s), [(r, 0.0), (r, 0.007), (r * 0.7, 0.012)], mat, seg=14)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        bolt(b, add(link.at(x, 0.0, s * z), (rb * math.cos(a), rb * math.sin(a), 0.0)), (0.0, 0.0, s), r=0.0055, h=0.005)


def _motor(b, link, x, s, z, r=0.046, depth=0.04, fins=10):
    """Стакан мотора сустава на боковой грани: рёбра охлаждения по кругу, крышка, гайка."""
    o = link.at(x, 0.0, s * z)
    d = (0.0, 0.0, s)
    b.lathe(o, d, [(r, 0.0), (r, 0.008), (r * 0.88, 0.012), (r * 0.88, depth), (r * 0.72, depth + 0.006), (r * 0.32, depth + 0.006)],
            IRON, seg=18)
    for k in range(fins):
        a = 2 * math.pi * k / fins
        c = add(o, (math.cos(a) * r * 0.93, math.sin(a) * r * 0.93, s * depth * 0.55))
        b.box(c, (0.007, 0.007, depth * 0.72), IRON, bevel=0.0)
    bolt(b, add(o, (0.0, 0.0, s * (depth + 0.006))), d, r=0.009, h=0.006, mat=STEEL)


def _hose(b, pts, r=0.0065, mat=CABLE):
    b.tube(pts, r, mat, seg=6, fillet=0.02)


def _lug(b, link, p, half=0.016, r=0.017):
    """Проушина пальца поршня на звене: щёки и палец поперёк."""
    x, y = p
    for s in (-1.0, 1.0):
        b.loft([[link.at(x - 0.02, 0.0, s * half - 0.004), link.at(x + 0.02, 0.0, s * half - 0.004), link.at(x + 0.02, 0.0, s * half + 0.004),
                 link.at(x - 0.02, 0.0, s * half + 0.004)],
                [link.at(x - r, y, s * half - 0.004), link.at(x + r, y, s * half - 0.004), link.at(x + r, y, s * half + 0.004),
                 link.at(x - r, y, s * half + 0.004)]], IRON)
        b.lathe(link.at(x, y, s * half), (0.0, 0.0, s), [(r, 0.0), (r, 0.004)], IRON, seg=12)
    b.lathe(link.at(x, y, -half - 0.008), (0.0, 0.0, 1.0), [(0.0075, 0.0), (0.0075, 2 * half + 0.016)], STEEL, seg=8)


def piston(a, b_, c_len, r_len, big=0.021, small=0.011):
    """Цилиндр от a к b_ длиной c_len (кость ka/pa) и шток от b_ к a длиной r_len (кость kb/pb): проушины на
    пальцах, на цилиндре — рёбра, штуцеры и шланг, на штоке — грязесъёмник."""
    d = unit(sub(b_, a))
    cyl, rod = Body(), Body()
    cyl.lathe(add(a, (0, 0, -0.011)), (0, 0, 1), [(0.016, 0.0), (0.016, 0.022)], IRON, seg=12)
    cyl.lathe(add(a, mul(d, 0.012)), d, [(0.012, 0.0), (big, 0.012), (big, c_len - 0.04), (big + 0.005, c_len - 0.036),
                                        (big + 0.005, c_len - 0.018), (small + 0.004, c_len - 0.014)], IRON, seg=14)
    for k in range(3):
        cyl.lathe(add(a, mul(d, 0.04 + 0.03 * k)), d, [(big + 0.003, 0.0), (big + 0.003, 0.005)], IRON, seg=14)
    side = unit((d[1], -d[0], 0.0))
    for t in (0.05, c_len - 0.05):
        cyl.lathe(add(add(a, mul(d, t)), mul(side, big - 0.003)), side, [(0.005, 0.0), (0.005, 0.012), (0.0035, 0.016)], STEEL, seg=6)
    _hose(cyl, [add(add(a, mul(d, 0.05)), mul(side, big + 0.012)), add(add(a, mul(d, c_len * 0.5)), mul(side, big + 0.02)),
                add(add(a, mul(d, c_len - 0.05)), mul(side, big + 0.012))], r=0.004, mat=RUBBER)
    rod.lathe(add(b_, (0, 0, -0.011)), (0, 0, 1), [(0.015, 0.0), (0.015, 0.022)], IRON, seg=12)
    rod.lathe(add(b_, mul(d, -r_len)), d, [(small, 0.0), (small, r_len - 0.012)], STEEL, seg=12)
    rod.lathe(add(b_, mul(d, -0.03)), d, [(small + 0.004, 0.0), (small + 0.004, 0.018)], IRON, seg=12)
    return cyl, rod


# --- части ноги ---

def hip_parts(L):
    """Кулак поворота (вертикальная ось) под рамой, вилка к оси качания и два мотора качания по бокам."""
    P = L["P"]
    hip = Body()
    hip.lathe((0.0, HIP_Y - 0.06, 0.0), (0, 1, 0), [(0.034, 0.0), (0.048, 0.008), (0.048, 0.112), (0.034, 0.12)], IRON, seg=18)
    for s in (-1.0, 1.0):
        hip.lathe((0.0, HIP_Y + s * 0.06, 0.0), (0, s, 0), [(0.022, 0.0), (0.022, 0.008), (0.014, 0.013)], STEEL, seg=10)
    for s in (-1.0, 1.0):   # щёки вилки
        hip.loft([[(x, HIP_Y - 0.042, s * 0.05 - 0.009), (x, HIP_Y + 0.042, s * 0.05 - 0.009), (x, HIP_Y + 0.042, s * 0.05 + 0.009),
                   (x, HIP_Y - 0.042, s * 0.05 + 0.009)] for x in (0.02, P[0])], IRON)
        _disc(hip, Link(P, 0.0), 0.0, 0.0, s * 0.05 - 0.009, s * 0.05 + 0.009, 0.044, IRON, seg=18)
        _motor(hip, Link(P, 0.0), 0.0, s, 0.059, r=0.042, depth=0.036)
    for k, z in enumerate((-0.016, 0.0, 0.016)):   # гребёнка проводов в кулак
        _hose(hip, [(-0.05, HIP_Y + 0.03 - 0.004 * k, z), (-0.01, HIP_Y + 0.05, z), (0.03, HIP_Y + 0.046, z * 1.6)],
              mat=CABLE if k == 1 else RUBBER)
    return hip


def thigh_parts(L):
    """Бедро: балка коробом с окнами, по спинке — проушина и ложементы цилиндра колена (кости ka/kb), щиток в
    краске над его основанием; по бокам — тяги и кабель."""
    from solid import solid
    th = L["th"]
    hh = lambda x: 0.036 - 0.006 * x / L1
    base = Body()
    base.loft([_hexr(th, x, 0.034, hh(x), 0.3) for x in (0.0, L1 * 0.5, L1)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.04, 0.04, 0.05, None, seg=20)
    cuts = Body()
    for x0, x1 in ((0.075, 0.15), (0.19, 0.265)):   # окна облегчения
        cuts.loft([_rect(th, x, -hh(x) + 0.012, hh(x) - 0.012, -0.06, 0.06) for x in (x0, x1)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(thigh, th, 0.0, s, 0.04, 0.034, n=6, rb=0.04)
    for s in (-1.0, 1.0):   # тяги и кабели по бокам
        thigh.tube([th.at(0.04, -0.022, s * 0.042), th.at(L1 - 0.07, -0.02, s * 0.038)], 0.0065, STEEL, seg=6)
        for x in (0.11, 0.23):
            thigh.box(th.at(x, -0.022, s * 0.04), (0.014, 0.016, 0.012), IRON, bevel=0.002, rot=th.m)
    _hose(thigh, [th.at(0.02, 0.03, 0.03), th.at(0.16, 0.042, 0.034), th.at(L1 - 0.07, 0.03, 0.032)])
    _hose(thigh, [th.at(0.02, 0.026, -0.03), th.at(0.18, 0.04, -0.034), th.at(L1 - 0.07, 0.028, -0.03)], r=0.006, mat=RUBBER)
    thigh.box(th.at(KNEE_A[0], 0.04, 0.0), (0.05, 0.03, 0.03), IRON, bevel=0.003, rot=th.m)   # подушка проушины
    _lug(thigh, th, KNEE_A)
    for x in (0.16, 0.26):   # хомуты-ложементы под цилиндром
        thigh.box(th.at(x, 0.04, 0.0), (0.016, 0.014, 0.05), IRON, bevel=0.002, rot=th.m)
    # щиток: гранёный кожух над основанием цилиндра колена, в краске (цилиндр уходит под него)
    guard = Body()
    g = lambda x, w, e: [th.at(x, 0.034, -w), th.at(x, 0.09 + e, -w * 0.92), th.at(x, 0.112 + e, -w * 0.45),
                         th.at(x, 0.112 + e, w * 0.45), th.at(x, 0.09 + e, w * 0.92), th.at(x, 0.034, w)]
    guard.loft([g(0.0, 0.036, -0.012), g(0.03, 0.046, 0.0), g(0.15, 0.046, 0.0), g(0.19, 0.038, -0.014)], PAINT)
    for x in (0.06, 0.14):
        for s in (-1.0, 1.0):
            bolt(guard, th.at(x, 0.075, s * 0.046), (0.0, 0.0, s), r=0.0055, h=0.004)
    return thigh, guard


def shin_parts(L):
    """Голень: узел колена, две боковые щёки с окнами (сужаются к голеностопу), распорки, трубопровод между щёк,
    проушины поршней; щиток снаружи в краске."""
    from solid import solid
    sh = L["sh"]
    hgt = lambda x: 0.05 - 0.016 * x / L2
    Z0, Z1 = 0.022, 0.036   # щёки: от z0 до z1 с каждой стороны
    base = Body()
    for s in (-1.0, 1.0):
        base.loft([[sh.at(x, -hgt(x), s * Z0), sh.at(x, hgt(x), s * Z0), sh.at(x, hgt(x), s * Z1), sh.at(x, -hgt(x), s * Z1)]
                   for x in (0.0, L2 * 0.5, L2)], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.044, 0.044, 0.058, None, seg=24)        # узел колена
    bx, by = KNEE_B                                                      # рог колена за осью: под шток цилиндра
    for s in (-1.0, 1.0):
        adds.loft([_rect(sh, x, -0.02, y1, s * 0.014, s * 0.03) for x, y1 in ((0.02, 0.05), (bx - 0.004, by + 0.014))], None)
        _disc(adds, sh, bx, by, s * 0.014, s * 0.03, 0.02, None, seg=14)
    for s in (-1.0, 1.0):
        _disc(adds, sh, L2, 0.0, s * Z0, s * Z1, 0.04, None, seg=20)      # вилка голеностопа
    for x in (0.12, 0.27, 0.43):                                         # распорки
        adds.box(sh.at(x, 0.0, 0.0), (0.028, hgt(x) * 1.7, 2 * Z0 + 0.004), None, bevel=0.0, rot=sh.m)
    cuts = Body()
    for x0, x1 in ((0.065, 0.1), (0.15, 0.24), (0.3, 0.4), (0.46, 0.5)):  # окна в щеках
        cuts.loft([_rect(sh, x, -hgt(x) + 0.013, hgt(x) - 0.013, -0.06, 0.06) for x in (x0, x1)], None)
    for s in (-1.0, 1.0):
        cuts.lathe(sh.at(0.0, 0.0, s * 0.05), (0.0, 0.0, -s), [(0.03, 0.0), (0.03, 0.008)], None, seg=20)   # гнёзда крышек
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    shin = Body()
    shin.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(shin, sh, 0.0, s, 0.042, 0.03, n=6, rb=0.04)
        _cap(shin, sh, L2, s, Z1, 0.026, n=4, rb=0.031)
        for x in (0.12, 0.27, 0.43):   # болты распорок
            for y in (-1.0, 1.0):
                bolt(shin, sh.at(x, y * hgt(x) * 0.55, s * Z1), (0.0, 0.0, s), r=0.0045, h=0.004)
    # трубопровод между щёк
    shin.tube([sh.at(0.06, -0.012, 0.0), sh.at(L2 - 0.08, -0.008, 0.0)], 0.009, STEEL, seg=8)
    shin.tube([sh.at(0.06, 0.016, 0.0), sh.at(L2 - 0.08, 0.012, 0.0)], 0.006, CABLE, seg=6)
    shin.lathe(sh.at(KNEE_B[0], KNEE_B[1], -0.038), (0.0, 0.0, 1.0), [(0.0075, 0.0), (0.0075, 0.076)], STEEL, seg=8)   # палец рога
    _lug(shin, sh, ANK_A)
    # щиток снаружи (спинка y+) на верхней половине: гранёный, с гребнем
    plate = Body()
    pl = lambda x, ww, e: [sh.at(x, hgt(x) + 0.004, -ww), sh.at(x, hgt(x) + 0.026 + e, -ww * 0.72), sh.at(x, hgt(x) + 0.034 + e, 0.0),
                           sh.at(x, hgt(x) + 0.026 + e, ww * 0.72), sh.at(x, hgt(x) + 0.004, ww)]
    plate.loft([pl(0.07, 0.034, -0.012), pl(0.1, 0.044, 0.0), pl(0.27, 0.044, 0.0), pl(0.3, 0.036, -0.014)], PAINT)
    for x in (0.12, 0.2, 0.26):
        for s in (-1.0, 1.0):
            bolt(plate, sh.at(x, hgt(x) + 0.02, s * 0.04), (0.0, 0.3, s), r=0.0048, h=0.004)
    return shin, plate


def tarsus_parts(L):
    """Плюсна: узел голеностопа, коробчатая балка, амортизатор с пружиной по внешней грани, проушина поршня,
    вилка ступни внизу, резиновый отбойник."""
    from solid import solid
    tr = L["tr"]
    w = lambda x: 0.03 - 0.006 * x / L3
    base = Body()
    base.loft([_hexr(tr, x, 0.02, w(x), 0.3) for x in (0.0, L3 * 0.5, L3 - 0.03)], None)
    adds = Body()
    _disc(adds, tr, 0.0, 0.0, -0.0215, 0.0215, 0.034, None, seg=20)
    for s in (-1.0, 1.0):
        adds.loft([_rect(tr, x, -0.024, 0.024, s * 0.02, s * 0.03) for x in (L3 - 0.06, L3 + 0.004)], None)
        _disc(adds, tr, L3, 0.0, s * 0.02, s * 0.03, 0.026, None, seg=16)
    cuts = Body()
    cuts.loft([_rect(tr, x, -0.008, 0.008, -0.05, 0.05) for x in (0.07, 0.14)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    tar = Body()
    tar.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(tar, tr, L3, s, 0.03, 0.018)
    # амортизатор по внешней грани (y+): стакан, шток, пружина
    o = tr.at(0.03, 0.045, 0.0)
    d = tr.dir(1.0, 0.0)
    tar.lathe(o, d, [(0.008, 0.0), (0.016, 0.008), (0.016, 0.07), (0.012, 0.076)], IRON, seg=14)
    tar.lathe(add(o, mul(d, 0.07)), d, [(0.007, 0.0), (0.007, 0.09)], STEEL, seg=10)
    for k in range(6):
        tar.torus(add(o, mul(d, 0.084 + 0.012 * k)), d, 0.014, 0.003, STEEL, seg=12, tseg=5)
    for x in (0.03, 0.18):
        tar.box(tr.at(x, 0.034, 0.0), (0.02, 0.024, 0.018), IRON, bevel=0.002, rot=tr.m)
    _lug(tar, tr, ANK_B, half=0.014, r=0.015)
    _hose(tar, [tr.at(0.02, -0.02, 0.024), tr.at(0.12, -0.026, 0.024), tr.at(L3 - 0.05, -0.02, 0.026)])
    tar.box(tr.at(0.0, -0.032, 0.0), (0.018, 0.02, 0.026), RUBBER, bevel=0.004, rot=tr.m)   # отбойник
    return tar


def foot_parts(L):
    """Ступня-платформа: подошва с резиной и протектором, два носка на шарнире, пятка, вилка голеностопа,
    тяги к носкам, щитки в краске. Вдоль ноги наружу (+x), ось — ось ступни F."""
    from solid import solid
    F = L["F"]
    H = FOOT_H
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    sole = Body()
    ring = lambda x, w, h: [at(x, -H + 0.006, -w), at(x, -H + h, -w * 0.9), at(x, -H + h, w * 0.9), at(x, -H + 0.006, w)]
    sole.loft([ring(-0.085, 0.045, 0.03), ring(-0.06, 0.055, 0.036), ring(0.06, 0.062, 0.036), ring(0.085, 0.056, 0.03)], None)
    adds = Body()
    for s in (-1.0, 1.0):   # щёки вилки голеностопа
        adds.loft([[at(-0.04, -H + 0.03, s * 0.031), at(0.04, -H + 0.03, s * 0.031), at(0.04, -H + 0.03, s * 0.041), at(-0.04, -H + 0.03, s * 0.041)],
                   [at(-0.022, 0.0, s * 0.031), at(0.022, 0.0, s * 0.031), at(0.022, 0.0, s * 0.041), at(-0.022, 0.0, s * 0.041)]], None)
        adds.lathe(at(0.0, 0.0, s * 0.031), (0.0, 0.0, s), [(0.026, 0.0), (0.026, 0.01)], None, seg=18)
    adds.box(at(-0.1, -H + 0.026, 0.0), (0.04, 0.04, 0.07), None, bevel=0.0)   # пятка
    cuts = Body()
    for x0, x1 in ((-0.03, 0.0), (0.012, 0.048)):
        cuts.box(at((x0 + x1) / 2, -H + 0.04, 0.0), (x1 - x0, 0.02, 0.05), None, bevel=0.0)
    t = solid([("add", sole), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    foot = Body()
    foot.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(foot, Link(F, 0.0), 0.0, s, 0.041, 0.02)
    # носки: два пальца на шарнире впереди, каждый — клин с резиной снизу и щитком сверху
    for s in (-1.0, 1.0):
        z = s * 0.031
        foot.lathe(at(0.09, -H + 0.022, z - 0.024), (0, 0, 1), [(0.012, 0.0), (0.012, 0.048)], STEEL, seg=10)
        toe = [[at(x, -H + 0.004, z - w), at(x, -H + hh, z - w * 0.85), at(x, -H + hh, z + w * 0.85), at(x, -H + 0.004, z + w)]
               for x, w, hh in ((0.088, 0.026, 0.036), (0.13, 0.024, 0.03), (0.16, 0.018, 0.016))]
        foot.loft(toe, IRON)
        foot.box(at(0.125, -H + 0.003, z), (0.06, 0.006, 0.04), RUBBER, bevel=0.002)
        foot.box(at(0.122, -H + 0.034, z), (0.05, 0.006, 0.032), PAINT, bevel=0.002)
        bolt(foot, at(0.142, -H + 0.037, z), (0, 1, 0), r=0.004, h=0.003)
    # резина подошвы и пятки, протектор
    foot.box(at(0.0, -H + 0.003, 0.0), (0.16, 0.006, 0.11), RUBBER, bevel=0.002)
    foot.box(at(-0.1, -H + 0.003, 0.0), (0.036, 0.006, 0.066), RUBBER, bevel=0.002)
    for k in range(6):
        foot.box(at(-0.065 + 0.026 * k, -H - 0.001, 0.0), (0.008, 0.004, 0.1), RUBBER, bevel=0.0)
    # тяги от вилки к носкам, щитки по бокам подошвы
    for s in (-1.0, 1.0):
        foot.tube([at(0.02, -0.01, s * 0.046), at(0.07, -H + 0.04, s * 0.046), at(0.1, -H + 0.03, s * 0.04)], 0.005, STEEL, seg=6, fillet=0.01)
        foot.box(at(0.0, -H + 0.04, s * 0.05), (0.13, 0.008, 0.014), PAINT, bevel=0.002)
        for x in (-0.05, 0.05):
            bolt(foot, at(x, -H + 0.045, s * 0.05), (0, 1, 0), r=0.004, h=0.003)
    foot.lathe(at(-0.12, -H + 0.026, 0.0), (-1, 0, 0), [(0.012, 0.0), (0.012, 0.01), (0.008, 0.014)], RUBBER, seg=10)
    return foot


def leg_parts(leg):
    """Части ноги в осях ноги: {кость: [(тело, износ)]} и точки суставов."""
    L = leg.points()
    thigh, guard = thigh_parts(L)
    shin, plate = shin_parts(L)
    (kc, kr), (ac, ar) = piston_lengths(leg)
    ka, kb = piston(L["ka"], L["kb"], kc, kr)
    pa, pb = piston(L["pa"], L["pb"], ac, ar, big=0.018, small=0.0095)
    parts = {"hip": [(hip_parts(L), None)], "thigh": [(thigh, None), (guard, WEAR_LEG)], "shin": [(shin, None), (plate, WEAR_LEG)],
             "tarsus": [(tarsus_parts(L), None)], "foot": [(foot_parts(L), None)],
             "ka": [(ka, None)], "kb": [(kb, None)], "pa": [(pa, None)], "pb": [(pb, None)]}
    return L, parts
