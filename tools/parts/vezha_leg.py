"""Нога «Вежи» (docs/vezha.md): три звена молнией и опорная лапа. Складывается в пачку, как нож: голень уходит
в жёлоб бедра, плюсна охватывает голень и уходит туда же.

Оси рецепта ноги: ось поворота бедра — ось y в начале координат, нога уходит в +x, z — вбок. Звено описано в
своих осях (Link): x — вдоль звена от сустава к суставу, y — спинка (у бедра сверху), z — вбок.
Плюсна всегда параллельна бедру (пантограф), поэтому нога решается как две длины: L1 + L3 и L2.
"""
import math

from kit import Body, add, mat_mul, mat_vec, mul, rot_y, rot_z, sub, unit

PAINT, WORN, IRON, STEEL = "vezha_paint", "vezha_worn", "vezha_iron", "vezha_steel"
RUBBER, GLASS, EYE, MARK = "rubber_black", "vezha_glass", "vezha_eye", "vezha_mark"
BRASS, CANVAS = "vezha_brass", "vezha_canvas"

# --- размеры, м ---
U_PITCH = 0.11                 # ось качания бедра — на столько дальше оси поворота
L1, L2, L3 = 0.58, 0.44, 0.32  # бедро, голень, плюсна (между осями суставов)
FOOT_H = 0.08                  # ось лапы над подошвой
A1 = math.radians(-45.0)       # бинд: бедро и плюсна вниз-наружу под 45°,
A2 = math.radians(-135.0)      # голень — вниз-внутрь
HIP_Y = FOOT_H + (L1 + L2 + L3) * math.sin(-A1)   # высота осей бёдер в бинде
ZP = -0.162                    # поршень подъёма — снаружи бедра (в пачке — с внешней стороны)
THIGH_MIN, THIGH_MAX = math.radians(-88.0), math.radians(32.0)   # ход бедра к горизонту

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


def leg_points():
    """Суставы ноги в бинде и концы поршня подъёма (оси ноги)."""
    P = (U_PITCH, HIP_Y, 0.0)
    th = Link(P, A1)
    K = th.end(L1)
    sh = Link(K, A2)
    A = sh.end(L2)
    ta = Link(A, A1)
    F = ta.end(L3)
    return {"P": P, "K": K, "A": A, "F": F, "th": th, "sh": sh, "ta": ta,
            "pa": (0.05, HIP_Y - 0.08, ZP), "pb": th.at(0.42, 0.0, ZP)}


def _rect(link, x, y0, y1, z0, z1):
    return [link.at(x, y0, z0), link.at(x, y1, z0), link.at(x, y1, z1), link.at(x, y0, z1)]


def _disc(b, link, x, y, z0, z1, r, mat, seg=18):
    """Цилиндр вдоль z ноги от z0 до z1 с осью в точке (x, y) звена."""
    b.lathe(link.at(x, y, z0), (0.0, 0.0, 1.0), [(r, 0.0), (r, z1 - z0)], mat, seg=seg)


def _cap(b, link, x, s, z, r, mat=STEEL, n=0, rb=0.0):
    """Крышка оси на боковой грани (s — знак стороны) и n болтов вокруг на радиусе rb."""
    b.lathe(link.at(x, 0.0, s * z), (0.0, 0.0, s), [(r, 0.0), (r, 0.008), (r * 0.7, 0.014)], mat, seg=12)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        bolt(b, add(link.at(x, 0.0, s * z), (rb * math.cos(a), rb * math.sin(a), 0.0)), (0.0, 0.0, s), r=0.007, h=0.006)


def thigh_parts(L):
    """Бедро: жёлоб из двух щёк и спинки, открыт снизу. Сверху щиток в краске, на колене — привод."""
    from solid import solid
    th = L["th"]
    hh = lambda x: 0.075 + (0.058 - 0.075) * x / L1   # полувысота щеки
    base = Body()
    base.loft([_rect(th, x, hh(x) - 0.014, hh(x), -0.115, 0.115) for x in (0.10, 0.30, 0.51)], None)   # спинка
    adds = Body()
    for s in (-1.0, 1.0):
        adds.loft([_rect(th, x, -hh(x), hh(x), s * 0.10 - 0.015, s * 0.10 + 0.015) for x in (0.0, L1 * 0.5, L1)], None)
        _disc(adds, th, 0.0, 0.0, s * 0.10 - 0.015, s * 0.10 + 0.015, 0.078, None, seg=20)
        _disc(adds, th, L1, 0.0, s * 0.10 - 0.015, s * 0.10 + 0.015, 0.064, None, seg=20)
    cuts = Body()
    for x, r in ((0.165, 0.032), (0.27, 0.028), (0.36, 0.022), (0.487, 0.02)):   # окна облегчения
        _disc(cuts, th, x, -0.004, -0.14, 0.14, r, None, seg=14)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _disc(b, th, 0.0, 0.0, s * 0.0625 - 0.0215, s * 0.0625 + 0.0215, 0.056, IRON)     # ступица оси качания
        _disc(b, th, L1, 0.0, s * 0.066 - 0.0185, s * 0.066 + 0.0185, 0.036, STEEL, seg=12)  # втулка колена
        _cap(b, th, 0.0, s, 0.115, 0.04, n=6, rb=0.058)
    # привод колена: стакан мотора на внешней щеке (−z), плоская крышка на внутренней
    b.lathe(th.at(L1, 0.0, -0.115), (0.0, 0.0, -1.0), [(0.058, 0.0), (0.058, 0.012), (0.05, 0.016), (0.05, 0.04),
                                                       (0.042, 0.046), (0.02, 0.046)], IRON, seg=18)
    for k in range(8):   # рёбра охлаждения стакана
        a = 2 * math.pi * k / 8
        d = (math.cos(a), math.sin(a), 0.0)
        b.box(add(th.at(L1, 0.0, -0.143), mul(d, 0.05)), (0.012, 0.006, 0.024), IRON, bevel=0.0,
              rot=rot_z(a))
    _cap(b, th, L1, 1.0, 0.115, 0.05, mat=IRON, n=6, rb=0.04)
    # палец поршня и короб проводки по внешней щеке
    b.lathe(th.at(0.42, 0.0, -0.113), (0.0, 0.0, -1.0), [(0.019, 0.0), (0.019, 0.07), (0.012, 0.076)], STEEL, seg=10)
    b.tube([th.at(0.07, -0.05, 0.123), th.at(0.20, -0.052, 0.123), th.at(0.50, -0.042, 0.123)], 0.008, RUBBER, seg=6)
    for x in (0.12, 0.30, 0.46):
        b.box(th.at(x, -0.05 + 0.01 * x / 0.46, 0.122), (0.022, 0.03, 0.02), STEEL, bevel=0.003, rot=th.m)
    # щиток: спинка с ребром по оси, отгибы на щёки, болты; метка — три косые полосы у колена
    guard = Body()
    ring = lambda x, w, e: [th.at(x, hh(x) - 0.03, -w), th.at(x, hh(x) + 0.004, -w), th.at(x, hh(x) + 0.017 + e, -w * 0.52),
                            th.at(x, hh(x) + 0.017 + e, w * 0.52), th.at(x, hh(x) + 0.004, w), th.at(x, hh(x) - 0.03, w)]
    guard.loft([ring(0.075, 0.105, 0.0), ring(0.13, 0.129, 0.004), ring(0.30, 0.129, 0.007), ring(0.47, 0.127, 0.004),
                ring(0.535, 0.10, 0.0)], PAINT)
    for x in (0.15, 0.31, 0.45):
        for s in (-1.0, 1.0):
            bolt(guard, th.at(x, hh(x) + 0.0115, s * 0.098), unit(th.dir(0.0, 0.84, s * 0.54)), r=0.007, h=0.006)
    # смотровой лючок на щитке у бедренного конца: тонкая крышка на четырёх винтах
    e0 = 0.0055
    guard.box(th.at(0.2, hh(0.2) + 0.017 + e0 + 0.002, 0.0), (0.085, 0.006, 0.07), PAINT, bevel=0.002, rot=th.m)
    for sx in (-1.0, 1.0):
        for sz in (-1.0, 1.0):
            bolt(guard, th.at(0.2 + sx * 0.032, hh(0.2) + 0.017 + e0 + 0.005, sz * 0.025), th.dir(0.0, 1.0), r=0.005, h=0.004)
    marks = Body()
    for k in range(3):
        x = 0.36 + 0.036 * k
        e = 0.007 - 0.003 * (x - 0.30) / 0.17
        marks.box(th.at(x, hh(x) + 0.017 + e + 0.0006, 0.0), (0.016, 0.003, 0.105), MARK, bevel=0.0,
                  rot=mat_mul(th.m, rot_y(0.5)))
    return [(b, None), (guard, WEAR_LEG), (marks, None)]


def shin_parts(L):
    """Голень: одна балка с карманами, спинка (наружу в стойке) под узким щитком."""
    from solid import solid
    sh = L["sh"]
    hh = lambda x: 0.046 + (0.034 - 0.046) * x / L2
    base = Body()
    base.loft([_rect(sh, x, -hh(x), hh(x), -0.045, 0.045) for x in (0.0, L2 * 0.5, L2)], None)
    adds = Body()
    _disc(adds, sh, 0.0, 0.0, -0.045, 0.045, 0.056, None, seg=20)
    _disc(adds, sh, L2, 0.0, -0.045, 0.045, 0.046, None, seg=18)
    cuts = Body()
    for s in (-1.0, 1.0):
        cuts.loft([_rect(sh, x, -hh(x) + 0.012, hh(x) - 0.012, s * 0.047 - 0.014, s * 0.047 + 0.014)
                   for x in (0.085, L2 - 0.075)], None)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):   # магистраль в кармане и болты перемычек
        b.tube([sh.at(0.10, 0.006, s * 0.036), sh.at(L2 - 0.09, 0.004, s * 0.036)], 0.007, STEEL, seg=6)
        for x in (0.066, L2 - 0.058):
            bolt(b, sh.at(x, 0.0, s * 0.045), (0.0, 0.0, s), r=0.008, h=0.006)
    greave = Body()
    ring = lambda x, w: [sh.at(x, hh(x) - 0.006, -w), sh.at(x, hh(x) + 0.009, -w * 0.6), sh.at(x, hh(x) + 0.009, w * 0.6),
                         sh.at(x, hh(x) - 0.006, w)]
    greave.loft([ring(0.075, 0.03), ring(0.11, 0.041), ring(0.30, 0.039), ring(0.36, 0.026)], PAINT)
    return [(b, None), (greave, WEAR_LEG)]


def tarsus_parts(L):
    """Плюсна: жёлоб из двух щёк и подошвенной полосы, открыт к голени. Конец — вилка оси лапы."""
    from solid import solid
    ta = L["ta"]
    top = lambda x: 0.044 + (0.036 - 0.044) * x / L3
    bot = lambda x: -0.062 + 0.012 * x / L3   # −0.062 у голеностопа … −0.05 у лапы
    base = Body()
    base.loft([_rect(ta, x, bot(x), bot(x) + 0.012, -0.075, 0.075) for x in (0.075, L3 - 0.035)], None)   # полоса
    adds = Body()
    for s in (-1.0, 1.0):
        adds.loft([_rect(ta, x, bot(x), top(x), s * 0.064 - 0.011, s * 0.064 + 0.011) for x in (0.0, L3 * 0.5, L3)], None)
        _disc(adds, ta, 0.0, 0.0, s * 0.064 - 0.011, s * 0.064 + 0.011, 0.054, None, seg=18)
        _disc(adds, ta, L3, 0.0, s * 0.064 - 0.011, s * 0.064 + 0.011, 0.041, None, seg=16)
    cuts = Body()
    for x, r in ((0.125, 0.021), (0.215, 0.018)):
        _disc(cuts, ta, x, -0.008, -0.1, 0.1, r, None, seg=12)
    t = solid([("add", base), ("add", adds), ("cut", cuts)], bevel=0.004, bevel_angle=28.0)
    b = Body()
    b.faces = [(p, IRON, s) for p, _, s in t.faces]
    for s in (-1.0, 1.0):
        _cap(b, ta, 0.0, s, 0.075, 0.03, n=0)
        _cap(b, ta, L3, s, 0.075, 0.022, n=0)
    # пружина-демпфер лапы в жёлобе и её упор
    b.tube([ta.at(0.10, -0.034, 0.0), ta.at(0.25, -0.03, 0.0)], 0.014, IRON, seg=8)
    b.tube([ta.at(0.25, -0.03, 0.0), ta.at(0.30, -0.027, 0.0)], 0.007, STEEL, seg=6)
    skid = Body()
    ring = lambda x, w: [ta.at(x, bot(x) + 0.005, -w), ta.at(x, bot(x) - 0.011, -w * 0.7), ta.at(x, bot(x) - 0.011, w * 0.7),
                         ta.at(x, bot(x) + 0.005, w)]
    skid.loft([ring(0.06, 0.06), ring(0.10, 0.081), ring(L3 - 0.07, 0.081), ring(L3 - 0.03, 0.06)], PAINT)
    return [(b, None), (skid, WEAR_LEG)]


def foot_parts(L):
    """Лапа: башмак с резиновой подошвой на оси, два стальных когтя вперёд и шпора назад. Оси — мира (подошва вниз)."""
    F = L["F"]
    f = Body()
    at = lambda x, y, z=0.0: add(F, (x, y, z))
    H = FOOT_H
    _disc(f, Link(F, 0.0), 0.0, 0.0, -0.05, 0.05, 0.03, IRON, seg=14)
    f.loft([[at(-0.03, -0.012, -0.036), at(0.035, -0.012, -0.036), at(0.035, -0.012, 0.036), at(-0.03, -0.012, 0.036)],
            [at(-0.05, -H + 0.03, -0.056), at(0.075, -H + 0.03, -0.056), at(0.075, -H + 0.03, 0.056),
             at(-0.05, -H + 0.03, 0.056)]], IRON)
    f.box(at(0.014, -H + 0.022, 0.0), (0.15, 0.02, 0.122), IRON, bevel=0.005)
    f.box(at(0.014, -H + 0.008, 0.0), (0.138, 0.016, 0.11), RUBBER, bevel=0.004)
    for s in (-1.0, 1.0):   # когти
        f.loft([[at(0.08, -H + 0.032, s * 0.038 - 0.018), at(0.08, -H + 0.032, s * 0.038 + 0.018),
                 at(0.08, -H + 0.008, s * 0.038 + 0.018), at(0.08, -H + 0.008, s * 0.038 - 0.018)],
                [at(0.118, -H + 0.022, s * 0.043 - 0.012), at(0.118, -H + 0.022, s * 0.043 + 0.012),
                 at(0.118, -H + 0.004, s * 0.043 + 0.012), at(0.118, -H + 0.004, s * 0.043 - 0.012)],
                [at(0.14, -H + 0.004, s * 0.046 - 0.004), at(0.14, -H + 0.004, s * 0.046 + 0.004),
                 at(0.14, -H - 0.005, s * 0.046 + 0.004), at(0.14, -H - 0.005, s * 0.046 - 0.004)]], STEEL)
    f.loft([[at(-0.058, -H + 0.03, -0.016), at(-0.058, -H + 0.03, 0.016), at(-0.058, -H + 0.008, 0.016),
             at(-0.058, -H + 0.008, -0.016)],
            [at(-0.088, -H + 0.016, -0.007), at(-0.088, -H + 0.016, 0.007), at(-0.088, -H + 0.0, 0.007),
             at(-0.088, -H + 0.0, -0.007)]], STEEL)
    return [(f, None)]


def hip_parts(L):
    """Бедренный узел: кулак между палубами рамы, шея с языком оси качания, рог пальца поршня сбоку."""
    h = Body()
    h.lathe((0.0, HIP_Y - 0.07, 0.0), (0, 1, 0), [(0.05, 0.0), (0.078, 0.008), (0.078, 0.132), (0.05, 0.14)], IRON, seg=18)   # кулак
    h.box((0.07, HIP_Y, 0.0), (0.07, 0.125, 0.074), IRON, bevel=0.006)                # шея к оси качания
    _disc(h, Link(L["P"], 0.0), 0.0, 0.0, -0.039, 0.039, 0.052, IRON)
    pa = L["pa"]
    ring = lambda z, y0, y1: [(-0.03, y1, z), (0.03, y1, z), (0.03, y0, z), (-0.03, y0, z)]
    h.loft([ring(-0.06, HIP_Y - 0.066, HIP_Y - 0.022), ring(-0.1, HIP_Y - 0.066, HIP_Y - 0.03),
            ring(-0.139, HIP_Y - 0.105, HIP_Y - 0.055)], IRON)                         # рог пальца поршня
    h.box((pa[0] * 0.5 - 0.005, pa[1], -0.1335), (pa[0] + 0.07, 0.05, 0.013), IRON, bevel=0.004)
    h.lathe((pa[0], pa[1], -0.139), (0, 0, -1), [(0.018, 0.0), (0.018, 0.042), (0.011, 0.048)], STEEL, seg=10)
    return [(h, None)]


def piston(a, b_, c_len, r_len, big=0.025, small=0.0125):
    """Цилиндр от a к b_ длиной c_len и шток от b_ к a длиной r_len. Возвращает (тело цилиндра, тело штока)."""
    d = unit(sub(b_, a))
    cyl, rod = Body(), Body()
    cyl.lathe(add(a, (0, 0, -0.016)), (0, 0, 1), [(0.024, 0.0), (0.024, 0.032)], IRON, seg=10)          # проушина
    cyl.lathe(add(a, mul(d, 0.02)), d, [(0.016, 0.0), (big, 0.012), (big, c_len - 0.045), (big + 0.006, c_len - 0.04),
                                        (big + 0.006, c_len - 0.022), (small + 0.005, c_len - 0.02)], IRON, seg=12)
    rod.lathe(add(b_, (0, 0, -0.015)), (0, 0, 1), [(0.021, 0.0), (0.021, 0.03)], IRON, seg=10)
    rod.lathe(add(b_, mul(d, -r_len)), d, [(small, 0.0), (small, r_len - 0.015)], STEEL, seg=10)
    return cyl, rod


def piston_lengths(L):
    """Длины цилиндра и штока по ходу бедра: шток не выходит из цилиндра и не торчит сзади."""
    P = L["P"]
    d = []
    steps = 40
    for k in range(steps + 1):
        a = THIGH_MIN + (THIGH_MAX - THIGH_MIN) * k / steps - A1
        d.append(math.dist(L["pa"], add(P, mat_vec(rot_z(a), sub(L["pb"], P)))))
    lo, hi = min(d), max(d)
    c = r = min(lo - 0.03, (hi + 0.05) * 0.5)
    assert c + r >= hi + 0.04, ("ход поршня", lo, hi, c, r)
    return c, r


def leg_parts(L):
    """Части ноги в осях ноги: {кость: [(тело, износ)]}. Кости без приставки ноги."""
    return {"hip": hip_parts(L), "thigh": thigh_parts(L), "shin": shin_parts(L), "tarsus": tarsus_parts(L),
            "foot": foot_parts(L)}
