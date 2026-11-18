#!/usr/bin/env bash
# Регистры, вытеснение и размер кода шейдеров кадра против потолков (docs/perf_arch.md, «Проверка в CI»).
#   tools/shader_stats/check.sh КАТАЛОГ_SPV tests/shader_budget.txt [вывод.txt]
# Собирает каждый конвейер из списка компилятором AMD (RADV, Mesa) на «пустом» устройстве — видеокарта не нужна —
# и сверяет с потолками. Строка списка: «шейдер константы регистры scratch_Б код_КБ», константы — значения
# constant_id 0, 1, … через запятую или «-»; scratch — память потока на волну (вытесненные регистры и локальные
# массивы по переменному номеру). Больше потолка — ошибка; потолок поднимают только вместе со строкой в
# docs/decisions.md (как потолки работы, docs/budget.md). Compute-шейдер без строки в списке — тоже ошибка
set -euo pipefail
spv_dir=$1
budget=$2
out=${3:-shader_stats.txt}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
tool=${TMPDIR:-/tmp}/ana_shader_stats
cc -O1 -o "$tool" "$here/shader_stats.c" "$root/third_party/volk/volk.c" -I"$root/third_party/volk" \
    -I"$root/third_party/vulkan/include" -ldl
icd=/usr/share/vulkan/icd.d/radeon_icd.json
[ -f "$icd" ] || { echo "нет RADV ($icd): apt install mesa-vulkan-drivers"; exit 1; }
export VK_ICD_FILENAMES=$icd RADV_FORCE_FAMILY=${RADV_FORCE_FAMILY:-phoenix}  # Radeon 780M: ноутбук пользователя
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-$(mktemp -d)}
jobs=${SHADER_STATS_JOBS:-3}  # light_cells на один вид — до 3 ГБ памяти компилятора
work=$(mktemp -d)
n=0
while read -r name spec vmax smax kmax; do
    case "$name" in ''|\#*) continue ;; esac
    args=()
    [ "$spec" != - ] && IFS=, read -r -a args <<< "$spec"
    (
        r=$("$tool" "$spv_dir/$name.spv" "${args[@]}" 2>/dev/null) || { echo "$name $spec НЕ СОБРАН" > "$work/$n"; exit 0; }
        get() { grep -oE "\| $1=[0-9]+" <<< "$r" | head -1 | grep -oE '[0-9]+$'; }
        printf '%s %s %s %s %s %s %s %s %s\n' "$name" "$spec" "$(get VGPRs)" "$(get 'Scratch size')" \
            "$(( $(get 'Code size') / 1024 ))" "$(get 'Subgroups per SIMD')" "$vmax" "$smax" "$kmax" > "$work/$n"
    ) &
    n=$((n + 1))
    while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n; done
done < "$budget"
wait
{
    printf '%-24s %-6s %8s %10s %8s %5s\n' "шейдер" "конст." "регистры" "scratch, Б" "код, КБ" "волн"
    for ((i = 0; i < n; ++i)); do
        read -r name spec v s k waves vmax smax kmax < "$work/$i"
        if [ "$v" = "НЕ" ]; then
            echo "$name $spec: ОШИБКА — конвейер не собран"
            continue
        fi
        mark=""
        [ "$v" -gt "$vmax" ] && mark="$mark регистры > $vmax"
        [ "$s" -gt "$smax" ] && mark="$mark scratch > $smax Б"
        [ "$k" -gt "$kmax" ] && mark="$mark код > $kmax КБ"
        printf '%-24s %-6s %8s %10s %8s %5s%s\n' "$name" "$spec" "$v" "$s" "$k" "$waves" "${mark:+  ← ПОТОЛОК:$mark}"
    done
    for f in "$spv_dir"/*.comp.spv; do
        b=$(basename "$f" .spv)
        grep -q "^$b " "$budget" || echo "$b: ОШИБКА — нет строки в $(basename "$budget")"
    done
} | tee "$out"
rm -rf "$work"
# у tee свой код; ошибка — по меткам в выводе
if grep -q "ПОТОЛОК\|ОШИБКА" "$out"; then
    echo "шейдеры выше потолков tests/shader_budget.txt: облегчить шейдер или поднять потолок строкой в docs/decisions.md"
    exit 1
fi
echo "регистры и код шейдеров в потолках"
