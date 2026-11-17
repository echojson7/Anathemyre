#!/usr/bin/env bash
# Пакеты в CI (docs/build.md, «CI»): ci_apt.sh ЕСТЬ_ЛИ_SDL3_В_КЕШЕ пакет…
# Заголовки X11 и звука нужны только сборке SDL3; она в кеше — не ставятся. apt-get update — всегда: по старым
# спискам образа ставилась прежняя сборка Mesa (25.2.8 …04.4 вместо …04.5), и весь кеш шейдеров Mesa шёл мимо
# (он привязан к сборке драйвера) — первый кадр вдвое дольше, чем сэкономленные ~20 с
set -e
hit=$1
shift
p="$*"
if [ "$hit" != true ]; then
    p="$p cmake ninja-build libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev
        libxkbcommon-dev libasound2-dev libpulse-dev"
fi
sudo apt-get update -qq
# shellcheck disable=SC2086
sudo apt-get install -y -qq --no-install-recommends $p
