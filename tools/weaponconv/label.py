# Подписи на листе: python3 label.py in.png out.png "ИМЯ:x:y" ...
import sys
from PIL import Image, ImageDraw, ImageFont
im = Image.open(sys.argv[1]).convert('RGB'); d = ImageDraw.Draw(im)
f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf', 22)
for a in sys.argv[3:]:
    t, x, y = a.rsplit(':', 2)
    d.text((int(x), int(y)), t, fill=(30, 30, 30), font=f)
im.save(sys.argv[2])
