#!/bin/sh
# Проверка слоёв: каждый файл src/<папка>/ включает только папки, которые его слою можно видеть (docs/code.md).
# Порядок: core → мир (defs, sim, ai, world, io) → content → view → app; platform, render, audio — боковые,
# мира не видят. ./tools/layers.sh — без вывода, если всё чисто; иначе строки нарушений и код 1
cd "$(dirname "$0")/.."
allowed() {
    case "$1" in
        core)                     echo "core platform" ;;
        platform)                 echo "core platform" ;;
        render)                   echo "core platform render" ;;
        audio)                    echo "core platform audio" ;;
        defs|sim|ai|world|io)     echo "core platform defs sim ai world io" ;;
        content)                  echo "core platform defs sim ai world io content" ;;
        view)                     echo "core platform defs sim ai world io content render audio view" ;;
        app)                      echo "core platform defs sim ai world io content render audio view app" ;;
        *)                        echo "" ;;
    esac
}
bad=0
for f in $(find src -name '*.[ch]' | sort); do
    from=$(echo "$f" | cut -d/ -f2)
    ok=$(allowed "$from")
    [ -n "$ok" ] || { echo "$f: папка $from не в таблице слоёв"; bad=1; continue; }
    for inc in $(sed -n 's/^#include "\([^"]*\)".*/\1/p' "$f"); do
        to=$(realpath -m --relative-to=src "$(dirname "$f")/$inc" | cut -d/ -f1)
        case " $ok " in
            *" $to "*) ;;
            *) echo "$f: $from не видит $to ($inc)"; bad=1 ;;
        esac
    done
done
exit $bad
