#!/bin/sh
# Подтянуть свежий код, собрать релиз и запустить игру. Пользователь запускает только его.
# ./tools/run.sh [аргументы игры]. В WSL собирается и запускается Windows .exe (родной драйвер видеокарты),
# на чистом Linux — Linux-версия. ANA_TARGET=linux|windows задаёт цель явно.
# Первый запуск качает компилятор и собирает SDL3 (несколько минут), дальше — секунды.
set -e
cd "$(dirname "$0")/.."

if git rev-parse --abbrev-ref --symbolic-full-name '@{u}' >/dev/null 2>&1; then
    git pull --ff-only
fi

TARGET=${ANA_TARGET:-}
if [ -z "$TARGET" ]; then
    TARGET=linux
    grep -qi microsoft /proc/version 2>/dev/null && TARGET=windows
fi

make -j"$(nproc 2>/dev/null || echo 4)" TARGET="$TARGET"
BIN=./build/linux-release/anathemyre
[ "$TARGET" = windows ] && BIN=./build/windows-release/anathemyre.exe
# Код выхода и журнал — на виду: игра, упавшая при старте, не должна выглядеть как «ничего не произошло»
set +e
"$BIN" "$@"
rc=$?
if [ $rc -ne 0 ]; then
    echo "игра вышла с кодом $rc; журнал: $(dirname "$BIN")/anathemyre.log (docs/build.md, «Если игра не запускается»)" >&2
fi
exit $rc
