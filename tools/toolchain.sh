#!/bin/sh
# Компиляторы одной версии LLVM: llvm-mingw (Windows .exe) и официальный LLVM (Linux, с libc++ и ASan).
# Качает по sha256 в кеш ${ANA_CACHE:-~/.cache/anathemyre}. Повторный вызов ничего не делает.
# ./tools/toolchain.sh linux | windows
set -e
cd "$(dirname "$0")/.."
. tools/versions.sh
C=${ANA_CACHE:-$HOME/.cache/anathemyre}
mkdir -p "$C/dl"

die() { echo "toolchain: $*" >&2; exit 1; }

# fetch <url> <sha256> <файл>
fetch() {
    if [ -f "$3" ] && echo "$2  $3" | sha256sum -c - >/dev/null 2>&1; then return; fi
    echo "toolchain: качаю $(basename "$3")"
    curl -fL --retry 4 --retry-delay 2 -o "$3.part" "$1" || die "не скачалось: $1"
    echo "$2  $3.part" | sha256sum -c - >/dev/null 2>&1 || { rm -f "$3.part"; die "sha256 не совпал: $1"; }
    mv "$3.part" "$3"
}

windows() {
    out=$C/llvm-mingw-$LLVM_MINGW_VER
    [ -f "$out/.ok" ] && return
    name=llvm-mingw-$LLVM_MINGW_VER-ucrt-ubuntu-22.04-x86_64
    fetch "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_VER/$name.tar.xz" \
        "$LLVM_MINGW_SHA" "$C/dl/$name.tar.xz"
    rm -rf "$out" "$C/$name"
    tar -xJf "$C/dl/$name.tar.xz" -C "$C"
    mv "$C/$name" "$out"
    touch "$out/.ok"
}

# Официальный выпуск — 2 ГБ, из него берётся только нужное (~0.5 ГБ): clang, lld, libc++, compiler-rt
linux() {
    out=$C/llvm-$LLVM_VER
    [ -f "$out/.ok" ] && return
    name=LLVM-$LLVM_VER-Linux-X64
    fetch "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/$name.tar.xz" \
        "$LLVM_SHA" "$C/dl/$name.tar.xz"
    rm -rf "$out" "$C/$name"
    echo "toolchain: распаковываю LLVM $LLVM_VER (пара минут)"
    tar -xJf "$C/dl/$name.tar.xz" -C "$C" \
        "$name/bin/clang" "$name/bin/clang++" "$name/bin/clang-$LLVM_MAJOR" "$name/bin/ld.lld" "$name/bin/lld" \
        "$name/bin/llvm-ar" "$name/bin/llvm-ranlib" "$name/bin/llvm-symbolizer" \
        "$name/lib/clang" "$name/lib/x86_64-unknown-linux-gnu" "$name/include/c++" "$name/include/x86_64-unknown-linux-gnu"
    mv "$C/$name" "$out"
    touch "$out/.ok"
    # Архив больше не нужен: при смене версии скачается новый
    rm -f "$C/dl/$name.tar.xz"
}

case "$1" in
    linux) linux ;;
    windows) windows ;;
    *) die "использование: tools/toolchain.sh linux | windows" ;;
esac
