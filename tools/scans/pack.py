"""Пакет сканов CC0 (docs/scans.md): картинки 1K из кеша → scans.pak для игры.

  pack.py scans.txt <кеш> <выход.pak>

Формат (u32 младшим байтом вперёд): "ASCN", версия 1, слоёв, размер, мипов; имена слоёв по 16 байт; потом две
картинки-массива — цвет (RGB sRGB, A — AO) и нормаль (XYZ · 0.5 + 0.5 OpenGL, A — шероховатость), каждая по мипам
от size до 1, внутри мипа слои подряд, RGBA8. Мипы — среднее 2 × 2: цвет в линейном свете, нормаль без
нормировки (короткая нормаль дальнего мипа — это погашенный наклон, игра переводит его в шероховатость)."""
import struct
import sys

import numpy as np
from PIL import Image


def layers_of(path):
    out = []
    for line in open(path, encoding="utf-8"):
        w = line.split("#")[0].split()
        if len(w) == 3 and w[0] == "scan":
            if len(w[1]) > 16 or not w[1].isascii():
                sys.exit(f"pack: имя слоя {w[1]} длиннее 16 букв латиницы")
            out.append((w[1], w[2]))
    return out


def load(cache, asset, name, mode):
    im = Image.open(f"{cache}/{asset}_{name}_1k.jpg").convert(mode)
    return np.asarray(im, dtype=np.float64) / 255.0


def to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def down(a):
    return 0.25 * (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2])


def main():
    src, cache, out = sys.argv[1:4]
    layers = layers_of(src)
    size = None
    albedo, normal = [], []  # по слою: список мипов, линейные float
    for _, asset in layers:
        col = to_lin(load(cache, asset, "diff", "RGB"))
        ao = load(cache, asset, "ao", "L")
        nor = load(cache, asset, "nor_gl", "RGB") * 2.0 - 1.0
        rough = load(cache, asset, "rough", "L")
        s = col.shape[0]
        if col.shape[:2] != (s, s) or s & (s - 1) or size not in (None, s):
            sys.exit(f"pack: {asset} — не квадрат степени двойки или другой размер")
        size = s
        a = np.dstack([col, ao])
        n = np.dstack([nor, rough])
        am, nm = [a], [n]
        while am[-1].shape[0] > 1:
            am.append(down(am[-1]))
            nm.append(down(nm[-1]))
        albedo.append(am)
        normal.append(nm)
    mips = len(albedo[0])
    head = b"ASCN" + struct.pack("<4I", 1, len(layers), size, mips)
    head += b"".join(name.encode().ljust(16, b"\0") for name, _ in layers)
    body_a, body_n = [], []
    for m in range(mips):
        for l in range(len(layers)):
            a = albedo[l][m]
            a8 = np.dstack([to_srgb(a[..., :3]), a[..., 3:]])
            n = normal[l][m]
            n8 = np.dstack([n[..., :3] * 0.5 + 0.5, n[..., 3:]])
            body_a.append(np.round(np.clip(a8, 0, 1) * 255).astype(np.uint8).tobytes())
            body_n.append(np.round(np.clip(n8, 0, 1) * 255).astype(np.uint8).tobytes())
    data = head + b"".join(body_a) + b"".join(body_n)
    with open(out, "wb") as f:
        f.write(data)
    print(f"scans: {len(layers)} слоёв {size}², мипов {mips}, {len(data) / 2**20:.1f} МБ → {out}")


main()
