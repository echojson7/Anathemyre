"""Руки «Гриди» (docs/gryd.md): два манипулятора держат винтовку — правая кисть на рукояти, левая на передней
рукояти. Плечо — шаровой узел в гнезде корпуса, плечо и предплечье — балки в щитках, локоть двигает гидроцилиндр
по наружной стороне (как трицепс), кисть — три пальца и большой палец, обхватившие рукоять.

Игра ставит кисти на рукояти каждый кадр (решение двумя длинами, локоть — к своей точке-«полюсу», src/view/mech.cpp),
поворот кисти — как у винтовки. Здесь та же геометрия для бинда (arm_ik) и проверки хода цилиндра.
"""
import math

import numpy as np

from kit import Body, add, cross, mul, sub, unit
from shape import Frame, hexbolt, pin, rbox, rcyl, rlathe, rloft, seg_for

PAINT, WORN, IRON, STEEL, HOSE, BRASS = "gryd_paint", "gryd_worn", "gryd_iron", "vezha_steel", "rubber_black", "vezha_brass"
WEAR = (WORN, 0.014, 0.65, 0.03)

UA, FA = 0.40, 0.42          # плечо и предплечье (между осями)
EA = (0.10, -0.052)           # цилиндр локтя на плече: вдоль, к наружной стороне сгиба
EB = 0.07                     # шток на роге предплечья за локтем


def arm_ik(S, W, pole, la=UA, lb=FA):
    """Локоть для плеча S и запястья W: в плоскости через S, W и точку pole, локоть — к полюсу."""
    S, W, pole = np.asarray(S, float), np.asarray(W, float), np.asarray(pole, float)
    d = W - S
    L = np.linalg.norm(d)
    L = min(max(L, abs(la - lb) + 1e-4), la + lb - 1e-4)
    x = d / np.linalg.norm(d)
    p = pole - S
    p = p - x * np.dot(p, x)
    y = p / np.linalg.norm(p)
    a = (la * la + L * L - lb * lb) / (2 * L)
    h = math.sqrt(max(la * la - a * a, 0.0))
    return tuple(S + x * a + y * h)


def arm_frames(S, E, W):
    """Оси плеча и предплечья: x — вдоль звена, z — ось локтя (общая), y — внутрь сгиба (у плеча)."""
    S, E, W = (tuple(map(float, v)) for v in (S, E, W))
    xu = unit(sub(E, S))
    xf = unit(sub(W, E))
    n = cross(xu, xf)
    n = unit(n) if sum(v * v for v in n) > 1e-12 else (1.0, 0.0, 0.0)
    yu, yf = cross(n, xu), cross(n, xf)
    fu = Frame(S, ((xu[0], yu[0], n[0]), (xu[1], yu[1], n[1]), (xu[2], yu[2], n[2])))
    ff = Frame(E, ((xf[0], yf[0], n[0]), (xf[1], yf[1], n[1]), (xf[2], yf[2], n[2])))
    return fu, ff


def cyl_points(S, E, W):
    """Концы цилиндра локтя: на плече (ea) и на роге предплечья (eb)."""
    fu, ff = arm_frames(S, E, W)
    return fu.at(EA[0], EA[1]), ff.at(-EB, 0.0)


def upper_parts(S, E, W, side):
    """Плечо: шаровой узел, балка, щиток снаружи сверху, вилка локтя, проушина цилиндра."""
    fu, _ = arm_frames(S, E, W)
    b, sh, det = Body(), Body(), Body()
    rlathe(b, add(S, (-side * 0.05, 0.0, 0.0)), (side, 0.0, 0.0),
           [(0.045, 0.0), (0.06, 0.012), (0.066, 0.035), (0.06, 0.062), (0.04, 0.078)], IRON)       # шар в гнезде
    rloft(b, fu, [(0.03, 0.034, 0.04, 0.014, 0.0), (0.2, 0.032, 0.036, 0.013, 0.0), (0.33, 0.028, 0.032, 0.012, 0.0)], IRON, n=3)
    for sz in (-1.0, 1.0):   # вилка локтя
        rloft(b, fu.sub(UA - 0.08, 0.0, sz * 0.044), [(0.0, 0.009, 0.034, 0.008, 0.0), (0.11, 0.009, 0.042, 0.009, 0.0)], IRON, end=0.006)
    pin(b, E, fu.dir(0, 0, 1), 0.016, 0.06, STEEL)
    # щиток: корыто по наружной стороне с отгибом
    rloft(sh, fu.sub(0.05, -0.044, 0.0), [(0.0, 0.044, 0.008, 0.006, 0.0), (0.14, 0.048, 0.009, 0.006, -0.004),
                                          (0.27, 0.04, 0.008, 0.006, 0.0)], PAINT, n=3, end=0.007)
    for x in (0.08, 0.26):
        for sz in (-1.0, 1.0):
            hexbolt(det, fu.at(x, -0.053, sz * 0.032), fu.dir(0, -1, 0), 0.005, 0.005, IRON)
    ea = fu.at(EA[0], EA[1])
    rcyl(det, add(ea, mul(fu.dir(0, 0, 1), -0.022)), fu.dir(0, 0, 1), 0.016, 0.044, IRON, e=0.004)
    rbox(det, fu.at(EA[0], EA[1] * 0.55), (0.03, 0.03, 0.03), 0.008, IRON, rot=fu.m)
    return b, sh, det


def fore_parts(S, E, W, side):
    """Предплечье: ступица локтя с рогом, балка, кожух в краске, кольцо запястья, шланг."""
    _, ff = arm_frames(S, E, W)
    b, sh, det = Body(), Body(), Body()
    rcyl(b, ff.at(0, 0, -0.035), ff.dir(0, 0, 1), 0.04, 0.07, IRON, e=0.007)
    rloft(b, ff, [(-EB - 0.012, 0.016, 0.018, 0.008, 0.0), (0.0, 0.03, 0.036, 0.012, 0.0), (0.2, 0.03, 0.033, 0.012, 0.0),
                  (FA - 0.04, 0.026, 0.028, 0.011, 0.0)], IRON, n=3, end=0.006)
    eb = ff.at(-EB, 0.0)
    rcyl(det, add(eb, mul(ff.dir(0, 0, 1), -0.02)), ff.dir(0, 0, 1), 0.014, 0.04, IRON, e=0.004)
    rlathe(b, ff.at(FA - 0.05), ff.dir(1, 0, 0), [(0.03, 0.0), (0.036, 0.006), (0.036, 0.03), (0.03, 0.036)], IRON)   # кольцо запястья
    rloft(sh, ff.sub(0.06, 0.0, 0.0), [(0.0, 0.042, 0.044, 0.02, 0.0), (0.14, 0.046, 0.047, 0.022, 0.0), (0.26, 0.04, 0.042, 0.02, 0.0)],
          PAINT, n=3, end=0.012)
    for x in (0.09, 0.29):
        for a in (0.0, math.pi):
            hexbolt(det, ff.at(x, 0.044 * math.cos(a), 0.044 * math.sin(a) + 0.0), ff.dir(0, math.cos(a), 0), 0.0045, 0.004, IRON, washer=False)
    det.tube([ff.at(0.02, 0.0, side * 0.04), ff.at(0.2, 0.0, side * 0.052), ff.at(FA - 0.06, 0.0, side * 0.04)], 0.006, HOSE, seg=10, fillet=0.04)
    return b, sh, det


def hand_frame(grip_c, axis, palm, rg=0.024):
    """Оси кисти: x — вдоль рукояти вниз, y — к ладонной коробке, начало — середина коробки; запястье — над ней."""
    a = unit(axis)
    s = unit(sub(palm, mul(a, sum(p * q for p, q in zip(palm, a)))))
    f = cross(a, s)
    back = Frame(add(grip_c, mul(s, rg + 0.024)), ((a[0], s[0], f[0]), (a[1], s[1], f[1]), (a[2], s[2], f[2])))
    return back, back.at(-0.10, 0.0, 0.0)


def hand(grip_c, axis, palm, spread=0.026, rg=0.024):
    """Кисть на рукояти: тыльная коробка у запястья с ладони palm (единичный вектор от оси рукояти), три пальца
    вокруг рукояти, большой палец навстречу. grip_c — точка оси рукояти у среднего пальца, axis — ось рукояти вниз."""
    b, det = Body(), Body()
    back, wrist = hand_frame(grip_c, axis, palm, rg)
    a, s, f = back.dir(1, 0, 0), back.dir(0, 1, 0), back.dir(0, 0, 1)
    # ладонь: коробка с ладонной стороны, от запястья к пальцам
    rbox(b, back.at(-0.02, 0.004, 0.0), (0.11, 0.032, 0.07), 0.012, IRON, rot=back.m)
    rbox(b, back.at(-0.02, 0.022, 0.0), (0.085, 0.01, 0.055), 0.004, PAINT, rot=back.m)
    # от ладони к запястью
    rloft(b, Frame.look(back.at(-0.05, 0.004, 0.0), sub(wrist, back.at(-0.05, 0.004, 0.0))),
          [(0.0, 0.026, 0.02, 0.01, 0.0), (max(0.02, math.dist(wrist, back.at(-0.05)) - 0.005), 0.024, 0.024, 0.01, 0.0)], IRON, n=3, end=0.006)
    rlathe(b, wrist, unit(sub(back.o, wrist)), [(0.022, -0.02), (0.03, -0.012), (0.03, 0.012), (0.022, 0.02)], IRON)
    rf = rg + 0.012
    for i in (-1, 0, 1):
        c = add(grip_c, mul(a, i * spread))
        prev = None
        for k, (t0, t1, w) in enumerate(((0.0, 1.1, 0.012), (1.1, 2.2, 0.011), (2.2, 3.0, 0.0095))):
            p0 = add(c, add(mul(s, rf * math.cos(t0)), mul(f, rf * math.sin(t0))))
            p1 = add(c, add(mul(s, rf * math.cos(t1)), mul(f, rf * math.sin(t1))))
            fr = Frame.look(p0, sub(p1, p0), up=a)
            ln = math.dist(p0, p1)
            rloft(b, fr, [(0.0, w, 0.011, 0.007, 0.0), (ln, w * 0.92, 0.0105, 0.007, 0.0)], IRON, n=3, end=0.005)
            rcyl(det, add(p0, mul(a, -0.012)), a, 0.0085, 0.024, STEEL, e=0.002)    # шарнир фаланги
    # большой палец: навстречу пальцам с другой стороны, над ними
    c = add(grip_c, mul(a, -spread * 1.9))
    for t0, t1 in ((0.0, -1.3), (-1.3, -2.4)):
        p0 = add(c, add(mul(s, rf * math.cos(t0)), mul(f, rf * math.sin(t0))))
        p1 = add(c, add(mul(s, rf * math.cos(t1)), mul(f, rf * math.sin(t1))))
        rloft(b, Frame.look(p0, sub(p1, p0), up=a), [(0.0, 0.0125, 0.012, 0.007, 0.0), (math.dist(p0, p1), 0.011, 0.011, 0.007, 0.0)],
              IRON, n=3, end=0.005)
        rcyl(det, add(p0, mul(a, -0.012)), a, 0.009, 0.024, STEEL, e=0.002)
    return b, det
