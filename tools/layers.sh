#!/bin/sh
# Слои (docs/architecture.md, «Кто кого может включать»): каждый файл src/<модуль>/ включает только разрешённые модули.
# Чужие заголовки: SDL — только platform, Vulkan/volk/VMA/vk-bootstrap — только render.
# Без вывода, если всё чисто; иначе строки нарушений и код 1
cd "$(dirname "$0")/.." || exit 2
allowed() {
    case "$1" in
        core)     echo "core" ;;
        platform) echo "core platform" ;;
        asset)    echo "core asset" ;;
        anim)     echo "core asset anim" ;;
        sim)      echo "core anim sim" ;;
        ai)       echo "core sim ai" ;;
        render)   echo "core asset platform render" ;;
        view)     echo "core asset platform anim sim render view" ;;
        debug|app) echo "core asset platform anim sim ai render view debug app" ;;
        *)        echo "" ;;
    esac
}
bad=0
for f in $(find src -name '*.cpp' -o -name '*.h' | sort); do
    from=$(echo "$f" | cut -d/ -f2)
    ok=$(allowed "$from")
    [ -n "$ok" ] || { echo "$f: модуль $from не в таблице слоёв"; bad=1; continue; }
    for inc in $(sed -n 's/^#include "\([^"]*\)".*/\1/p' "$f"); do
        case "$inc" in
            volk/*|vma/*|vk-bootstrap/*)
                [ "$from" = render ] || { echo "$f: Vulkan-библиотеки только в render ($inc)"; bad=1; }
                continue ;;
        esac
        to=$(realpath -m --relative-to=src "$(dirname "$f")/$inc" | cut -d/ -f1)
        case " $ok " in
            *" $to "*) ;;
            *) echo "$f: $from не видит $to ($inc)"; bad=1 ;;
        esac
    done
    if grep -qE '^#include <(SDL3/|SDL\.h)' "$f" && [ "$from" != platform ]; then
        echo "$f: SDL только в platform"; bad=1
    fi
    if grep -qE '^#include <vulkan/' "$f" && [ "$from" != render ]; then
        echo "$f: Vulkan только в render"; bad=1
    fi
done
exit $bad
