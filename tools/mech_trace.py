#!/usr/bin/env python3
"""Разбор следа движения «Вежи» (docs/vezha.md): anathemyre --frames N --mech-trace след.txt

    python tools/mech_trace.py след.txt                                        итог по состояниям и проверки
    python tools/mech_trace.py след.txt --sheet лист.png [первый шаг число азимут подъём]   кадры-палочки (нужен PIL)

Проверки: скольжение стоящих лап, суставы у земли, лапа под осью машины, лапы друг у друга, поворот бедра от своей
диагонали, скачок высоты за кадр, длина ноги. Лист: тело и ноги палочками (зелёная точка — лапа стоит, красная — в
шаге, серая — сложена), внизу — кто в воздухе, высота (синяя) и скорость (красная) по времени.
"""
import math
import sys

STATES = ("лежит", "раскл", "стоит", "идёт", "склад")


def load(path):
    out = []
    for line in open(path):
        v = line.split()
        if len(v) < 11: continue
        f = [float(x) for x in v]
        st = int(f[0]); at = f[1:4]; yaw, h, pitch, roll, hull, gun, eye = f[4:11]
        k = 11
        pts = {}
        for name in ("chassis", "hull", "gun", "eye"):
            pts[name] = f[k:k+3]; k += 3
        legs = []
        for _ in range(4):
            j = [f[k+3*i:k+3*i+3] for i in range(5)]; k += 15
            legs.append((j, int(f[k]))); k += 1
        out.append(dict(st=st, at=at, yaw=yaw, h=h, pitch=pitch, roll=roll, hull=hull, gun=gun, eye=eye, pts=pts, legs=legs))
    return out


def summary(fr):
    """Итог по отрезкам состояний."""
    seg = 0
    for i in range(1, len(fr) + 1):
        if i < len(fr) and fr[i]["st"] == fr[seg]["st"]:
            continue
        rs = fr[seg:i]
        v = [math.hypot(fr[j]["at"][0] - fr[j - 1]["at"][0], fr[j]["at"][2] - fr[j - 1]["at"][2]) * 30 for j in range(max(seg, 1), i)]
        dh = [abs(fr[j]["h"] - fr[j - 1]["h"]) for j in range(max(seg, 1), i)]
        reach = [math.dist(j[1], j[4]) for f in rs for j, flag in f["legs"] if flag == 1]
        print("%5.1f–%5.1f с %s  h %.2f…%.2f  vmax %.2f  скачок h %.3f  нога %.2f…%.2f" % (
            seg / 30, i / 30, STATES[fr[seg]["st"]], min(f["h"] for f in rs), max(f["h"] for f in rs), max(v or [0]),
            max(dh or [0]), min(reach or [0]), max(reach or [0])))
        seg = i


def checks(fr):
    slip, slip_at, low, near, close = 0.0, None, [], [], []
    yaw_max = [0.0] * 4
    for i, f in enumerate(fr):
        ch, yaw = f["pts"]["chassis"], f["yaw"]
        s, c = math.sin(yaw), math.cos(yaw)
        feet = []
        for k, (j, flag) in enumerate(f["legs"]):
            if i and flag == 1 and fr[i - 1]["legs"][k][1] == 1:
                d = math.dist(fr[i - 1]["legs"][k][0][4], j[4])
                if d > slip:
                    slip, slip_at = d, (i, k)
            low += [(i, k, n) for n in range(1, 4) if j[n][1] - f["at"][1] < 0.03]
            if flag == 2:
                continue
            fx, fz, hx, hz = j[4][0] - ch[0], j[4][2] - ch[2], j[0][0] - ch[0], j[0][2] - ch[2]
            if math.hypot(fx, fz) < 0.45:
                near.append((i, k))
            bx, bz, hbx, hbz = c * fx - s * fz, s * fx + c * fz, c * hx - s * hz, s * hx + c * hz
            dy = (math.degrees(math.atan2(-(bz - hbz), bx - hbx) - math.atan2(-hbz, hbx)) + 180) % 360 - 180
            if abs(dy) > abs(yaw_max[k]):
                yaw_max[k] = dy
            feet.append((k, j[4]))
        close += [(i, a[0], b[0]) for n, a in enumerate(feet) for b in feet[n + 1:] if math.dist(a[1], b[1]) < 0.3]
    print("скольжение стоящих лап, худшее: %.4f м" % slip, slip_at or "")
    print("суставы у земли:", len(low), low[:6])
    print("лапа ближе 0.45 м к оси машины:", len(near), near[:6])
    print("лапы ближе 0.3 м друг к другу:", len(close), close[:6])
    print("поворот бедра от диагонали, худший по ногам, град:", [round(y) for y in yaw_max])


def sheet(fr, path, first=0, step=3, count=36, az=35.0, el=18.0):
    from PIL import Image, ImageDraw
    az, el = math.radians(az), math.radians(el)
    W, H, cols = 260, 190, 6
    rows = (count + cols - 1) // cols
    img = Image.new("RGB", (W * cols, H * rows + 150), (250, 250, 250))
    d = ImageDraw.Draw(img)
    ca, sa, ce, se = math.cos(az), math.sin(az), math.cos(el), math.sin(el)
    def proj(p, c, ox, oy, s=70):
        x, y, z = p[0] - c[0], p[1], p[2] - c[2]
        u = x * ca - z * sa
        w = x * sa + z * ca
        v = y * ce - w * se
        return ox + u * s, oy - v * s
    for i in range(count):
        n = first + i * step
        if n >= len(fr): break
        f = fr[n]
        ox, oy = (i % cols) * W + W / 2, (i // cols) * H + H * 0.8
        c = f["at"]
        # земля: сетка 1 м
        for g in range(-2, 3):
            base = (round(c[0]) + g, 0, round(c[2]) - 2); end = (round(c[0]) + g, 0, round(c[2]) + 2)
            d.line([proj(base, c, ox, oy), proj(end, c, ox, oy)], fill=(225, 225, 225))
            base = (round(c[0]) - 2, 0, round(c[2]) + g); end = (round(c[0]) + 2, 0, round(c[2]) + g)
            d.line([proj(base, c, ox, oy), proj(end, c, ox, oy)], fill=(225, 225, 225))
        ch, hu, gu = f["pts"]["chassis"], f["pts"]["hull"], f["pts"]["gun"]
        cols_leg = [(200, 60, 40), (40, 110, 200), (40, 160, 90), (190, 130, 30)]
        order = sorted(range(4), key=lambda k: -(f["legs"][k][0][0][0] * sa + f["legs"][k][0][0][2] * ca))
        for k in order:
            j, flag = f["legs"][k]
            d.line([proj(ch, c, ox, oy), proj(j[0], c, ox, oy)], fill=(90, 90, 90), width=3)
            d.line([proj(p, c, ox, oy) for p in j], fill=cols_leg[k], width=3)
            fx, fy = proj((j[4][0], j[4][1] - 0.08, j[4][2]), c, ox, oy)
            d.ellipse([fx - 4, fy - 3, fx + 4, fy + 3], fill=(0, 150, 0) if flag == 1 else ((220, 0, 0) if flag == 0 else (150, 150, 150)))
        yaw = f["yaw"] + f["hull"]
        tip = (gu[0] + math.sin(yaw) * math.cos(f["gun"]) * 1.1, gu[1] + math.sin(f["gun"]) * 1.1, gu[2] + math.cos(yaw) * math.cos(f["gun"]) * 1.1)
        hx, hy = proj((hu[0], hu[1] + 0.25, hu[2]), c, ox, oy)
        d.ellipse([hx - 30, hy - 14, hx + 30, hy + 14], outline=(180, 110, 20), width=3)
        d.line([proj(gu, c, ox, oy), proj(tip, c, ox, oy)], fill=(30, 30, 30), width=3)
        d.text((ox - W / 2 + 4, (i // cols) * H + 3), "%d %.2fs st%d h%.2f" % (n, n / 30.0, f["st"], f["h"]), fill=(0, 0, 0))
    # диаграмма: лапы в воздухе, высота, скорость
    y0 = H * rows + 10
    n = len(fr)
    sx = (W * cols - 60) / max(n, 1)
    for k in range(4):
        for i, f in enumerate(fr):
            flag = f["legs"][k][1]
            col = (0, 150, 0) if flag == 1 else ((220, 0, 0) if flag == 0 else (170, 170, 170))
            d.rectangle([50 + i * sx, y0 + k * 12, 50 + (i + 1) * sx, y0 + k * 12 + 9], fill=col)
        d.text((5, y0 + k * 12 - 2), "fr fl rl rr".split()[k], fill=(0, 0, 0))
    prev = None
    for i, f in enumerate(fr):
        p = (50 + i * sx, y0 + 140 - f["h"] * 70)
        if prev: d.line([prev, p], fill=(0, 0, 200))
        prev = p
    prev = None
    for i in range(1, n):
        v = math.hypot(fr[i]["at"][0] - fr[i-1]["at"][0], fr[i]["at"][2] - fr[i-1]["at"][2]) * 30
        p = (50 + i * sx, y0 + 140 - v * 20)
        if prev: d.line([prev, p], fill=(200, 0, 0))
        prev = p
    for i, f in enumerate(fr):
        if i and fr[i-1]["st"] != f["st"]:
            d.line([50 + i * sx, y0, 50 + i * sx, y0 + 140], fill=(120, 120, 120))
            d.text((52 + i * sx, y0 + 50), "st%d" % f["st"], fill=(80, 80, 80))
    d.text((5, y0 + 60), "h син\nv красн", fill=(0, 0, 0))
    for t in range(0, n, 30):
        d.text((50 + t * sx, y0 + 128), "%ds" % (t // 30), fill=(0, 0, 0))
    img.save(path)


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    fr = load(a[0])
    if len(a) > 2 and a[1] == "--sheet":
        nums = [float(v) for v in a[3:8]]
        ints = [int(v) for v in nums[:3]]
        sheet(fr, a[2], *ints, *nums[3:])
        return
    summary(fr)
    checks(fr)


main()
