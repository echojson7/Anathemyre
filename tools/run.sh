#!/bin/sh
# Подтянуть свежий код, собрать релиз и запустить.
# ./tools/run.sh [ветка] [аргументы игры...]
#
# В WSL с установленным mingw-w64 собирается и запускается нативный
# Windows-exe (быстрее всего). ANA_TARGET=linux заставит собрать Linux-версию.
set -e
cd "$(dirname "$0")/.."

case "$1" in
    -*|"") ;;
    *) git fetch origin "$1"; git checkout "$1"; shift ;;
esac
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git pull --ff-only origin "$BRANCH"

JOBS=$(nproc 2>/dev/null || echo 4)
TARGET=${ANA_TARGET:-auto}
if [ "$TARGET" = auto ]; then
    TARGET=linux
    case "$(uname -s)" in
        MINGW*|MSYS*) TARGET=windows ;;
        *) if grep -qi microsoft /proc/version 2>/dev/null && command -v x86_64-w64-mingw32-gcc >/dev/null; then
               TARGET=windows
           fi ;;
    esac
fi

make -j"$JOBS" TARGET_OS="$TARGET"
if [ "$TARGET" = windows ]; then
    exec ./build/anathemyre.exe "$@"
fi
# WSL2 без mingw: без этого Mesa рисует на процессоре (llvmpipe), а не на видеокарте
if [ -e /dev/dxg ] && [ -z "$GALLIUM_DRIVER" ]; then
    export GALLIUM_DRIVER=d3d12
fi
exec ./build/anathemyre "$@"
