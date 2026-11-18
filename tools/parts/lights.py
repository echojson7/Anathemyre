"""Строки light заготовок (огни 6е, docs/lamps.md) из ламп рецептов: без Blender, по тем же функциям, что и сетки.

    python3 tools/parts/lights.py            переписать блок ламп в data/prefabs/<имя>.txt
    python3 tools/parts/lights.py --check    только сверить (код 1, если блок устарел)

Блок — между строками-метками BEGIN и END в конце заготовки; остальное в файле не трогается.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ispod_meshes  # noqa: E402

BEGIN = "# --- лампы: tools/parts/lights.py, руками не править ---"
END = "# --- конец ламп ---"
# Сетки с лампами (бетон ispod_shell ламп не держит и без Blender не строится)
PREFABS = {"ispod": {k: ispod_meshes.MESHES[k] for k in ("ispod_tech", "ispod_hall", "ispod_track")}}


def num(v):
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def block(meshes):
    lines = [BEGIN, "#   light <x> <y> <z> <ax> <ay> <az> <fx> <fy> <fz> <назад> <лм> <К> <радиус> [<r> <g> <b>]"]
    for name in sorted(meshes):
        body, _ = meshes[name]()
        for c, axis, face, back, lm, kelvin, size, rgb in body.lights:
            vals = (*c, *axis, *face, back, lm, kelvin, size, *(rgb or ()))
            lines.append("light " + " ".join(num(v) for v in vals) + f"  # {name}")
    lines.append(END)
    return "\n".join(lines) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "prefabs")
    bad = False
    for prefab, meshes in PREFABS.items():
        path = os.path.join(root, prefab + ".txt")
        text = open(path, encoding="utf-8").read()
        head = text.split(BEGIN)[0].rstrip("\n") + "\n\n"
        new = head + block(meshes)
        if new == text:
            continue
        bad = True
        if check:
            print(f"{path}: блок ламп устарел (python3 tools/parts/lights.py)")
        else:
            open(path, "w", encoding="utf-8").write(new)
            print(f"{path}: лампы переписаны")
    sys.exit(1 if bad and check else 0)


if __name__ == "__main__":
    main()
