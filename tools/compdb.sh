#!/bin/sh
# compile_commands.json для clangd по исходным файлам (не по unity-файлам): ./tools/compdb.sh <компилятор> <флаги>
# Обычно через make compdb. Флаги -MMD -MP выкидываются
cd "$(dirname "$0")/.." || exit 2
cxx=$1
flags=$(echo "$2" | sed 's/-MMD//; s/-MP//')
dir=$(pwd)
{
    echo "["
    first=1
    for f in $(find src -name '*.cpp' | sort); do
        [ $first = 1 ] || echo ","
        first=0
        printf '  {"directory": "%s", "file": "%s", "command": "%s %s -c %s"}' "$dir" "$f" "$cxx" "$flags" "$f"
    done
    echo
    echo "]"
} > compile_commands.json
echo "compdb: compile_commands.json, $(grep -c '"file"' compile_commands.json) файлов"
