#!/bin/sh
# Сканы CC0 (docs/scans.md): качает картинки tools/scans/scans.txt в $ANA_CACHE/scans (по умолчанию
# ~/.cache/anathemyre), сверяет sha256 и пакует в <data>/scans.pak. Зовёт make scans. Нужны curl, python3, numpy, PIL.
#   tools/scans/fetch.sh build/linux-release/data
set -e
cd "$(dirname "$0")/../.."
OUT=${1:?нужна папка данных сборки}
DL=${ANA_CACHE:-$HOME/.cache/anathemyre}/scans
BASE=https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k
mkdir -p "$DL" "$OUT"
# Строки «scan <слой> <ассет>» и «<карта> <sha256>» → «<ассет> <карта> <sha256>»
sed 's/#.*//' tools/scans/scans.txt | awk '$1 == "scan" {a = $3; next} NF == 2 {print a, $1, $2}' |
while read -r asset map sum; do
    f="${asset}_${map}_1k.jpg"
    if [ ! -f "$DL/$f" ] || [ "$(sha256sum "$DL/$f" | cut -d' ' -f1)" != "$sum" ]; then
        # Poly Haven просит свой User-Agent у клиентов
        curl -fsSL --retry 3 -A "texture-fetch/1.0" -o "$DL/$f.part" "$BASE/$asset/$f"
        [ "$(sha256sum "$DL/$f.part" | cut -d' ' -f1)" = "$sum" ] || { echo "scans: $f — не та sha256"; exit 1; }
        mv "$DL/$f.part" "$DL/$f"
    fi
done
python3 -I tools/scans/pack.py tools/scans/scans.txt "$DL" "$OUT/scans.pak"
