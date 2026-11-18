"""Вещи (docs/wear.md): рецепты одежды над телом стенда. Каждая вещь — Garment из кусков Part (wear_core):
область тела полями, высота угла (слой, складки, ступени), материал треугольника, пыль и затенение вершины.
Низ (штаны, хакама, ботинки, обмотки) и рубаха — здесь, верх — wear_upper.py.

Высоты слоёв над телом для ткани (м) — общие у всех вещей (LAYER): верхняя вещь лежит над нижней там, где они
встречаются. Свободные вещи (штаны, хакама, полы) берут сильнее сглаженное тело (Part loose) и «трубу» вокруг оси
ноги или руки: ткань висит прямо, а не по икре и колену.
"""
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import humanconv as H  # noqa: E402
import wear_core as W  # noqa: E402
from wear_core import smoothstep as ss  # noqa: E402

LAYER = dict(shirt=0.004, pants=0.006, hakama=0.008, boots=0.010, wraps=0.013, jacket=0.010, coat=0.014,
             vest=0.019, belt=0.024, pauldron=0.026, hood=0.022, gloves=0.004, armwrap=0.012)
# LOD: доли треугольников LOD 0 (meshpack --prune --lods; цепочка кончается, где упрощение встало)
LODS = (0.5, 0.2, 0.07, 0.025)
TAU = 2 * np.pi


def load_body(src):
    base, groups = H.load_obj(os.path.join(src, "base.obj"))
    Z = np.load(os.path.join(src, "targets.npz"), allow_pickle=False)
    rigs, weights = {}, {}
    for s in H.SKELETONS:
        r = json.load(open(os.path.join(src, "rig.%s.json" % s)))
        rigs[s] = r["bones"] if "bones" in r else r
        weights[s] = json.load(open(os.path.join(src, "weights.%s.json" % s)))["weights"]
    return W.Body(base, groups, Z, rigs, weights, H.mh_load(src))


class F:
    """Поля на вершинах тела стенда (> 0 — внутри) для рецептов"""

    def __init__(self, b):
        self.b = b
        L = b.limb
        self.leg = (L == W.LEG_L) | (L == W.LEG_R)
        self.arm = (L == W.ARM_L) | (L == W.ARM_R)
        self.tor = L == W.TORSO

    @staticmethod
    def only(m):
        return np.where(m, 1.0, -1.0)

    def below(self, y):
        return y - self.b.y

    def above(self, y):
        return self.b.y - y

    def leg_to(self, al, torso=1.0, arm=-1.0):
        # Ноги до длины al от бедра; туловище — torso, руки — arm
        return np.where(self.leg, al - self.b.along, np.where(self.arm, arm, torso))

    def leg_from(self, al):
        return np.where(self.leg, self.b.along - al, -1.0)

    def arm_to(self, al, torso=1.0, leg=-1.0):
        return np.where(self.arm, al - self.b.along, np.where(self.leg, leg, torso))

    def arm_from(self, al):
        return np.where(self.arm, self.b.along - al, -1.0)


def is_leg(v):
    return v["limb"] >= W.LEG_L


def is_arm(v):
    return (v["limb"] == W.ARM_L) | (v["limb"] == W.ARM_R)


def pants_tube(b, v, extra=0.0):
    # Штанина прямого кроя: труба вокруг оси ноги, у паха изнутри уже, ниже голени заправлена в ботинок.
    # То же над бёдрами у вещей поверх штанов (куртка, полы): extra — запас сверху
    k, a = b.knee, b.ankle
    al, th = v["al"], v["th"]
    tube = np.interp(al, [0.0, 0.12, k, a - 0.32, a - 0.22, a], [0.0, 0.092, 0.074, 0.068, 0.05, 0.045]) + extra
    inner = 1 - 0.6 * ss(-0.6, -1.4, th) * (1 - ss(0.15, 0.35, al))
    return is_leg(v) * ss(0.02, 0.12, al) * np.maximum(0, tube - v["rad"]) * inner


def mats(names):
    return {n: i for i, n in enumerate(names)}


def helix(v, t, pitch, turn):
    # Обмотка спиралью: доля ширины витка 0..1 снизу вверх; виток — у треугольника (ступень на линии витка)
    ph = -v["al"] / pitch + turn * v["th"] / TAU
    pc = -t["al"] / pitch + turn * t["th"] / TAU
    ph = ph + np.round(pc - ph)  # тот же оборот, что у треугольника (шов угла ±π)
    return np.clip(ph - np.floor(pc), 0.0, 1.0)


# ---------------------------------------------------------------- штаны
def trousers(b):
    f = F(b)
    M = ["wear_canvas", "wear_patch", "wear_band", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_trousers", M, hide=W.mask("shin", "knee", "thigh", "hips"), carry=(0.012, 0.0))
    yw = b.y_pelvis + 0.075   # пояс
    yb = yw - 0.045
    k, a = b.knee, b.ankle
    hem = a - 0.035
    lp = LAYER["pants"]

    def patch(v):
        return (np.cos(v["th"]) > np.cos(0.85)) & (v["al"] > k - 0.06) & (v["al"] < k + 0.10) & is_leg(v)

    def pocket(v):
        return (v["th"] > 0.75) & (v["th"] < 1.95) & (v["al"] > 0.17) & (v["al"] < 0.35) & is_leg(v)

    def corner(v, t):
        al, th = v["al"], v["th"]
        legv = is_leg(v)
        h = np.full(len(al), lp)
        # Прямой крой: штанина — труба, а не по икре и колену; ниже голени — в ботинке
        h += pants_tube(b, v)
        # Сборки над голенищем (штаны заправлены в ботинки): кольца с неровной фазой
        ank = legv * ss(a - 0.36, a - 0.30, al) * (1 - ss(a - 0.26, a - 0.22, al))
        h += 0.007 * ank * np.maximum(0, np.sin(TAU * al / 0.05 + 1.7 * np.sin(2 * th) + 0.6 * np.cos(3 * th)))
        # Складки под коленом сзади и у паха
        kb = legv * np.exp(-((al - k - 0.02) / 0.05) ** 2) * ss(1.6, 2.6, np.abs(th))
        h += 0.005 * kb * np.maximum(0, np.cos(TAU * (al - k) / 0.035))
        cr = legv * np.exp(-((al - 0.06) / 0.05) ** 2) * ss(-0.3, -1.2, th)
        h += 0.004 * cr * np.maximum(0, np.cos(TAU * (al + 0.3 * th) / 0.04))
        # Пояс, заплата, карман с клапаном — ступенями
        h += 0.003 * ((t["y"] > yb) & ~is_leg(t))
        h += 0.0025 * patch(t)
        pk = pocket(t)
        edge = np.minimum(np.minimum(t["al"] - 0.17, 0.35 - t["al"]), np.minimum(t["th"] - 0.75, 1.95 - t["th"]) * 0.08)
        h += pk * (0.006 + 0.008 * np.clip(edge / 0.03, 0, 1))
        h += (pk & (t["al"] < 0.215)) * 0.004
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_canvas"])
        r[(t["y"] > yb) & ~is_leg(t)] = m["wear_band"]
        r[patch(t)] = m["wear_patch"]
        return r

    def dirt(v, t):
        al = v["al"]
        d = is_leg(v) * (0.8 * ss(a - 0.45, hem, al) + 0.5 * np.exp(-((al - k) / 0.07) ** 2) * (np.cos(v["th"]) > 0))
        return m["wear_dust"], np.clip(d + 0.1, 0, 0.9)

    def ao(v, t):
        back = ss(2.0, 2.8, np.abs(v["th"])) * is_leg(v) * np.exp(-((v["al"] - k) / 0.06) ** 2)
        return 1.0 - 0.3 * back - 0.25 * is_leg(v) * ss(a - 0.3, a - 0.05, v["al"])

    keep = [f.below(yw), f.leg_to(hem), f.only(~f.arm)]
    cuts = [np.where(f.leg, -1.0, b.y - yb),
            np.where(f.leg, np.cos(b.th) - np.cos(0.85), -1.0), f.leg_from(k - 0.06), f.leg_to(k + 0.10, -1.0),
            np.where(f.leg, b.th - 0.75, -1.0), np.where(f.leg, 1.95 - b.th, -1.0),
            f.leg_from(0.17), f.leg_to(0.35, -1.0), f.leg_to(0.215, -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.004, ao=ao, loose=True))
    return g


# ---------------------------------------------------------------- хакама: широкие штаны в складку
def hakama(b):
    f = F(b)
    M = ["wear_canvas", "wear_band", "wear_dust", "wear_lining"]
    m = mats(M)
    g = W.Garment("wear_hakama", M, hide=W.mask("knee", "thigh", "hips"), carry=(0.02, 0.0))
    yw = b.y_pelvis + 0.11
    k, a = b.knee, b.ankle
    hem = a - 0.02
    lp = LAYER["hakama"]
    yboard = b.y_pelvis - 0.01  # низ спинки (коси-ита)

    def board(v):
        return (~is_leg(v)) & (np.abs(v["th"]) > 2.2) & (v["y"] > yboard)

    def corner(v, t):
        al, th = v["al"], v["th"]
        legv = is_leg(v)
        h = np.full(len(al), lp)
        # Широкая штанина колоколом; изнутри у паха уже, чтобы штанины не входили друг в друга
        tube = np.interp(al, [0.0, 0.15, k, a], [0.0, 0.095, 0.125, 0.14])
        inner = 1 - 0.55 * ss(-0.5, -1.5, th)
        h += legv * ss(0.0, 0.25, al) * np.maximum(0, tube - v["rad"]) * inner
        # Складки: по пять спереди и сзади, глубже к низу
        pl = np.maximum(0, np.cos(5 * th)) ** 1.5
        h += legv * 0.016 * pl * ss(0.05, 0.6, al)
        h += ~legv * 0.004 * np.maximum(0, np.cos(9 * th)) * ss(yw, b.y_pelvis - 0.05, v["y"])
        # Спинка — жёсткая трапеция над тканью
        h += 0.004 * board(t)
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_canvas"])
        r[board(t)] = m["wear_band"]
        return r

    def dirt(v, t):
        return m["wear_dust"], np.clip(is_leg(v) * 0.9 * ss(a - 0.35, hem, v["al"]) + 0.08, 0, 0.9)

    def ao(v, t):
        pl = np.maximum(0, np.cos(5 * v["th"])) ** 1.5
        return 1.0 - 0.35 * is_leg(v) * (1 - pl) * ss(0.1, 0.6, v["al"])

    keep = [f.below(yw), f.leg_to(hem), f.only(~f.arm)]
    cuts = [np.where(f.tor, np.abs(b.th) - 2.2, -1.0), np.where(f.tor, b.y - yboard, -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.004, ao=ao, loose=True, double=True, smooth_w=6))
    return g


# ---------------------------------------------------------------- нательная рубаха
def shirt(b):
    f = F(b)
    M = ["wear_cotton", "wear_band", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_shirt", M, hide=W.mask("waist", "chest", "uparm", "elbow", "forearm"))
    e, w = b.elbow, b.wrist
    cuff = w - 0.012
    hemy = b.y_pelvis - 0.005
    yn = b.y_neck - 0.012
    ls = LAYER["shirt"]

    def neck_line(x, z):
        # Ворот: спереди ниже (круглый вырез), сзади у основания шеи
        return yn - 0.035 * np.clip(z / 0.09, 0, 1) * np.clip(1 - (x / 0.08) ** 2, 0, 1)

    def corner(v, t):
        al, th = v["al"], v["th"]
        armv = is_arm(v)
        h = np.full(len(al), ls)
        tube = np.interp(al, [0.0, 0.1, e, w], [0.0, 0.050, 0.044, 0.034])
        h += armv * ss(0.0, 0.1, al) * np.maximum(0, tube - v["rad"])
        el = armv * np.exp(-((al - e) / 0.05) ** 2) * ss(0.3, 1.4, -th)
        h += 0.0035 * el * np.maximum(0, np.sin(TAU * al / 0.028))
        cf = armv * ss(cuff - 0.13, cuff - 0.04, al)
        h += 0.003 * cf * np.maximum(0, np.sin(TAU * al / 0.035 + 2.0 * np.sin(th)))
        h += 0.0015 * (is_arm(t) & (t["al"] > cuff - 0.04))
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_cotton"])
        r[is_arm(t) & (t["al"] > cuff - 0.04)] = m["wear_band"]
        return r

    def dirt(v, t):
        d = 0.5 * is_arm(v) * ss(w - 0.25, cuff, v["al"]) + 0.3 * ss(hemy + 0.15, hemy, v["y"])
        return m["wear_dust"], d

    keep = [f.above(hemy), neck_line(b.x, b.z) - b.y, f.arm_to(cuff, leg=1.0)]
    cuts = [f.arm_from(cuff - 0.04)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.003))
    return g


# ---------------------------------------------------------------- ботинки
def boots(b):
    f = F(b)
    M = ["wear_leather", "wear_sole", "wear_strap", "wear_steel", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_boots", M, hide=W.mask("foot", "ankle"))
    a = b.ankle
    top = a - 0.22
    lb = LAYER["boots"]
    y_welt = 0.032

    def strap(v):
        s = (np.abs(v["al"] - (a - 0.05)) < 0.014) | (np.abs(v["al"] - (a - 0.15)) < 0.014)
        return s & is_leg(v) & (v["y"] > y_welt + 0.01)

    def corner(v, t):
        al = v["al"]
        h = np.full(len(al), lb)
        # Голенище — труба чуть шире ноги, у верха раструб
        tube = np.interp(al, [top, a - 0.1, a], [0.062, 0.056, 0.055])
        h += (al < a) * np.maximum(0, tube - v["rad"])
        h += 0.006 * ss(top + 0.03, top, al)
        # Сборки на сгибе голеностопа спереди
        fr = np.exp(-((al - a + 0.02) / 0.03) ** 2) * (np.cos(v["th"]) > 0.3)
        h += 0.003 * fr * np.maximum(0, np.sin(TAU * al / 0.02))
        # Подошва с рантом: ниже y_welt — шире, низ толще
        h += 0.006 * (t["y"] < y_welt) + 0.004 * (v["ny"] < -0.6)
        # Носок — округлый, ремни — ступенью
        h += 0.004 * ss(a + 0.10, a + 0.20, al)
        h += 0.0025 * strap(t)
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_leather"])
        r[strap(t)] = m["wear_strap"]
        r[t["y"] < y_welt] = m["wear_sole"]
        return r

    def dirt(v, t):
        return m["wear_dust"], np.clip(0.6 * ss(0.07, 0.0, v["y"]) + 0.1, 0, 0.8)

    keep = [f.leg_from(top)]
    cuts = [b.y - y_welt, f.leg_from(a - 0.064), f.leg_to(a - 0.036, -1.0), f.leg_from(a - 0.164),
            f.leg_to(a - 0.136, -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.004, loose=True))
    # Пряжки ремней снаружи голенища
    for lb_ in (W.LEG_L, W.LEG_R):
        for al_ in (a - 0.05, a - 0.15):
            j = b.pick(lb_, al=al_, th=1.45)
            V, T = W.box(0.026, 0.022, 0.006, bevel=0.0015)
            g.attach(b, j, V, T, np.full(len(T), m["wear_steel"]), h=lb + 0.012)
    return g


# ---------------------------------------------------------------- обмотки голеней
def wraps_leg(b):
    f = F(b)
    M = ["wear_wrap", "wear_strap", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_wraps_leg", M, hide=W.mask("shin"))
    k, a = b.knee, b.ankle
    lo, hi = a - 0.25, k + 0.05
    pitch = 0.055
    lw = LAYER["wraps"]

    def tie(v):
        return is_leg(v) & (np.abs(v["al"] - (hi + 0.02)) < 0.008)

    def turn(v):
        return np.where(v["limb"] == W.LEG_L, 1.0, -1.0)

    def corner(v, t):
        al = v["al"]
        tube = np.interp(al, [hi, k + 0.2, lo], [0.062, 0.06, 0.054])
        h = lw + np.maximum(0, tube - v["rad"])
        # Витки внахлёст снизу вверх: нижний край витка лежит поверх предыдущего
        h += 0.0035 * (1.0 - helix(v, t, pitch, turn(v)))
        h += 0.002 * tie(t)
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_wrap"])
        r[tie(t)] = m["wear_strap"]
        return r

    def dirt(v, t):
        return m["wear_dust"], np.clip(0.25 + 0.6 * ss(hi, lo, v["al"]), 0, 0.9)

    def ao(v, t):
        return 0.7 + 0.3 * helix(v, t, pitch, turn(v)) ** 0.5

    tb = np.where(b.limb == W.LEG_L, 1.0, -1.0)
    keep = [f.leg_from(hi), f.leg_to(lo, -1.0)]
    cuts = [np.where(f.leg, np.sin(np.pi * (-b.along / pitch + tb * b.th / TAU)), -1.0),
            f.leg_from(hi + 0.012), f.leg_to(hi + 0.028, -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.003, ao=ao, loose=True))
    return g


def catalog(b):
    import wear_upper as U
    return [trousers(b), hakama(b), shirt(b), boots(b), wraps_leg(b)] + U.catalog(b)


def build_all(b, data_dir, meshpack):
    out = []
    for g in catalog(b):
        g.write(b, data_dir, meshpack, LODS)
        out.append(g)
    return out
