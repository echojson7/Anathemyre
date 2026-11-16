"""Древковое (ЖЕРДЬ): КОСА, ТРУБА.

Формы — со склада беты (tools/weaponconv/polearms.py ветки stage0/test-room), переписаны на замкнутые тела
без Blender. Торец древка у x = 0, остриё к +X. Начало сетки — верхняя рука (у беты кулак был у нижней, на
0.25–0.3 м от торца; вторая рука на 0.62 м выше): нижняя кисть — grip2 = −0.62 в data/weapons.txt.
"""
from wkit import Weapon, edge_sec, lerp, math, ring, smooth


# ================================================================ КОСА 2.84: недораспрямлённое полотно косы на трубе
def kosa():
    w = Weapon()
    L = 2.84
    B0 = 2.12                   # пятка полотна
    w.rod(0.0, B0 + 0.06, 0.0145, "pipe", n=16)
    w.wrap(0.25, 0.55, 0.0158, 0.0158, "rag", step=0.03, bulge=0.0022)
    w.wrap(0.85, 1.1, 0.0158, 0.0158, "rag", step=0.03, bulge=0.0022)
    w.loft([-0.012, -0.010, 0.0], lambda x: ring(0.015 if x < -0.011 else 0.017, 16, 0), ["dark"], shade="smooth")
    # муфта: пятка косы загнута и примотана проволокой
    w.wrap(B0 - 0.02, B0 + 0.09, 0.0175, 0.0185, "wire", step=0.005, bulge=0.0009, per=3)

    def mid(x):
        u = (x - B0) / (L - B0)
        return 0.02 + 0.055 * u ** 2.2              # полотно выгнуто вперёд-вверх

    def hgt(x):
        return lerp(0.062, 0.03, (x - B0) / (L - B0))

    def sec(x):
        h = hgt(x)
        t = (x - (L - 0.16)) / 0.16
        top = mid(x) + h / 2
        bot = mid(x) - h / 2 + (h * 0.95 * t ** 1.6 if t > 0 else 0)
        if top - bot < 0.0015:
            bot = top - 0.0015
        return edge_sec(top, bot, 0.0018, min(0.016, (top - bot) * 0.6))
    xs = [B0 + 0.03 + (L - B0 - 0.03) * i / 90 for i in range(91)]
    xs[-1] = L - 0.0008
    w.loft(xs, sec, ["steel", "edge"])
    # ребро жёсткости косы по обуху (с одной стороны полотна, как у настоящей косы)
    w.tube([(x, -0.0022, mid(x) + hgt(x) / 2 - 0.004) for x in xs[:-6:3]], 0.0028, "steel", seg=8)

    # короткий шип на обухе
    def spur(x):
        u = (x - B0 - 0.1) / 0.05
        hh = 0.03 * (1 - abs(u * 2 - 1)) if 0 <= u <= 1 else 0.0
        z0 = mid(x) + 0.028
        return [(-0.003, z0 + hh * (1 - u) + 0.0005, 0), (0.003, z0 + hh * (1 - u) + 0.0005, 0), (0.003, z0 - 0.004, 0),
                (-0.003, z0 - 0.004, 0)]
    w.loft([B0 + 0.1 + 0.05 * i / 10 for i in range(11)], spur, ["steel"])
    return w, (0.92, 0.0)


# ================================================================ ТРУБА 2.2: водопроводная труба одним куском, конец расплющен в жало
def truba():
    w = Weapon()
    L = 2.2
    R = 0.0167
    F0 = L - 0.36               # отсюда труба расплющена
    w.rod(0.03, F0 + 0.001, R, "pipe", n=18)

    # расплющенный конец и жало; шов трубы раскрылся трещиной
    def flat(x):
        u = min(1.0, (x - F0) / 0.12)
        ry = lerp(R, 0.0035, smooth(u))
        rz = lerp(R, 0.026, smooth(u))
        t = (x - (L - 0.2)) / 0.2
        if t > 0:
            rz *= (1 - t ** 1.2)
            ry *= (1 - 0.7 * t)
        return ring(ry, 18, 0, max(rz, 0.0006))
    fx = [F0 + (L - F0) * (i / 60) for i in range(61)]
    fx[-1] = L - 0.0008
    w.loft(fx, flat, ["pipe"], shade="smooth")
    w.tube([(x, -0.0040, 0.006 + 0.0015 * math.sin(x * 90)) for x in [F0 + 0.08 + 0.17 * i / 20 for i in range(21)]],
           0.0011, "hole", seg=6)
    # резьбовая заглушка на торце: восемь граней, нитки резьбы ступенями
    prof = []
    for k in range(10):
        x = 0.035 * k / 10
        r = 0.0205 + (0.001 if k % 2 else 0.0)
        prof += [(x, r), (x + 0.0035, r)]
    w.profile(prof, "iron", n=8, phase=math.pi / 8, shade="flat")
    w.wrap(0.3, 0.56, 0.0178, 0.0178, "rag", step=0.03, bulge=0.0022)
    w.wrap(0.85, 1.1, 0.0178, 0.0178, "rag", step=0.03, bulge=0.0022)
    return w, (0.92, 0.0)


ITEMS = {"kosa": kosa, "truba": truba}
