#!/usr/bin/env bash
# Пакеты в CI (docs/build.md, «CI»): ci_apt.sh ЕСТЬ_ЛИ_SDL3_В_КЕШЕ пакет…
# Заголовки X11 и звука нужны только сборке SDL3; она в кеше — не ставятся. apt-get update (~20 с) — только если
# по спискам образа машины пакет не нашёлся или не скачался
set -e
hit=$1
shift
p="$*"
if [ "$hit" != true ]; then
    p="$p cmake ninja-build libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev
        libxkbcommon-dev libasound2-dev libpulse-dev"
fi
# shellcheck disable=SC2086
if ! sudo apt-get install -y -qq --no-install-recommends $p; then
    echo "списки пакетов образа устарели — apt-get update"
    sudo apt-get update -qq
    sudo apt-get install -y -qq --no-install-recommends $p
fi
