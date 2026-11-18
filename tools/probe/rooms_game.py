"""Пробные комнаты в игру (docs/assets_rooms.md): те же четыре способа, что в rooms.py, → сетки .mesh фабрики.

    python rooms_game.py <папка_сканов> <data/meshes> <meshpack> [способы…]

На каждый способ сетка room_<способ> (бетон, с коллайдером); наполнение (трубы, лампы, перила, обломки) —
room_now_fill у «как сейчас» и общее room_fill у остальных. Оси игры: x — восток, y — вверх, z — север; Blender (x, y, z) → игра (x, z, −y). Печатает строки
part и light заготовки data/prefabs/rooms.txt.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCANS, OUT, MESHPACK = sys.argv[1], sys.argv[2], sys.argv[3]
ONLY = sys.argv[4:] or ["now", "kit", "cast", "hybrid"]
# rooms.py читает аргументы при загрузке: сканы, папка кадров, способ
sys.argv = [sys.argv[0], SCANS, "/tmp", "now"]
os.environ.setdefault("VOXEL", "0.03")
os.environ.setdefault("DECIMATE", "0.15")  # ~250 тыс. треугольников на отливку: проба, не игровая модель
sys.path.insert(0, HERE)
for p in (os.path.join(HERE, "..", "parts"), os.path.join(HERE, "..", "..", "tools", "parts")):
    if os.path.isdir(p):
        sys.path.insert(0, p)
import bpy  # noqa: E402
import rooms as R  # noqa: E402
import export  # noqa: E402

# имя материала Blender → материал игры (data/materials.txt)
MAT = {"wall": "concrete", "floor": "concrete_floor", "rust": "rust_metal", "paint": "steel_paint",
       "steel": "steel_galv", "lamp": "lamp_glass", "sand": "sand"}
GAP = 12.0  # шаг комнат по z игры
LIFT = 0.05  # пол комнаты над землёй площадки (без мерцания пола с землёй)


class Tris:
    """Тело для export.write: грани — уже треугольники."""

    def __init__(self):
        self.faces = []
        self.lights = []


def g(v):
    """Blender → игра."""
    return (v[0], v[2], -v[1])


def collect(obs):
    body = Tris()
    dg = bpy.context.evaluated_depsgraph_get()
    for ob in obs:
        if ob.type not in ('MESH', 'CURVE') or ob.hide_render:
            continue
        e = ob.evaluated_get(dg)
        me = e.to_mesh()
        me.calc_loop_triangles()
        mw = ob.matrix_world
        names = [m.name.split(".")[0] if m else "wall" for m in me.materials] or ["wall"]
        vs = [g(mw @ v.co) for v in me.vertices]
        for t in me.loop_triangles:
            a, b, c = (vs[i] for i in t.vertices)
            mat = MAT.get(names[min(t.material_index, len(names) - 1)], "concrete")
            # обход: Blender — против часовой снаружи; поворот осей (x, z, −y) его сохраняет
            body.faces.append(((a, b, c), mat, t.use_smooth))
        e.to_mesh_clear()
    return body


def colliders():
    """Коробки бетона в осях игры: стены разрезаны у проёма двери (щель под потолком коллайдеру не нужна)."""
    out = []
    for (name, x0, x1, y0, y1, z0, z1, mat, kind) in R.concrete_volumes():
        if name == "wall_right":
            for a, b, h0, h1 in ((y0, R.DOOR[0], z0, z1), (R.DOOR[1], y1, z0, z1), (R.DOOR[0], R.DOOR[1], R.DOOR[2], z1)):
                out.append((x0, x1, a, b, h0, h1))
        elif kind != "stair":
            out.append((x0, x1, y0, y1, z0, z1))
    for k in range(R.STAIR[2]):  # ступень — коробка на проступь
        x0 = R.STAIR[0] + k * R.RUN
        out.append((x0, x0 + R.RUN, R.STAIR[1], R.D, 0.0, (k + 1) * R.RISE))
    return [(x0, x1, z0, z1, -y1, -y0) for (x0, x1, y0, y1, z0, z1) in out]


def lamp_lines(dz):
    """Две лампы дневного света комнаты (как в rooms.props): строки light заготовки."""
    W, D = R.W, R.D
    out = []
    for (lx, ly, lz, ax) in ((2.4, D - 0.08, 2.9, 'x'), (W - 0.08, 4.2, 2.9, 'y')):
        if ax == 'x':
            c, axis, face = g((lx, ly - 0.1, lz - 0.03)), (0.6, 0, 0), (0, 0, 1)
        else:
            c, axis, face = g((lx - 0.1, ly, lz - 0.03)), (0, 0, 0.6), (-1, 0, 0)
        c = (c[0], c[1] + LIFT, c[2] + dz)
        out.append("light " + " ".join(export_num(v) for v in (*c, *axis, *face, 0.1, 1800, 4000, 0.017)))
    return out


def export_num(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def main():
    lines = []
    for k, mode in enumerate(["now", "kit", "cast", "hybrid"]):
        dz = -GAP * k
        lines.append(f"part room_{mode} 0 {LIFT} {export_num(dz)}")
        fill_name = "room_now_fill" if mode == "now" else "room_fill"
        lines.append(f"part {fill_name} 0 {LIFT} {export_num(dz)}")
        lines += [ln + f"  # room_{mode}" for ln in lamp_lines(dz)]
        if mode not in ONLY:
            continue
        t0 = time.time()
        R.reset()
        M = R.materials("now")  # для сетки нужны только имена материалов
        shell = {"now": R.build_now, "kit": R.build_kit, "cast": R.build_cast, "hybrid": R.build_hybrid}[mode](M)
        fill = R.props(M, mode != "now") + R.debris(M, mode != "now")
        for name, obs, boxes in ((f"room_{mode}", shell, colliders()), (fill_name, fill, ())):
            n, bad = export.write(name, collect(obs), OUT, MESHPACK, boxes)
            print("%-18s треугольников %8d  незакрытых рёбер %d  коробок %d  %.0f с" % (name, n, bad, len(boxes),
                                                                                       time.time() - t0))
    print("\n".join(lines))


main()
