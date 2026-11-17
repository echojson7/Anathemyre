#!/usr/bin/env python3
"""HDR-снимок игры (--shot f.bmp --hdr → f.bmp.hdr16) → PNG с выбранным цветом кадра (docs/lighting_progress.md).

hdr.py снимок.hdr16 выход.png [--tm agx|punchy|aces|ue] [--ev поправка] [--wb кельвины] [--d доля]
                              [--meter] [--local колено наклон]
HDR уже умножен на экспозицию глаза (и местную, если её не выключили --nolocal); альфа — свет места × экспозиция
(до шага 2 — лк). Экспозицию кадра скрипт берёт из журнала рядом (снимок.log, строка «глаз: … экспозиция X»).
- --tm: AgX (как post.glsl), AgX «Punchy» (CDL power 1.35, sat 1.4 — как в Blender), ACES (Hill, как post.glsl),
  ue — плёночная кривая UE5 (FilmToneMap: AP1, slope 0.88, toe 0.55, shoulder 0.26, white clip 0.04).
- --wb K --d D: баланс белого глаза — фон Крис в конусах CAT02 от света K кельвинов к D65 с долей D, яркость та же.
- --meter: экспозиция по свету места, куда смотрит камера (среднее log2 альфы с весом к центру), а не у камеры.
- --local k s: местная экспозиция в свет: до k стопов как есть, дальше наклон s (как local_tone в data/sky.txt).
"""
import sys, struct, re, os
import numpy as np
from PIL import Image

Y = np.array([0.2126, 0.7152, 0.0722])


def read(path):
    b = open(path, 'rb').read()
    assert b[:5] == b'HDR16', 'не HDR16'
    w, h = struct.unpack('<II', b[8:16])
    return np.frombuffer(b[16:16 + w * h * 8], dtype=np.float16).reshape(h, w, 4).astype(np.float32)


def exposure_of(path):
    """(замер глаза, лк; экспозиция; альфа — свет места × экспозиция) из журнала снимка (снимок.log рядом)."""
    log = re.sub(r'\.bmp\.hdr16$', '.log', path)
    if not os.path.exists(log): return None
    m, rel = None, False
    for line in open(log, encoding='utf-8', errors='replace'):
        if 'глаз:' in line and 'экспозиция' in line:
            m = re.search(r'у глаза ([0-9.e+-]+) лк.*?экспозиция ([0-9.e+-]+)', line)
            rel = 'в кадре' in line  # с 17.11 (шаг 2) альфа — свет места × экспозиция, раньше — лк
    return (float(m.group(1)), float(m.group(2)), rel) if m else None


def local_lux(a, path):
    """Свет места, лк, из альфы снимка."""
    eye = exposure_of(path)
    return a[..., 3] / eye[1] if eye and eye[2] else a[..., 3]


def agx_base(c):
    to = np.array([[0.842479062253094, 0.0423282422610123, 0.0423756549057051],
                   [0.0784335999999992, 0.878468636469772, 0.0784336],
                   [0.0792237451477643, 0.0791661274605434, 0.879142973793104]])
    lo, hi = -12.47393, 4.026069
    x = np.clip(np.log2(np.maximum(c @ to, 1e-10)), lo, hi)  # mat3 GLSL — по столбцам: c @ M
    x = (x - lo) / (hi - lo)
    x2 = x * x; x4 = x2 * x2
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232


def agx_end(x):
    fr = np.array([[1.19687900512017, -0.0528968517574562, -0.0529716355144438],
                   [-0.0980208811401368, 1.15190312990417, -0.0980434501171241],
                   [-0.0990297440797205, -0.0989611768448433, 1.15107367264116]])
    return np.maximum(x @ fr, 0.0) ** 2.2


def agx(c):
    return agx_end(agx_base(c))


def agx_punchy(c, power=1.35, sat=1.4):
    x = agx_base(c)
    luma = (x * Y).sum(-1, keepdims=True)
    x = np.maximum(x, 0.0) ** power
    x = luma ** power + sat * (x - luma ** power)  # luma той же кривой — серое остаётся серым
    return agx_end(np.clip(x, 0.0, 1.0))


def aces(c):
    to = np.array([[0.59719, 0.07600, 0.02840], [0.35458, 0.90834, 0.13383], [0.04823, 0.01566, 0.83777]])
    fr = np.array([[1.60475, -0.10208, -0.00327], [-0.53108, 1.10813, -0.07276], [-0.07367, -0.00605, 1.07602]])
    v = (c * 1.68) @ to
    v = (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.4329510) + 0.238081)
    return np.clip(v @ fr, 0.0, 1.0)


# sRGB (D65) ↔ ACEScg (AP1, D60 через CAT Брэдфорда) — строками: v_ap1 = M @ v_srgb
SRGB_AP1 = np.array([[0.6130974, 0.3395231, 0.0473795],
                     [0.0701937, 0.9163539, 0.0134525],
                     [0.0206156, 0.1095698, 0.8698146]])
AP1_SRGB = np.linalg.inv(SRGB_AP1)
AP1_Y = np.array([0.2722287, 0.6740818, 0.0536895])


def ue_filmic(c, slope=0.88, toe=0.55, shoulder=0.26, black=0.0, white=0.04):
    """FilmToneMap UE4/UE5 (TonemapCommon.ush) без «glow» и «red modifier» ACES: кривая в log10 по каналам AP1."""
    w = np.maximum(c @ SRGB_AP1.T, 0.0)
    w = (w * AP1_Y).sum(-1, keepdims=True) * 0.04 + w * 0.96          # pre desaturate
    toe_s = 1 + black - toe
    sh_s = 1 + white - shoulder
    in_m, out_m = 0.18, 0.18
    if toe > 0.8:
        toe_m = (1 - toe - out_m) / slope + np.log10(in_m)
    else:
        bt = (out_m + black) / toe_s - 1
        toe_m = np.log10(in_m) - 0.5 * np.log((1 + bt) / (1 - bt)) * (toe_s / slope)
    str_m = (1 - toe) / slope - toe_m
    sh_m = shoulder / slope - str_m
    lc = np.log10(np.maximum(w, 1e-10))
    st = slope * (lc + str_m)
    tc = -black + (2 * toe_s) / (1 + np.exp((-2 * slope / toe_s) * (lc - toe_m)))
    sc = (1 + white) - (2 * sh_s) / (1 + np.exp((2 * slope / sh_s) * (lc - sh_m)))
    tc = np.where(lc < toe_m, tc, st)
    sc = np.where(lc > sh_m, sc, st)
    t = np.clip((lc - toe_m) / (sh_m - toe_m), 0, 1)
    if sh_m < toe_m: t = 1 - t  # у UE по умолчанию плечо начинается раньше конца носка: прямого участка нет
    t = (3 - 2 * t) * t * t
    tone = tc + (sc - tc) * t
    tone = (tone * AP1_Y).sum(-1, keepdims=True) * 0.07 + tone * 0.93  # post desaturate
    return np.clip(np.maximum(tone, 0.0) @ AP1_SRGB.T, 0.0, 1.0)


def srgb(x):
    x = np.clip(x, 0, 1)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * x ** (1 / 2.4) - 0.055)


def save(path, lin):
    Image.fromarray((srgb(lin) * 255 + 0.5).astype(np.uint8)).save(path)


def kelvin_rgb(k):
    """Как lamp_kelvin_rgb (src/view/lamps.cpp): кривая Планка (Kim et al.) → линейный sRGB, Y = 1."""
    t = min(max(k, 1667.0), 25000.0); t2 = t * t; t3 = t2 * t
    x = (-0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910 if t <= 4000 else
         -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390)
    x2 = x * x; x3 = x2 * x
    y = (-1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683 if t <= 2222 else
         -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867 if t <= 4000 else
         3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483)
    X, Z = x / y, (1 - x - y) / y
    rgb = np.maximum([3.2404542 * X - 1.5371385 - 0.4985314 * Z, -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
                      0.0556434 * X - 0.2040259 + 1.0572252 * Z], 0.0)
    return rgb / (rgb @ Y)


# CAT02: линейный sRGB → XYZ → конусы
SRGB_XYZ = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
CAT02 = np.array([[0.7328, 0.4296, -0.1624], [-0.7036, 1.6975, 0.0061], [0.0030, 0.0136, 0.9834]])
RGB_LMS = CAT02 @ SRGB_XYZ
LMS_RGB = np.linalg.inv(RGB_LMS)


def wb_matrix(white_rgb, d):
    """Фон Крис: свет white_rgb (линейный sRGB) виден белым D65 с долей d; яркость белого та же."""
    lw = RGB_LMS @ np.asarray(white_rgb, float)
    lr = RGB_LMS @ np.ones(3) * (np.asarray(white_rgb) @ Y)
    g = d * lr / lw + (1 - d)
    m = LMS_RGB @ np.diag(g) @ RGB_LMS
    out = m @ white_rgb
    return m * ((np.asarray(white_rgb) @ Y) / (out @ Y))


def center_weight(h, w):
    yy, xx = np.mgrid[0:h, 0:w]
    r2 = ((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2
    return np.exp(-r2 / (2 * 0.45 ** 2))


def process(a, path, tm='agx', ev=0.0, wb=None, d=0.75, meter=False, local=None, adapt=0.85, ref=100000.0):
    c = a[..., :3].copy()
    e_loc = local_lux(a, path)
    eye = exposure_of(path)
    info = {}
    if eye and (meter or local):
        e_eye, ex = eye[0], eye[1]
        if meter:
            wgt = center_weight(*e_loc.shape)
            lg = (np.log2(np.maximum(e_loc, 1e-6)) * wgt).sum() / wgt.sum()
            e_view = 2.0 ** lg
            ev_ref = np.log2(ref / 2.5)
            ev_new = ev_ref + adapt * (np.log2(e_view / 2.5) - ev_ref)
            ex_new = 2 ** 0.5 / (1.2 * 2 ** ev_new)
            c *= ex_new / ex
            info['свет места'] = e_view
            info['экспозиция'] = (ex, ex_new)
            ex = ex_new
        if local:
            k, s = local
            dd = np.log2(np.maximum(e_loc * ex / np.pi, 1e-9))
            x = np.maximum(dd - k, 0.0)
            wdt = 0.5
            g = k + s * x + (1 - s) * wdt * (1 - np.exp(-x / wdt))
            gain = np.where(dd > k, 2.0 ** (g - dd), 1.0)
            c *= gain[..., None]
    c *= 2.0 ** ev
    if wb:
        m = wb_matrix(kelvin_rgb(wb), d)
        c = np.maximum(c @ m.T, 0.0)
    f = {'agx': agx, 'punchy': agx_punchy, 'aces': aces, 'ue': ue_filmic}[tm]
    return f(c), info


if __name__ == '__main__':
    path, out = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    kw = {}
    for i, s in enumerate(args):
        if s == '--tm': kw['tm'] = args[i + 1]
        if s == '--ev': kw['ev'] = float(args[i + 1])
        if s == '--wb': kw['wb'] = float(args[i + 1])
        if s == '--d': kw['d'] = float(args[i + 1])
        if s == '--meter': kw['meter'] = True
        if s == '--local': kw['local'] = (float(args[i + 1]), float(args[i + 2]))
    a = read(path)
    img, info = process(a, path, **kw)
    save(out, img)
    print('свет места: медиана %.3g лк' % np.median(local_lux(a, path)), info)
