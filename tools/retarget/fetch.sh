#!/bin/sh
# Сырые клипы CMU для ретаргета (docs/anim.md): BVH из зеркала una-dinosauria/cmu-mocap (конверсия Б. Хане,
# CMU Graphics Lab Motion Capture Database — свободно для любого использования) на закреплённом коммите, sha256 —
# в clips.txt. Кладёт в downloads/anims/cmu (не в git). Нужны curl и sha256sum.
#   tools/retarget/fetch.sh
set -e
cd "$(dirname "$0")/../.."
MIRROR=https://raw.githubusercontent.com/una-dinosauria/cmu-mocap/09a07f54f3bbb58797325f009282d0b2048a2871/data
grep '^source .* cmu/' tools/retarget/clips.txt | while read -r _ name file sum _; do
    out="downloads/anims/$file"
    if [ -f "$out" ] && [ "$(sha256sum "$out" | cut -d' ' -f1)" = "$sum" ]; then continue; fi
    mkdir -p "$(dirname "$out")"
    curl -fsSL --retry 3 -o "$out.part" "$MIRROR/${file#cmu/}"
    [ "$(sha256sum "$out.part" | cut -d' ' -f1)" = "$sum" ] || { echo "fetch: $file — не та sha256"; exit 1; }
    mv "$out.part" "$out"
    echo "fetch: $name"
done
