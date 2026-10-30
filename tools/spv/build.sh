#!/bin/sh
# Офлайн-сборка SPIR-V из tools/spv/list.txt (glslang). .spv лежат в репо: игре glslang не нужен.
# В data/spv/index.txt — sha256 исходника каждого .spv (склейка файлов, стадия, define).
# ./tools/spv/build.sh          пересобрать изменённые
# ./tools/spv/build.sh --all    пересобрать все
# ./tools/spv/build.sh --check  сверить index.txt с исходниками, собрать во временную папку, проверить .spv (CI)
set -e
cd "$(dirname "$0")/../.."
MODE=${1:-}
OUT=data/spv
TMP=build/spv
mkdir -p "$OUT" "$TMP"
command -v glslangValidator >/dev/null || { echo "нет glslangValidator (apt install glslang-tools)"; exit 1; }
NEW_INDEX=$TMP/index.txt
: > "$NEW_INDEX"
FAIL=0
grep -v '^#' tools/spv/list.txt | grep -v '^[[:space:]]*$' | while read -r NAME STAGE REST; do
    DEFS=""
    FILES=""
    for t in $REST; do
        case "$t" in -D*) DEFS="$DEFS $t" ;; *) FILES="$FILES $t" ;; esac
    done
    SRC=$TMP/$NAME.$STAGE
    # shellcheck disable=SC2086
    cat $FILES > "$SRC"
    HASH=$( { echo "$STAGE$DEFS"; cat "$SRC"; } | sha256sum | cut -d' ' -f1)
    echo "$NAME $HASH" >> "$NEW_INDEX"
    OLD=$(grep "^$NAME " "$OUT/index.txt" 2>/dev/null | cut -d' ' -f2 || true)
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
else
    sort "$NEW_INDEX" | cmp -s - "$OUT/index.txt" || { echo "data/spv/index.txt не совпадает со списком"; exit 1; }
fi
