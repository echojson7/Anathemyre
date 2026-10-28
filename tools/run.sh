#!/bin/sh
# Подтянуть свежий код, собрать релиз и запустить.
# ./tools/run.sh            текущая ветка
# ./tools/run.sh имя-ветки  переключиться на ветку
set -e
cd "$(dirname "$0")/.."

if [ -n "$1" ]; then
    git fetch origin "$1"
    git checkout "$1"
fi
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git pull --ff-only origin "$BRANCH"

JOBS=$(nproc 2>/dev/null || echo 4)
make -j"$JOBS"
exec ./build/anathemyre
