#!/bin/sh
# Скачать vk.xml нужной версии (сверка sha256) и пересобрать src/render/vk.h.
# ./tools/vkgen/gen.sh          пересобрать
# ./tools/vkgen/gen.sh --check  только сверить, что vk.h совпадает с want.txt (так делает CI)
set -e
cd "$(dirname "$0")/../.."
VER=v1.3.275
SHA=3d9a86fcf699a25bf240d1becfeaa063860254cf4eb06523d250c3e2dc4c7017
XML=build/vkgen/vk-$VER.xml
mkdir -p build/vkgen
if [ ! -f "$XML" ]; then
    curl -sSfL -o "$XML.part" "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/$VER/xml/vk.xml"
    mv "$XML.part" "$XML"
fi
echo "$SHA  $XML" | sha256sum -c --quiet
if [ "$1" = --check ]; then
    python3 tools/vkgen/vkgen.py "$XML" tools/vkgen/want.txt build/vkgen/vk.h
    cmp -s build/vkgen/vk.h src/render/vk.h || { echo "vk.h устарел: ./tools/vkgen/gen.sh"; exit 1; }
else
    python3 tools/vkgen/vkgen.py "$XML" tools/vkgen/want.txt src/render/vk.h
fi
