"""Ноги «Тати» (docs/tat.md): четыре разные ноги, у каждой своё строение, лапа и сложение.

    ходуля (fl)  длинная паучья: бедро вверх-наружу, колено выше корпуса, голень-спица, короткая отвесная плюсна,
                 лапа-крюк;
    упор   (fr)  короткая толстая молния, как у Вежи: плюсна параллельна бедру, поршень, широкая лапа с шипом —
                 держит отдачу винтовки;
    лыжа   (rl)  бедро почти горизонтально, голень-труба, отвесная плюсна на шаровой опоре, дырчатая изогнутая
                 лыжа вдоль хода — не вязнет в песке;
    жало   (rr)  молния с длинной плюсной-лезвием и гранёной острой лапой: втыкается в грунт.

Оси рецепта ноги — как у Вежи (tools/parts/vezha_leg.py): ось поворота бедра — ось y в начале координат, нога
уходит в +x, z — вбок. Звено описано в своих осях (Link): x — вдоль звена, y — спинка, z — вбок.
Плюсна двух видов: «par» — параллельна бедру (пантограф, нога решается как две длины L1 + L3 и L2), «vert» —
отвесна в осях рамы (нога решается как две длины L1 и L2 до голеностопа над лапой).
"""
import math

from kit import Body, add, mat_vec, mul, rot_y, rot_z, sub, unit
from vezha_leg import Link, bolt, piston

PAINT, WORN, IRON, STEEL = "tat_paint", "tat_worn", "tat_iron", "vezha_steel"
RUBBER, GLASS, LENS, MARK, NET = "rubber_black", "vezha_glass", "tat_lens", "tat_mark", "tat_net"

HIP_Y = 0.62                          # оси бёдер над опорой в бинде (стоит)
WEAR_LEG = (WORN, 0.024, 0.8, 0.07)  # щитки ног бьются сильнее корпуса


class Leg:
    """Строение ноги: где стоит, длины звеньев, вид плюсны, ход бедра. Бинд — стоя, лапа на выносе reach."""

    def __init__(self, name, deg, corner, u, l1, l2, l3, foot_h, mode, reach, th_min, th_max, kind):
        self.name, self.deg, self.corner = name, deg, corner
        self.u, self.l1, self.l2, self.l3, self.foot_h, self.mode = u, l1, l2, l3, foot_h, mode
        self.th_min, self.th_max, self.kind = math.radians(th_min), math.radians(th_max), kind
        self.a1, self.a2, self.a3 = solve(self, reach, foot_h - HIP_Y)

    def points(self):
        """Суставы в бинде (оси ноги): ось качания P, колено K, голеностоп A, ось лапы F и звенья."""
        P = (self.u, HIP_Y, 0.0)
        th = Link(P, self.a1)
        K = th.end(self.l1)
        sh = Link(K, self.a2)
        A = sh.end(self.l2)
        ta = Link(A, self.a3)
        F = ta.end(self.l3)
        return {"P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh, "ta": ta}


def solve(leg, x, y):
    """Углы бедра, голени и плюсны к горизонту для оси лапы в (x, y) от оси качания (колено вверх)."""
    if leg.mode == "vert":
        la, lb, ty = leg.l1, leg.l2, y + leg.l3
    else:
        la, lb, ty = leg.l1 + leg.l3, leg.l2, y
    d = min(max(math.hypot(x, ty), abs(la - lb) + 1e-4), la + lb - 1e-4)
    a = math.atan2(ty, x) + math.acos(max(-1.0, min(1.0, (la * la + d * d - lb * lb) / (2 * la * d))))
    a = min(max(a, leg.th_min), leg.th_max)
    s = math.atan2(ty - la * math.sin(a), x - la * math.cos(a))
    t = -math.pi / 2 if leg.mode == "vert" else a
    return a, s, t


# Ноги: имя, поворот рецепта вокруг y (+x рецепта → (cos, 0, −sin)), ось поворота бедра (x, z) от оси машины
LEGS = (
    Leg("fl", -146.0, (-0.285, 0.215), 0.065, 0.40, 0.64, 0.13, 0.045, "vert", 0.50, -40.0, 78.0, "stilt"),
    Leg("fr", -50.0, (0.30, 0.235), 0.08, 0.30, 0.36, 0.20, 0.06, "par", 0.42, -70.0, 30.0, "brace"),
    Leg("rl", 141.0, (-0.275, -0.335), 0.07, 0.36, 0.46, 0.09, 0.04, "vert", 0.40, -62.0, 55.0, "ski"),
    Leg("rr", 44.0, (0.29, -0.31), 0.07, 0.30, 0.36, 0.30, 0.05, "par", 0.40, -70.0, 30.0, "spike"),
)


# --- общие мелочи ---

def _rect(link, x, y0, y1, z0, z1):
    return [link.at(x, y0, z0), link.at(x, y1, z0), link.at(x, y1, z1), link.at(x, y0, z1)]


def _hexr(link, x, w, h, c=0.35):
    """Шестигранное сечение звена: ширина w (по z), высота h (по y), срез углов долей c."""
    return [link.at(x, -h, -w * (1 - c)), link.at(x, -h * (1 - c), -w), link.at(x, h * (1 - c), -w),
            link.at(x, h, -w * (1 - c)), link.at(x, h, w * (1 - c)), link.at(x, h * (1 - c), w),
            link.at(x, -h * (1 - c), w), link.at(x, -h, w * (1 - c))]


def _disc(b, link, x, y, z0, z1, r, mat, seg=16):
    b.lathe(link.at(x, y, z0), (0.0, 0.0, 1.0), [(r, 0.0), (r, z1 - z0)], mat, seg=seg)


def _cap(b, link, x, s, z, r, mat=STEEL, n=0, rb=0.0):
    """Крышка оси на боковой грани (s — знак стороны) и n болтов вокруг."""
    b.lathe(link.at(x, 0.0, s * z), (0.0, 0.0, s), [(r, 0.0), (r, 0.007), (r * 0.7, 0.012)], mat, seg=12)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        bolt(b, add(link.at(x, 0.0, s * z), (rb * math.cos(a), rb * math.sin(a), 0.0)), (0.0, 0.0, s), r=0.006, h=0.005)


def _clips(b, link, xs, y, z, size=(0.018, 0.022, 0.016)):
    for x in xs:
        b.box(link.at(x, y, z), size, STEEL, bevel=0.002, rot=link.m)


def _motor(b, link, x, s, z, r=0.042, depth=0.05, fins=8):
    """Стакан мотора сустава на боковой грани: ребристый, с крышкой."""
    o = link.at(x, 0.0, s * z)
    d = (0.0, 0.0, s)
    b.lathe(o, d, [(r, 0.0), (r, 0.01), (r * 0.86, 0.014), (r * 0.86, depth), (r * 0.7, depth + 0.006), (r * 0.3, depth + 0.006)],
            IRON, seg=16)
    for k in range(fins):
        a = 2 * math.pi * k / fins
        c = add(o, (math.cos(a) * r * 0.9, math.sin(a) * r * 0.9, s * depth * 0.55))
        b.box(c, (0.008, 0.008, depth * 0.7), IRON, bevel=0.0, rot=rot_z(a))
    bolt(b, add(o, (0.0, 0.0, s * (depth + 0.006))), d, r=0.009, h=0.006)


def _knuckle(b, r, h, pin=True):
    """Кулак поворота бедра (вертикальная ось) и шея к оси качания."""
    b.lathe((0.0, HIP_Y - h / 2, 0.0), (0, 1, 0), [(r * 0.7, 0.0), (r, 0.008), (r, h - 0.008), (r * 0.7, h)], IRON, seg=16)
    if pin:
        for s in (-1.0, 1.0):
            b.lathe((0.0, HIP_Y + s * h / 2, 0.0), (0, s, 0), [(r * 0.45, 0.0), (r * 0.45, 0.008), (r * 0.3, 0.013)], STEEL, seg=10)


# --- ходуля: длинная паучья нога ---

def stilt_parts(leg, L):
    from solid import solid
    th, sh, ta = L["th"], L["sh"], L["ta"]
    P = L["P"]
    hip = Body()
    _knuckle(hip, 0.05, 0.11)
    hip.box(((P[0]) * 0.5, HIP_Y, 0.0), (P[0] + 0.03, 0.075, 0.08), IRON, bevel=0.007)
    _disc(hip, Link(P, 0.0), 0.0, 0.0, -0.046, 0.046, 0.05, IRON)
    _motor(hip, Link(P, 0.0), 0.0, 1.0, 0.046, r=0.042, depth=0.044)
    # бедро: короб из двух щёк и спинки, сверху острый гребень в краске
    hh = lambda x: 0.046 - 0.01 * x / leg.l1
    base = Body()
    base.loft([_rect(th, x, -hh(x), hh(x), -0.042, 0.042) for x in (0.0, leg.l1 * 0.5, leg.l1)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.042, 0.042, 0.056, None, seg=20)
    _disc(adds, th, leg.l1, 0.0, -0.042, 0.042, 0.044, None, seg=18)
    cuts = Body()
    for x, r in ((0.11, 0.02), (0.19, 0.019), (0.27, 0.016)):
        _disc(cuts, th, x, 0.0, -0.06, 0.06, r, None, seg=12)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.003, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    fin = Body()
    # щиток бедра: гранёный кожух сверху и по бокам до середины высоты, гребень посередине
    fin.loft([[th.at(x, -0.006, -0.05), th.at(x, hh(x) + 0.004, -0.052), th.at(x, hh(x) + 0.016, -0.03),
               th.at(x, hh(x) + 0.026 * (1 - x / leg.l1) + 0.018, 0.0), th.at(x, hh(x) + 0.016, 0.03), th.at(x, hh(x) + 0.004, 0.052),
               th.at(x, -0.006, 0.05)] for x in (0.05, 0.09, 0.28, 0.35)], PAINT)
    for x in (0.1, 0.27):
        for s_ in (-1.0, 1.0):
            bolt(fin, th.at(x, hh(x) - 0.012, s_ * 0.052), (0.0, 0.0, s_), r=0.006, h=0.005)
    _cap(thigh, th, leg.l1, 1.0, 0.042, 0.032, n=4, rb=0.028)
    _motor(thigh, th, leg.l1, -1.0, 0.042, r=0.036, depth=0.036, fins=6)
    thigh.tube([th.at(0.03, -0.042, 0.048), th.at(0.2, -0.046, 0.048), th.at(0.36, -0.04, 0.048)], 0.007, RUBBER, seg=6)
    _clips(thigh, th, (0.12, 0.25), -0.044, 0.046)
    # голень: спица шестигранного сечения, к лапе сужается, спереди нож в краске; по спине — магистраль
    shin = Body()
    shin.loft([_hexr(sh, x, w, h) for x, w, h in ((0.0, 0.036, 0.04), (0.08, 0.04, 0.046), (0.35, 0.031, 0.035),
                                                   (leg.l2 - 0.02, 0.022, 0.024))], IRON)
    _disc(shin, sh, 0.0, 0.0, -0.042, 0.042, 0.048, IRON, seg=18)
    _disc(shin, sh, leg.l2, 0.0, -0.028, 0.028, 0.03, IRON, seg=14)
    _cap(shin, sh, 0.0, 1.0, 0.042, 0.03, n=5, rb=0.036)
    _cap(shin, sh, 0.0, -1.0, 0.042, 0.03, n=5, rb=0.036)
    # щиток голени: гранёный желоб в краске по спине на две трети длины, нож-гребень посередине
    blade = Body()
    hb = lambda x: 0.046 - 0.022 * x / leg.l2
    blade.loft([[sh.at(x, hb(x) - 0.02, -0.046 + 0.02 * x / leg.l2), sh.at(x, hb(x) + 0.006, -0.04 + 0.016 * x / leg.l2),
                 sh.at(x, hb(x) + 0.022, 0.0), sh.at(x, hb(x) + 0.006, 0.04 - 0.016 * x / leg.l2),
                 sh.at(x, hb(x) - 0.02, 0.046 - 0.02 * x / leg.l2)] for x in (0.06, 0.1, 0.3, 0.42, 0.46)], PAINT)
    for x in (0.12, 0.36):
        for s_ in (-1.0, 1.0):
            bolt(blade, sh.at(x, hb(x) - 0.006, s_ * (0.042 - 0.016 * x / leg.l2)), (0.0, 0.0, s_), r=0.0055, h=0.004)
    shin.tube([sh.at(0.04, -0.04, 0.026), sh.at(0.30, -0.034, 0.026), sh.at(leg.l2 - 0.06, -0.024, 0.018)], 0.006, STEEL, seg=6)
    for x in (0.14, 0.32, 0.48):
        shin.box(sh.at(x, -0.034 + 0.014 * x / leg.l2, 0.024), (0.016, 0.016, 0.02), STEEL, bevel=0.002, rot=sh.m)
    # плюсна: отвесный амортизатор — стакан, пружина, шток
    tar = Body()
    A = L["A"]
    tar.lathe(add(A, (0.0, 0.0, -0.03)), (0, 0, 1), [(0.024, 0.0), (0.024, 0.06)], IRON, seg=12)
    tar.lathe(add(A, (0.0, -0.01, 0.0)), (0, -1, 0), [(0.024, 0.0), (0.024, 0.05), (0.016, 0.058)], IRON, seg=12)
    tar.lathe(add(A, (0.0, -0.05, 0.0)), (0, -1, 0), [(0.011, 0.0), (0.011, leg.l3 - 0.04)], STEEL, seg=8)
    for k in range(4):
        tar.torus(add(A, (0.0, -0.064 - 0.012 * k, 0.0)), (0, 1, 0), 0.018, 0.004, STEEL, seg=12, tseg=5)
    # лапа-крюк: ось, ступица, два изогнутых когтя наружу-вниз и пята
    F = L["F"]
    H = leg.foot_h
    foot = Body()
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    foot.lathe(at(0.0, 0.0, -0.022), (0, 0, 1), [(0.016, 0.0), (0.016, 0.044)], STEEL, seg=10)
    foot.box(at(0.0, -0.006, 0.0), (0.04, 0.03, 0.05), IRON, bevel=0.005)
    for s in (-1.0, 1.0):
        foot.tube([at(0.0, -0.01, s * 0.016), at(0.035, -0.012, s * 0.018), at(0.065, -H + 0.012, s * 0.02), at(0.07, -H - 0.003, s * 0.02),
                   at(0.052, -H - 0.008, s * 0.02)], 0.009, IRON, seg=6, fillet=0.012,
                  radii=lambda i, n: 0.009 - 0.006 * i / max(n - 1, 1))
    foot.lathe(at(-0.012, -H + 0.006, 0.0), (0, -1, 0), [(0.024, 0.0), (0.024, 0.008), (0.016, 0.012)], RUBBER, seg=12)
    foot.box(at(-0.02, -H + 0.018, 0.0), (0.03, 0.022, 0.03), IRON, bevel=0.004)
    return {"hip": [(hip, None)], "thigh": [(thigh, None), (fin, WEAR_LEG)], "shin": [(shin, None), (blade, WEAR_LEG)],
            "tarsus": [(tar, None)], "foot": [(foot, None)]}


# --- упор: короткая тяжёлая молния с поршнем и широкой лапой ---

def brace_parts(leg, L):
    from solid import solid
    th, sh, ta = L["th"], L["sh"], L["ta"]
    P = L["P"]
    hip = Body()
    _knuckle(hip, 0.06, 0.13)
    hip.box((P[0] * 0.5 + 0.005, HIP_Y, 0.0), (P[0] + 0.05, 0.1, 0.07), IRON, bevel=0.007)
    _disc(hip, Link(P, 0.0), 0.0, 0.0, -0.036, 0.036, 0.048, IRON)
    pa = piston_points(leg, L)[0]
    ring = lambda z, y0, y1: [(-0.026, y1, z), (0.026, y1, z), (0.026, y0, z), (-0.026, y0, z)]
    hip.loft([ring(-0.05, HIP_Y - 0.05, HIP_Y - 0.012), ring(-0.09, HIP_Y - 0.05, HIP_Y - 0.02),
              ring(-0.118, pa[1] - 0.026, pa[1] + 0.022)], IRON)
    hip.lathe((pa[0], pa[1], -0.118), (0, 0, -1), [(0.016, 0.0), (0.016, 0.034), (0.01, 0.04)], STEEL, seg=10)
    # бедро: короб с окнами, сверху литой щиток в краске с рёбрами
    hh = lambda x: 0.058 - 0.012 * x / leg.l1
    base = Body()
    base.loft([_rect(th, x, -hh(x), hh(x), -0.075, 0.075) for x in (0.0, leg.l1 * 0.5, leg.l1)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.075, 0.075, 0.064, None, seg=20)
    _disc(adds, th, leg.l1, 0.0, -0.075, 0.075, 0.05, None, seg=18)
    cuts = Body()
    for s in (-1.0, 1.0):
        cuts.loft([_rect(th, x, -hh(x) + 0.014, hh(x) - 0.02, s * 0.077 - 0.012, s * 0.077 + 0.012) for x in (0.08, leg.l1 - 0.07)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(thigh, th, 0.0, s, 0.075, 0.044, n=6, rb=0.052)
        _cap(thigh, th, leg.l1, s, 0.075, 0.034)
        thigh.tube([th.at(0.07, -0.004, s * 0.068), th.at(leg.l1 - 0.07, -0.004, s * 0.068)], 0.008, STEEL, seg=6)
    thigh.lathe(th.at(0.24, 0.0, -0.075), (0, 0, -1), [(0.017, 0.0), (0.017, 0.045), (0.011, 0.05)], STEEL, seg=10)   # палец поршня
    guard = Body()
    g = lambda x, w, e: [th.at(x, hh(x) - 0.024, -w), th.at(x, hh(x) + 0.004, -w), th.at(x, hh(x) + 0.02 + e, -w * 0.45),
                         th.at(x, hh(x) + 0.02 + e, w * 0.45), th.at(x, hh(x) + 0.004, w), th.at(x, hh(x) - 0.024, w)]
    guard.loft([g(0.03, 0.08, 0.0), g(0.07, 0.095, 0.004), g(0.22, 0.095, 0.006), g(0.28, 0.08, 0.0)], PAINT)
    for x in (0.08, 0.2):
        for s in (-1.0, 1.0):
            bolt(guard, th.at(x, hh(x) + 0.012, s * 0.07), unit(th.dir(0.0, 0.8, s * 0.6)), r=0.006, h=0.005)
    # голень: балка с карманами и щиток спереди
    sh_h = lambda x: 0.04 - 0.008 * x / leg.l2
    base = Body()
    base.loft([_rect(sh, x, -sh_h(x), sh_h(x), -0.04, 0.04) for x in (0.0, leg.l2 * 0.5, leg.l2)], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.04, 0.04, 0.048, None, seg=18)
    _disc(adds, sh, leg.l2, 0.0, -0.04, 0.04, 0.04, None, seg=16)
    cuts = Body()
    for s in (-1.0, 1.0):
        cuts.loft([_rect(sh, x, -sh_h(x) + 0.01, sh_h(x) - 0.01, s * 0.042 - 0.012, s * 0.042 + 0.012) for x in (0.07, leg.l2 - 0.06)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    shin = Body()
    shin.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        bolt(shin, sh.at(0.06, 0.0, s * 0.04), (0.0, 0.0, s), r=0.007, h=0.005)
    greave = Body()
    gr = lambda x, w: [sh.at(x, sh_h(x) - 0.005, -w), sh.at(x, sh_h(x) + 0.009, -w * 0.6), sh.at(x, sh_h(x) + 0.009, w * 0.6),
                       sh.at(x, sh_h(x) - 0.005, w)]
    greave.loft([gr(0.06, 0.03), gr(0.09, 0.042), gr(0.27, 0.04), gr(0.31, 0.028)], PAINT)
    # плюсна: вилка двумя щеками и полоса снизу
    tar = Body()
    for s in (-1.0, 1.0):
        tar.loft([_rect(ta, x, -0.04, 0.034, s * 0.056 - 0.01, s * 0.056 + 0.01) for x in (0.0, leg.l3)], IRON)
        _disc(tar, ta, 0.0, 0.0, s * 0.056 - 0.01, s * 0.056 + 0.01, 0.046, IRON, seg=16)
        _disc(tar, ta, leg.l3, 0.0, s * 0.056 - 0.01, s * 0.056 + 0.01, 0.036, IRON, seg=14)
        _cap(tar, ta, 0.0, s, 0.066, 0.026)
    tar.loft([_rect(ta, x, -0.05, -0.036, -0.066, 0.066) for x in (0.05, leg.l3 - 0.03)], PAINT)
    tar.tube([ta.at(0.05, -0.02, 0.0), ta.at(leg.l3 - 0.04, -0.018, 0.0)], 0.011, IRON, seg=8)
    # лапа: широкая плита, три пальца веером, шпора назад и шип вниз — якорь отдачи
    F = L["F"]
    H = leg.foot_h
    foot = Body()
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    _disc(foot, Link(F, 0.0), 0.0, 0.0, -0.045, 0.045, 0.028, IRON, seg=14)
    foot.loft([[at(-0.03, -0.01, -0.035), at(0.03, -0.01, -0.035), at(0.03, -0.01, 0.035), at(-0.03, -0.01, 0.035)],
               [at(-0.06, -H + 0.026, -0.06), at(0.06, -H + 0.026, -0.06), at(0.06, -H + 0.026, 0.06), at(-0.06, -H + 0.026, 0.06)]], IRON)
    foot.box(at(0.0, -H + 0.014, 0.0), (0.14, 0.018, 0.13), RUBBER, bevel=0.004)
    for a in (-0.55, 0.0, 0.55):
        d = (math.cos(a), 0.0, math.sin(a))
        o = at(0.05 * d[0], -H + 0.02, 0.05 * d[2])
        tip = add(o, (0.07 * d[0], -0.012, 0.07 * d[2]))
        foot.tube([o, add(o, (0.04 * d[0], 0.006, 0.04 * d[2])), tip], 0.013, IRON, seg=6,
                  radii=lambda i, n: 0.016 - 0.008 * i / max(n - 1, 1))
        foot.lathe(tip, (d[0], -0.3, d[2]), [(0.008, 0.0), (0.008, 0.008), (0.002, 0.022)], STEEL, seg=6)
    foot.tube([at(-0.05, -H + 0.024, 0.0), at(-0.09, -H + 0.012, 0.0), at(-0.105, -H - 0.002, 0.0)], 0.01, STEEL, seg=6,
              radii=lambda i, n: 0.011 - 0.006 * i / max(n - 1, 1))
    foot.lathe(at(0.0, -H + 0.006, 0.0), (0, -1, 0), [(0.012, 0.0), (0.012, 0.01), (0.003, 0.026)], STEEL, seg=8)
    return {"hip": [(hip, None)], "thigh": [(thigh, None), (guard, WEAR_LEG)], "shin": [(shin, None), (greave, WEAR_LEG)],
            "tarsus": [(tar, None)], "foot": [(foot, None)]}


# --- лыжа: нога с дырчатой изогнутой подошвой ---

def ski_parts(leg, L):
    from solid import solid
    th, sh = L["th"], L["sh"]
    P = L["P"]
    hip = Body()
    _knuckle(hip, 0.05, 0.11)
    hip.box((P[0] * 0.5, HIP_Y, 0.0), (P[0] + 0.03, 0.07, 0.07), IRON, bevel=0.006)
    _disc(hip, Link(P, 0.0), 0.0, 0.0, -0.04, 0.04, 0.044, IRON)
    _motor(hip, Link(P, 0.0), 0.0, -1.0, 0.04, r=0.04, depth=0.045)
    # бедро: литая балка, сверху крышка в краске на болтах
    hh = lambda x: 0.042 - 0.008 * x / leg.l1
    base = Body()
    base.loft([_hexr(th, x, 0.04, hh(x), 0.3) for x in (0.0, leg.l1 * 0.5, leg.l1)], None)
    adds = Body()
    _disc(adds, th, 0.0, 0.0, -0.042, 0.042, 0.05, None, seg=18)
    _disc(adds, th, leg.l1, 0.0, -0.042, 0.042, 0.04, None, seg=16)
    cuts = Body()
    for x in (0.12, 0.24):
        cuts.box(th.at(x, -hh(x), 0.0), (0.06, 0.03, 0.05), None, bevel=0.0, rot=th.m)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    cover = Body()
    cover.loft([[th.at(x, hh(x) - 0.012, -0.046), th.at(x, hh(x) + 0.008, -0.04), th.at(x, hh(x) + 0.014, 0.0),
                 th.at(x, hh(x) + 0.008, 0.04), th.at(x, hh(x) - 0.012, 0.046)] for x in (0.05, 0.12, 0.26, 0.31)], PAINT)
    for x in (0.08, 0.28):
        for s in (-1.0, 1.0):
            bolt(cover, th.at(x, hh(x) + 0.009, s * 0.032), th.dir(0.0, 1.0), r=0.0055, h=0.004)
    for s in (-1.0, 1.0):
        _cap(thigh, th, leg.l1, s, 0.042, 0.03, n=4, rb=0.03)
    thigh.tube([th.at(0.02, -0.035, -0.046), th.at(0.18, -0.04, -0.048), th.at(leg.l1 - 0.03, -0.03, -0.046)], 0.006, RUBBER, seg=6)
    # голень: труба в гильзе (как телескоп), хомуты
    shin = Body()
    shin.lathe(sh.at(0.0, 0.0, 0.0), sh.dir(1.0, 0.0), [(0.014, 0.0), (0.042, 0.014), (0.042, 0.22), (0.037, 0.234), (0.03, 0.24),
                                                       (0.03, leg.l2 - 0.03), (0.02, leg.l2 - 0.012)], IRON, seg=16)
    _disc(shin, sh, 0.0, 0.0, -0.044, 0.044, 0.048, IRON, seg=18)
    for x in (0.06, 0.16):
        shin.lathe(sh.at(x, 0.0, 0.0), sh.dir(1.0, 0.0), [(0.045, 0.0), (0.045, 0.014)], STEEL, seg=16)
    shin.lathe(sh.at(0.246, 0.0, 0.0), sh.dir(1.0, 0.0), [(0.033, 0.0), (0.033, 0.009)], RUBBER, seg=14)
    shin.tube([sh.at(0.03, 0.038, 0.026), sh.at(0.2, 0.042, 0.026), sh.at(leg.l2 - 0.06, 0.028, 0.022)], 0.006, STEEL, seg=6)
    # кожух гильзы в краске: полуобечайка сверху и по бокам
    sleeve = Body()
    sleeve.loft([[sh.at(x, -0.012, -0.05), sh.at(x, 0.026, -0.044), sh.at(x, 0.05, -0.02), sh.at(x, 0.05, 0.02),
                  sh.at(x, 0.026, 0.044), sh.at(x, -0.012, 0.05)] for x in (0.025, 0.05, 0.2, 0.225)], PAINT)
    for x in (0.08, 0.18):
        for s_ in (-1.0, 1.0):
            bolt(sleeve, sh.at(x, 0.0, s_ * 0.05), (0.0, 0.0, s_), r=0.0055, h=0.004)
    # плюсна: короткий палец с шаровой опорой
    A = L["A"]
    tar = Body()
    _disc(tar, Link(A, 0.0), 0.0, 0.0, -0.026, 0.026, 0.022, IRON, seg=12)
    tar.lathe(add(A, (0.0, -0.01, 0.0)), (0, -1, 0), [(0.016, 0.0), (0.016, leg.l3 - 0.02)], IRON, seg=10)
    from vezha_leg import sphere
    sphere(tar, add(A, (0.0, -leg.l3 + 0.012, 0.0)), 0.022, STEEL, seg=12, rings=6)
    # лыжа: изогнутая полоса вдоль хода машины, нос и пятка загнуты, дыры рядами, рёбра и крепление сверху
    F = L["F"]
    H = leg.foot_h
    yaw = math.radians(leg.deg)
    fwd = mat_vec(rot_y(-yaw), (0.0, 0.0, 1.0))   # ось z машины в осях ноги
    side = (fwd[2], 0.0, -fwd[0])
    pt = lambda s, w, y: add(F, add(add(mul(fwd, s), mul(side, w)), (0.0, y, 0.0)))
    length, half = 0.38, 0.072
    rise = lambda s: 0.06 * max(0.0, (s - 0.11) / 0.08) ** 2 if s > 0.11 else 0.03 * max(0.0, (-s - 0.12) / 0.07) ** 2
    rings = []
    for k in range(13):
        s = -0.19 + length * k / 12
        w = half * (1.0 - 0.35 * max(0.0, abs(s) - 0.12) / 0.07)
        y0 = -H + rise(s)
        rings.append([pt(s, -w, y0 + 0.014), pt(s, -w, y0), pt(s, w, y0), pt(s, w, y0 + 0.014)])
    plate = Body()
    plate.loft(rings, None, smooth=False)
    cuts = Body()
    for i in range(5):
        for j in range(3):
            s = -0.1 + 0.05 * i + (0.025 if j == 1 else 0.0)
            w = (j - 1) * 0.036
            if abs(s) > 0.11:
                continue
            cuts.lathe(pt(s, w, -H - 0.02), (0, 1, 0), [(0.012, 0.0), (0.012, 0.06)], None, seg=8)
    t = solid([("add", plate), ("cut", cuts)], bevel=0.0)
    foot = Body()
    foot.faces = [(p, IRON, s) for p, _, s in t.faces]
    for w in (-0.05, 0.05):
        foot.loft([[pt(s, w - 0.006, -H + 0.012 + rise(s)), pt(s, w + 0.006, -H + 0.012 + rise(s)), pt(s, w + 0.006, -H + 0.03 + rise(s) * 0.5),
                    pt(s, w - 0.006, -H + 0.03 + rise(s) * 0.5)] for s in (-0.15, -0.05, 0.05, 0.15)], PAINT)
    foot.loft([[pt(s, -0.03, -H + 0.012), pt(s, 0.03, -H + 0.012), pt(s, 0.02, -0.005), pt(s, -0.02, -0.005)] for s in (-0.05, 0.05)], IRON)
    foot.lathe(add(F, (0.0, -0.012, 0.0)), (0, 1, 0), [(0.03, 0.0), (0.03, 0.01), (0.018, 0.016)], STEEL, seg=12)
    for s in (-0.14, 0.14):
        for w in (-0.05, 0.05):
            bolt(foot, pt(s, w, -H + 0.014 + rise(s)), (0, 1, 0), r=0.005, h=0.004)
    return {"hip": [(hip, None)], "thigh": [(thigh, None), (cover, WEAR_LEG)], "shin": [(shin, None), (sleeve, WEAR_LEG)],
            "tarsus": [(tar, None)], "foot": [(foot, None)]}


# --- жало: молния с длинной плюсной-лезвием и острой лапой ---

def spike_parts(leg, L):
    from solid import solid
    th, sh, ta = L["th"], L["sh"], L["ta"]
    P = L["P"]
    hip = Body()
    _knuckle(hip, 0.052, 0.12)
    hip.box((P[0] * 0.5, HIP_Y, 0.0), (P[0] + 0.04, 0.08, 0.064), IRON, bevel=0.006)
    _disc(hip, Link(P, 0.0), 0.0, 0.0, -0.034, 0.034, 0.044, IRON)
    pa = piston_points(leg, L)[0]
    ring = lambda z, y0, y1: [(-0.024, y1, z), (0.024, y1, z), (0.024, y0, z), (-0.024, y0, z)]
    hip.loft([ring(-0.045, HIP_Y - 0.046, HIP_Y - 0.012), ring(-0.08, HIP_Y - 0.046, HIP_Y - 0.02),
              ring(-0.104, pa[1] - 0.024, pa[1] + 0.02)], IRON)
    hip.lathe((pa[0], pa[1], -0.104), (0, 0, -1), [(0.014, 0.0), (0.014, 0.03), (0.009, 0.036)], STEEL, seg=10)
    # бедро: жёлоб из двух щёк, сверху короткий щиток
    hh = lambda x: 0.048 - 0.01 * x / leg.l1
    base = Body()
    base.loft([_rect(th, x, hh(x) - 0.012, hh(x), -0.06, 0.06) for x in (0.06, 0.16, leg.l1 - 0.06)], None)
    adds = Body()
    for s in (-1.0, 1.0):
        adds.loft([_rect(th, x, -hh(x), hh(x), s * 0.05 - 0.012, s * 0.05 + 0.012) for x in (0.0, leg.l1 * 0.5, leg.l1)], None)
        _disc(adds, th, 0.0, 0.0, s * 0.05 - 0.012, s * 0.05 + 0.012, 0.052, None, seg=18)
        _disc(adds, th, leg.l1, 0.0, s * 0.05 - 0.012, s * 0.05 + 0.012, 0.042, None, seg=16)
    cuts = Body()
    for x, r in ((0.12, 0.02), (0.2, 0.017)):
        _disc(cuts, th, x, -0.006, -0.08, 0.08, r, None, seg=12)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.0035, bevel_angle=28.0)
    thigh = Body()
    thigh.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _disc(thigh, th, 0.0, 0.0, s * 0.031 - 0.007, s * 0.031 + 0.007, 0.04, IRON)
        _cap(thigh, th, 0.0, s, 0.062, 0.034, n=5, rb=0.042)
        _cap(thigh, th, leg.l1, s, 0.062, 0.026)
    thigh.lathe(th.at(0.24, 0.0, -0.062), (0, 0, -1), [(0.014, 0.0), (0.014, 0.042), (0.009, 0.046)], STEEL, seg=10)
    guard = Body()
    g = lambda x, w: [th.at(x, hh(x) - 0.01, -w), th.at(x, hh(x) + 0.012, -w * 0.7), th.at(x, hh(x) + 0.012, w * 0.7), th.at(x, hh(x) - 0.01, w)]
    guard.loft([g(0.05, 0.05), g(0.09, 0.066), g(0.22, 0.066), g(0.26, 0.05)], PAINT)
    # голень: двойная тяга, между щёк — тяга-распорка и кабель
    shin = Body()
    for s_ in (-1.0, 1.0):
        shin.loft([_rect(sh, x, -0.032, 0.032, s_ * 0.03 - 0.012, s_ * 0.03 + 0.012) for x in (0.0, leg.l2)], IRON)
        _disc(shin, sh, 0.0, 0.0, s_ * 0.03 - 0.012, s_ * 0.03 + 0.012, 0.044, IRON, seg=18)
        _disc(shin, sh, leg.l2, 0.0, s_ * 0.03 - 0.012, s_ * 0.03 + 0.012, 0.036, IRON, seg=16)
        _cap(shin, sh, leg.l2, s_, 0.042, 0.026)
    for x in (0.1, leg.l2 * 0.5, leg.l2 - 0.1):
        _disc(shin, sh, x, 0.0, -0.043, 0.043, 0.013, STEEL, seg=8)
    shin.box(sh.at(leg.l2 * 0.5, 0.0, 0.0), (leg.l2 - 0.12, 0.04, 0.024), IRON, bevel=0.003, rot=sh.m)
    shin.tube([sh.at(0.05, 0.04, 0.0), sh.at(leg.l2 * 0.5, 0.044, 0.0), sh.at(leg.l2 - 0.05, 0.038, 0.0)], 0.007, RUBBER, seg=6)
    plate = Body()
    plate.loft([[sh.at(x, 0.026, -0.05), sh.at(x, 0.046, -0.038), sh.at(x, 0.052, 0.0), sh.at(x, 0.046, 0.038), sh.at(x, 0.026, 0.05)]
                for x in (0.05, 0.08, 0.27, 0.3)], PAINT)
    # плюсна: длинное лезвие, гребень в краске
    tar = Body()
    tar.loft([_hexr(ta, x, w, h, 0.4) for x, w, h in ((0.0, 0.03, 0.032), (0.05, 0.03, 0.03), (leg.l3 - 0.04, 0.022, 0.022),
                                                     (leg.l3, 0.02, 0.02))], IRON)
    _disc(tar, ta, 0.0, 0.0, -0.034, 0.034, 0.036, IRON, seg=14)
    _disc(tar, ta, leg.l3, 0.0, -0.026, 0.026, 0.026, IRON, seg=12)
    ridge = Body()
    ridge.loft([[ta.at(x, 0.02, -0.014), ta.at(x, 0.04 - 0.012 * x / leg.l3, -0.005), ta.at(x, 0.04 - 0.012 * x / leg.l3, 0.005),
                 ta.at(x, 0.02, 0.014)] for x in (0.05, 0.16, leg.l3 - 0.05)], PAINT)
    tar.tube([ta.at(0.04, -0.03, 0.016), ta.at(leg.l3 - 0.05, -0.022, 0.014)], 0.005, RUBBER, seg=6)
    # лапа-жало: ось, гранёный конус в точку, резиновое кольцо-упор
    F = L["F"]
    H = leg.foot_h
    foot = Body()
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    _disc(foot, Link(F, 0.0), 0.0, 0.0, -0.03, 0.03, 0.02, IRON, seg=12)
    foot.lathe(at(0.0, 0.004, 0.0), (0, -1, 0), [(0.024, 0.0), (0.03, 0.012), (0.03, 0.02), (0.02, 0.032), (0.012, H - 0.008), (0.002, H + 0.002)],
               STEEL, seg=6, smooth=False)
    foot.lathe(at(0.0, -0.022, 0.0), (0, -1, 0), [(0.033, 0.0), (0.033, 0.008)], RUBBER, seg=12)
    return {"hip": [(hip, None)], "thigh": [(thigh, None), (guard, WEAR_LEG)], "shin": [(shin, None), (plate, WEAR_LEG)],
            "tarsus": [(tar, None), (ridge, WEAR_LEG)], "foot": [(foot, None)]}


# --- поршень подъёма (у упора и жала) ---

def piston_points(leg, L):
    """Концы поршня в бинде: палец на роге бедренного узла и палец на бедре (снаружи, −z)."""
    if leg.kind == "brace":
        return (0.05, HIP_Y - 0.05, -0.118), L["th"].at(0.24, 0.0, -0.118)
    return (0.05, HIP_Y - 0.05, -0.104), L["th"].at(0.24, 0.0, -0.104)


def piston_lengths(leg, L):
    """Длины цилиндра и штока по ходу бедра: шток не выходит из цилиндра и не торчит сзади."""
    P = L["P"]
    pa, pb = piston_points(leg, L)
    d = []
    for k in range(41):
        a = leg.th_min + (leg.th_max - leg.th_min) * k / 40 - leg.a1
        d.append(math.dist(pa, add(P, mat_vec(rot_z(a), sub(pb, P)))))
    lo, hi = min(d), max(d)
    c = r = min(lo - 0.03, (hi + 0.05) * 0.5)
    assert c + r >= hi + 0.04, ("ход поршня", leg.name, lo, hi, c, r)
    return c, r


BUILDERS = {"stilt": stilt_parts, "brace": brace_parts, "ski": ski_parts, "spike": spike_parts}


def leg_parts(leg):
    """Части ноги в осях ноги: {кость: [(тело, износ)]} и тела поршня (или None)."""
    L = leg.points()
    parts = BUILDERS[leg.kind](leg, L)
    pist = None
    if leg.kind in ("brace", "spike"):
        pa, pb = piston_points(leg, L)
        c, r = piston_lengths(leg, L)
        big = 0.022 if leg.kind == "brace" else 0.018
        pist = (pa, pb) + piston(pa, pb, c, r, big=big, small=big * 0.5)
    return L, parts, pist
