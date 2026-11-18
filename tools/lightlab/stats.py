#!/usr/bin/env python3
# stats.py img.png [name x0 y0 x1 y1]... — средние sRGB, линейные, яркость и насыщенность по областям
import sys
from PIL import Image
import numpy as np
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(float) / 255
lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
L = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
print('кадр: средняя лин. яркость %.4f, медиана sRGB %.3f, p5 %.3f p95 %.3f, доля пересвета %.3f, доля черноты %.3f' % (
    L.mean(), np.median(a.mean(2)), np.percentile(a.mean(2), 5), np.percentile(a.mean(2), 95),
    (a.max(2) > 0.98).mean(), (a.max(2) < 0.03).mean()))
args = sys.argv[2:]
for i in range(0, len(args), 5):
    n = args[i]; x0, y0, x1, y1 = map(int, args[i + 1:i + 5])
    r = a[y0:y1, x0:x1].reshape(-1, 3).mean(0)
    rl = lin[y0:y1, x0:x1].reshape(-1, 3).mean(0)
    y = 0.2126 * rl[0] + 0.7152 * rl[1] + 0.0722 * rl[2]
    ch = rl / max(rl.sum(), 1e-9)
    print('%-14s sRGB %s  лин.Y %.4f  цветность r %.3f g %.3f b %.3f' % (n, np.round(r, 3), y, ch[0], ch[1], ch[2]))
