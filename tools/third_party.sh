#!/bin/sh
# SDL3 закреплённой версии: качается по sha256 и собирается своим CMake статически в build/third_party/sdl3-<цель>.
# Это исключение из правила «третьи библиотеки собирает наш Makefile» (docs/stack.md): SDL3 без CMake не собрать.
# ./tools/third_party.sh linux | windows. Повторный вызов ничего не делает, если эта версия уже собрана (метка .ok-<версия>).
set -e
cd "$(dirname "$0")/.."
. tools/versions.sh
C=${ANA_CACHE:-$HOME/.cache/anathemyre}
T=$1
OUT=build/third_party/sdl3-$T

die() { echo "third_party: $*" >&2; exit 1; }

[ -f "$OUT/.ok-$SDL3_VER" ] && exit 0
command -v cmake >/dev/null || die "нет cmake: sudo apt install -y cmake ninja-build"

mkdir -p "$C/dl" build/third_party
tgz=$C/dl/SDL3-$SDL3_VER.tar.gz
if ! { [ -f "$tgz" ] && echo "$SDL3_SHA  $tgz" | sha256sum -c - >/dev/null 2>&1; }; then
    echo "third_party: качаю SDL3 $SDL3_VER"
    curl -fL --retry 4 --retry-delay 2 -o "$tgz.part" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VER/SDL3-$SDL3_VER.tar.gz" || die "не скачалось"
    echo "$SDL3_SHA  $tgz.part" | sha256sum -c - >/dev/null 2>&1 || { rm -f "$tgz.part"; die "sha256 SDL3 не совпал"; }
    mv "$tgz.part" "$tgz"
fi
src=build/third_party/src/SDL3-$SDL3_VER
if [ ! -d "$src" ]; then mkdir -p build/third_party/src; tar -xzf "$tgz" -C build/third_party/src; fi

case $T in
    linux)
        tc=$C/llvm-$LLVM_VER/bin
        [ -x "$tc/clang" ] || die "нет LLVM: ./tools/toolchain.sh linux"
        # Только X11; звук — ALSA и PulseAudio (грузятся при запуске). Остальное выключено,
        # иначе CMake SDL3 требует dev-пакет на каждую возможность
        set -- -DCMAKE_C_COMPILER="$tc/clang" -DCMAKE_AR="$tc/llvm-ar" -DCMAKE_RANLIB="$tc/llvm-ranlib" \
            -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_X11_XTEST=OFF -DSDL_X11_XSHAPE=OFF -DSDL_X11_XDBE=OFF \
            -DSDL_X11_XSYNC=OFF -DSDL_FRIBIDI=OFF -DSDL_LIBTHAI=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
            -DSDL_PIPEWIRE=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF -DSDL_HIDAPI_LIBUSB=OFF -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF ;;
    windows)
        tc=$C/llvm-mingw-$LLVM_MINGW_VER/bin
        [ -x "$tc/x86_64-w64-mingw32-clang" ] || die "нет llvm-mingw: ./tools/toolchain.sh windows"
        set -- -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
            -DCMAKE_C_COMPILER="$tc/x86_64-w64-mingw32-clang" -DCMAKE_RC_COMPILER="$tc/x86_64-w64-mingw32-windres" ;;
    *) die "использование: tools/third_party.sh linux | windows" ;;
esac
gen=
command -v ninja >/dev/null && gen="-G Ninja"

echo "third_party: собираю SDL3 $SDL3_VER для $T"
B=build/third_party/build-sdl3-$T
LOG=build/third_party/sdl3-$T.log
rm -rf "$OUT" "$B"
# shellcheck disable=SC2086
cmake -S "$src" -B "$B" $gen "$@" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$(pwd)/$OUT" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_C_FLAGS="-march=x86-64-v3" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_INSTALL_DOCS=OFF -DSDL_GPU=OFF -DSDL_RENDER=OFF -DSDL_CAMERA=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
    > "$LOG" 2>&1 || { grep -iE "error|could" "$LOG" | head -10; die "cmake SDL3 ($T) упал, журнал $LOG"; }
cmake --build "$B" -j"$(nproc 2>/dev/null || echo 4)" >> "$LOG" 2>&1 \
    || { grep -iE "error" "$LOG" | head -10; die "сборка SDL3 ($T) упала, журнал $LOG"; }
cmake --install "$B" >> "$LOG" 2>&1
rm -rf "$B"
touch "$OUT/.ok-$SDL3_VER"
