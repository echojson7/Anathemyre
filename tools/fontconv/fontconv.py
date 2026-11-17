#!/usr/bin/env python3
# TTF → .font: атлас букв нужных размеров (альфа 8 бит), метрики и кернинг для нескольких начертаний.
# Игра читает только .font (src/ui/ui.cpp). Формат — docs/ui.md «Шрифт».
# Запуск: python3 tools/fontconv/fontconv.py data/ui/inter.font tools/fontconv/Inter-Regular.ttf tools/fontconv/Inter-SemiBold.ttf
# Нужен Pillow с FreeType и raqm (кернинг и цифры одной ширины — по таблицам шрифта)
import struct
import sys

from PIL import Image, ImageDraw, ImageFont, features

# Кегли в пикселях окна: интерфейс берёт ближайший, без растяжения. Крупные — для 4K и масштаба 2
SIZES = list(range(9, 29)) + list(range(30, 49, 2))
ATLAS_W = 2048
PAD = 1
KERN_REF = 1024  # кернинг считается на этом кегле и хранится в 1/1024 кегля

# Латиница, кириллица с Ё/ё и знаки: заголовки заглавными, строки обычным регистром
CHARS = list(range(0x20, 0x7F))
CHARS += [0x401, 0x451] + list(range(0x410, 0x450))
CHARS += [0xA0, 0xB0, 0xB1, 0xB5, 0xB7, 0xAB, 0xBB, 0xD7, 0x2009, 0x2013, 0x2014, 0x2022, 0x2026, 0x2116,
          0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2248, 0x2039, 0x203A, 0x25B6, 0x25BC, 0x2713]
DIGITS = set(range(0x30, 0x3A))


def face_glyphs(path, face):
    # Цифры — табличные (tnum): одной ширины, числа в столбцах не дрожат
    glyphs, metrics = [], []
    for px in SIZES:
        f = ImageFont.truetype(path, px, layout_engine=ImageFont.Layout.RAQM)
        asc, desc = f.getmetrics()
        cap = -f.getbbox("H", anchor="ls")[1]
        metrics.append((px, asc, desc, cap))
        for cp in CHARS:
            ch = chr(cp)
            feat = ["tnum"] if cp in DIGITS else None
            adv = f.getlength(ch, features=feat)
            x0, y0, x1, y1 = f.getbbox(ch, anchor="ls", features=feat)
            w, h = max(0, x1 - x0), max(0, y1 - y0)
            img = Image.new("L", (max(w, 1), max(h, 1)), 0)
            if w and h:
                ImageDraw.Draw(img).text((-x0, -y0), ch, font=f, fill=255, anchor="ls", features=feat)
            glyphs.append([face, px, cp, w, h, x0, y0, adv, img, 0, 0])
    return glyphs, metrics


def face_kerning(path):
    # Пары букв, у которых шаг пары отличается от суммы шагов; цифры не трогаем (табличные)
    f = ImageFont.truetype(path, KERN_REF, layout_engine=ImageFont.Layout.RAQM)
    adv = {cp: f.getlength(chr(cp)) for cp in CHARS}
    letters = [cp for cp in CHARS if cp > 0x20 and cp not in DIGITS and cp not in (0xA0, 0x2009)]
    out = []
    for ia, a in enumerate(CHARS):
        if a not in letters:
            continue
        for ib, b in enumerate(CHARS):
            if b not in letters:
                continue
            k = f.getlength(chr(a) + chr(b)) - adv[a] - adv[b]
            k = int(round(k))
            if k:
                out.append((ia, ib, k))
    return out


def main():
    if not features.check("raqm"):
        sys.exit("нужен Pillow с raqm (кернинг и табличные цифры)")
    out, faces = sys.argv[1], sys.argv[2:]
    glyphs, metrics, kerns = [], [], []
    for i, path in enumerate(faces):
        g, m = face_glyphs(path, i)
        glyphs += g
        metrics.append(m)
        kerns.append(face_kerning(path))

    # Полки по высоте: сначала высокие
    order = sorted(range(len(glyphs)), key=lambda i: -glyphs[i][4])
    x = y = shelf = 0
    for i in order:
        g = glyphs[i]
        if not g[3]:
            continue
        if x + g[3] + PAD > ATLAS_W:
            x, y, shelf = 0, y + shelf + PAD, 0
        g[9], g[10] = x, y
        x += g[3] + PAD
        shelf = max(shelf, g[4])
    atlas_h = y + shelf + PAD
    atlas_h = (atlas_h + 3) & ~3
    atlas = Image.new("L", (ATLAS_W, atlas_h), 0)
    for g in glyphs:
        if g[3]:
            atlas.paste(g[8], (g[9], g[10]))

    with open(out, "wb") as o:
        o.write(b"AFNT")
        o.write(struct.pack("<IHHHHHH", 2, ATLAS_W, atlas_h, len(faces), len(SIZES), len(CHARS), 0))
        o.write(struct.pack("<%dI" % len(CHARS), *CHARS))
        per_face = len(SIZES) * len(CHARS)
        for fi in range(len(faces)):
            for px, asc, desc, cap in metrics[fi]:
                o.write(struct.pack("<hhhh", px, asc, desc, cap))
            for g in glyphs[fi * per_face:(fi + 1) * per_face]:
                # x, y в атласе; w, h; смещение от пера и линии строки; шаг пера в 1/64 пикселя
                o.write(struct.pack("<HHBBbbH", g[9], g[10], g[3], g[4], g[5], g[6], int(round(g[7] * 64))))
            o.write(struct.pack("<I", len(kerns[fi])))
            for a, b, k in kerns[fi]:
                o.write(struct.pack("<HHh", a, b, k))
        o.write(atlas.tobytes())
    print("%s: %d начертания x %d кеглей x %d букв, пар кернинга %s, атлас %dx%d"
          % (out, len(faces), len(SIZES), len(CHARS), [len(k) for k in kerns], ATLAS_W, atlas_h))


if __name__ == "__main__":
    main()
