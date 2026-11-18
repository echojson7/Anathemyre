#!/bin/sh
# Виды материалов (docs/materials.md) по файлам data/shaders/materials/<вид>.glsl, в порядке имён.
# Из одного списка — switch в шейдере и номера видов для игры, поэтому они не расходятся:
#   <out>/shaders/gen/materials.glsl   #include всех видов и surface_kind()
#   <out>/shaders/gen/material_kinds.glsl  номера видов константами KIND_<ВИД> (без самих видов)
#   <out>/data/material_kinds.txt      имена видов по строке, номер = строка
# Строка «// как: <вид>» в файле вида — его считает функция того вида (у песка дюн — песок, sand.glsl).
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
kglsl="$out/shaders/gen/material_kinds.glsl"
list="$out/data/material_kinds.txt"
{
    echo '// создано tools/matgen.sh по data/shaders/materials/*.glsl, не править'
    echo '#include "material_kinds.glsl"'
    for k in $kinds; do echo "#include \"materials/$k.glsl\""; done
    echo 'Surface surface_kind(uint kind, SurfIn i) {'
    echo '    switch (kind) {'
    # «// как: <вид>» — вид без своей функции, его считает функция того вида (по i.m.kind). Метки таких видов —
    # подряд перед общим return (default — у первого вида): один вызов функции на все её виды, у шейдера одна копия общего кода
    for f in $kinds; do
        n=0
        labels=""
        [ "$f" = "$first" ] && labels=" default:"
        for k in $kinds; do
            as=$(sed -n 's|^// как: *\([a-z0-9_]*\).*|\1|p' "data/shaders/materials/$k.glsl" | head -n 1)
            [ "${as:-$k}" = "$f" ] && labels="$labels case ${n}u:"
            n=$((n + 1))
        done
        [ -n "$labels" ] && echo "   $labels return surf_$f(i);"
    done
    echo '    }'
    echo '}'
} > "$glsl.new"
{
    echo '// создано tools/matgen.sh по data/shaders/materials/*.glsl, не править'
    echo '#ifndef MATERIAL_KINDS_GLSL'
    echo '#define MATERIAL_KINDS_GLSL'
    n=0
    for k in $kinds; do echo "const uint KIND_$(echo "$k" | tr 'a-z' 'A-Z') = ${n}u;"; n=$((n + 1)); done
    echo '#endif'
} > "$kglsl.new"
echo "$kinds" > "$list.new"
for f in "$glsl" "$kglsl" "$list"; do
    if cmp -s "$f.new" "$f"; then rm -f -- "$f.new"; else mv -- "$f.new" "$f"; fi
done
