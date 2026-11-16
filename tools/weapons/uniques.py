"""Уникальное: ЛОПАСТЬ (ЛИСТ), РЕШЕТО (ГНЁТ), ВЕРЕТЕНО (ЖЕРДЬ).

Формы — со склада беты (tools/weaponconv/lopast.py, resheto.py, vereteno.py ветки stage0/test-room; РЕШЕТО и
ВЕРЕТЕНО там сведены с концептами пользователя), переписаны на замкнутые тела без Blender.
"""
from wkit import Weapon, diamond, lerp, math, ring, saw, smooth, spline
from kit import rnd


# ================================================================ ЛОПАСТЬ: меч из лопатки турбины мёртвой машины
# Перо — крыловой профиль, закручен по длине; полка лопатки стала гардой, хвостовик-ёлочка — рукоятью; у передней
# кромки отверстия охлаждения, у полки тусклые знаки
def lopast():
    w = Weapon()
    L = 0.74          # перо от полки до кончика
    TW = math.radians(20)

    def chord(x):
        return lerp(0.088, 0.068, x / L)

    def top_z(x):
        # передняя кромка (сверху) почти прямая, у кончика косой срез вниз: остриё уходит к задней кромке
        c = chord(x)
        z = c * 0.5
        t = (x - (L - 0.11)) / 0.11
        if t > 0:
            z -= c * 0.86 * t ** 1.25
        return z

    def bot_z(x):
        # задняя кромка — режущая, у кончика чуть поднимается к острию
        c = chord(x)
        z = -c * 0.5
        t = (x - (L - 0.05)) / 0.05
        if t > 0:
            z += c * 0.06 * t * t
        return z

    def thick(x):
        return lerp(0.2, 0.13, x / L)

    def surf(x, s, side):
        # точка профиля: s 0 — передняя кромка (обух), 1 — задняя (лезвие); side +1 — одна сторона, −1 — другая
        zt, zb = top_z(x), bot_z(x)
        c = zt - zb
        cf = chord(x)
        sa = (0.5 * cf - zt) / cf + s * c / cf
        th = thick(x) * cf
        h = 5 * th * (0.2969 * math.sqrt(max(sa, 0)) - 0.126 * sa - 0.3516 * sa ** 2 + 0.2843 * sa ** 3 - 0.1036 * sa ** 4)
        h = max(h, 0.0004)
        camber = 0.07 * cf * math.sin(math.pi * min(sa, 1))
        y = -camber - side * h
        z = zt - s * c
        a = TW * x / L        # закрутка вокруг середины хорды
        yr = y * math.cos(a) - z * math.sin(a)
        zr = y * math.sin(a) + z * math.cos(a)
        # перо слегка изогнуто по длине вниз, к лезвию, как лопатка в колесе
        return yr, zr + 0.026 * (x / L) ** 2

    NS = 14

    def blade_sec(x):
        ss = [(i / NS) ** 1.6 for i in range(NS + 1)]
        front = [(*surf(x, s, 1), 1 if s > 0.84 else 0) for s in ss]
        back = [(*surf(x, s, -1), 1 if s > 0.84 else 0) for s in reversed(ss[1:-1])]
        return front + back
    xs = [0.0] + [L * (i / 50) for i in range(1, 50)] + [L - 0.0015]
    w.loft(xs, blade_sec, ["steel", "edge"], shade="smooth")

    # полка лопатки — гарда: косой параллелограмм
    def plat_sec(x):
        k = (x + 0.02) / 0.02
        hy, z0, z1 = 0.026, -0.072 - 0.006 * k, 0.07 - 0.006 * k
        return [(-hy, z1, 0), (hy, z1, 0), (hy, z0, 0), (-hy, z0, 0)]
    w.loft([-0.02 + 0.02 * i / 4 for i in range(5)], plat_sec, ["dark"], shade="flat")

    # хвостовик-ёлочка: рукоять с зубцами сверху и снизу, к торцу расширяется замком из трёх лопастей
    GL = 0.26

    def root_sec(x):
        d = -0.02 - x
        teeth = 0.0042 * saw(d / 0.022)
        e = d - (GL - 0.06)
        lobe = 0.0 if e < 0 else 0.0035 * (1 + e / 0.02) * saw(e / 0.02)
        hz = 0.0165 + (teeth if e < 0 else 0) + lobe
        hy = 0.0135
        return [(-hy, hz, 0), (hy, hz, 0), (hy, -hz, 0), (-hy, -hz, 0)]
    w.loft([-0.02 - GL * i / 120 for i in range(121)][::-1], root_sec, ["dark"], shade="flat")
    # шнур кольцами поверх ёлочки, между пачками видны зубцы
    for c0 in (0.045, 0.105, 0.165):
        for k in range(9):
            x = -0.02 - c0 - k * 0.0052
            m = w.mark()
            w.torus((x, 0.0, 0.0), "x", 0.0215, 0.0029, "cord", seg=12, rseg=5)
            w.apply(m, lambda p, x=x: (p[0], p[1] * 0.72, p[2]))
    # отверстия охлаждения у передней кромки, с обеих сторон пера
    for i in range(11):
        x = 0.05 + i * 0.052
        if x > L - 0.13:
            break
        for side in (1, -1):
            y, z = surf(x, 0.1, side)
            w.disc_y(x, z, y, 0.0026, 0.0018, "hole", seg=8)
    # знаки: пиксельные глифы 5×5 на одной стороне, стёрты местами
    PX = 0.0028
    n = 0
    for g in range(4):
        gx = 0.06 + g * 0.03
        for i in range(5):
            for j in range(5):
                n += 1
                on = rnd(7, n) < 0.45 or (i == 2 and rnd(8, n) < 0.7)
                if not on or rnd(9, n) < 0.35:
                    continue
                x = gx + i * PX
                y, z = surf(x, 0.42 + j * 0.055, 1)
                w.boxc((x, y - 0.0001, z), (PX * 0.86, 0.0006, PX * 0.86), "glyph", bevel=0.0)
    return w, (-0.075, 0.0)


# ================================================================ РЕШЕТО: тяжёлый клинок по концепту пользователя
# Волнистая кромка с заточкой полосой, сколы на обухе у гарды, остриё поднято к обуху; линии сведены с концептом
# (столбцы концепта промерены по яркости), обух у пяты опущен на 16 px по снимку пользователя
S_RE = 1.5 / 830.0          # м на пиксель концепта
RX0, RY0 = 240.0, 215.0      # пята клинка и линия обуха в пикселях концепта


def P(px, py):
    return ((px - RX0) * S_RE, -(py - RY0) * S_RE)


def resheto():
    w = Weapon()

    def SH(px):
        return 16.0 if px <= 630 else 16.0 * max(0.0, (846 - px) / 216.0)

    # контур клинка: (x, z, точка линии заточки или None)
    pts = [(*P(240, 232), None)]
    for cx, wd, d in ((285, 3, 5), (318, 6, 10), (362, 4.5, 7)):
        pts += [(*P(cx - wd, 232), None), (*P(cx - wd * 0.2, 232 + d), None), (*P(cx + wd, 232), None)]
    top = [(600, 216), (630, 217)] + [(lerp(630, 770, t / 6), lerp(217, 240, t / 6)) for t in range(1, 7)]
    top += spline([(770, 240), (790, 238.5), (810, 234), (830, 228), (846, 221)], 4)[1:]
    pts += [(*P(x, y + SH(x)), None) for x, y in top[:-1]]
    low = spline([(846, 221), (840, 230), (825, 242), (810, 252), (790, 263), (770, 272), (750, 281), (730, 288),
                  (710, 295), (690, 300), (650, 305), (610, 304), (570, 297), (520, 288), (470, 282), (420, 280),
                  (370, 284), (330, 292), (300, 299), (275, 301), (255, 301), (240, 301)], 4)
    for i, (x, y) in enumerate(low):
        if i == 0:
            pts.append((*P(x, y), None))
            continue
        b = 20.0 * min(1.0, (846 - x) / 60.0) ** 0.7     # заточка — полоса вдоль кромки, к острию сужается
        pts.append((*P(x, y), P(x, y - max(b, 0.6))))
    # убрать совпадающие соседние точки (стыки сплайнов)
    clean = []
    for p in pts:
        if not clean or abs(p[0] - clean[-1][0]) > 1e-7 or abs(p[1] - clean[-1][1]) > 1e-7:
            clean.append(p)
    if abs(clean[0][0] - clean[-1][0]) < 1e-7 and abs(clean[0][1] - clean[-1][1]) < 1e-7:
        clean.pop()
    T, TE = 0.0042, 0.0005           # полутолщина плоскости и у кромки
    ring_it, ring_ot, ring_ob, ring_ib = [], [], [], []
    for x, z, g in clean:
        sharp = g is not None and math.hypot(x - g[0], z - g[1]) > 0.002
        q = g if g is not None else (x, z)
        xo = TE if sharp else T
        mat = "edge" if sharp else "forged"
        ring_it.append(((q[0], T, q[1]), mat))
        ring_ot.append(((x, xo, z), mat))
        ring_ob.append(((x, -xo, z), mat))
        ring_ib.append(((q[0], -T, q[1]), "forged"))
    # грань после точки: заточка, если у неё или у следующей точки есть подводка
    def mats(r):
        out = []
        for i, (p, m) in enumerate(r):
            m2 = r[(i + 1) % len(r)][1]
            out.append((p, "edge" if "edge" in (m, m2) else "forged"))
        return out
    ring_it2, ring_ot2, ring_ob2 = mats(ring_it), mats(ring_ot), mats(ring_ob)
    ring_ib2 = [(p, "forged") for p, _ in ring_ib]
    # плоскости — крышки колец (материал первой точки: обух, плоскость стали)
    ring_it2[0] = (ring_it2[0][0], "forged")
    w.rings([ring_it2, ring_ot2, ring_ob2, ring_ib2], shade="flat")
    # гарда: толстый брусок у пяты, выше обуха и ниже кромки
    gx0, gz0 = P(212, 222.5)
    gx1, gz1 = P(240, 310.5)
    w.boxc(((gx0 + gx1) / 2, 0.0, (gz0 + gz1) / 2), (gx1 - gx0, 0.03, gz0 - gz1), "dark", bevel=0.002)
    # рукоять: кожаная, прямая по оси середины гарды; витки шнура поверх; навершие — короткий стакан
    ax, az = P(212, 266.5)
    bx, _ = P(55, 266.5)
    w.rod(bx, ax, 0.0185, "leather", n=16, z=az)
    w.wrap(bx + 0.01, ax - 0.006, 0.0185, 0.0185, "leather", step=0.026, bulge=0.0016, z=az)
    px, pz = P(38, 266.5)
    w.profile([(px - 0.03, 0.027), (px - 0.026, 0.03), (px + 0.026, 0.03), (px + 0.03, 0.027)], "dark", n=12,
              shade="flat", z=pz)
    return w, P(179, 266.5)


# ================================================================ ВЕРЕТЕНО: двусторонняя глефа по концепту
# Длинный гранёный клинок и шип на другом конце; 2.38 м от кончика шипа до острия
S_VE = 2.38 / 837.0
VX0, VZ0 = 25.0, 249.0      # кончик шипа и ось в пикселях концепта


def V(px, py=VZ0):
    return ((px - VX0) * S_VE, -(py - VZ0) * S_VE)


def vereteno():
    w = Weapon()

    def dia(top, ridge, bot, T, band=0.0):
        return diamond(V(0, top)[1], V(0, ridge)[1], V(0, bot)[1], T, band=band)

    # клинок: 400..862 px, ребро чуть ниже середины, остриё стрельчатое
    TOP, RID, BOT = 231.0, 250.5, 266.0

    def blade_sec(xp):
        t = max(0.0, (xp - 805.0) / 57.0)
        k = 1.0 - t ** 1.7 if t < 1 else 0.0
        T = 0.011 * (0.35 + 0.65 * k)
        return dia(lerp(249.0, TOP, k), lerp(249.0, RID, k), lerp(249.0, BOT, k), max(T, 0.0008), band=0.16)
    xs = sorted(set(round(x, 3) for x in [400.0 + i * 15.0 for i in range(27)] + [805 + 57 * (i / 16) ** 0.8 for i in range(1, 17)]
                    if x <= 862))
    xs[-1] = 861.6
    w.loft([V(x)[0] for x in xs], lambda X: blade_sec(X / S_VE + VX0), ["steel", "edge"])

    # втулка: от древка расширяется к пяте клинка, восемь граней
    sock = [(360, 7.3), (372, 7.5), (382, 8.0), (390, 8.9), (396, 10.0), (400.5, 10.8)]

    def interp(xp):
        if xp <= sock[0][0]:
            return sock[0][1]
        for a, b in zip(sock, sock[1:]):
            if a[0] <= xp <= b[0]:
                return lerp(a[1], b[1], (xp - a[0]) / (b[0] - a[0]))
        return sock[-1][1]
    sx = [360 + i * 2.7 for i in range(15)] + [400.5]
    w.loft([V(x)[0] for x in sx], lambda X: ring(interp(X / S_VE + VX0) * S_VE, 8, 0, phase=math.pi / 8), ["iron"])
    # древко: тёмный восьмигранный прут
    w.rod(V(194)[0], V(361)[0], 7.3 * S_VE, "dark", n=8, phase=math.pi / 8, shade="strips")

    # муфта между шипом и древком: короткая гранёная, края скошены
    def collar(xp):
        u = (xp - 182.0) / 14.0
        r = 7.6 + 3.4 * min(1.0, smooth(u / 0.3), smooth((1 - u) / 0.3))
        return ring(r * S_VE, 8, 0, phase=math.pi / 8)
    cx = [182 + 14 * i / 24 for i in range(25)]
    w.loft([V(x)[0] for x in cx], lambda X: collar(X / S_VE + VX0), ["iron"])

    # шип: гранёный, от муфты к кончику
    def spike(xp):
        t = (xp - 25.0) / 42.0
        k = 1.0 - (1.0 - min(t, 1.0)) ** 1.4
        h = 8.0 * k * (1.0 - 0.06 * max(0.0, (xp - 60) / 122))
        T = 0.0085 * max(k, 0.1)
        return dia(249 - h, 249.0, 249 + h, T)
    spx = [25.4] + [25 + 42 * (i / 14) ** 1.3 for i in range(1, 15)] + [67 + i * 11.5 for i in range(1, 11)]
    w.loft([V(x)[0] for x in spx], lambda X: spike(X / S_VE + VX0), ["steel"])
    return w, (0.9, 0.0)


ITEMS = {"lopast": lopast, "resheto": resheto, "vereteno": vereteno}
