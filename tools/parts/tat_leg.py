"""Ноги «Тати» (docs/tat.md): четыре одинаковые длинные ноги паука, как у машины из ролика-концепта.

Строение ноги: кулак поворота под рамой и мотор качания, бедро уходит наружу и вверх (колено выше корпуса,
когда машина присела), длинная голень — несущая балка с кожухом привода сверху и тягой снизу, короткая отвесная
плюсна-амортизатор и лапа-коготь: крюк вперёд, два боковых пальца и шпора назад. Нога симметрична поперёк
своей плоскости, поэтому левые и правые ноги — одно и то же тело, повёрнутое на свой угол.

Оси рецепта ноги — как у Вежи (tools/parts/vezha_leg.py): ось поворота бедра — ось y в начале координат, нога
уходит в +x, z — вбок. Звено описано в своих осях (Link): x — вдоль звена, y — спинка, z — вбок.
Плюсна отвесна в осях рамы: нога решается как две длины L1 и L2 до голеностопа над лапой.
"""
import math

from kit import Body, add, mul
from vezha_leg import Link, bolt

PAINT, WORN, IRON, STEEL = "tat_paint", "tat_worn", "tat_iron", "vezha_steel"
RUBBER, GLASS, LENS, MARK, CABLE = "rubber_black", "vezha_glass", "tat_lens", "tat_mark", "tat_cable"
BRASS = "vezha_brass"

HIP_Y = 0.62                          # оси бёдер над опорой в бинде (стоит)
WEAR_LEG = (WORN, 0.014, 0.65, 0.025)  # щитки ног бьются сильнее корпуса

# --- размеры ноги, м (числа решения — в MECH_DESC, src/view/mech_common.cpp) ---
U = 0.075                  # ось качания бедра дальше оси поворота
L1, L2, L3 = 0.40, 0.50, 0.10   # бедро, голень, плюсна (между осями суставов)
FOOT_H = 0.045             # ось лапы над подошвой
REACH = 0.44               # бинд: вынос оси лапы от оси качания
TH_MIN, TH_MAX = -50.0, 95.0   # ход бедра к горизонту, град


class Leg:
    """Место ноги на раме: имя, поворот рецепта вокруг y (+x рецепта → (cos, 0, −sin)), ось поворота (x, z)."""

    def __init__(self, name, deg, corner):
        self.name, self.deg, self.corner = name, deg, corner
        self.u, self.l1, self.l2, self.l3, self.foot_h = U, L1, L2, L3, FOOT_H
        self.th_min, self.th_max = math.radians(TH_MIN), math.radians(TH_MAX)
        self.a1, self.a2, self.a3 = solve(self, REACH, FOOT_H - HIP_Y)

    def points(self):
        """Суставы в бинде (оси ноги): ось качания P, колено K, голеностоп A, ось лапы F и звенья."""
        P = (self.u, HIP_Y, 0.0)
        th = Link(P, self.a1)
        K = th.end(self.l1)
        sh = Link(K, self.a2)
        A = sh.end(self.l2)
        F = add(A, (0.0, -self.l3, 0.0))
        return {"P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh}


def solve(leg, x, y):
    """Углы бедра, голени и плюсны к горизонту для оси лапы в (x, y) от оси качания (колено вверх)."""
    la, lb, ty = leg.l1, leg.l2, y + leg.l3
    d = min(max(math.hypot(x, ty), abs(la - lb) + 1e-4), la + lb - 1e-4)
    a = math.atan2(ty, x) + math.acos(max(-1.0, min(1.0, (la * la + d * d - lb * lb) / (2 * la * d))))
    a = min(max(a, leg.th_min), leg.th_max)
    s = math.atan2(ty - la * math.sin(a), x - la * math.cos(a))
    return a, s, -math.pi / 2


# Ноги по кругу fr, fl, rl, rr: углы на 40° от поперечной оси, рама 0.40 × 0.50 м между осями поворота
LEGS = (
    Leg("fr", -40.0, (0.20, 0.24)),
    Leg("fl", -140.0, (-0.20, 0.24)),
    Leg("rl", 140.0, (-0.20, -0.26)),
    Leg("rr", 40.0, (0.20, -0.26)),
)


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
    # гребёнка проводов в кулак
    for k, z in enumerate((-0.016, 0.0, 0.016)):
        _hose(hip, [(-0.05, HIP_Y + 0.03 - 0.004 * k, z), (-0.01, HIP_Y + 0.05, z), (0.03, HIP_Y + 0.046, z * 1.6)],
              mat=CABLE if k == 1 else RUBBER)
    return hip


def thigh_parts(L):
    """Бедро: балка коробом с окнами, сверху привод колена (цилиндр со штоком), щиток в краске на ближней
    половине; по бокам — тяга и кабель."""
    from solid import solid
    th = L["th"]
    hh = lambda x: 0.036 - 0.008 * x / L1
    base = Body()
    base.loft([_hexr(th, x, 0.034, hh(x), 0.3) for x in (0.0, L1 * 0.5, L1)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.04, 0.04, 0.05, None, seg=20)
    _disc(adds, th, L1, 0.0, -0.034, 0.034, 0.04, None, seg=18)
    cuts = Body()
    for x0, x1 in ((0.08, 0.17), (0.21, 0.30)):   # окна облегчения
        cuts.loft([_rect(th, x, -hh(x) + 0.012, hh(x) - 0.012, -0.06, 0.06) for x in (x0, x1)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(thigh, th, 0.0, s, 0.04, 0.034, n=6, rb=0.04)
        _cap(thigh, th, L1, s, 0.034, 0.026)
    # привод колена поверх балки: цилиндр с рёбрами, шток к рогу колена
    o = th.at(0.05, 0.058, 0.0)
    d = th.dir(1.0, 0.0)
    thigh.lathe(o, d, [(0.012, 0.0), (0.022, 0.01), (0.022, 0.2), (0.016, 0.21)], IRON, seg=14)
    for k in range(5):
        thigh.lathe(add(o, mul(d, 0.03 + 0.035 * k)), d, [(0.024, 0.0), (0.024, 0.006)], IRON, seg=14)
    thigh.lathe(add(o, mul(d, 0.21)), d, [(0.009, 0.0), (0.009, 0.13)], STEEL, seg=10)
    for x in (0.05, L1 - 0.03):   # проушины привода
        thigh.box(th.at(x, 0.046, 0.0), (0.03, 0.03, 0.02), IRON, bevel=0.003, rot=th.m)
        _disc(thigh, th, x, 0.058, -0.016, 0.016, 0.011, STEEL, seg=10)
    # тяги и кабели по бокам
    for s in (-1.0, 1.0):
        thigh.tube([th.at(0.04, -0.022, s * 0.042), th.at(L1 - 0.05, -0.02, s * 0.038)], 0.0065, STEEL, seg=6)
        for x in (0.12, 0.26):
            thigh.box(th.at(x, -0.022, s * 0.04), (0.014, 0.016, 0.012), IRON, bevel=0.002, rot=th.m)
    _hose(thigh, [th.at(0.02, 0.03, 0.03), th.at(0.18, 0.042, 0.034), th.at(L1 - 0.06, 0.03, 0.032)])
    _hose(thigh, [th.at(0.02, 0.026, -0.03), th.at(0.2, 0.04, -0.034), th.at(L1 - 0.06, 0.028, -0.03)], r=0.006, mat=RUBBER)
    # щиток: гранёный кожух над приводом, в краске
    guard = Body()
    g = lambda x, w, e: [th.at(x, 0.03, -w), th.at(x, 0.07 + e, -w * 0.92), th.at(x, 0.088 + e, -w * 0.45),
                         th.at(x, 0.088 + e, w * 0.45), th.at(x, 0.07 + e, w * 0.92), th.at(x, 0.03, w)]
    guard.loft([g(0.035, 0.034, -0.008), g(0.06, 0.044, 0.0), g(0.2, 0.044, 0.0), g(0.24, 0.036, -0.01)], PAINT)
    for x in (0.08, 0.19):
        for s in (-1.0, 1.0):
            bolt(guard, th.at(x, 0.062, s * 0.044), (0.0, 0.0, s), r=0.0055, h=0.004)
    return thigh, guard


def shin_parts(L):
    """Голень: балка сужается к лапе; снаружи кожух привода под щитком в краске, изнутри тяга и кабель."""
    from solid import solid
    sh = L["sh"]
    w = lambda x: 0.032 - 0.013 * x / L2
    base = Body()
    base.loft([_hexr(sh, x, w(x), w(x) * 1.15, 0.38) for x in (0.0, 0.08, L2 * 0.6, L2)], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.034, 0.034, 0.046, None, seg=20)
    _disc(adds, sh, L2, 0.0, -0.02, 0.02, 0.026, None, seg=14)
    cuts = Body()
    for x0, x1 in ((0.07, 0.14), (0.17, 0.24)):
        cuts.loft([_rect(sh, x, -0.08, 0.08, -0.008, 0.008) for x in (x0, x1)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    shin = Body()
    shin.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(shin, sh, 0.0, s, 0.034, 0.034, n=5, rb=0.038)
    # кожух привода: капсула на внешней спинке верхней половины
    o = sh.at(0.05, 0.05, 0.0)
    d = sh.dir(1.0, 0.0)
    shin.lathe(o, d, [(0.008, 0.0), (0.022, 0.012), (0.03, 0.03), (0.03, 0.17), (0.024, 0.2), (0.012, 0.215)], IRON, seg=16)
    for k in range(4):
        shin.lathe(add(o, mul(d, 0.05 + 0.035 * k)), d, [(0.032, 0.0), (0.032, 0.006)], IRON, seg=16)
    shin.box(sh.at(0.09, 0.03, 0.0), (0.03, 0.02, 0.02), IRON, bevel=0.003, rot=sh.m)
    shin.box(sh.at(0.23, 0.03, 0.0), (0.02, 0.02, 0.018), IRON, bevel=0.003, rot=sh.m)
    # тяга и кабель с хомутами по внутренней стороне
    shin.tube([sh.at(0.05, -0.036, 0.0), sh.at(L2 - 0.08, -0.024, 0.0)], 0.006, STEEL, seg=6)
    _hose(shin, [sh.at(0.02, -0.03, 0.022), sh.at(0.25, -0.03, 0.02), sh.at(L2 - 0.06, -0.016, 0.016)])
    for x in (0.12, 0.26, 0.38):
        shin.box(sh.at(x, -0.026 + 0.008 * x / L2, 0.0), (0.012, 0.022, 0.05 - 0.02 * x / L2), STEEL, bevel=0.002, rot=sh.m)
    # щиток в краске на кожухе: прячет капсулу, гребень посередине
    plate = Body()
    pl = lambda x, ww, e: [sh.at(x, 0.05, -ww), sh.at(x, 0.084 + e, -ww * 0.7), sh.at(x, 0.092 + e, 0.0), sh.at(x, 0.084 + e, ww * 0.7),
                           sh.at(x, 0.05, ww)]
    plate.loft([pl(0.06, 0.03, -0.01), pl(0.09, 0.04, 0.0), pl(0.19, 0.04, 0.0), pl(0.23, 0.032, -0.012)], PAINT)
    for x in (0.1, 0.18):
        for s in (-1.0, 1.0):
            bolt(plate, sh.at(x, 0.075, s * 0.034), (0.0, 0.0, s), r=0.005, h=0.004)
    return shin, plate


def tarsus_parts(L):
    """Плюсна: отвесный амортизатор — ось голеностопа, стакан, пружина, шток к лапе."""
    A = L["A"]
    tar = Body()
    tar.lathe(add(A, (0.0, 0.0, -0.024)), (0, 0, 1), [(0.02, 0.0), (0.02, 0.048)], STEEL, seg=12)
    tar.lathe(add(A, (0.0, 0.018, 0.0)), (0, -1, 0), [(0.016, 0.0), (0.022, 0.012), (0.022, 0.05), (0.016, 0.056)], IRON, seg=14)
    tar.lathe(add(A, (0.0, -0.035, 0.0)), (0, -1, 0), [(0.0095, 0.0), (0.0095, L3 - 0.03)], STEEL, seg=10)
    for k in range(4):
        tar.torus(add(A, (0.0, -0.046 - 0.011 * k, 0.0)), (0, 1, 0), 0.016, 0.0035, STEEL, seg=12, tseg=5)
    return tar


def foot_parts(L):
    """Лапа-коготь: ступица на оси, крюк вперёд, два пальца в стороны, шпора назад, резиновая пята."""
    F = L["F"]
    H = FOOT_H
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    foot = Body()
    foot.lathe(at(0.0, 0.0, -0.022), (0, 0, 1), [(0.014, 0.0), (0.014, 0.044)], STEEL, seg=10)
    foot.box(at(0.0, -0.008, 0.0), (0.05, 0.03, 0.044), IRON, bevel=0.006)
    # крюк вперёд и два пальца: каждый — гнутая трубка, сужается к острию
    for a, ln, r in ((0.0, 0.095, 0.011), (0.85, 0.07, 0.009), (-0.85, 0.07, 0.009)):
        d = (math.cos(a), 0.0, math.sin(a))
        p = lambda t, y: at(d[0] * t, y, d[2] * t)
        foot.tube([p(0.01, -0.012), p(ln * 0.45, -0.008), p(ln * 0.85, -H + 0.016), p(ln, -H - 0.002), p(ln * 0.86, -H - 0.006)],
                  r, IRON, seg=7, fillet=0.012, radii=lambda i, n, r=r: r * (1.0 - 0.7 * i / max(n - 1, 1)))
        foot.box(p(ln * 0.4, -0.004), (0.022, 0.014, 0.016), STEEL, bevel=0.002)
    foot.tube([at(-0.016, -0.012), at(-0.05, -0.02), at(-0.072, -H + 0.004), at(-0.07, -H - 0.004)], 0.0085, IRON, seg=6, fillet=0.01,
              radii=lambda i, n: 0.009 - 0.005 * i / max(n - 1, 1))
    foot.lathe(at(0.0, -H + 0.012, 0.0), (0, -1, 0), [(0.02, 0.0), (0.022, 0.006), (0.016, 0.012)], RUBBER, seg=12)
    return foot


def leg_parts(leg):
    """Части ноги в осях ноги: {кость: [(тело, износ)]}."""
    L = leg.points()
    thigh, guard = thigh_parts(L)
    shin, plate = shin_parts(L)
    parts = {"hip": [(hip_parts(L), None)], "thigh": [(thigh, None), (guard, WEAR_LEG)], "shin": [(shin, None), (plate, WEAR_LEG)],
             "tarsus": [(tarsus_parts(L), None)], "foot": [(foot_parts(L), None)]}
    return L, parts
