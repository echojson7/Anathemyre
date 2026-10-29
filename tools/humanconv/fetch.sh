#!/bin/sh
# Скачать исходники MakeHuman (CC0) и пересобрать data/meshes/mh_*.mesh.
#   tools/humanconv/fetch.sh [папка для загрузки]
# Нужны curl, python3, numpy
set -e
cd "$(dirname "$0")/../.."
DL=${1:-build/makehuman}
mkdir -p "$DL/mh" data/meshes
R=https://raw.githubusercontent.com/makehumancommunity/makehuman/master/makehuman/data
for f in 3dobjs/base.obj rigs/default.mhskel rigs/default_weights.mhw; do
    [ -f "$DL/mh/$(basename $f)" ] || curl -fsSL -o "$DL/mh/$(basename $f)" "$R/$f"
done
# Морфы в сжатом виде есть только в пакете PyPI
if [ ! -f "$DL/targets.npz" ]; then
    curl -fsSL -o "$DL/mh.whl" https://files.pythonhosted.org/packages/52/7b/d3d997e4b8ee3b16af4d5e9ff5994af82c6467db18fa50e535bb057ff077/makehuman-1.3.2-py3-none-any.whl
    python3 -I -c "import sys, zipfile; open(sys.argv[2], 'wb').write(zipfile.ZipFile(sys.argv[1]).read('makehuman/data/targets.npz'))" "$DL/mh.whl" "$DL/targets.npz"
fi
python3 -I tools/humanconv/humanconv.py "$DL/mh" "$DL/targets.npz" data/meshes
