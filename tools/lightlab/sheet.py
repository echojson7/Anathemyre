#!/usr/bin/env python3
"""Лист сравнения цвета кадра: один или несколько HDR-снимков × набор настроек hdr.py → одна картинка с подписями.

sheet.py выход.png 'снимок1.hdr16,снимок2.hdr16' 'подпись: ключи hdr.py' ['подпись: ключи'] …
Пример: sheet.py s.png a.hdr16,b.hdr16 'agx: --tm agx' 'ue meter: --tm ue --meter --local 1.5 0.35'
Строка — снимок, столбец — настройка; кадры уменьшены вдвое.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw
import hdr


def parse(spec):
    label, _, rest = spec.partition(':')
    a = rest.split()
    kw = {}
    for i, s in enumerate(a):
        if s == '--tm': kw['tm'] = a[i + 1]
        if s == '--ev': kw['ev'] = float(a[i + 1])
        if s == '--wb': kw['wb'] = float(a[i + 1])
        if s == '--d': kw['d'] = float(a[i + 1])
        if s == '--meter': kw['meter'] = True
        if s == '--adapt': kw['adapt'] = float(a[i + 1])
        if s == '--local': kw['local'] = (float(a[i + 1]), float(a[i + 2]))
    return label.strip(), kw


if __name__ == '__main__':
    out, shots, specs = sys.argv[1], sys.argv[2].split(','), [parse(s) for s in sys.argv[3:]]
    tiles = []
    for p in shots:
        a = hdr.read(p)
        row = []
        for label, kw in specs:
            img, info = hdr.process(a, p, **kw)
            im = Image.fromarray((hdr.srgb(img) * 255 + 0.5).astype(np.uint8))
            im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
            d = ImageDraw.Draw(im)
            d.rectangle([0, 0, 7 * len(label) + 6, 14], fill=(0, 0, 0))
            d.text((3, 2), label, fill=(255, 255, 0))
            row.append(im)
        tiles.append(row)
    w, h = tiles[0][0].size
    sheet = Image.new('RGB', (w * len(specs), h * len(shots)))
    for r, row in enumerate(tiles):
        for c, im in enumerate(row):
            sheet.paste(im, (c * w, r * h))
    sheet.save(out)
    print(out, sheet.size)
