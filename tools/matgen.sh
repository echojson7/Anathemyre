#!/bin/sh
# Виды материалов (docs/materials.md) по файлам data/shaders/materials/<вид>.glsl, в порядке имён.
# Из одного списка — switch в шейдере и номера видов для игры, поэтому они не расходятся:
#   <out>/shaders/gen/materials.glsl   #include всех видов и surface_kind()
#   <out>/data/material_kinds.txt      имена видов по строке, номер = строка
# Файл переписывается только при изменении (иначе make пересобирал бы шейдеры каждый раз).
# ./tools/matgen.sh build/<цель>
set -e
cd "$(dirname "$0")/.."
out=$1
[ -n "$out" ] || { echo "использование: tools/matgen.sh build/<цель>" >&2; exit 2; }
mkdir -p "$out/shaders/gen" "$out/data"
kinds=$(ls data/shaders/materials/*.glsl | sed 's|.*/||; s|\.glsl$||' | LC_ALL=C sort)
first=$(echo "$kinds" | head -n 1)

glsl="$out/shaders/gen/materials.glsl"
list="$out/data/material_kinds.txt"
{
    echo '// создано tools/matgen.sh по data/shaders/materials/*.glsl, не править'
    for k in $kinds; do echo "#include \"materials/$k.glsl\""; done
    echo 'Surface surface_kind(uint kind, SurfIn i) {'
    echo '    switch (kind) {'
    n=0
    for k in $kinds; do echo "    case ${n}u: return surf_$k(i);"; n=$((n + 1)); done
    echo "    default: return surf_$first(i);"
    echo '    }'
    echo '}'
} > "$glsl.new"
echo "$kinds" > "$list.new"
for f in "$glsl" "$list"; do
    if cmp -s "$f.new" "$f"; then rm -f -- "$f.new"; else mv -- "$f.new" "$f"; fi
done
