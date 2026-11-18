"""Атлас прядей для волос карточками (docs/hair.md, «Карточки»): тысячи волосков кривыми Blender (Curves), трассировка
Cycles сверху ортогональной камерой, три прохода — данные (корень→кончик, номер волоска, глубина), затенение
между волосками, нормаль. Из них — карты .tex: цвет (серый тон волоска и затенение, в альфе — покрытие), нормаль
(касательное пространство карточки: x — поперёк, y — к корню), маска (R — корень→кончик, G — номер волоска,
B — затенение, A — глубина).

    python3 tools/hairtex/strands.py <рабочая папка> <texpack> <data>

Нужен bpy 5.3 (pip install bpy==5.3.0). Вне CI, как make parts: текстуры коммитятся, пересобирает тот, кто менял
рецепт. Волоски — свой рецепт (CC0 по построению), случайность — свой splitmix64, а не numpy.

Атлас 1024 × 2048: восемь столбцов по 128 px, у каждого столбца корень наверху (v = 0), кончик внизу (v = 1).
Столбец — карточка шириной W и длиной L (1 : 16). Виды столбцов — COLUMNS."""
import os
import subprocess
import sys

import numpy as np

W_PX, H_PX = 1024, 2048
NCOL = 8
L = 0.48               # длина карточки, м (высота атласа)
W = L / 16             # ширина столбца, м
# Радиус волоска у корня и у кончика, м: толще настоящего (~0.04 мм), чтобы волосок был ~1 px атласа — тоньше он
# сливается в серую дымку без рисунка прядей
R_ROOT, R_TIP = 0.00013, 0.00004

# Столбцы: густота (волосков), сбор к оси пряди к кончику, доля укороченных, разлёт отбившихся, волна (м), семя
COLUMNS = [
    dict(n=300, clump=0.65, short=0.35, fly=0.02, wave=0.0015, seed="плотная-1"),
    dict(n=280, clump=0.80, short=0.45, fly=0.03, wave=0.0025, seed="плотная-2"),
    dict(n=260, clump=0.55, short=0.30, fly=0.02, wave=0.0010, seed="плотная-3"),
    dict(n=170, clump=0.85, short=0.50, fly=0.06, wave=0.0030, seed="средняя-1"),
    dict(n=150, clump=0.70, short=0.55, fly=0.08, wave=0.0020, seed="средняя-2"),
    dict(n=100, clump=0.95, short=0.60, fly=0.05, wave=0.0025, seed="кончики"),
    dict(n=40, clump=0.35, short=0.40, fly=0.25, wave=0.0040, seed="отбившиеся"),
    dict(n=520, clump=0.30, short=0.15, fly=0.00, wave=0.0008, seed="подложка"),
]


class Rng:
    """splitmix64 — одинаковые числа на любой машине"""

    def __init__(self, name):
        s = 0x9E3779B97F4A7C15
        for ch in name.encode():
            s = ((s ^ ch) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        self.s = s

    def u(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        z ^= z >> 31
        return (z >> 11) / float(1 << 53)


def column_strands(k, c, seg=48):
    """Волоски столбца k: точки (n × seg × 3) и радиусы (n × seg). Корень наверху (z = L/2), кончик вниз.
    Волоски собираются к осям 2–3 прядок к кончику (clump), часть короче (тоньше к низу), отбившиеся уходят вбок"""
    r = Rng(c["seed"])
    x0c = -L / 4 + W * (k + 0.5)       # середина столбца по x
    half = W * 0.42
    nclump = 2 + int(r.u() * 2)
    centers = [x0c + (r.u() * 2 - 1) * half * 0.55 for _ in range(nclump)]
    t = np.linspace(0.0, 1.0, seg)
    P, R = [], []
    for i in range(c["n"]):
        x0 = x0c + (r.u() * 2 - 1) * half
        y0 = (r.u() * 2 - 1) * 0.0018            # толщина пряди: ±1.8 мм вглубь
        length = 1.0 - (r.u() ** 0.7) * 0.55 if r.u() < c["short"] else 0.92 + 0.08 * r.u()
        cc = min(centers, key=lambda q: abs(q - x0))
        pull = c["clump"] * (0.6 + 0.4 * r.u())
        ph, fr = r.u() * 6.283, 3.0 + 4.0 * r.u()
        amp = c["wave"] * (0.5 + r.u())
        fly = c["fly"] * (r.u() ** 3) * (1 if r.u() < 0.5 else -1)
        s = t * length
        x = x0 + (cc - x0) * pull * s ** 1.5 + amp * np.sin(ph + fr * s * 6.283) * s + fly * W * 6 * s ** 2
        x = np.clip(x, x0c - W * 0.49, x0c + W * 0.49)   # в своём столбце
        y = y0 * (1.0 - 0.5 * s)
        z = L / 2 - s * L * 0.985 - L * 0.005
        P.append(np.stack([x, y, z], 1))
        R.append(R_ROOT + (R_TIP - R_ROOT) * t ** 1.5)
    return np.array(P), np.array(R)


def build_scene(bpy):
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    sc = bpy.context.scene
    allP, allR = [], []
    for k, c in enumerate(COLUMNS):
        P, R = column_strands(k, c)
        allP.append(P.reshape(-1, 3))
        allR.append(R.reshape(-1))
        n, seg = P.shape[0], P.shape[1]
        print("столбец %d (%s): волосков %d" % (k, c["seed"], n))
    P = np.concatenate(allP)
    Rr = np.concatenate(allR)
    seg = 48
    hc = bpy.data.hair_curves.new("strands")
    hc.add_curves([seg] * (len(P) // seg))
    hc.position_data.foreach_set("vector", P.astype(np.float32).ravel())
    rad = hc.attributes.get("radius") or hc.attributes.new("radius", "FLOAT", "POINT")
    rad.data.foreach_set("value", Rr.astype(np.float32))
    ob = bpy.data.objects.new("strands", hc)
    sc.collection.objects.link(ob)
    mat = bpy.data.materials.new("pass")
    hc.materials.append(mat)
    cam = bpy.data.cameras.new("cam")
    cam.type = "ORTHO"
    cam.ortho_scale = L
    co = bpy.data.objects.new("cam", cam)
    co.location = (0.0, -1.0, 0.0)
    co.rotation_euler = (np.pi / 2, 0.0, 0.0)   # смотрит вдоль +y, верх — +z
    sc.collection.objects.link(co)
    sc.camera = co
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 48
    sc.cycles.use_denoising = False
    sc.cycles.max_bounces = 0
    sc.render.resolution_x, sc.render.resolution_y = W_PX, H_PX
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = "OPEN_EXR"
    sc.render.image_settings.color_depth = "32"
    sc.view_settings.view_transform = "Standard"
    sc.cycles_curves.shape = "THICK"
    sc.cycles_curves.subdivisions = 2
    if sc.world is None:
        sc.world = bpy.data.worlds.new("w")
    return mat


def set_pass(mat, kind):
    """Материал прохода: излучение со значением прохода (свет не нужен, кроме затенения)"""
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(em.outputs[0], out.inputs["Surface"])
    comb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(comb.outputs[0], em.inputs["Color"])
    if kind == "data":
        hi = nt.nodes.new("ShaderNodeHairInfo")
        nt.links.new(hi.outputs["Intercept"], comb.inputs[0])
        nt.links.new(hi.outputs["Random"], comb.inputs[1])
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(geo.outputs["Position"], sep.inputs[0])
        mp = nt.nodes.new("ShaderNodeMapRange")   # глубина y: −2 мм (к камере) → 0, +2 мм → 1
        mp.inputs["From Min"].default_value = -0.002
        mp.inputs["From Max"].default_value = 0.002
        nt.links.new(sep.outputs[1], mp.inputs[0])
        nt.links.new(mp.outputs[0], comb.inputs[2])
    elif kind == "ao":
        ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
        ao.samples = 8
        ao.inputs["Distance"].default_value = 0.003
        nt.links.new(ao.outputs["AO"], comb.inputs[0])
        nt.links.new(ao.outputs["AO"], comb.inputs[1])
        nt.links.new(ao.outputs["AO"], comb.inputs[2])
    elif kind == "normal":
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(geo.outputs["Normal"], sep.inputs[0])
        # Касательное пространство карточки: x — вправо (+x мира), y — к корню (+z), z — к камере (−y)
        for i, (src, sign) in enumerate(((0, 1.0), (2, 1.0), (1, -1.0))):
            m = nt.nodes.new("ShaderNodeMath")
            m.operation = "MULTIPLY_ADD"
            m.inputs[1].default_value = 0.5 * sign
            m.inputs[2].default_value = 0.5
            nt.links.new(sep.outputs[src], m.inputs[0])
            nt.links.new(m.outputs[0], comb.inputs[i])


def render(bpy, path):
    sc = bpy.context.scene
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    im = bpy.data.images.load(path)
    a = np.array(im.pixels[:], np.float32).reshape(im.size[1], im.size[0], 4)[::-1]  # строки сверху вниз
    bpy.data.images.remove(im)
    return a


def dilate(rgb, cov, steps=64):
    """Цвет под пустыми пикселями — от соседей (мипы не тянут чёрное в край волоска)"""
    out = rgb.copy()
    have = cov > 1e-3
    for _ in range(steps):
        if have.all():
            break
        acc = np.zeros_like(out)
        cnt = np.zeros(have.shape, np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            h = np.roll(have, (dy, dx), (0, 1))
            acc += np.roll(out, (dy, dx), (0, 1)) * h[..., None]
            cnt += h
        new = ~have & (cnt > 0)
        out[new] = acc[new] / cnt[new][:, None]
        have = have | new
    return out


def save_png(path, a):
    from PIL import Image
    a = np.clip(np.rint(a * 255.0), 0, 255).astype(np.uint8)
    Image.fromarray(a, "RGBA" if a.shape[2] == 4 else "RGB").save(path)


def main():
    work, texpack, data = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    import bpy
    mat = build_scene(bpy)
    res = {}
    for kind in ("data", "ao", "normal"):
        set_pass(mat, kind)
        res[kind] = render(bpy, os.path.join(work, kind + ".exr"))
        print("проход %s готов" % kind)
    cov = np.clip(res["data"][..., 3], 0.0, 1.0)
    inv = 1.0 / np.maximum(cov, 1e-4)[..., None]
    d = res["data"][..., :3] * inv          # Cycles отдаёт цвет, умноженный на покрытие
    ao = np.clip(res["ao"][..., 0] / np.maximum(cov, 1e-4), 0.0, 1.0)
    nrm = res["normal"][..., :3] * inv
    tip, rid, depth = np.clip(d[..., 0], 0, 1), np.clip(d[..., 1], 0, 1), np.clip(d[..., 2], 0, 1)
    # Тон волоска: свой у каждого (±18 %), к корню темнее, затенение между волосками
    tone = (0.80 + 0.5 * (rid - 0.5)) * (0.78 + 0.22 * tip) * (0.30 + 0.70 * ao)
    color = dilate(np.stack([tone, tone, tone], -1), cov)
    mask = dilate(np.stack([tip, rid, ao, depth], -1), cov)
    n2 = dilate(nrm, cov)
    flat = np.array([0.5, 0.5, 1.0], np.float32)
    n2 = np.where((cov > 1e-3)[..., None], n2, flat)
    # Нормаль у края волоска — к плоской по покрытию (полупрозрачный край не блестит боком)
    n2 = flat + (n2 - flat) * np.clip(cov * 1.5, 0, 1)[..., None]
    save_png(os.path.join(work, "hair_strands_color.png"), np.concatenate([color, cov[..., None]], -1))
    save_png(os.path.join(work, "hair_strands_normal.png"), np.concatenate([n2, np.ones_like(cov)[..., None]], -1))
    save_png(os.path.join(work, "hair_strands_mask.png"), mask)
    for name, mode in (("color", "color"), ("normal", "normal"), ("mask", "linear")):
        src = os.path.join(work, "hair_strands_%s.png" % name)
        dst = os.path.join(data, "textures", "hair_strands_%s.tex" % name)
        subprocess.run([texpack, mode, src, dst], check=True)
    print("атлас прядей: покрытие %.2f, затенение %.2f..%.2f" % (cov.mean(), ao[cov > 0.5].min(), ao[cov > 0.5].max()))


if __name__ == "__main__":
    main()
