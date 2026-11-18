#!/bin/bash
# Очередь снимков для разбора света (docs/lighting_progress.md, «Как снимать и мерить»).
#   tools/lightlab/shots.sh список.txt [процессов]
# Строка списка: «папка имя ключи игры…» (960×540, 4 кадра, без интерфейса). Снимок — папка/имя.png, журнал —
# папка/имя.log; с --hdr — ещё папка/имя.bmp.hdr16. Игра — build/linux-release (Xvfb + lavapipe, не во время make).
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
one() {
    out=$1; name=$2; shift 2
    mkdir -p "$out"
    (cd "$root/build/linux-release" && xvfb-run -a -s "-screen 0 1280x720x24" ./anathemyre --nohud --novsync \
        --window 960 540 --frames 4 --shot "$out/$name.bmp" "$@" > "$out/$name.log" 2>&1)
    python3 -c "from PIL import Image; Image.open('$out/$name.bmp').save('$out/$name.png')" 2>/dev/null && rm -f "$out/$name.bmp"
    echo "$name: $(grep 'глаз' "$out/$name.log" | tail -1)"
}
export -f one
export root
xargs -P "${2:-2}" -L 1 bash -c 'one "$@"' _ < "$1"
