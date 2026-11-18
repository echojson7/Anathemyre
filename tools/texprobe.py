#!/usr/bin/env python3
"""Проба текстур в движке (docs/textures.md): стальной щит с болтами и труба рядом, развёртка и карты.

Проверяет весь путь ассета: glTF 2.0 → meshpack (cgltf, развёртка VUV0, касательные по развёртке) → .mesh;
PNG → texpack (BC7 цвет + затенение, BC5 нормаль, BC4 кривизна и шероховатость) → .tex; вид материала baked.
Карты считаются из поля высот numpy (как запекание с детальной модели): болты куполами, канавка рамки, выпуклая
буква «F» и стрелка вверх — по ним видно, не перевёрнута ли развёртка и не зеркальна ли нормаль. Задняя сторона
щита — та же область картинки зеркально (касательная со знаком −1), труба — развёртка по кругу со швом.

    python3 tools/texprobe.py data build/tools/meshpack build/tools/texpack

Только numpy и стандартная библиотека (PNG пишется сам): CI пересобирает и сверяет байты с git."""
import json
import math
import os
import struct
import subprocess
import sys
import zlib

import numpy as np

N = 512             # сторона карт, текселей
FACE_V = 0.75       # лицо щита — u 0..1, v 0..0.75 (картинка 512 × 384); кромки — полоса v 0.75..1
PANEL = (1.6, 1.2, 0.12)   # щит: ширина, высота, толщина, м
PIPE_R, PIPE_H, PIPE_SEG = 0.2, 1.2, 32
PANEL_X, PIPE_X = -0.3, 0.95  # середины по x: вся проба около нуля
MATERIAL = "tex_probe_paint"


def png(path, img):
    """RGBA8 (h × w × 4, uint8) → PNG без фильтров строк: байты зависят только от картинки."""
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def blur(a, r):
    """Размытие коробкой радиуса r по обеим осям с заворотом (карта тайлится по u для трубы)."""
    k = 2 * r + 1
    out = a.copy()
    for axis in (0, 1):
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, axis=axis)
        out = acc / k
    return out


def maps():
    """Поле высот (м) и маски лица щита и кромок → цвет sRGB + AO, нормаль, кривизна, шероховатость."""
    fh = int(N * FACE_V)
    y, x = np.mgrid[0:N, 0:N].astype(np.float64)
    px_m = PANEL[0] / N                      # метров на тексель по лицу
    h = np.zeros((N, N))
    rough = np.full((N, N), 0.55)
    kind = np.zeros((N, N), dtype=np.int32)  # 0 краска, 1 болт, 2 канавка, 3 буква
    face = y < fh
    # Канавка рамки: 4 см от края, ширина 1 см, глубина 2 мм
    inset, gw = 0.04 / px_m, 0.01 / px_m
    dx = np.minimum(x, N - 1 - x)
    dy = np.minimum(y, fh - 1 - y)
    d = np.minimum(dx, dy)
    groove = face & (np.abs(d - inset) < gw / 2)
    h[groove] -= 0.002
    kind[groove] = 2
    rough[groove] = 0.8
    # Болты 4 × 3 куполами: радиус 2.2 см, высота 6 мм
    br = 0.022 / px_m
    for i in range(4):
        for j in range(3):
            cx = (0.12 + i * (0.76 / 3)) * N
            cy = (0.15 + j * 0.35) * fh
            r2 = ((x - cx) ** 2 + (y - cy) ** 2) / br ** 2
            dome = face & (r2 < 1.0)
            h[dome] = np.maximum(h[dome], 0.006 * np.sqrt(1.0 - r2[dome]))
            kind[dome] = 1
            rough[dome] = 0.4
    # Буква «F» и стрелка вверх в левом верхнем углу: выпуклые на 1.5 мм — по ним видна ориентация развёртки
    f0x, f0y, s = 0.25 * N, 0.08 * fh, 0.012 / px_m
    letter = np.zeros((N, N), dtype=bool)
    letter |= (x >= f0x) & (x < f0x + s) & (y >= f0y) & (y < f0y + 6 * s)
    letter |= (x >= f0x) & (x < f0x + 4 * s) & (y >= f0y) & (y < f0y + s)
    letter |= (x >= f0x) & (x < f0x + 3 * s) & (y >= f0y + 2.5 * s) & (y < f0y + 3.5 * s)
    ax, ay = 0.36 * N, f0y
    letter |= (x >= ax - s / 2) & (x < ax + s / 2) & (y >= ay + 1.5 * s) & (y < ay + 6 * s)
    tri = (y >= ay) & (y < ay + 2 * s) & (np.abs(x - ax) < (y - ay) * 1.0 + 0.5)
    letter |= tri
    letter &= face
    h[letter] += 0.0015
    kind[letter] = 3
    rough[letter] = 0.5
    hs = blur(h, 1)
    # Нормаль OpenGL: x вправо по картинке, y — к её верху (против строк)
    gx = (np.roll(hs, -1, 1) - np.roll(hs, 1, 1)) / (2 * px_m)
    gy = (np.roll(hs, -1, 0) - np.roll(hs, 1, 0)) / (2 * px_m)
    nrm = np.stack([-gx, gy, np.ones_like(hs)], axis=-1)
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
    # Кривизна: лапласиан высоты (выпукло — выше 0.5) и кромки коробки у краёв области лица и полосы кромок —
    # как дала бы фаска детальной модели
    lap = (np.roll(hs, 1, 0) + np.roll(hs, -1, 0) + np.roll(hs, 1, 1) + np.roll(hs, -1, 1) - 4 * hs) / px_m ** 2
    curv = 0.5 - np.clip(lap * 0.004, -0.5, 0.5)
    edge = np.minimum(np.minimum(x, N - 1 - x), np.where(face, np.minimum(y, fh - 1 - y), np.minimum(y - fh, N - 1 - y)))
    curv = np.maximum(curv, 0.5 + 0.5 * np.clip(1.0 - edge / 8.0, 0.0, 1.0))
    # Затенение: углубление против размытой высоты
    cav = blur(h, 6) - h
    ao = np.clip(1.0 - cav * 250.0, 0.35, 1.0)
    # Цвет (sRGB): оливковая краска с бледной шахматкой 8 × 6 (видно мипы и растяжение), болты сталью,
    # буква — белая трафаретная, красная метка в углу u = 0, v = 0
    paint = np.array([92, 104, 70], dtype=np.float64)
    chk = (((x // (N / 8)).astype(int) + (y // (fh / 6)).astype(int)) % 2)[..., None]
    col = paint * (0.92 + 0.08 * chk)
    col = np.where((kind == 1)[..., None], np.array([118, 116, 110.0]), col)
    col = np.where((kind == 2)[..., None], paint * 0.7, col)
    col = np.where((kind == 3)[..., None], np.array([214, 208, 190.0]), col)
    mark = (x < 0.05 * N) & (y < 0.05 * N)
    col = np.where(mark[..., None], np.array([200, 40, 30.0]), col)
    col = np.where(face[..., None], col, paint * 0.95)
    rgba = np.concatenate([col, ao[..., None] * 255.0], axis=-1)
    to8 = lambda a: np.clip(np.floor(a + 0.5), 0, 255).astype(np.uint8)
    one = lambda a: to8(np.repeat(a[..., None] * 255.0, 4, axis=-1))
    nmap = to8(np.concatenate([(nrm * 0.5 + 0.5) * 255.0, np.full((N, N, 1), 255.0)], axis=-1))
    return to8(rgba), nmap, one(curv), one(rough)


class Geo:
    """Треугольники одного примитива: положение, нормаль, uv (v вниз по картинке)."""

    def __init__(self):
        self.p, self.n, self.uv, self.idx = [], [], [], []

    def quad(self, c, n, uv):
        base = len(self.p)
        for k in range(4):
            self.p.append(c[k])
            self.n.append(n)
            self.uv.append(uv[k])
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]


def geometry():
    g = Geo()
    w, hgt, t = PANEL
    x0, x1, z0, z1 = PANEL_X - w / 2, PANEL_X + w / 2, -t / 2, t / 2
    # Лицо (+z): u слева направо, v сверху вниз — вся область лица
    g.quad([(x0, 0, z1), (x1, 0, z1), (x1, hgt, z1), (x0, hgt, z1)], (0, 0, 1),
           [(0, FACE_V), (1, FACE_V), (1, 0), (0, 0)])
    # Тыл (−z): та же область зеркально — касательная по развёртке выйдет со знаком −1
    g.quad([(x1, 0, z0), (x0, 0, z0), (x0, hgt, z0), (x1, hgt, z0)], (0, 0, -1),
           [(1, FACE_V), (0, FACE_V), (0, 0), (1, 0)])
    # Кромки и верх — полоса v 0.75..1
    s0, s1 = FACE_V, 1.0
    g.quad([(x0, hgt, z1), (x1, hgt, z1), (x1, hgt, z0), (x0, hgt, z0)], (0, 1, 0), [(0, s1), (1, s1), (1, s0), (0, s0)])
    g.quad([(x1, 0, z1), (x1, 0, z0), (x1, hgt, z0), (x1, hgt, z1)], (1, 0, 0), [(0, s1), (0.1, s1), (0.1, s0), (0, s0)])
    g.quad([(x0, 0, z0), (x0, 0, z1), (x0, hgt, z1), (x0, hgt, z0)], (-1, 0, 0), [(0.9, s1), (1, s1), (1, s0), (0.9, s0)])
    # Труба: развёртка по кругу (u 0..1 — вся ширина, шов — две вершины на одном месте), v — по высоте
    base = len(g.p)
    for k in range(PIPE_SEG + 1):
        a = 2 * math.pi * k / PIPE_SEG
        nx, nz = math.sin(a), math.cos(a)
        for yy, vv in ((0.0, FACE_V), (PIPE_H, 0.0)):
            g.p.append((PIPE_X + PIPE_R * nx, yy, PIPE_R * nz))
            g.n.append((nx, 0.0, nz))
            g.uv.append((k / PIPE_SEG, vv))
    for k in range(PIPE_SEG):
        a, b = base + 2 * k, base + 2 * k + 2
        g.idx += [a, b, b + 1, a, b + 1, a + 1]
    return g


def gltf(path, g):
    """Один узел, один примитив, буфер рядом (.bin). Числа — f32 little-endian, индексы u32."""
    pos = np.array(g.p, dtype="<f4")
    nrm = np.array(g.n, dtype="<f4")
    uv = np.array(g.uv, dtype="<f4")
    idx = np.array(g.idx, dtype="<u4")
    blobs = [pos.tobytes(), nrm.tobytes(), uv.tobytes(), idx.tobytes()]
    offs, o = [], 0
    for b in blobs:
        offs.append(o)
        o += len(b)
    binp = path[:-5] + ".bin"
    with open(binp, "wb") as f:
        f.write(b"".join(blobs))
    views = [{"buffer": 0, "byteOffset": offs[k], "byteLength": len(blobs[k])} for k in range(4)]
    acc = [
        {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3",
         "min": pos.min(0).tolist(), "max": pos.max(0).tolist()},
        {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
        {"bufferView": 3, "componentType": 5125, "count": len(idx), "type": "SCALAR"},
    ]
    doc = {
        "asset": {"version": "2.0", "generator": "tools/texprobe.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "tex_probe"}],
        "materials": [{"name": MATERIAL}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "buffers": [{"uri": os.path.basename(binp), "byteLength": o}],
        "bufferViews": views, "accessors": acc,
    }
    with open(path, "w") as f:
        json.dump(doc, f, indent=1, sort_keys=True)


def main():
    if len(sys.argv) != 4:
        sys.exit("использование: texprobe.py <data> <meshpack> <texpack>")
    data, meshpack, texpack = sys.argv[1:]
    work = os.path.join("build", "texprobe")
    os.makedirs(work, exist_ok=True)
    os.makedirs(os.path.join(data, "textures"), exist_ok=True)
    col, nrm, curv, rough = maps()
    for name, img, kind in (("color", col, "color"), ("normal", nrm, "normal"), ("curv", curv, "mask"), ("rough", rough, "mask")):
        p = os.path.join(work, "tex_probe_%s.png" % name)
        png(p, img)
        subprocess.run([texpack, kind, p, os.path.join(data, "textures", "tex_probe_%s.tex" % name)], check=True)
    gp = os.path.join(work, "tex_probe.gltf")
    gltf(gp, geometry())
    subprocess.run([meshpack, gp, os.path.join(data, "meshes", "tex_probe.mesh")], check=True)


if __name__ == "__main__":
    main()
