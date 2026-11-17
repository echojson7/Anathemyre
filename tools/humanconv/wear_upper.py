"""Верх одежды (docs/wear.md): запашная куртка, длинная куртка с разрезными полами, жилет-разгрузка, оби, ремень
с подсумками, обмотки рук, капюшон с шарфом, наплечник из лома, перчатки без пальцев."""
import numpy as np

import wear_core as W
from wear import LAYER, TAU, F, helix, is_arm, is_leg, mats, pants_tube
from wear_core import smoothstep as ss

Y_CROSS = 1.30  # запах: левая пола поверх правой, край — линия x = 0.3 · (Y_CROSS − y)
LAP = 0.30


def lap_x(y):
    return LAP * (Y_CROSS - y)


def sleeve(v, b, tube_end, cuff_band, layer_al0=0.1):
    # Рукав: труба вокруг руки, складки на сгибе локтя
    al, th = v["al"], v["th"]
    armv = is_arm(v)
    tube = np.interp(al, [0.0, layer_al0, b.elbow, b.wrist], [0.0, 0.058, 0.052, tube_end])
    h = armv * ss(0.0, layer_al0, al) * np.maximum(0, tube - v["rad"])
    el = armv * np.exp(-((al - b.elbow) / 0.06) ** 2) * ss(0.2, 1.4, -th)
    h += 0.006 * el * np.maximum(0, np.sin(TAU * al / 0.035 + 0.8 * np.cos(th)))
    return h


# ---------------------------------------------------------------- запашная куртка
def jacket(b):
    f = F(b)
    M = ["wear_indigo", "wear_band", "wear_quilt", "wear_dust", "wear_lining"]
    m = mats(M)
    g = W.Garment("wear_jacket", M, margin=0.05, carry=(0.016, 0.016))
    w = b.wrist
    cuff = w - 0.028
    hemy = b.y_pelvis - 0.075
    ytop = b.y_neck + 0.045
    lj = LAYER["jacket"]

    def front(v):
        return (v["z"] > 0.0) & ~is_arm(v)

    def top_panel(v):
        return front(v) & (v["x"] < lap_x(v["y"])) & (v["y"] < Y_CROSS + 0.3)

    def collar(v):
        # Ворот-полоса вдоль края левой полы, у правой — выше перекрёста, и стойка вокруг шеи сзади
        dl = (lap_x(v["y"]) - v["x"]) * 0.958
        dr = (v["x"] + lap_x(v["y"])) * 0.958
        c1 = front(v) & (dl > 0) & (dl < 0.04)
        c2 = front(v) & (dr > 0) & (dr < 0.04) & (v["y"] > Y_CROSS - 0.05)
        c3 = ~is_arm(v) & (v["y"] > b.y_neck - 0.045)
        return c1 | c2 | c3

    def yoke(v):
        return ((v["y"] > b.y_chest + 0.17) | (is_arm(v) & (v["al"] < 0.13))) & ~collar(v)

    def corner(v, t):
        h = np.full(len(v["al"]), lj) + sleeve(v, b, 0.044, cuff) + pants_tube(b, v, 0.004)
        tor = ~is_arm(v)
        # Стойка ворота отходит от шеи, полы чуть свободнее на животе и спине
        h += tor * 0.012 * ss(b.y_neck - 0.06, b.y_neck + 0.03, v["y"])
        h += tor * 0.004 * ss(b.y_chest, b.y_pelvis, v["y"])
        # Складки на спине у пояса и подмышками
        h += tor * 0.003 * ss(b.y_pelvis + 0.12, b.y_pelvis, v["y"]) * np.maximum(0, np.sin(9 * v["th"]))
        # Запах: левая пола на толщину ткани выше, к бокам — плавно
        h += 0.004 * top_panel(t) * ss(-0.02, 0.06, v["z"])
        h += 0.004 * collar(t) + 0.0025 * yoke(t)
        h += 0.003 * (is_arm(t) & (t["al"] > cuff - 0.05))
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_indigo"])
        r[yoke(t)] = m["wear_quilt"]
        r[collar(t) | (is_arm(t) & (t["al"] > cuff - 0.05))] = m["wear_band"]
        return r

    def dirt(v, t):
        d = 0.35 * ss(hemy + 0.2, hemy, v["y"]) + 0.45 * is_arm(v) * ss(w - 0.3, cuff, v["al"])
        d += 0.3 * is_arm(v) * np.exp(-((v["al"] - b.elbow) / 0.05) ** 2)
        return m["wear_dust"], np.clip(d, 0, 0.8)

    def ao(v, t):
        return 1.0 - 0.25 * is_arm(v) * np.exp(-((v["al"] - b.elbow) / 0.06) ** 2) * ss(0.2, 1.4, -v["th"])

    # Вырез запаха над перекрёстом спереди
    vcut = np.maximum(np.maximum(Y_CROSS - b.y, np.abs(b.x) + lap_x(b.y)), 0.02 - b.z)
    lap = np.where(f.tor & (b.z > -0.02), lap_x(b.y) - b.x, -1.0)
    keep = [f.above(hemy), ytop - b.y, f.arm_to(cuff, leg=1.0), vcut]
    cuts = [lap, np.where(f.tor & (b.z > -0.02), lap_x(b.y) - b.x - 0.04 / 0.958, -1.0),
            np.where(f.tor & (b.z > -0.02), b.x + lap_x(b.y) - 0.04 / 0.958, -1.0), b.y - (b.y_neck - 0.045),
            b.y - (b.y_chest + 0.17), f.arm_from(0.13), f.arm_from(cuff - 0.05)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.004, ao=ao, loose=True, smooth_w=2))
    return g


# ---------------------------------------------------------------- длинная куртка с разрезными полами
def coat(b):
    f = F(b)
    M = ["wear_coat", "wear_leather", "wear_dust", "wear_lining", "wear_strap", "wear_steel"]
    m = mats(M)
    g = W.Garment("wear_coat", M, margin=0.08, carry=(0.03, 0.022))
    w, k = b.wrist, b.knee
    cuff = w - 0.035
    hem = k + 0.20
    ytop = b.y_neck + 0.07
    lc = LAYER["coat"]
    gap = 0.035  # полы спереди не сходятся

    def collar(v):
        return ~is_arm(v) & ~is_leg(v) & (v["y"] > b.y_neck - 0.05)

    def corner(v, t):
        al, th = v["al"], v["th"]
        h = np.full(len(al), lc) + sleeve(v, b, 0.05, cuff)
        tor = ~is_arm(v) & ~is_leg(v)
        legv = is_leg(v)
        h += tor * 0.018 * ss(b.y_neck - 0.07, b.y_neck + 0.05, v["y"])
        h += tor * 0.006 * ss(b.y_chest, b.y_pelvis - 0.05, v["y"])
        # Полы висят: снаружи и сзади колоколом от бедра, изнутри — уже
        tube = np.interp(al, [0.0, 0.15, k, hem], [0.0, 0.11, 0.13, 0.145])
        inner = 1 - 0.7 * ss(-0.4, -1.3, th)
        h += legv * ss(-0.02, 0.15, al) * np.maximum(0, tube - v["rad"]) * inner
        h += legv * 0.008 * np.maximum(0, np.sin(4 * th + 1.0)) * ss(0.1, hem, al)
        h += 0.005 * collar(t)
        h += 0.003 * (is_arm(t) & (t["al"] > cuff - 0.06))
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_coat"])
        r[collar(t) | (is_arm(t) & (t["al"] > cuff - 0.06))] = m["wear_leather"]
        return r

    def dirt(v, t):
        d = 0.85 * is_leg(v) * ss(k - 0.15, hem, v["al"]) + 0.4 * is_arm(v) * ss(w - 0.3, cuff, v["al"])
        return m["wear_dust"], np.clip(d + 0.1, 0, 0.9)

    th = b.th
    # Полы на ногах: перед (θ от −0.9 до 1.45) и зад (θ > 1.75 или < −2.25), разрез сбоку и изнутри
    front = np.where(f.leg, np.minimum(th + 0.9, 1.45 - th), -1.0)
    back = np.where(f.leg, np.maximum(th - 1.75, -2.25 - th), -1.0)
    flap = np.where(f.leg, np.maximum(front, back), 1.0)
    side = np.where(f.leg, np.maximum(0.30 - b.along, flap), 1.0)
    keep = [ytop - b.y, f.arm_to(cuff, leg=1.0), f.leg_to(hem, arm=1.0), side,
            np.where(f.tor, np.maximum(np.abs(b.x) - gap, -b.z), 1.0),          # спереди разрыв между полами
            np.where(f.tor, b.y - (b.y_hip - 0.06), 1.0)]                         # туловище — до паха
    cuts = [b.y - (b.y_neck - 0.05), f.arm_from(cuff - 0.06)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.004, loose=True, double=True, smooth_w=8))
    # Застёжка на груди: две пряжки у края пол
    for x in (-gap - 0.01, gap + 0.01):
        j = b.pick(W.TORSO, y=b.y_chest + 0.12, x=x, z=0.2)
        V, T = W.box(0.03, 0.018, 0.006, bevel=0.0015)
        g.attach(b, j, V, T, np.full(len(T), m["wear_steel"]), h=lc + 0.012)
    return g


# ---------------------------------------------------------------- жилет-разгрузка
def vest(b):
    f = F(b)
    M = ["wear_tech", "wear_quilt", "wear_strap", "wear_steel", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_vest", M, margin=0.05, carry=(0.0, 0.03))
    lv = LAYER["vest"]
    hemy = b.y_pelvis + 0.0
    yn = b.y_neck - 0.02

    def arm_hole(x, y):
        # Пройма: туловище без плеча — |x| меньше 0.15 выше груди
        return 0.155 - np.abs(x) + 0.5 * np.maximum(0, b.y_chest + 0.10 - y)

    def corner(v, t):
        h = np.full(len(v["al"]), lv)
        # Стёганые валики поперёк, у пройм тоньше
        h += 0.005 * np.maximum(0, np.sin(TAU * v["y"] / 0.06)) * ss(0.0, 0.03, arm_hole(v["x"], v["y"]))
        h += 0.004 * ss(b.y_chest, b.y_pelvis, v["y"])
        h += 0.003 * (np.abs(t["x"]) < 0.018) * (t["z"] > 0)  # планка застёжки
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_tech"])
        r[(np.abs(t["x"]) < 0.018) & (t["z"] > 0)] = m["wear_strap"]
        return r

    def dirt(v, t):
        return m["wear_dust"], 0.15 + 0.3 * ss(hemy + 0.2, hemy, v["y"])

    neck = yn - 0.05 * np.clip(b.z / 0.08, 0, 1) - b.y
    keep = [f.above(hemy), neck, np.where(f.tor, arm_hole(b.x, b.y), -1.0)]
    cuts = [np.where(f.tor & (b.z > 0), 0.018 - np.abs(b.x), -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, dirt, thick=0.006))
    # Подсумки на груди и боках, пряжки плечевых ремней
    for x, y, wd, ht, dp in ((-0.075, b.y_chest + 0.02, 0.085, 0.10, 0.035), (0.075, b.y_chest + 0.02, 0.085, 0.10, 0.035),
                             (-0.075, b.y_chest - 0.12, 0.075, 0.085, 0.03), (0.075, b.y_chest - 0.12, 0.075, 0.085, 0.03)):
        j = b.pick(W.TORSO, y=y, x=x, z=0.3)
        V, T = W.box(wd, ht, dp, bevel=0.006, taper=0.08)
        g.attach(b, j, V, T, np.full(len(T), m["wear_quilt"]), h=lv + 0.004)
        V, T = W.box(wd + 0.004, ht * 0.35, 0.008, bevel=0.002)
        V = V + np.array([0, ht * 0.33, dp - 0.004])
        g.attach(b, j, V, T, np.full(len(T), m["wear_strap"]), h=lv + 0.004)
    return g


# ---------------------------------------------------------------- оби: широкий пояс в несколько слоёв с узлом
def sash(b):
    f = F(b)
    M = ["wear_sash", "wear_strap", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_sash", M, margin=0.02, carry=(0.022, 0.024))
    y0, y1 = b.y_pelvis - 0.01, b.y_pelvis + 0.13
    ls = LAYER["belt"] - 0.002

    def cord(v):
        return np.abs(v["y"] - (y0 + y1) * 0.5) < 0.006

    def corner(v, t):
        y = v["y"]
        # Три слоя внахлёст: каждый выше лежит поверх нижнего, край наклонён
        s = (y - y0) / (y1 - y0) * 3.0
        sc = (t["y"] - y0) / (y1 - y0) * 3.0
        frac = np.clip(s - np.clip(np.floor(sc), 0, 2), 0, 1)
        h = ls + 0.003 * (1 - frac) + 0.006 * ss(b.y_chest, b.y_pelvis, y)
        h += 0.004 * cord(t)
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_sash"])
        r[cord(t)] = m["wear_strap"]
        return r

    keep = [f.above(y0), f.below(y1), f.only(f.tor)]
    lines = np.sin(np.pi * ((b.y - y0) / (y1 - y0) * 3.0))
    cuts = [np.where(f.tor, lines, -1.0), np.where(f.tor, 0.006 - np.abs(b.y - (y0 + y1) * 0.5), -1.0)]
    g.add(W.Part(b, keep, cuts, corner, mat, lambda v, t: (m["wear_dust"], 0.2), thick=0.004))
    # Узел сзади слева и два свисающих конца
    j = b.pick(W.TORSO, y=(y0 + y1) * 0.5, x=-0.07, z=-0.3)
    V, T = W.box(0.11, 0.075, 0.035, bevel=0.012)
    g.attach(b, j, V, T, np.full(len(T), m["wear_sash"]), h=ls + 0.006)
    for dx, ln in ((-0.03, 0.24), (0.025, 0.19)):
        V, T = W.box(0.055, ln, 0.008, bevel=0.003)
        V = V + np.array([dx, -ln * 0.5 - 0.02, 0.012])
        g.attach(b, j, V, T, np.full(len(T), m["wear_sash"]), h=ls + 0.006)
    return g


# ---------------------------------------------------------------- ремень с подсумками
def belt(b):
    f = F(b)
    M = ["wear_leather", "wear_steel", "wear_strap", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_belt", M, margin=0.012, carry=(0.03, 0.0))
    yw = b.y_pelvis + 0.06
    y0, y1 = yw - 0.045, yw - 0.005
    lb = LAYER["belt"]

    keep = [f.above(y0), f.below(y1), f.only(f.tor)]
    g.add(W.Part(b, keep, [], lambda v, t: lb + 0.004 * ss(b.y_chest, b.y_pelvis, v["y"]), None,
                 lambda v, t: (m["wear_dust"], 0.25), thick=0.005))
    yc = (y0 + y1) * 0.5
    # Пряжка спереди
    j = b.pick(W.TORSO, y=yc, x=0.0, z=0.3)
    V, T = W.box(0.055, 0.05, 0.008, bevel=0.003)
    g.attach(b, j, V, T, np.full(len(T), m["wear_steel"]), h=lb + 0.006)
    # Подсумки справа, сзади и фляжка слева сзади (левое бедро — оружие)
    for x, z, wd, ht, dp in ((0.13, 0.1, 0.07, 0.09, 0.04), (0.10, -0.3, 0.09, 0.08, 0.045),
                             (-0.04, -0.3, 0.08, 0.075, 0.04)):
        j = b.pick(W.TORSO, y=yc - 0.02, x=x, z=z)
        V, T = W.box(wd, ht, dp, bevel=0.007, taper=0.1)
        V = V + np.array([0, -ht * 0.3, 0])
        g.attach(b, j, V, T, np.full(len(T), m["wear_leather"]), h=lb + 0.006)
        V, T = W.box(wd + 0.006, ht * 0.4, 0.01, bevel=0.003)
        V = V + np.array([0, ht * 0.05, dp - 0.004])
        g.attach(b, j, V, T, np.full(len(T), m["wear_strap"]), h=lb + 0.006)
    return g


# ---------------------------------------------------------------- обмотки предплечий
def wraps_arm(b):
    f = F(b)
    M = ["wear_wrap", "wear_strap", "wear_dust"]
    m = mats(M)
    g = W.Garment("wear_wraps_arm", M, margin=0.02)
    e, w = b.elbow, b.wrist
    lo, hi = e + 0.06, w + 0.035
    pitch = 0.045
    la = LAYER["armwrap"]

    def turn(v):
        return np.where(v["limb"] == W.ARM_L, 1.0, -1.0)

    def corner(v, t):
        tube = np.interp(v["al"], [lo, w, hi], [0.046, 0.036, 0.034])
        h = la + np.maximum(0, tube - v["rad"])
        h += 0.003 * (1 - helix(v, t, -pitch, turn(v)))
        return h

    tb = np.where(b.limb == W.ARM_L, 1.0, -1.0)
    keep = [f.arm_from(lo), f.arm_to(hi, -1.0)]
    cuts = [np.where(f.arm, np.sin(np.pi * (b.along / pitch + tb * b.th / TAU)), -1.0)]
    g.add(W.Part(b, keep, cuts, corner, lambda t: m["wear_wrap"],
                 lambda v, t: (m["wear_dust"], 0.25 + 0.4 * ss(lo, hi, v["al"])),
                 thick=0.003, ao=lambda v, t: 0.7 + 0.3 * helix(v, t, -pitch, turn(v)) ** 0.5, loose=True))
    return g


# ---------------------------------------------------------------- капюшон за спиной и шарф
def hood(b):
    f = F(b)
    M = ["wear_wrap", "wear_coat", "wear_dust", "wear_lining"]
    m = mats(M)
    g = W.Garment("wear_hood", M, margin=0.04, carry=(0.0, 0.03))
    yn = b.y_neck
    lh = LAYER["hood"]

    def roll(v):
        return (v["y"] > yn - 0.07) & (v["y"] < yn + 0.06)

    def corner(v, t):
        y = v["y"]
        # Шарф — два толстых витка вокруг шеи; капюшон — складками на спине и плечах
        s = np.clip((y - (yn - 0.07)) / 0.13, 0, 1)
        sc = np.clip((t["y"] - (yn - 0.07)) / 0.13, 0, 1)
        k = np.floor(sc * 2)
        r1 = np.sin(np.pi * np.clip(s * 2 - k, 0, 1))
        h = lh + 0.02 * r1 * roll(t) + 0.012 * roll(t)
        back = ~roll(t) & (v["z"] < 0.02)
        hump = ss(yn - 0.24, yn - 0.10, y) * ss(0.05, -0.08, v["z"])
        h += back * (0.03 * hump + 0.006 * np.maximum(0, np.sin(7 * v["th"])))
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_coat"])
        r[roll(t)] = m["wear_wrap"]
        return r

    # Шарф вокруг шеи, капюшон спадает на спину до лопаток
    keep = [f.only(f.tor), yn + 0.06 - b.y, np.maximum(b.y - (yn - 0.07), np.where(b.z < 0.02, b.y - (yn - 0.26), -1.0)),
            np.maximum(0.17 - np.abs(b.x), b.y - (yn - 0.07))]
    cuts = [b.y - (yn - 0.07), b.y - (yn - 0.005)]
    g.add(W.Part(b, keep, cuts, corner, mat, lambda v, t: (m["wear_dust"], 0.3), thick=0.005, loose=True,
                 double=True))
    return g


# ---------------------------------------------------------------- наплечник из лома на левом плече
def pauldron(b):
    f = F(b)
    M = ["wear_metal", "wear_leather", "wear_strap", "wear_steel"]
    m = mats(M)
    g = W.Garment("wear_pauldron", M, margin=0.05)
    lp = LAYER["pauldron"]
    c = b.J["upperarm_l"]
    R0 = 0.104  # купол вокруг плечевого сустава
    band = 0.042

    def dist(x, y, z):
        return np.sqrt((x - c[0]) ** 2 + (y - c[1]) ** 2 + (z - c[2]) ** 2)

    def corner(v, t):
        # Купол: поверхность выдута до сферы вокруг сустава; три пластины внахлёст сверху вниз, верхняя наружу
        d = dist(v["x"], v["y"], v["z"])
        h = lp + 0.004 + np.maximum(0, R0 - d)
        p = (c[1] + 0.13 - v["y"]) / band
        k = np.clip(np.floor((c[1] + 0.13 - t["y"]) / band), 0, 3)
        frac = np.clip(p - k, 0, 1)
        h += 0.009 * (1 - frac) + 0.004 * (3 - k)
        return h

    def ao(v, t):
        p = (c[1] + 0.13 - v["y"]) / band
        k = np.clip(np.floor((c[1] + 0.13 - t["y"]) / band), 0, 3)
        return 0.75 + 0.25 * np.clip(p - k, 0, 1) ** 0.5

    d = dist(b.x, b.y, b.z)
    side = (f.arm & (b.limb == W.ARM_L)) | (f.tor & (b.x < -0.06))
    zone = np.where(side, np.minimum(0.122 - d, b.y - (c[1] - 0.10)), -1.0)
    cuts = [np.sin(np.pi * (c[1] + 0.13 - b.y) / band)]
    g.add(W.Part(b, [zone], cuts, corner, lambda t: m["wear_metal"], None, thick=0.005, ao=ao, smooth_w=3))
    # Ремень вокруг плеча под пластинами
    g.add(W.Part(b, [np.where(b.limb == W.ARM_L, np.minimum(b.along - 0.13, 0.155 - b.along), -1.0)], [],
                 lambda v, t: lp + np.maximum(0, 0.058 - v["rad"]), lambda t: m["wear_strap"], None, thick=0.004))
    for k in range(3):
        j = b.pick(W.ARM_L, y=c[1] + 0.13 - band * (k + 0.5), z=0.06, x=c[0] - 0.05)
        V, T = W.box(0.012, 0.012, 0.004, bevel=0.002)
        g.attach(b, j, V, T, np.full(len(T), m["wear_steel"]), h=lp + 0.004 + 0.012)
    return g


# ---------------------------------------------------------------- перчатки без пальцев
def gloves(b):
    f = F(b)
    M = ["wear_leather", "wear_strap"]
    m = mats(M)
    g = W.Garment("wear_gloves", M, margin=0.015)
    w = b.wrist
    lo, hi = w - 0.07, w + 0.075
    lg = LAYER["gloves"]

    def cuff(v):
        return is_arm(v) & (v["al"] < w - 0.02)

    def corner(v, t):
        h = lg + 0.004 * cuff(t) + np.maximum(0, 0.036 - v["rad"]) * cuff(v)
        return h

    def mat(t):
        r = np.full(len(t["al"]), m["wear_leather"])
        r[cuff(t)] = m["wear_strap"]
        return r

    keep = [f.arm_from(lo), f.arm_to(hi, -1.0)]
    g.add(W.Part(b, keep, [f.arm_from(w - 0.02)], corner, mat, None, thick=0.003))
    return g


def catalog(b):
    return [jacket(b), coat(b), vest(b), sash(b), belt(b), wraps_arm(b), hood(b), pauldron(b), gloves(b)]
