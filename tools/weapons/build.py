#!/usr/bin/env python3
"""Оружие (docs/weapons.md): python3 tools/weapons/build.py <data> <meshpack> [имена…]

Пишет data/meshes/weapon_<имя>.mesh и описание data/weapons.txt (хват, ношение, длины — из той же таблицы и
границ сетки). Чистый Python без Blender, поэтому идёт в `make meshes`, и CI сверяет байты.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import onehand  # noqa: E402
import poles  # noqa: E402
import twohand  # noqa: E402
import uniques  # noqa: E402
import wkit  # noqa: E402

ITEMS = {}
for mod in (onehand, twohand, poles, uniques):
    ITEMS.update(mod.ITEMS)

# Хват и ношение (договор с видом рук, docs/weapons.md «Описание»). Все y — по оси сетки от кулака верхней руки,
# м. grip2 — вторая кисть (0 — одна рука); carry — место на теле: hip_l (пояс слева), back (за спиной, рукоять над
# правым плечом), back_pole (древко за спиной наискось); carry_at — точка подвеса на оси (None — середина вещи).
# issue — вес в выборе оружия человеку по зерну (0 — не выдаётся). Семейство — навык беты, пока справка.
TABLE = [
    # имя       семейство   руки    grip2   carry        carry_at  issue
    ("zatochka", "ШИЛО",     "1",    0.0,   "hip_l",     0.05,     1.0),
    ("rezak",    "ШИЛО",     "1",    0.0,   "hip_l",     0.06,     1.0),
    ("ressora",  "ЛИСТ",     "1",    0.0,   "hip_l",     0.06,     1.0),
    ("polosa",   "ЛИСТ",     "1",    0.0,   "hip_l",     0.06,     1.0),
    ("lopast",   "ЛИСТ",     "1",    0.0,   "hip_l",     0.075,    0.3),
    ("sekach",   "КОЛУН",    "1",    0.0,   "hip_l",     0.08,     1.0),
    ("teslo",    "КОЛУН",    "1",    0.0,   "hip_l",     0.52,     1.0),
    ("dolgaya",  "ДВУХВАТ",  "2",   -0.17,  "back",      0.07,     1.0),
    ("balka",    "ДВУХВАТ",  "2",   -0.14,  "back",      0.1,      1.0),
    ("otval",    "ГНЁТ",     "2",   -0.2,   "back",      0.1,      1.0),
    ("resheto",  "ГНЁТ",     "2",   -0.15,  "back",      0.1,      0.3),
    ("bagor",    "ЖЕРДЬ",    "pole", -0.62, "back_pole", None,     1.0),
    ("kosa",     "ЖЕРДЬ",    "pole", -0.62, "back_pole", None,     1.0),
    ("truba",    "ЖЕРДЬ",    "pole", -0.62, "back_pole", None,     1.0),
    ("vereteno", "ЖЕРДЬ",    "pole", -0.4,  "back_pole", None,     0.3),
]

HEAD = """# Оружие (docs/weapons.md). Файл пишет tools/weapons/build.py (make meshes) из своей таблицы и границ сеток:
# править там, CI сверяет. Только вид: шаг мира оружия пока не знает, человеку его выбирает вид по зерну.
#
# weapon <имя>          сетка meshes/weapon_<имя>.mesh. Оси сетки: начало — середина кулака верхней руки (у гарды
#                       или выше по древку), остриё по +Y, лезвие по +X, толщина по Z
#   family <слово>      семейство-навык (справка, бой его пока не читает)
#   hands 1|2|pole      одна рука, две на рукояти, древко
#   grip2 <y>           где вторая кисть по оси, м (0 — одна рука)
#   carry <место> <y>   где носят (hip_l — пояс слева, back — за спиной, back_pole — древко за спиной) и точка
#                       подвеса на оси, м
#   tip <y>  butt <y>   остриё (или верх) и торец рукояти по оси, м
#   issue <вес>         вес в выборе оружия по зерну человека, 0 — не выдаётся
"""


def fmt(v):
    s = "%.3f" % v
    s = s.rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def main():
    if len(sys.argv) < 3:
        sys.exit("использование: build.py <data> <meshpack> [имена]")
    data, meshpack, only = sys.argv[1], sys.argv[2], sys.argv[3:]
    out = [HEAD]
    for name, family, hands, grip2, carry, carry_at, issue in TABLE:
        w, fist = ITEMS[name]()
        tris = wkit.build(w, fist)
        bad = wkit.open_edges(tris)
        lo, hi = wkit.extent(tris)
        if not only or name in only:
            print("weapon_%-10s треугольников %5d  незакрытых рёбер %d  длина %.2f м" % (name, len(tris), bad, hi[1] - lo[1]))
            if bad:
                sys.exit("сетка weapon_%s не замкнута" % name)
            wkit.write("weapon_" + name, tris, os.path.join(data, "meshes"), meshpack)
        at = (hi[1] + lo[1]) / 2 if carry_at is None else carry_at
        out.append("\nweapon %s\n    family %s\n    hands %s\n    grip2 %s\n    carry %s %s\n    tip %s\n    butt %s\n"
                   "    issue %s\n" % (name, family, hands, fmt(grip2), carry, fmt(at), fmt(hi[1]), fmt(lo[1]), fmt(issue)))
    with open(os.path.join(data, "weapons.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(out))


if __name__ == "__main__":
    main()
