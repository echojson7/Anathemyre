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

# WSL: .exe запускается из копии на диске Windows (docs/build.md, «Запуск из WSL»). Из build/ он читал бы
# сетки, шейдеры и кеши через \\wsl.localhost — это в разы медленнее диска (ассеты 3.8 с вместо долей секунды).
# Копируются только изменённые файлы; настройки, кеши и журнал в копии остаются. ANA_LOCAL=0 — запуск из build/
win_local_dir() {
    command -v rsync >/dev/null 2>&1 || { echo "run.sh: нет rsync (sudo apt install rsync) — игра запустится из build/, медленнее" >&2; return 1; }
    la=$(cd /mnt/c 2>/dev/null && cmd.exe /c "echo %LOCALAPPDATA%" 2>/dev/null | tr -d '\r')
    case "$la" in ""|*%*) return 1 ;; esac
    d=$(wslpath -u "$la" 2>/dev/null) || return 1
    [ -n "$d" ] && [ -d "$d" ] || return 1
    echo "$d/Anathemyre/game"
}
if [ "$TARGET" = windows ] && [ "${ANA_LOCAL:-1}" != 0 ] && LOCAL=$(win_local_dir) && mkdir -p "$LOCAL"; then
    SRC=./build/windows-release
    # Только .exe, шейдеры и данные; лишнее из прошлых сборок в них удаляется, остальное в копии не трогается
    if rsync -rt --delete --include=anathemyre.exe --include=shaders/ --include='shaders/*.spv' --include='data/***' \
        --exclude='*' "$SRC/" "$LOCAL/"; then
        # Первый раз — настройки и кеши из build/, чтобы не начинать с нуля
        for f in settings.txt pipelines.cache erosion.cache; do
            [ -f "$LOCAL/$f" ] || [ ! -f "$SRC/$f" ] || cp "$SRC/$f" "$LOCAL/$f"
        done
        BIN="$LOCAL/anathemyre.exe"
    else
        echo "run.sh: копия в $LOCAL не обновилась — игра запустится из build/" >&2
    fi
fi
# Код выхода и журнал — на виду: игра, упавшая при старте, не должна выглядеть как «ничего не произошло»
set +e
"$BIN" "$@"
rc=$?
if [ $rc -ne 0 ]; then
    echo "игра вышла с кодом $rc; журнал: $(dirname "$BIN")/anathemyre.log (docs/build.md, «Если игра не запускается»)" >&2
fi
exit $rc
