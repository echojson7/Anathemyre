"""Общее для оружия (docs/weapons.md): лофт по сечениям, обмотки, заклёпки, гайки, вывод в .mesh.

Чистый Python без Blender: каждая деталь — замкнутое тело (kit.Body), детали одной вещи могут входить друг в
друга (рукоять в гарду), булевых операций нет. Одно описание — те же байты на любом запуске.

Оси скрипта (как у скриптов беты, tools/weaponconv склада): вдоль вещи +X (к острию), толщина — Y, вверх — Z
(лезвие у клинков вниз, к −Z). Вывод переводит в оси игры: начало — середина кулака верхней руки (у гарды),
остриё по +Y, лезвие по +X, толщина по Z.
"""
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(HERE, "..", "parts"))
sys.path.append(os.path.join(HERE, ".."))
from kit import Body, seg_for, unit  # noqa: E402
from export import build_mesh, canonical, open_edges, triangles  # noqa: E402

# Материалы игры (data/materials.txt, раздел «Оружие»): ключ скрипта → имя
MAT = {
    "steel": "wpn_steel",       # клинок: плоскость
    "edge": "wpn_edge",         # подводка, заточенная кромка
    "forged": "wpn_forged",     # тёмная кованая сталь (тесак, балка, отвал)
    "file": "wpn_file",         # напильник
    "iron": "wpn_iron",         # чернёное железо: гарды, втулки, навершия
    "dark": "wpn_dark",         # воронёные мелкие детали
    "pipe": "wpn_pipe",         # водопроводная труба
    "rust": "wpn_rust",         # ржавчина: шов, приварка
    "wire": "wpn_wire",         # проволока, заклёпки
    "leather": "wpn_leather",
    "wood": "wpn_wood",
    "rag": "wpn_rag",
    "cord": "wpn_cord",
    "tape": "wpn_tape",
    "textolite": "wpn_textolite",
    "hole": "wpn_hole",         # отверстие: тёмный вкладыш
    "glyph": "wpn_glyph",       # тусклые знаки лопатки
}


def lerp(a, b, t): return a + (b - a) * t


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def saw(u):
    """Пила витка обмотки: медленный подъём к краю, резкий спуск (виток внахлёст)."""
    f = u - math.floor(u)
    return f if f < 0.75 else (1 - f) * 3


def spline(ctrl, n):
    """Катмулл-Ром по опорным точкам, n точек на участок."""
    out = []
    c = [ctrl[0]] + list(ctrl) + [ctrl[-1]]
    for i in range(1, len(c) - 2):
        p0, p1, p2, p3 = c[i - 1], c[i], c[i + 1], c[i + 2]
        for k in range(n):
            t = k / n
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in range(len(p1))))
    out.append(tuple(ctrl[-1]))
    return out


# Число точек круглых сечений внутри одной протяжки (Weapon.loft): первый проход собирает, сколько нужно по
# допуску хорды, второй строит все сечения с этим числом — у колец одной протяжки число точек одно
_RING = {"auto": False, "need": 0, "force": 0}


def ring(r, n=16, m=0, rz=None, phase=0.0):
    """Сечение-эллипс: полуоси r по Y и rz по Z. В круглой протяжке (shade smooth или bands) n — нижняя
    граница, сверх неё — по допуску хорды (kit.seg_for); в гранёной (strips, flat) — ровно n."""
    rz = r if rz is None else rz
    if _RING["auto"]:
        if _RING["force"]:
            n = max(n, _RING["force"])
        else:
            n = max(n, seg_for(max(r, rz)))
            _RING["need"] = max(_RING["need"], n)
    return [(r * math.cos(phase + 2 * math.pi * i / n), rz * math.sin(phase + 2 * math.pi * i / n), m) for i in range(n)]


def diamond(zt, zr, zb, T, e=0.0006, band=0.0):
    """Сечение обоюдоострого клинка: кромки сверху и снизу, ребро на zr; band — доля от кромки до ребра под
    заточкой (материал 1 — грани у кромок)."""
    pts = [(e, zt)]
    if band:
        pts.append((T * band, lerp(zt, zr, band)))
    pts.append((T, zr))
    if band:
        pts.append((T * band, lerp(zb, zr, band)))
    pts.append((e, zb))
    out = pts + [(-y, z) for y, z in reversed(pts)]
    n = len(out)
    res = []
    for i in range(n):
        a, b = out[i], out[(i + 1) % n]
        m = 1 if band and (abs(a[0]) <= e * 1.01 or abs(b[0]) <= e * 1.01) else 0
        res.append((a[0], a[1], m))
    return res


def edge_sec(zt, zb, T, band, e=0.0004):
    """Сечение однолезвийного клинка: обух сверху (zt), лезвие снизу (zb), спуск высотой band с обеих сторон
    (грань спуска — материал 1)."""
    zb2 = min(zb + band, zt - 0.0005)
    return [(-T, zt, 0), (T, zt, 0), (T, zb2, 1), (e, zb, 0), (-e, zb, 1), (-T, zb2, 0)]


class Weapon:
    """Одна вещь: тело в осях скрипта и группы сглаживания граней.

    Группа грани (поле «гладкая» у kit.Body): 0 — плоская, иначе номер группы; нормаль угла делится только
    с гранями той же группы. Клинок — полосы вдоль длины (ребро и подводка остаются резкими поперёк),
    обмотка — пояса поперёк (виток читается), рукоять и кольца — одна группа (круглые)."""

    def __init__(self):
        self.body = Body()
        self.group = 0

    def mark(self):
        """Сколько граней сейчас: начало участка для apply()."""
        return len(self.body.faces)

    def apply(self, mark, f):
        """Перенести точки граней с mark до конца функцией f(p) → p (поворот детали, сделанной вдоль X)."""
        self.body.faces[mark:] = [(tuple(f(p) for p in pts), m, g) for pts, m, g in self.body.faces[mark:]]

    def _g(self):
        self.group += 1
        return self.group

    def loft(self, xs, section, mats, shade="strips", cap0=True, cap1=True):
        """Тело протяжкой: section(x) → [(y, z, мат)] одинаковой длины, мат — номер в mats грани после точки.
        shade: strips — полосы вдоль, bands — пояса поперёк, smooth — круглое, flat — плоские грани."""
        _RING.update(auto=shade in ("smooth", "bands"), need=0, force=0)
        try:
            secs = [section(x) for x in xs]
            if _RING["auto"] and (_RING["need"] or len({len(q) for q in secs}) > 1):
                _RING["force"] = _RING["need"]
                secs = [section(x) for x in xs]
        finally:
            _RING.update(auto=False, need=0, force=0)
        rings = [[((x, y, z), mats[m]) for y, z, m in sec] for x, sec in zip(xs, secs)]
        self.rings(rings, shade, cap0, cap1)

    def rings(self, rings, shade="strips", cap0=True, cap1=True):
        """Тело по готовым кольцам точек: [[(точка, ключ материала грани после неё)]], кольца одной длины."""
        n = len(rings[0])
        base = self._g() if shade == "smooth" else 0
        strips = [self._g() for _ in range(n)] if shade == "strips" else None
        faces = []
        for ra, rb in zip(rings, rings[1:]):
            band = self._g() if shade == "bands" else 0
            for i in range(n):
                k = (i + 1) % n
                g = strips[i] if strips else (band or base)
                faces.append(((ra[i][0], ra[k][0], rb[k][0], rb[i][0]), MAT[ra[i][1]], g))
        if cap0:
            faces.append((tuple(p for p, _ in reversed(rings[0])), MAT[rings[0][0][1]], 0))
        if cap1:
            faces.append((tuple(p for p, _ in rings[-1]), MAT[rings[-1][0][1]], 0))
        self.body._closed(faces)

    def wrap(self, x0, x1, r, rz, mat, step=0.016, bulge=0.0015, z=0.0, y=0.0, n=16, per=5):
        """Обмотка внахлёст: каждый виток чуть толще к краю (пила), per станций на виток."""
        cnt = max(2, int(round((x1 - x0) / step * per)))
        xs = [x0 + (x1 - x0) * i / cnt for i in range(cnt + 1)]

        def sec(x):
            b = bulge * saw((x - x0) / step) + 0.0004 * math.sin(x * 397.0)
            return [(yy + y, zz + z, m) for yy, zz, m in ring(r + b, n, 0, rz + b)]
        self.loft(xs, sec, [mat], shade="bands")

    def profile(self, prof, mat, n=16, phase=0.0, shade="smooth", z=0.0):
        """Тело вращения вокруг оси вещи по профилю [(x, r)]: две точки в одном x — ступень (резьба, кольцо)."""
        m = self.mark()
        self.loft(list(range(len(prof))), lambda i: [(yy, zz + z, m) for yy, zz, m in ring(prof[i][1], n, 0, phase=phase)],
                  [mat], shade=shade)
        self.apply(m, lambda p: (prof[int(round(p[0]))][0], p[1], p[2]))

    def rod(self, x0, x1, r, mat, rz=None, n=12, y=0.0, z=0.0, phase=0.0, shade="smooth"):
        self.loft([x0, x1], lambda x: [(yy + y, zz + z, m) for yy, zz, m in ring(r, n, 0, rz, phase)], [mat], shade=shade)

    def slab(self, x0, x1, y0, y1, z0, z1, mat, bevel=0.0):
        """Брусок по границам; bevel — фаска по контуру сечения YZ (рёбра вдоль X остаются)."""
        b = min(bevel, (y1 - y0) * 0.4, (z1 - z0) * 0.4)
        if b > 0:
            # скругление четвертью круга из 4 отрезков на каждом углу (не одна фаска)
            sec = []
            for cy, cz, a0 in ((y1 - b, z1 - b, 0.5), (y1 - b, z0 + b, 0.0), (y0 + b, z0 + b, -0.5), (y0 + b, z1 - b, -1.0)):
                for k in range(5):
                    a = math.pi * (a0 - 0.5 * k / 4)
                    sec.append((cy + b * math.cos(a), cz + b * math.sin(a)))
            self.loft([x0, x1], lambda x: [(yy, zz, 0) for yy, zz in sec], [mat], shade="smooth", cap0=True, cap1=True)
        else:
            sec = [(y0, z1), (y1, z1), (y1, z0), (y0, z0)]
            self.loft([x0, x1], lambda x: [(yy, zz, 0) for yy, zz in sec], [mat], shade="flat")

    def boxc(self, c, size, mat, bevel=0.0015):
        """Брусок с фаской на всех рёбрах (kit.Body.box), середина c, размеры size — в осях скрипта."""
        b = Body()
        b.box(c, size, MAT[mat], bevel=bevel)
        g = self._g()
        self.body.faces += [(p, m, g) for p, m, _ in b.faces]

    def tube(self, pts, r, mat, seg=10):
        b = Body()
        b.tube(pts, r, MAT[mat], seg=seg)
        g = self._g()
        self.body.faces += [(p, m, g if s else 0) for p, m, s in b.faces]

    def torus(self, c, axis, R, r, mat, seg=28, rseg=8):
        """Кольцо: середина c, ось axis ('x' | 'y' | 'z'), радиус R, сечение r. seg и rseg — нижние границы,
        сверх них — по допуску хорды."""
        seg = max(seg, seg_for(R + r))
        out = []
        for k in range(seg + 1):
            a = 2 * math.pi * k / seg
            u, v = R * math.cos(a), R * math.sin(a)
            p = {"x": (0.0, u, v), "y": (u, 0.0, v), "z": (u, v, 0.0)}[axis]
            out.append((c[0] + p[0], c[1] + p[1], c[2] + p[2]))
        out[-1] = out[0]
        b = Body()
        b.tube(out, r, MAT[mat], seg=rseg)
        g = self._g()
        self.body.faces += [(p, m, g if s else 0) for p, m, s in b.faces]

    def disc_y(self, x, z, y, r, depth, mat, seg=12, phase=0.0, exact=False):
        """Цилиндр поперёк вещи (ось Y): отверстие-вкладыш, шляпка болта, колесо навершия. seg — нижняя граница
        (сверх неё — по допуску хорды), exact — гранёный по замыслу (гайка), ровно seg."""
        seg = seg if exact else max(seg, seg_for(r))
        pts = [(x + r * math.cos(phase + 2 * math.pi * k / seg), z + r * math.sin(phase + 2 * math.pi * k / seg))
               for k in range(seg)]
        y0, y1 = y - depth / 2, y + depth / 2
        a = [(px, y0, pz) for px, pz in pts]
        b = [(px, y1, pz) for px, pz in pts]
        g = self._g()
        faces = [((a[k], a[(k + 1) % seg], b[(k + 1) % seg], b[k]), MAT[mat], g) for k in range(seg)]
        faces.append((tuple(reversed(a)), MAT[mat], 0))
        faces.append((tuple(b), MAT[mat], 0))
        self.body._closed(faces)

    def dome_y(self, x, z, y, r, h, mat, side=-1, seg=12):
        """Головка заклёпки или болта: купол поперёк вещи, основание на y, вершина на y + side·h."""
        seg = max(seg, seg_for(r))
        prof = [(1.0, 0.0), (0.92, 0.45), (0.7, 0.8), (0.38, 0.97), (0.0, 1.0)]
        rings = []
        for k, (rr, hh) in enumerate(prof):
            rings.append([(x + r * rr * math.cos(2 * math.pi * i / seg), y + side * (h * hh - h * 0.35),
                           z + r * rr * math.sin(2 * math.pi * i / seg)) for i in range(seg)])
        g = self._g()
        faces = []
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(seg):
                faces.append(((r0[i], r0[(i + 1) % seg], r1[(i + 1) % seg], r1[i]), MAT[mat], g))
        faces.append((tuple(reversed(rings[0])), MAT[mat], 0))
        self.body._closed(faces)

    def nut_y(self, x, z, y, r, mat):
        """Шестигранная гайка поперёк вещи (ось Y)."""
        self.disc_y(x, z, y, r, r * 0.8, mat, seg=6, phase=math.pi / 6, exact=True)

    def plate(self, outline, y0, y1, mat, mat_rim=None):
        """Пластина по контуру в плоскости XZ (простой многоугольник), толщина от y0 до y1."""
        a = [(px, y0, pz) for px, pz in outline]
        b = [(px, y1, pz) for px, pz in outline]
        n = len(outline)
        g = 0
        faces = [((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]), MAT[mat_rim or mat], g) for k in range(n)]
        faces.append((tuple(reversed(a)), MAT[mat], 0))
        faces.append((tuple(b), MAT[mat], 0))
        self.body._closed(faces)


# ---------------------------------------------------------------- вывод
def to_game(p, fist):
    """Оси скрипта → оси игры: X ← −(z − fz) (лезвие), Y ← x − fx (к острию), Z ← −y. Поворот, не зеркало."""
    fx, fz = fist
    return (-(p[2] - fz), p[0] - fx, -p[1])


def build(w, fist):
    """Тело вещи в оси игры → треугольники в каноническом порядке."""
    b = Body()
    b.faces = [(tuple(to_game(p, fist) for p in pts), m, g) for pts, m, g in w.body.faces]
    return canonical(triangles(b))


def mesh_of(tris):
    """Треугольники → Mesh: группа грани (0 — плоская) — группа сглаживания, нормали взвешенные
    (export.build_mesh: плоская грань остаётся плоской до кромки)."""
    return build_mesh(tris)


def write(name, tris, out_dir, meshpack):
    """Треугольники → <out_dir>/<name>.mesh через meshpack (LOD и кластеры). Коллайдера у оружия нет."""
    m = mesh_of(tris)
    raw = os.path.join(out_dir, name + ".raw")
    with open(raw, "wb") as f:
        f.write(m.encode())
    try:
        subprocess.run([meshpack, raw, os.path.join(out_dir, name + ".mesh")], check=True)
    finally:
        os.remove(raw)


def extent(tris):
    """Границы по осям игры: (min, max) по X, Y, Z."""
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for t in tris:
        for p in t[:3]:
            for k in range(3):
                lo[k] = min(lo[k], p[k])
                hi[k] = max(hi[k], p[k])
    return lo, hi


__all__ = ["Weapon", "lerp", "smooth", "saw", "spline", "ring", "diamond", "edge_sec", "build", "write", "extent",
           "open_edges", "unit", "math"]
