"""Поверхность литого бетона для запекания (docs/bake.md): тайловые поля высот, цвета и шероховатости, numpy.

Своё описание, без сканов: опалубка из досок 15 см со ступеньками и наплывами на стыках, волокно дерева,
стяжки опалубки (конус с пробкой) сеткой 0.6 × 0.5 м, поры, пятна цементного молока; у пола — затирка и
истёртость. Поле — тайл SIZE × SIZE м: стена в плоскости (вдоль, вверх), пол — (x, z). Высоты в метрах, вниз —
углубление. Сканы CC0 (docs/scans.md) поверх — в игре строкой scan материала.

Детерминированно: только numpy с явным зерном и целочисленные сетки, без зависимости от числа ядер.
"""
import numpy as np

SIZE = 2.4          # тайл, м: 16 досок по 15 см, 4 × 4 стяжки
N = 2048            # текселей на тайл (~1.2 мм)
BOARD = 0.15        # ширина доски опалубки
TIE = (0.6, 0.6)    # шаг стяжек: вдоль, вверх
TIE_R = 0.011       # радиус пробки стяжки


def _noise(rng, n, cells, octaves=4, gain=0.5):
    """Тайловый шум значений: сумма октав, интерполяция косинусом, заворот по краю."""
    out = np.zeros((n, n))
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        c = cells * (2 ** o)
        g = rng.standard_normal((c, c))
        t = (np.arange(n) * c / n)
        i0 = np.floor(t).astype(int) % c
        i1 = (i0 + 1) % c
        f = t - np.floor(t)
        f = (1 - np.cos(f * np.pi)) * 0.5
        a = g[i0][:, i0] * (1 - f)[None, :] + g[i0][:, i1] * f[None, :]
        b = g[i1][:, i0] * (1 - f)[None, :] + g[i1][:, i1] * f[None, :]
        out += amp * (a * (1 - f)[:, None] + b * f[:, None])
        tot += amp
        amp *= gain
    return out / tot


def _blur(a, r):
    k = 2 * r + 1
    out = a.copy()
    for axis in (0, 1):
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, axis)
        out = acc / k
    return out


def _pits(rng, n, count, r_lo, r_hi, depth, px):
    """Поры: случайные ямки-полусферы (радиус в метрах), с заворотом."""
    h = np.zeros((n, n))
    rs = rng.uniform(r_lo, r_hi, count)
    cx = rng.uniform(0, n, count)
    cy = rng.uniform(0, n, count)
    for x, y, r in zip(cx, cy, rs):
        rp = max(r / px, 0.8)
        s = int(np.ceil(rp)) + 1
        ys = (np.arange(int(y) - s, int(y) + s + 1)) % n
        xs = (np.arange(int(x) - s, int(x) + s + 1)) % n
        dy = (np.arange(int(y) - s, int(y) + s + 1) - y)[:, None]
        dx = (np.arange(int(x) - s, int(x) + s + 1) - x)[None, :]
        q = 1.0 - (dx * dx + dy * dy) / (rp * rp)
        d = -depth * (r / r_hi) * np.sqrt(np.clip(q, 0, 1))
        blk = h[np.ix_(ys, xs)]
        h[np.ix_(ys, xs)] = np.minimum(blk, d)
    return h


def wall(seed=11):
    """Стена по опалубке. Строка 0 — верх тайла. → (высота м, цвет линейный rgb, шероховатость)."""
    rng = np.random.default_rng(seed)
    n, px = N, SIZE / N
    y, x = np.mgrid[0:n, 0:n].astype(np.float64)
    up = (n - 1 - y) * px          # вверх, м
    al = x * px                    # вдоль, м
    board = np.floor(up / BOARD).astype(int) % int(round(SIZE / BOARD))
    nb = int(round(SIZE / BOARD))
    # Доски: каждая чуть выше или ниже соседей (ступенька 0.5–1.5 мм), слегка повёрнута поперёк
    off = rng.uniform(-0.0012, 0.0012, nb)
    tilt = rng.uniform(-0.0015, 0.0015, nb)
    fb = up / BOARD - np.floor(up / BOARD)
    h = off[board] + tilt[board] * (fb - 0.5)
    # Наплыв цементного молока на стыке досок: тонкий валик 0.6 мм, рваный по длине
    seam = np.minimum(fb, 1 - fb) * BOARD
    rag = 0.5 + 0.5 * _noise(rng, n, 24, 3)
    h += 0.0006 * np.exp(-(seam / 0.0016) ** 2) * (rag > 0.35)
    # Волокно дерева вдоль доски: вытянутый шум
    grain = _noise(rng, n, 6, 3)
    fib = _blur(rng.standard_normal((n, n)), 1)
    fib = _blur(fib.T, 0).T
    stretch = np.zeros_like(fib)
    for d in range(-24, 25, 4):   # вытягивание вдоль (оси x картинки)
        stretch += np.roll(fib, d, 1)
    h += 0.00022 * stretch / 13.0 + 0.0004 * grain
    # Стяжки: пробка 11 мм, утоплена на 6 мм, вокруг конус и тёмное кольцо
    tx, ty = TIE
    du = (al + tx / 2) % tx - tx / 2
    dv = (up + ty / 2 - 0.05) % ty - ty / 2
    r = np.sqrt(du * du + dv * dv)
    cone = np.clip(1.0 - r / (TIE_R * 1.9), 0, 1)
    h -= 0.006 * np.clip(cone * 2.0, 0, 1)
    h += 0.0015 * (r < TIE_R * 0.75)          # пробка торчит из ямки
    # Поры: много мелких и немного крупных раковин
    h += _pits(rng, n, 9000, 0.0004, 0.0016, 0.0012, px)
    h += _pits(rng, n, 500, 0.0015, 0.0045, 0.0025, px)
    h += 0.0003 * _noise(rng, n, 64, 2)
    # Цвет: серо-бежевый бетон, доски чуть разного тона, разводы, потёки от стяжек вниз, светлое молоко на стыках
    base = np.array([0.37, 0.355, 0.325])
    tone = 1.0 + rng.uniform(-0.05, 0.05, nb)[board]
    mott = 1.0 + 0.10 * _noise(rng, n, 5, 5)
    streak = np.zeros((n, n))
    below = (np.abs(du) < 0.012 + 0.01 * np.clip(-dv / 0.3, 0, 1)) & (dv < -0.01) & (dv > -0.35)
    streak[below] = 1.0
    streak = _blur(streak, 6) * np.clip(1 + dv / 0.35, 0, 1)
    col = base[None, None, :] * (tone * mott)[..., None]
    col = col * (1 - 0.18 * streak[..., None])
    col = col * (1 - 0.35 * (cone > 0.05)[..., None] * (r > TIE_R * 0.75)[..., None])
    col = col * (1 - 0.25 * (r < TIE_R * 0.75)[..., None]) + np.array([0.02, 0.018, 0.015]) * (r < TIE_R * 0.75)[..., None]
    lime = np.exp(-(seam / 0.003) ** 2) * (rag > 0.5)
    col = col * (1 + 0.12 * lime[..., None])
    rough = np.clip(0.86 + 0.06 * _noise(rng, n, 12, 3) + 0.05 * (h < -0.0006), 0.6, 0.98)
    return h, col, rough


def floor(seed=23):
    """Пол и проступи: затирка, мелкий заполнитель, вытертые до гладкости места (ниже шероховатость)."""
    rng = np.random.default_rng(seed)
    n, px = N, SIZE / N
    h = 0.0005 * _noise(rng, n, 8, 5)
    h += _pits(rng, n, 5000, 0.0003, 0.0012, 0.0008, px)
    # Заполнитель: светлые и тёмные зёрна щебня, проступившие через затирку
    agg = np.zeros((n, n))
    cnt = 2500
    xs = rng.integers(0, n, cnt)
    ys = rng.integers(0, n, cnt)
    v = rng.uniform(-1, 1, cnt)
    agg[ys, xs] = v
    agg = _blur(agg, 3) * 7.0
    h += 0.0002 * np.clip(agg, -1, 1)
    trowel = _noise(rng, n, 3, 4)
    base = np.array([0.34, 0.325, 0.30])
    col = base[None, None, :] * (1 + 0.08 * trowel + 0.10 * np.clip(agg, -1, 1))[..., None]
    rough = np.clip(0.8 + 0.08 * _noise(rng, n, 10, 3), 0.55, 0.95)
    return h, col, rough
