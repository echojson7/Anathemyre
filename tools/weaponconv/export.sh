#!/bin/sh
# Выгрузка всех моделей оружия в data/meshes/weapons (нужен Blender 4.x, без окна)
set -e
cd "$(dirname "$0")"
OUT=../../data/meshes/weapons
mkdir -p "$OUT"
for f in resheto vereteno lopast twohand onehand polearms; do
    EXPORT="$OUT" NORENDER=1 blender -b -P "$f.py" -- /tmp/weaponconv.png 1
done
