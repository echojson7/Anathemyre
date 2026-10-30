#!/bin/sh
# Офлайн-сборка SPIR-V из tools/spv/list.txt (glslang). .spv лежат в репо: игре glslang не нужен.
# Строка списка — рецепт стадии, как его видит игра (rb_spv_recipe в src/render/rb.c):
#   <стадия vert|frag|comp> [-DИМЯ[=ЗНАЧ] ...] <файлы из data/shaders>
# Исходник: #version 450 core, rb.glsl, файлы по порядку; определения — ключами glslang (VULKAN он ставит сам).
# Имя .spv — файлы через «+», определения через «~», стадия: frame.glsl+scene.vert~SHADOW_PASS.vert.spv
# В data/spv/index.txt — sha256 исходника каждого .spv (склейка файлов, стадия, определения).
# ./tools/spv/build.sh          пересобрать изменённые
# ./tools/spv/build.sh --all    пересобрать все
# ./tools/spv/build.sh --check  сверить index.txt с исходниками, собрать во временную папку, проверить .spv (CI)
# Список пополняется сам: ANA_SPV_LIST=tools/spv/list.txt ./build/anathemyre ... (игра под GL дописывает рецепты)
set -e
cd "$(dirname "$0")/../.."
MODE=${1:-}
OUT=data/spv
TMP=build/spv
mkdir -p "$OUT" "$TMP"
command -v glslangValidator >/dev/null || { echo "нет glslangValidator (apt install glslang-tools)"; exit 1; }
NEW_INDEX=$TMP/index.txt
: > "$NEW_INDEX"
grep -v '^#' tools/spv/list.txt | grep -v '^[[:space:]]*$' | while read -r STAGE REST; do
    DEFS=""
    TAGS=""
    FILES=""
    NAME=""
    for t in $REST; do
        case "$t" in
            -D*) DEFS="$DEFS $t"; TAGS="$TAGS~${t#-D}" ;;
            *) FILES="$FILES data/shaders/$t"; NAME="${NAME:+$NAME+}$t" ;;
        esac
    done
    NAME="$NAME$TAGS.$STAGE"
    SRC=$TMP/$NAME
    # shellcheck disable=SC2086
    { printf '#version 450 core\n'; cat data/shaders/rb.glsl $FILES; } > "$SRC"
    HASH=$( { echo "$STAGE $DEFS"; cat "$SRC"; } | sha256sum | cut -d' ' -f1)
    echo "$NAME $HASH" >> "$NEW_INDEX"
    OLD=$(awk -v n="$NAME" '$1 == n { print $2 }' "$OUT/index.txt" 2>/dev/null || true)
    if [ "$MODE" = --check ]; then
        # shellcheck disable=SC2086
        glslangValidator -V --target-env vulkan1.3 -S "$STAGE" $DEFS -o "$TMP/$NAME.spv" "$SRC" >/dev/null || { echo "$NAME: не собирается"; exit 1; }
        [ "$OLD" = "$HASH" ] || { echo "$NAME: .spv устарел, ./tools/spv/build.sh"; exit 1; }
        [ -f "$OUT/$NAME.spv" ] || { echo "$NAME: нет $OUT/$NAME.spv"; exit 1; }
        if command -v spirv-val >/dev/null; then spirv-val --target-env vulkan1.3 "$OUT/$NAME.spv"; fi
        continue
    fi
    if [ "$MODE" = --all ] || [ "$OLD" != "$HASH" ] || [ ! -f "$OUT/$NAME.spv" ]; then
        # shellcheck disable=SC2086
        glslangValidator -V --target-env vulkan1.3 -S "$STAGE" $DEFS -o "$OUT/$NAME.spv" "$SRC" | grep -v "^$SRC$" || true
        [ -f "$OUT/$NAME.spv" ] || exit 1
        echo "собран $OUT/$NAME.spv"
    fi
done
if [ "$MODE" != --check ]; then
    sort "$NEW_INDEX" > "$OUT/index.txt"
    # .spv, которых больше нет в списке
    for f in "$OUT"/*.spv; do
        [ -e "$f" ] || continue
        n=$(basename "$f" .spv)
        awk -v n="$n" '$1 == n { f = 1 } END { exit !f }' "$OUT/index.txt" || { rm "$f"; echo "убран $f"; }
    done
else
    sort "$NEW_INDEX" | cmp -s - "$OUT/index.txt" || { echo "data/spv/index.txt не совпадает со списком"; exit 1; }
fi
