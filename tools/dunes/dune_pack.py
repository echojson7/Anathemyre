#!/usr/bin/env python3
# Карты дюн для пустынь (docs/terrain.md, «Дюны из карты»): сетка 5121² узлов с шагом 2 м (локация 10 240 м),
# высота дюн над дном, единица 5 см. Формат .dun: "DUN1", u32 ширина, u32 высота, u32 k, u32 байт потока; дальше
# поток Райса: остаток предсказания плоскостью (слева + снизу − по диагонали), зигзаг, k младших бит.
# Разбор — src/sim/dune_map.cpp.
#   dune_pack.py lidar <плитка_1м.npy>... <выход.dun>   — лидар USGS 3DEP (см. docs/terrain.md)
#   dune_pack.py wind <сетка.bin> <N> <клетка_м> <плита_м> <выход.dun>   — итог tools/dunes/dune_wind.c
import sys, struct
import numpy as np
from scipy import ndimage

SIDE = 5121          # узлов на сторону
STEP = 2.0           # м

def encode(q, path):
    q = q.astype(np.int64)
    p = np.zeros_like(q)
    p[1:, 1:] = q[1:, :-1] + q[:-1, 1:] - q[:-1, :-1]
    p[0, 1:] = q[0, :-1]
    p[1:, 0] = q[:-1, 0]
    r = (q - p).ravel()
    z = np.where(r >= 0, 2 * r, -2 * r - 1).astype(np.uint64)
    best = None
    for k in range(0, 8):
        bits = int((z >> k).sum()) + len(z) * (k + 1)
        if best is None or bits < best[1]: best = (k, bits)
    k = best[0]
    # Поток: частное унарно (единицы, затем ноль), не больше 31 — иначе 32 единицы и 32 бита значения
    out = bytearray()
    acc = 0; nb = 0
    def put(v, n):
        nonlocal acc, nb
        acc |= v << nb; nb += n
        while nb >= 8:
            out.append(acc & 255); acc >>= 8; nb -= 8
    for v in z.tolist():
        qv = v >> k
        if qv < 32:
            # qv единиц и ноль
            while qv >= 16: put(0xFFFF, 16); qv -= 16
            put((1 << qv) - 1, qv + 1)
            put(v & ((1 << k) - 1), k)
        else:
            put(0xFFFFFFFF, 32); put(v, 32)
    if nb: out.append(acc & 255)
    with open(path, 'wb') as f:
        f.write(b'DUN1' + struct.pack('<4I', q.shape[1], q.shape[0], k, len(out)))
        f.write(out)
    print(path, q.shape, 'k', k, len(out) / 1e6, 'МБ', 'высота до', q.max() * 0.05, 'м')

def finish(h, path):
    # h — метры, строки с юга на север. Дюны над дном: минус пологий фон, низ — 0
    h = h - np.percentile(h, 0.5)
    top = h.max()
    if top > 100.0: h = h * (100.0 / top)   # единица карты в игре — 1/640 м, до 102 м
    q = np.clip(np.round(h / 0.05), 0, 65535).astype(np.int64)
    encode(q, path)

def lidar(tiles, path):
    # Плитки — numpy 1 м строками с севера на юг, склеены по востоку. Вырез 10 240 м, отражение на краях,
    # среднее 2×2 → 2 м, фон — размытие 1.2 км (наклон долины и самые длинные волны — от генератора)
    a = np.concatenate([np.load(t) for t in tiles], axis=1).astype(np.float64)
    a = a[::-1]  # с юга на север
    n = int((SIDE - 1) * STEP)
    a = np.pad(a, ((0, max(0, n - a.shape[0])), (0, max(0, n - a.shape[1]))), mode='reflect')[:n, :n]
    a = a.reshape(n // 2, 2, n // 2, 2).mean(axis=(1, 3))
    a = np.pad(a, ((0, 1), (0, 1)), mode='edge')
    bg = ndimage.gaussian_filter(a, 600, mode='nearest')
    finish(a - bg, path)

def wind(src, n, cell, slab, path):
    g = np.fromfile(src, dtype=np.int16).reshape(n, n).astype(np.float64) * slab
    g = ndimage.gaussian_filter(g, 0.8, mode='wrap')   # ступени плит
    # Периодичная сетка cell м → узлы 2 м (кубически)
    ys = np.arange(SIDE) * STEP / cell
    yy, xx = np.meshgrid(ys, ys, indexing='ij')
    big = np.pad(g, 4, mode='wrap')
    h = ndimage.map_coordinates(big, [yy + 4, xx + 4], order=3, mode='wrap')
    finish(h, path)

if __name__ == '__main__':
    if sys.argv[1] == 'lidar': lidar(sys.argv[2:-1], sys.argv[-1])
    elif sys.argv[1] == 'wind': wind(sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5]), sys.argv[6])
