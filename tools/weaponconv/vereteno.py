# ВЕРЕТЕНО по концепту: двусторонняя глефа, длинный гранёный клинок и шип на другом конце
# blender -b -P vereteno.py -- out.png [сэмплы]
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wlib import *

OUT, SAMPLES = args()
S = 2.38 / 837.0          # м на пиксель концепта: от кончика шипа (25) до острия (862)
X0, Z0 = 25.0, 249.0      # кончик шипа и ось в пикселях концепта

def P(px, py=Z0):
    return ((px - X0) * S, -(py - Z0) * S)

reset()
M_BLADE = mat_steel('blade', (0.13, 0.13, 0.135), 0.42, 0.45, scale=5, bump=0.015, grain=120)
M_EDGE = mat_steel('edge', (0.22, 0.22, 0.225), 0.32, 0.3, scale=5, bump=0.01, grain=120)
M_SHAFT = mat_steel('shaft', (0.075, 0.075, 0.078), 0.5, 0.65, scale=7, bump=0.02, grain=150)

def diamond(top, ridge, bot, T, e=0.0006, band=0.0):
    # сечение-ромб: кромки сверху и снизу, ребро посередине; band — доля ширины под светлой заточкой
    zt, zr, zb = P(0, top)[1], P(0, ridge)[1], P(0, bot)[1]
    pts = [(e, zt, 1 if band else 0)]
    if band:
        pts.append((T * band * 0.9, lerp(zt, zr, band), 0))
    pts.append((T, zr, 1 if band else 0))
    if band:
        pts.append((T * band * 0.9, lerp(zb, zr, band), 1))
    pts.append((e, zb, 0))
    back = [(-y, z, 0) for y, z, _ in reversed(pts)]
    # материал грани после точки: заточка у кромок с обеих сторон
    out = pts + back
    if band:
        n = len(out)
        mats = [0] * n
        # грани кромка->полоса (сверху) и полоса->кромка (снизу) светлые, и торцы кромок
        for i in range(n):
            a, b = out[i], out[(i + 1) % n]
            ea = abs(a[0]) <= e * 1.01; eb = abs(b[0]) <= e * 1.01
            mats[i] = 1 if (ea or eb) else 0
        out = [(y, z, m) for (y, z, _), m in zip(out, mats)]
    return [(y, z, m) for y, z, m in out]

# ---------------------------------------------------------------- клинок: 400..862, ширина 35 px, ребро чуть ниже середины
TOP, RID, BOT = 231.0, 250.5, 266.0
def blade_sec(x_px):
    t = max(0.0, (x_px - 805.0) / 57.0)
    k = 1.0 - t ** 1.7 if t < 1 else 0.0          # остриё стрельчатое: края сходятся выпуклой дугой
    tip_z = 249.0
    top = lerp(tip_z, TOP, k); bot = lerp(tip_z, BOT, k); rid = lerp(tip_z, RID, k)
    T = 0.011 * (0.35 + 0.65 * k)
    return diamond(top, rid, bot, max(T, 0.0008), band=0.16)

xs = [400.0 + i * 15.0 for i in range(27)] + [805 + 57 * (i / 16) ** 0.8 for i in range(1, 17)]
xs = sorted(set(round(x, 3) for x in xs if x <= 862))
xs[-1] = 861.6
bl = loft('blade', [P(x)[0] for x in xs], lambda X: blade_sec(X / S + X0), [M_BLADE, M_EDGE])
bevel(bl, 0.0008)

# ---------------------------------------------------------------- втулка: от древка расширяется к пяте клинка, восемь граней
sock = [(360, 7.3), (372, 7.5), (382, 8.0), (390, 8.9), (396, 10.0), (400.5, 10.8)]
def sock_sec(x_px):
    r, = interp(sock, x_px)
    return ring(r * S, 8, 0, phase=math.pi / 8)
xs = [360 + i * 2.7 for i in range(15)] + [400.5]
so = loft('socket', [P(x)[0] for x in xs], lambda X: sock_sec(X / S + X0), [M_SHAFT])
bevel(so, 0.0006)

# ---------------------------------------------------------------- древко: тёмный круглый прут
# восемь граней, как на концепте: сверху светлая полоса
def shaft_sec(x_px):
    return ring(7.3 * S, 8, 0, phase=math.pi / 8)
sh = loft('shaft', [P(x)[0] for x in (194, 260, 320, 361)], lambda X: shaft_sec(X / S + X0), [M_SHAFT])
bevel(sh, 0.0008)

# ---------------------------------------------------------------- муфта между шипом и древком: проволока плотными витками
def collar_sec(x_px):
    u = (x_px - 182.0) / 14.0
    # узел: плотный бугор проволоки, по краям сходит к древку, поверх мелкие витки
    # узел: короткая гранёная муфта, плоская середина, края скошены
    r = 7.6 + 3.4 * min(1.0, smooth(u / 0.3), smooth((1 - u) / 0.3))
    return ring(r * S, 8, 0, phase=math.pi / 8)
xs = [182 + 14 * i / 64 for i in range(65)]
co = loft('collar', [P(x)[0] for x in xs], lambda X: collar_sec(X / S + X0), [M_SHAFT])
bevel(co, 0.0008)

# ---------------------------------------------------------------- шип: гранёный, от муфты к кончику, острый
def spike_sec(x_px):
    t = (x_px - 25.0) / 42.0
    k = 1.0 - (1.0 - min(t, 1.0)) ** 1.4       # от кончика быстро набирает ширину
    h = 8.0 * k * (1.0 - 0.06 * max(0.0, (x_px - 60) / 122))
    T = 0.0085 * max(k, 0.1)
    return diamond(249 - h, 249.0, 249 + h, T, band=0.0)
xs = [25.4] + [25 + 42 * (i / 14) ** 1.3 for i in range(1, 15)] + [67 + i * 11.5 for i in range(1, 11)]
sp = loft('spike', [P(x)[0] for x in xs], lambda X: spike_sec(X / S + X0), [M_BLADE])
bevel(sp, 0.0006)

export_weapon('vereteno', list(bpy.context.scene.objects), (0.78, 0.0))
stage(P(443)[0], 0.0, 886 * S)
render(OUT, SAMPLES)
