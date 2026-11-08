#!/bin/sh
# Исходники людей MakeHuman (CC0) и пересборка data/meshes/human_*.mesh, data/people/*.skel (docs/people.md).
#   tools/humanconv/fetch.sh <meshpack>        (зовёт make meshes)
# Качает в $ANA_CACHE/makehuman (по умолчанию ~/.cache/anathemyre), сверяет sha256. Нужны curl, python3, numpy.
# Сетка, скелеты и веса — MPFB2 (закреплённая версия), морфы — пакет makehuman с PyPI (там они сжаты в .npz)
set -e
cd "$(dirname "$0")/../.."
MESHPACK=${1:?нужен путь к meshpack}
DL=${ANA_CACHE:-$HOME/.cache/anathemyre}/makehuman
mkdir -p "$DL" data/people
MPFB=https://raw.githubusercontent.com/makehumancommunity/mpfb2/6ff1eb358f6c1959ced77e79e6dd2406d9a71f49/src/mpfb/data
WHL=https://files.pythonhosted.org/packages/52/7b/d3d997e4b8ee3b16af4d5e9ff5994af82c6467db18fa50e535bb057ff077/makehuman-1.3.2-py3-none-any.whl

get() {  # get <файл> <url> <sha256>
    if [ ! -f "$DL/$1" ] || [ "$(sha256sum "$DL/$1" | cut -d' ' -f1)" != "$3" ]; then
        curl -fsSL --retry 3 -o "$DL/$1.part" "$2"
        [ "$(sha256sum "$DL/$1.part" | cut -d' ' -f1)" = "$3" ] || { echo "humanconv: $1 — не та sha256"; exit 1; }
        mv "$DL/$1.part" "$DL/$1"
    fi
}
get base.obj "$MPFB/3dobjs/base.obj" 8e761e6624b8f54536409135d1636da63b32486a90d4897f84e121d144f6fb4c
get rig.game_engine.json "$MPFB/rigs/standard/rig.game_engine.json" b324ddb4b707721c19630bfadb87c3e3b38eed3b6e4590bb3cead43ea918f799
get weights.game_engine.json "$MPFB/rigs/standard/weights.game_engine.json" 9f4a773e74ce5ba08415b4f30070338e9f75bae5971b5075b10cd6b9caa58473
get rig.mixamo.json "$MPFB/rigs/standard/rig.mixamo.json" b4e491bacdcae797e53b7692169cf6a160c50fcc1eda5f0982e92fac1561ae56
get weights.mixamo.json "$MPFB/rigs/standard/weights.mixamo.json" 4561be4d7c0093d70e3dba4fbacbbbb6d66781573779fb12a0509cefa181b8ae
get makehuman.whl "$WHL" 384f0553a66659be95ff05b7c2d2d837376062f1d525dbd8920b029c4c6fa85e
[ -f "$DL/targets.npz" ] || python3 -I -c "import sys, zipfile; open(sys.argv[2], 'wb').write(zipfile.ZipFile(sys.argv[1]).read('makehuman/data/targets.npz'))" "$DL/makehuman.whl" "$DL/targets.npz"
python3 -I tools/humanconv/humanconv.py "$DL" data "$MESHPACK"
