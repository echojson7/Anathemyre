# Лист сравнения: сверху концепт, снизу рендер. python3 sheet.py concept.png render.png out.png
import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
w = max(a.width, b.width)
out = Image.new('RGB', (w, a.height + b.height + 6), (255, 255, 255))
out.paste(a, (0, 0)); out.paste(b, (0, a.height + 6))
out.save(sys.argv[3])
