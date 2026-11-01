#!/usr/bin/env python3
# TTF → .font: атлас букв нужных размеров (альфа 8 бит) и таблица метрик. Игра читает только .font (src/render/ui.c).
# Запуск: python3 tools/fontconv/fontconv.py tools/fontconv/IBMPlexSans-Medium.ttf data/ui/plex.font
# Нужен Pillow с FreeType. Формат — в начале ui.c (FONT_*).
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

SIZES = list(range(8, 41))          # кегли в пикселях окна: интерфейс берёт ближайший, без растяжения
ATLAS_W = 2048
PAD = 1

# Интерфейс пишет заглавными: строчные не нужны
CHARS = [c for c in range(0x20, 0x7F) if not (0x61 <= c <= 0x7A)]
CHARS += [0x401] + list(range(0x410, 0x430))
CHARS += [0xB0, 0xB7, 0xAB, 0xBB, 0xD7, 0x2013, 0x2014, 0x2026, 0x2116, 0x2192, 0x2190]


def main():
    src, out = sys.argv[1], sys.argv[2]
    glyphs, metrics = [], []
    for px in SIZES:
        f = ImageFont.truetype(src, px)
        asc, desc = f.getmetrics()
        cap = -f.getbbox("H", anchor="ls")[1]
        metrics.append((px, asc, desc, cap))
        for cp in CHARS:
            ch = chr(cp)
            adv = f.getlength(ch)
            x0, y0, x1, y1 = f.getbbox(ch, anchor="ls")
            w, h = max(0, x1 - x0), max(0, y1 - y0)
            img = Image.new("L", (max(w, 1), max(h, 1)), 0)
            if w and h:
                ImageDraw.Draw(img).text((-x0, -y0), ch, font=f, fill=255, anchor="ls")
            glyphs.append([px, cp, w, h, x0, y0, adv, img, 0, 0])

    # Полки по высоте: сначала высокие
    order = sorted(range(len(glyphs)), key=lambda i: -glyphs[i][3])
    x = y = shelf = 0
    for i in order:
        g = glyphs[i]
        if not g[2]:
            continue
        if x + g[2] + PAD > ATLAS_W:
            x, y, shelf = 0, y + shelf + PAD, 0
        g[8], g[9] = x, y
        x += g[2] + PAD
        shelf = max(shelf, g[3])
    atlas_h = y + shelf + PAD
    atlas_h = (atlas_h + 3) & ~3
    atlas = Image.new("L", (ATLAS_W, atlas_h), 0)
    for g in glyphs:
        if g[2]:
            atlas.paste(g[7], (g[8], g[9]))

    with open(out, "wb") as o:
        o.write(b"AFNT")
        o.write(struct.pack("<IHHHH", 1, ATLAS_W, atlas_h, len(SIZES), len(CHARS)))
        for px, asc, desc, cap in metrics:
            o.write(struct.pack("<hhhh", px, asc, desc, cap))
        o.write(struct.pack("<%dI" % len(CHARS), *CHARS))
        for g in glyphs:
            # x, y в атласе; w, h; смещение от пера и линии строки; шаг пера в 1/64 пикселя
            o.write(struct.pack("<HHBBbbH", g[8], g[9], g[2], g[3], g[4], g[5], int(round(g[6] * 64))))
        o.write(atlas.tobytes())
    print("%s: %d кеглей x %d букв, атлас %dx%d" % (out, len(SIZES), len(CHARS), ATLAS_W, atlas_h))


if __name__ == "__main__":
    main()
