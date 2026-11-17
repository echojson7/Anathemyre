#!/usr/bin/env python3
"""HDR-снимок игры (--shot f.bmp --hdr → f.bmp.hdr16) → PNG с выбранным тонмаппингом (разбор цвета кадра, docs/lighting_progress.md).

hdr.py снимок.hdr16 выход.png [--tm agx|aces|ue] [--ev поправка] [--wb кельвины_или_auto] [--d доля]
HDR уже умножен на экспозицию глаза (и местную, если она была включена); альфа — свет места, лк.
Тонмапперы — копии post.glsl (agx, aces) и плёночная кривая UE (ue) для сравнения.
"""
import sys, struct
import numpy as np
from PIL import Image


def read(path):
    b = open(path, 'rb').read()
    assert b[:5] == b'HDR16', 'не HDR16'
    w, h = struct.unpack('<II', b[8:16])
    a = np.frombuffer(b[16:16 + w * h * 8], dtype=np.float16).reshape(h, w, 4).astype(np.float32)
    return a


def agx(c):
    to = np.array([[0.842479062253094, 0.0423282422610123, 0.0423756549057051],
                   [0.0784335999999992, 0.878468636469772, 0.0784336],
                   [0.0792237451477643, 0.0791661274605434, 0.879142973793104]])
    fr = np.array([[1.19687900512017, -0.0528968517574562, -0.0529716355144438],
                   [-0.0980208811401368, 1.15190312990417, -0.0980434501171241],
                   [-0.0990297440797205, -0.0989611768448433, 1.15107367264116]])
    lo, hi = -12.47393, 4.026069
    x = np.clip(np.log2(np.maximum(c @ to, 1e-10)), lo, hi)  # mat3 GLSL — по столбцам: c @ M
    x = (x - lo) / (hi - lo)
    x2 = x * x; x4 = x2 * x2
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232
    return np.maximum(x @ fr, 0.0) ** 2.2


def aces(c):
    to = np.array([[0.59719, 0.07600, 0.02840], [0.35458, 0.90834, 0.13383], [0.04823, 0.01566, 0.83777]])
    fr = np.array([[1.60475, -0.10208, -0.00327], [-0.53108, 1.10813, -0.07276], [-0.07367, -0.00605, 1.07602]])
    v = (c * 1.68) @ to
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081)
    return np.clip(v @ fr, 0.0, 1.0)


def srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * x ** (1 / 2.4) - 0.055)


def save(path, lin):
    Image.fromarray((srgb(lin) * 255 + 0.5).astype(np.uint8)).save(path)


if __name__ == '__main__':
    a = read(sys.argv[1])
    out = sys.argv[2]
    tm, ev = 'agx', 0.0
    args = sys.argv[3:]
    for i, s in enumerate(args):
        if s == '--tm': tm = args[i + 1]
        if s == '--ev': ev = float(args[i + 1])
    c = a[..., :3] * 2.0 ** ev
    f = {'agx': agx, 'aces': aces}[tm]
    save(out, f(c))
    print('свет места: медиана %.3g лк' % np.median(a[..., 3]))
