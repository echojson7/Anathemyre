"""Ноги «Гриди» (docs/gryd.md): две ноги с обратным коленом, как у птицы, — бедро вперёд-вниз, голень назад-вниз
до заднего сустава (голеностоп, «обратное колено»), плюсна вперёд-вниз до ступни. Каждый сустав ниже бедра
двигают два гидроцилиндра по бокам звена; бедро качает большой поворотный привод (диск снаружи таза).

Оси рецепта ноги — как у Вежи и Тати: ось поворота бедра (рысканье) — ось y в начале координат, нога уходит в +x
(вперёд машины), z — вбок. out — куда наружу по z рецепта (правая нога: −1, левая: +1), несимметричное (диск
привода, щиток, шланги) кладётся по нему. Звено описано в своих осях (Link): x — вдоль звена, y — к «спинке».
Плюсна держит постоянный угол TAR к горизонту в осях таза: нога решается двумя длинами L1 и L2 до голеностопа.
"""
import math

from kit import Body, add, mul, sub, unit
from shape import hexbolt, helix, pin, rbox, rcyl, rlathe, rloft, Frame, seg_for
from vezha_leg import Link

PAINT, WORN, IRON, STEEL = "gryd_paint", "gryd_worn", "gryd_iron", "vezha_steel"
RUBBER, GLASS, LENS, MARK, HOSE = "rubber_black", "vezha_glass", "gryd_lens", "gryd_mark", "rubber_black"
BRASS = "vezha_brass"

HIP_Y = 1.16                           # оси бёдер над опорой в бинде (стоит)
WEAR_LEG = (WORN, 0.014, 0.65, 0.03)   # щитки ног бьются сильнее корпуса

# --- размеры ноги, м (числа решения — в MECH_DESC, src/view/mech_common.cpp) ---
U = 0.03                     # ось качания бедра впереди оси рысканья
L1, L2, L3 = 0.42, 0.54, 0.34    # бедро, голень, плюсна (между осями суставов)
TAR = math.radians(-72.0)    # плюсна к горизонту: вперёд и вниз
FOOT_H = 0.085               # ось ступни над подошвой
REACH = 0.07                 # бинд: ось ступни впереди оси качания
TH_MIN, TH_MAX = -118.0, 20.0    # ход бедра к горизонту, град
HALF = 0.19                  # оси ног от середины таза

# Гидроцилиндры (точки в осях звена; по два — по бокам звена на ±SIDE). Колено — от хвоста бедра за осью к рогу
# голени под коленом, голеностоп — от голени у колена к рогу плюсны над голеностопом. Подобраны перебором по всему
# ходу ноги (piston_lengths): шток не выходит из цилиндра, плечо силы не меньше 5 см
KNEE_A, KNEE_B = (0.02, -0.10), (0.04, -0.06)
ANK_A, ANK_B = (0.02, -0.10), (-0.06, 0.06)
KNEE_SIDE, ANK_SIDE = 0.068, 0.058


def solve(x, y, l1=L1, l2=L2, l3=L3, tar=TAR):
    """Углы бедра, голени и плюсны к горизонту для оси ступни в (x, y) от оси качания (колено вперёд)."""
    tx, ty = x - l3 * math.cos(tar), y - l3 * math.sin(tar)
    d = min(max(math.hypot(tx, ty), abs(l1 - l2) + 1e-4), l1 + l2 - 1e-4)
    a = math.atan2(ty, tx) + math.acos(max(-1.0, min(1.0, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d))))
    a = min(max(a, math.radians(TH_MIN)), math.radians(TH_MAX))
    s = math.atan2(ty - l1 * math.sin(a), tx - l1 * math.cos(a))
    return a, s, tar


class Leg:
    """Место ноги на тазу: имя, поворот рецепта вокруг y (+x рецепта → (cos, 0, −sin)), ось рысканья (x, z), наружу."""

    def __init__(self, name, corner, out):
        self.name, self.deg, self.corner, self.out = name, -90.0, corner, out
        self.u, self.l1, self.l2, self.l3, self.foot_h = U, L1, L2, L3, FOOT_H
        self.a1, self.a2, self.a3 = solve(REACH, FOOT_H - HIP_Y)

    def points(self, a1=None, a2=None):
        """Суставы (оси ноги): ось качания P, колено K, голеностоп A, ось ступни F, звенья и концы цилиндров."""
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
                "ka": th.at(*KNEE_A), "kb": sh.at(*KNEE_B), "ma": sh.at(*ANK_A), "mb": tr.at(*ANK_B)}


# Правая нога — leg[0], левая — leg[1] (порядок MECH_DESC «Гриди»)
LEGS = (Leg("r", (HALF, 0.0), -1.0), Leg("l", (-HALF, 0.0), 1.0))


def poses_range():
    """Позы всего хода ноги: высота осей бёдер 0.80 … 1.24, ступня от 0.55 позади до 0.45 впереди, на земле и
    поднятая в шаге на 0.15 м. Только достижимые и в пределах хода бедра."""
    out = []
    for lift, hs, xs in ((0.0, (0.80, 1.24, 25), (-0.55, 0.45, 31)), (0.15, (0.86, 1.22, 13), (-0.45, 0.40, 21))):
        for i in range(hs[2]):
            h = hs[0] + (hs[1] - hs[0]) * i / (hs[2] - 1)
            for j in range(xs[2]):
                x = xs[0] + (xs[1] - xs[0]) * j / (xs[2] - 1)
                tx, ty = x - L3 * math.cos(TAR), FOOT_H + lift - h - L3 * math.sin(TAR)
                d = math.hypot(tx, ty)
                if d > L1 + L2 - 0.03 or d < abs(L1 - L2) + 0.02:
                    continue
                a = math.atan2(ty, tx) + math.acos(max(-1.0, min(1.0, (L1 * L1 + d * d - L2 * L2) / (2 * L1 * d))))
                if not math.radians(TH_MIN) <= a <= math.radians(TH_MAX):
                    continue
                out.append(solve(x, FOOT_H + lift - h)[:2])
    return out


def piston_lengths(leg):
    """Длины цилиндров и штоков по всему ходу ноги: [(цилиндр, шток) колена, (…) голеностопа]."""
    knee, ank = [], []
    for a, s in poses_range():
        L = leg.points(a, s)
        knee.append(math.dist(L["ka"], L["kb"]))
        ank.append(math.dist(L["ma"], L["mb"]))
    out = []
    for d in (knee, ank):
        lo, hi = min(d), max(d)
        c = lo - 0.035
        r = c
        assert c + r >= hi + 0.035, ("ход поршня", lo, hi, c, r)
        out.append((c, r))
    return out


# --- детали ---

def piston(a, b, cyl, rod, rc=0.024, rr=0.012):
    """Гидроцилиндр от a к b: гильза с проушиной у a (кость цилиндра) и шток с проушиной у b (кость штока).
    Возвращает два тела (гильза, шток) в осях сетки."""
    d = unit(sub(b, a))
    g, s = Body(), Body()
    fa = Frame.look(a, d)
    # гильза: донце с проушиной, корпус, сальник
    side = fa.dir(0, 0, 1)
    rcyl(g, sub(a, mul(side, 0.022)), side, 0.017, 0.044, IRON, e=0.004)          # проушина поперёк
    rlathe(g, a, d, [(0.012, 0.0), (rc * 0.9, 0.012), (rc, 0.026), (rc, cyl - 0.03), (rc * 1.12, cyl - 0.026),
                     (rc * 1.12, cyl - 0.008), (rr * 1.5, cyl)], IRON)
    rcyl(g, fa.at(0.08, 0, 0), d, rc * 1.08, 0.012, IRON, e=0.003)                # хомут
    rcyl(g, fa.at(0.02, rc + 0.004, 0), d, 0.006, cyl - 0.07, BRASS, e=0.002)     # трубка вдоль гильзы
    hexbolt(g, fa.at(cyl - 0.035, rc + 0.002, 0), fa.dir(0, 1, 0), 0.006, 0.006, IRON, washer=False)
    # шток: проушина у b, хромированный стержень
    fb = Frame.look(b, mul(d, -1.0))
    rcyl(s, sub(b, mul(side, 0.019)), side, 0.015, 0.038, IRON, e=0.004)
    rlathe(s, b, mul(d, -1.0), [(0.011, 0.0), (rr * 1.25, 0.012), (rr, 0.022), (rr, rod)], STEEL, seg=seg_for(rr))
    return g, s, fb


def _hub(b, c, r, w, mat, out=0.0):
    """Ступица сустава: цилиндр поперёк ноги (ось z) со скруглёнными кромками и крышкой на болтах."""
    rcyl(b, (c[0], c[1], c[2] - w / 2), (0, 0, 1), r, w, mat, e=min(0.008, r * 0.2))
    for sz in (-1.0, 1.0):
        rcyl(b, (c[0], c[1], c[2] + sz * w / 2), (0, 0, sz), r * 0.62, 0.008, IRON, e=0.003)
        for k in range(6):
            a = 2 * math.pi * (k + 0.5) / 6
            hexbolt(b, (c[0] + r * 0.45 * math.cos(a), c[1] + r * 0.45 * math.sin(a), c[2] + sz * (w / 2 + 0.007)),
                    (0, 0, sz), 0.0055, 0.006, IRON, washer=False)


def hip_parts(leg):
    """Узел рысканья (кость hip): вал в подшипник таза, вилка оси качания, большой привод бедра снаружи."""
    o = leg.out
    P = (U, HIP_Y, 0.0)
    b = Body()
    rcyl(b, (0.0, HIP_Y + 0.035, 0.0), (0, 1, 0), 0.052, 0.10, IRON, e=0.006)                 # вал рысканья
    rcyl(b, (0.0, HIP_Y + 0.035, 0.0), (0, 1, 0), 0.064, 0.022, IRON, e=0.006)                # фланец вала
    # вилка: перемычка над бедром и две щеки по бокам
    rbox(b, (U - 0.005, HIP_Y + 0.062, 0.0), (0.13, 0.04, 0.17), 0.012, IRON)
    for sz in (-1.0, 1.0):
        f = Frame((U, HIP_Y, sz * 0.067))
        rloft(b, Frame.look(f.at(0, 0.075), (0, -1, 0), up=(1, 0, 0)),
              [(0.0, 0.016, 0.055, 0.010, 0.0), (0.06, 0.016, 0.06, 0.010, 0.0), (0.105, 0.016, 0.07, 0.012, 0.0)],
              IRON, end=0.008)
    # привод бедра снаружи: корпус-диск, крышка, рёбра, болты по кругу, ввод шлангов
    pz = o * 0.083
    body = Body()
    rcyl(body, (U, HIP_Y, pz - o * 0.012), (0, 0, o), 0.122, 0.07, PAINT, e=0.014)
    rcyl(body, (U, HIP_Y, pz + o * 0.056), (0, 0, o), 0.085, 0.018, PAINT, e=0.006)
    for k in range(12):
        a = 2 * math.pi * k / 12
        c, s = math.cos(a), math.sin(a)
        rbox(body, (U + 0.104 * c, HIP_Y + 0.104 * s, pz + o * 0.022), (0.026, 0.012, 0.062), 0.004, PAINT,
             rot=((c, -s, 0), (s, c, 0), (0, 0, 1)))
    det = Body()
    rcyl(det, (U, HIP_Y, pz + o * 0.072), (0, 0, o), 0.042, 0.016, IRON, e=0.005)               # колпак оси
    for k in range(10):
        a = 2 * math.pi * (k + 0.5) / 10
        hexbolt(det, (U + 0.066 * math.cos(a), HIP_Y + 0.066 * math.sin(a), pz + o * 0.074), (0, 0, o), 0.006, 0.006, IRON)
    for k in range(16):
        a = 2 * math.pi * (k + 0.5) / 16
        hexbolt(det, (U + 0.112 * math.cos(a), HIP_Y + 0.112 * math.sin(a), pz + o * 0.058), (0, 0, o), 0.005, 0.005, IRON, washer=False)
    for dy in (0.03, -0.03):   # штуцеры на корме привода и шланги вверх к тазу
        q = (U - 0.115, HIP_Y + dy, pz + o * 0.02)
        rcyl(det, add(q, (0.012, 0, 0)), (-1, 0, 0), 0.011, 0.03, BRASS, e=0.002)
        det.tube([add(q, (-0.016, 0, 0)), add(q, (-0.05, 0.02, 0)), add(q, (-0.06, 0.09, -o * 0.02)), (U - 0.12, HIP_Y + 0.14 + dy, o * 0.05)],
                 0.009, HOSE, seg=12, fillet=0.03)
    return b, body, det


def thigh_parts(leg, L):
    """Бедро: ступица качания, коробчатая балка с рёбрами и щитком-корытом, вилка колена."""
    th = L["th"]
    b = Body()
    _hub(b, L["P"], 0.08, 0.108, IRON)
    f = Frame(th.o, ((th.m[0][0], th.m[0][1], 0.0), (th.m[1][0], th.m[1][1], 0.0), (0.0, 0.0, 1.0)))
    rloft(b, f, [(0.03, 0.046, 0.068, 0.018, 0.0), (0.16, 0.045, 0.064, 0.017, 0.004), (0.30, 0.04, 0.054, 0.015, 0.004),
                 (0.37, 0.034, 0.048, 0.014, 0.0)], IRON, n=3)
    for sz in (-1.0, 1.0):   # вилка колена
        rloft(b, Frame(f.at(0.33, 0, sz * 0.05), f.m), [(0.0, 0.012, 0.045, 0.008, 0.0), (0.09, 0.012, 0.06, 0.010, 0.0)], IRON, end=0.006)
    pin(b, L["K"], (0, 0, 1), 0.02, 0.07, STEEL)
    # щиток в краске по передней грани (спинка бедра смотрит вперёд-вверх) с отгибами
    sh = Body()
    for sz in (-1.0, 1.0):
        rloft(sh, Frame(f.at(0.06, 0.052, sz * 0.036), f.m), [(0.0, 0.006, 0.020, 0.004, 0.0), (0.26, 0.006, 0.018, 0.004, -0.004)],
              PAINT, end=0.005)
    rloft(sh, Frame(f.at(0.05, 0.072, 0.0), f.m), [(0.0, 0.05, 0.007, 0.005, 0.0), (0.14, 0.052, 0.007, 0.005, 0.0),
                                                  (0.28, 0.044, 0.007, 0.005, -0.006)], PAINT, end=0.006)
    # щиток снаружи бедра: большой лист на стойках
    o = leg.out
    rloft(sh, Frame(f.at(0.05, 0.014, o * 0.062), f.m), [(0.0, 0.006, 0.056, 0.012, 0.0), (0.15, 0.006, 0.066, 0.014, 0.004),
                                                        (0.29, 0.006, 0.052, 0.012, 0.0)], PAINT, n=3, end=0.008)
    det = Body()
    for x in (0.09, 0.17, 0.25):
        hexbolt(det, f.at(x, 0.079, 0.03), f.dir(0, 1, 0), 0.006, 0.006, IRON)
        hexbolt(det, f.at(x, 0.079, -0.03), f.dir(0, 1, 0), 0.006, 0.006, IRON)
        hexbolt(det, f.at(x + 0.02, 0.03, o * 0.069), (0, 0, o), 0.0055, 0.005, IRON)
        hexbolt(det, f.at(x + 0.02, -0.03, o * 0.069), (0, 0, o), 0.0055, 0.005, IRON)
    for x in (0.12, 0.22):   # рёбра по внутреннему боку балки
        for sz in (1.0,):
            rbox(det, f.at(x, 0.0, -o * 0.047), (0.012, 0.09, 0.008), 0.003, IRON, rot=f.m)
    # хвост бедра за осью: проушины цилиндров колена
    for sz in (-1.0, 1.0):
        rloft(det, Frame.look(f.at(0.0, -0.03, sz * KNEE_SIDE), sub(L["ka"], f.at(0.0, -0.03))),
              [(0.0, 0.010, 0.022, 0.007, 0.0), (0.075, 0.010, 0.018, 0.007, 0.0)], IRON, end=0.006)
    # шланг вдоль бедра снаружи
    det.tube([f.at(0.02, 0.03, o * 0.05), f.at(0.12, 0.05, o * 0.047), f.at(0.3, 0.035, o * 0.04), f.at(0.38, -0.01, o * 0.04)],
             0.007, HOSE, seg=10, fillet=0.03)
    for x in (0.12, 0.3):
        rcyl(det, f.at(x, 0.04, o * 0.034), f.dir(1, 0, 0), 0.011, 0.012, IRON, e=0.003)
    return b, sh, det


def shin_parts(leg, L):
    """Голень: вилка колена, рог вперёд-вверх под шток колена, балка с тягой, щиток спереди, вилка голеностопа."""
    sh = L["sh"]
    f = Frame(sh.o, ((sh.m[0][0], sh.m[0][1], 0.0), (sh.m[1][0], sh.m[1][1], 0.0), (0.0, 0.0, 1.0)))
    b = Body()
    _hub(b, L["K"], 0.06, 0.07, IRON)
    rloft(b, f, [(0.05, 0.04, 0.056, 0.016, 0.0), (0.2, 0.038, 0.05, 0.015, 0.0), (0.42, 0.032, 0.042, 0.013, 0.0),
                 (0.49, 0.034, 0.046, 0.013, 0.0)], IRON, n=3)
    _hub(b, L["A"], 0.05, 0.075, IRON)
    pin(b, L["A"], (0, 0, 1), 0.017, 0.07, STEEL)
    det = Body()
    # проушины цилиндров колена (рог под коленом) и голеностопа (у колена, к спинке)
    for sz in (-1.0, 1.0):
        rloft(det, Frame.look(f.at(0.06, -0.02, sz * KNEE_SIDE), sub(L["kb"], f.at(0.06, -0.02))),
              [(0.0, 0.009, 0.02, 0.006, 0.0), (0.05, 0.009, 0.016, 0.006, 0.0)], IRON, end=0.005)
        rloft(det, Frame.look(f.at(0.08, -0.03, sz * ANK_SIDE), sub(L["ma"], f.at(0.08, -0.03))),
              [(0.0, 0.009, 0.02, 0.006, 0.0), (0.05, 0.009, 0.016, 0.006, 0.0)], IRON, end=0.005)
    # тяга вдоль передней грани голени на двух проушинах
    for x in (0.1, 0.42):
        rbox(det, f.at(x, 0.05, 0.0), (0.03, 0.028, 0.03), 0.008, IRON, rot=f.m)
    rcyl(det, f.at(0.1, 0.062, 0.0), f.dir(1, 0, 0), 0.009, 0.32, STEEL, e=0.002)
    # щиток в краске спереди голени (y+ голени смотрит вперёд-вниз) с ребром жёсткости
    plate = Body()
    rloft(plate, Frame(f.at(0.06, 0.084, 0.0), f.m), [(0.0, 0.056, 0.008, 0.006, 0.0), (0.18, 0.06, 0.008, 0.006, 0.006),
                                                     (0.34, 0.05, 0.008, 0.006, 0.0)], PAINT, n=3, end=0.007)
    o = leg.out
    rloft(plate, Frame(f.at(0.1, 0.02, o * 0.05), f.m), [(0.0, 0.006, 0.05, 0.01, 0.0), (0.16, 0.006, 0.058, 0.012, 0.0),
                                                        (0.3, 0.006, 0.044, 0.01, 0.0)], PAINT, n=3, end=0.007)
    rloft(plate, Frame(f.at(0.08, 0.092, 0.0), f.m), [(0.0, 0.008, 0.006, 0.004, 0.0), (0.28, 0.008, 0.006, 0.004, 0.0)], PAINT, end=0.004)
    for x in (0.09, 0.32):
        for sz in (-1.0, 1.0):
            hexbolt(det, f.at(x, 0.09, sz * 0.036), f.dir(0, 1, 0), 0.0055, 0.005, IRON)
    det.tube([f.at(0.0, -0.02, o * 0.085), f.at(0.12, -0.05, o * 0.05), f.at(0.36, -0.045, o * 0.042), f.at(0.5, -0.02, o * 0.05)],
             0.0065, HOSE, seg=10, fillet=0.04)
    return b, plate, det


def tarsus_parts(leg, L):
    """Плюсна: рог за голеностопом под шток, балка, амортизатор с пружиной, вилка ступни."""
    tr = L["tr"]
    f = Frame(tr.o, ((tr.m[0][0], tr.m[0][1], 0.0), (tr.m[1][0], tr.m[1][1], 0.0), (0.0, 0.0, 1.0)))
    b = Body()
    _hub(b, L["A"], 0.044, 0.05, IRON)
    rloft(b, f, [(-0.075, 0.026, 0.022, 0.009, 0.06), (0.0, 0.032, 0.04, 0.013, 0.0), (0.16, 0.031, 0.038, 0.012, 0.0),
                 (0.28, 0.028, 0.034, 0.011, 0.0)], IRON, n=3, end=0.008)
    for sz in (-1.0, 1.0):   # вилка ступни
        rloft(b, Frame(f.at(0.25, 0, sz * 0.048), f.m), [(0.0, 0.010, 0.032, 0.007, 0.0), (0.09, 0.010, 0.04, 0.008, 0.0)], IRON, end=0.006)
    pin(b, L["F"], (0, 0, 1), 0.016, 0.062, STEEL)
    det = Body()
    for sz in (-1.0, 1.0):   # проушины штоков голеностопа на роге
        rcyl(det, (L["mb"][0], L["mb"][1], sz * ANK_SIDE - 0.012), (0, 0, 1), 0.016, 0.024, IRON, e=0.004)
    rcyl(det, (L["mb"][0], L["mb"][1], -ANK_SIDE), (0, 0, 1), 0.009, 2 * ANK_SIDE, STEEL, e=0.002)
    # амортизатор спереди: стакан, пружина, шток
    a0, a1 = f.at(0.05, 0.05), f.at(0.24, 0.045)
    for p in (a0, a1):
        rbox(det, p, (0.03, 0.03, 0.034), 0.008, IRON, rot=f.m)
    d = unit(sub(a1, a0))
    rcyl(det, add(a0, mul(d, 0.01)), d, 0.019, 0.09, IRON, e=0.004)
    rcyl(det, add(a0, mul(d, 0.09)), d, 0.009, 0.1, STEEL, e=0.002)
    helix(det, add(a0, mul(d, 0.1)), add(a1, mul(d, -0.012)), 0.021, 0.0032, 6.5, STEEL, seg_turn=18, tseg=6)
    rcyl(det, add(a0, mul(d, 0.1)), d, 0.026, 0.006, IRON, e=0.002)
    rcyl(det, add(a1, mul(d, -0.018)), d, 0.026, 0.006, IRON, e=0.002)
    plate = Body()   # щиток плюсны сзади
    rloft(plate, Frame(f.at(0.02, -0.042, 0.0), f.m), [(0.0, 0.034, 0.006, 0.005, 0.0), (0.2, 0.03, 0.006, 0.005, 0.0)], PAINT, end=0.006)
    return b, plate, det


def foot_parts(leg, L):
    """Ступня-платформа: седло на оси, корпус, три пальца вперёд на шарнирах, шпора назад, резина подошвы."""
    F = L["F"]
    b = Body()
    rcyl(b, (F[0], F[1], -0.036), (0, 0, 1), 0.036, 0.072, IRON, e=0.006)          # втулка на оси
    rbox(b, (F[0] - 0.005, 0.048, 0.0), (0.16, 0.05, 0.12), 0.02, IRON)            # седло
    rloft(b, Frame((F[0] - 0.15, 0.03, 0.0)), [(0.0, 0.05, 0.02, 0.014, 0.0), (0.12, 0.07, 0.024, 0.016, 0.004),
                                                (0.26, 0.07, 0.022, 0.016, 0.0), (0.3, 0.06, 0.018, 0.014, -0.004)], IRON, n=3, end=0.012)
    shell = Body()   # кожух сверху в краске
    rloft(shell, Frame((F[0] - 0.12, 0.062, 0.0)), [(0.0, 0.05, 0.012, 0.01, 0.0), (0.1, 0.062, 0.016, 0.012, 0.006),
                                                    (0.2, 0.058, 0.014, 0.012, 0.0)], PAINT, n=3, end=0.01)
    det = Body()
    rub = Body()
    rbox(rub, (F[0] - 0.01, 0.006, 0.0), (0.24, 0.012, 0.13), 0.005, RUBBER)
    # пальцы: средний длиннее, боковые разведены
    for k, (ang, ln) in enumerate(((0.0, 0.17), (0.32, 0.135), (-0.32, 0.135))):
        c, s = math.cos(ang), math.sin(ang)
        root = (F[0] + 0.12, 0.03, s * 0.045)
        fr = Frame(root, ((c, 0, -s), (0, 1, 0), (s, 0, c)))
        rcyl(det, fr.at(0, 0, -0.022), fr.dir(0, 0, 1), 0.016, 0.044, STEEL, e=0.003)       # шарнир
        rloft(det, fr, [(0.0, 0.019, 0.016, 0.01, 0.0), (ln * 0.6, 0.021, 0.015, 0.01, -0.004), (ln, 0.015, 0.009, 0.006, -0.012)],
              IRON, n=3, end=0.008)
        rloft(shell, fr.sub(0.02, 0.016, 0.0), [(0.0, 0.017, 0.004, 0.003, 0.0), (ln * 0.55, 0.018, 0.004, 0.003, -0.004)], PAINT, end=0.004)
        rbox(rub, fr.at(ln * 0.55, -0.022, 0.0), (ln * 0.75, 0.012, 0.036), 0.005, RUBBER, rot=fr.m)
        hexbolt(det, fr.at(ln * 0.3, 0.021, 0.0), (0, 1, 0), 0.005, 0.005, IRON)
    # шпора назад
    fr = Frame((F[0] - 0.14, 0.035, 0.0))
    rloft(det, Frame.look(fr.o, (-1.0, -0.25, 0.0)), [(0.0, 0.03, 0.018, 0.01, 0.0), (0.08, 0.022, 0.012, 0.008, 0.0)], IRON, end=0.008)
    rbox(rub, (F[0] - 0.21, 0.008, 0.0), (0.05, 0.014, 0.05), 0.006, RUBBER)
    # тяги от седла к пальцам и болты кожуха
    for sz in (-1.0, 1.0):
        det.tube([(F[0] + 0.03, 0.07, sz * 0.05), (F[0] + 0.09, 0.055, sz * 0.05), (F[0] + 0.12, 0.04, sz * 0.045)], 0.006, STEEL, seg=10, fillet=0.02)
        for x in (-0.08, 0.02):
            hexbolt(det, (F[0] + x, 0.078, sz * 0.045), (0, 1, 0), 0.005, 0.005, IRON)
    return b, shell, rub, det


def leg_parts(leg):
    """Тела ноги по костям: [(кость, тело, износ)]; цилиндры — по два на сустав по бокам."""
    L = leg.points()
    (kc, kr), (ac, ar) = piston_lengths(leg)
    hb, hdrive, hdet = hip_parts(leg)
    tb, tplate, tdet = thigh_parts(leg, L)
    sb, splate, sdet = shin_parts(leg, L)
    rb, rplate, rdet = tarsus_parts(leg, L)
    fb, fshell, frub, fdet = foot_parts(leg, L)
    out = [("hip", hb, None), ("hip", hdrive, WEAR_LEG), ("hip", hdet, None),
           ("thigh", tb, None), ("thigh", tplate, WEAR_LEG), ("thigh", tdet, None),
           ("shin", sb, None), ("shin", splate, WEAR_LEG), ("shin", sdet, None),
           ("tarsus", rb, None), ("tarsus", rplate, WEAR_LEG), ("tarsus", rdet, None),
           ("foot", fb, None), ("foot", fshell, WEAR_LEG), ("foot", frub, None), ("foot", fdet, None)]
    for a, bb, cyl, rod, side, ca, cb in ((L["ka"], L["kb"], kc, kr, KNEE_SIDE, "ka", "kb"), (L["ma"], L["mb"], ac, ar, ANK_SIDE, "ma", "mb")):
        for sz in (-1.0, 1.0):
            g, s, _ = piston((a[0], a[1], sz * side), (bb[0], bb[1], sz * side), cyl, rod)
            out += [(ca, g, None), (cb, s, None)]
    return out
