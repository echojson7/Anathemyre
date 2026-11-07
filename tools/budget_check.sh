#!/bin/sh
# Потолки работы кадра (docs/budget.md, «Проверка в CI»): budget_check.sh потолки.txt кадр.txt
# В потолках строки «имя предел [на]»: предел работы на пиксель кадра (пиксели — сумма px_sky, px_ground,
# px_mesh из кадра) или, с «на плитку», на плитку кадра (сумма tiles_*). Выше предела — ошибка: работу кадра
# подняли выше бюджета. Предел поднимают только вместе со строкой в docs/decisions.md
lim=$1; got=$2
[ -f "$lim" ] && [ -f "$got" ] || { echo "budget_check: нет $lim или $got"; exit 2; }
awk '
    FNR == NR { if ($1 !~ /^#/ && NF >= 2) { cap[$1] = $2; per[$1] = $3; order[n++] = $1 } next }
    { have[$1] = $2; if ($1 ~ /^px_/) px += $2; if ($1 ~ /^tiles_/) tiles += $2 }
    END {
        bad = 0
        if (px <= 0 || tiles <= 0) { print "budget_check: в кадре нет px_* или tiles_*"; exit 2 }
        for (i = 0; i < n; ++i) {
            k = order[i]
            if (!(k in have)) { printf "  %-16s нет в кадре\n", k; bad = 1; continue }
            base = per[k] == "плитку" ? tiles : px
            r = have[k] / base
            mark = r > cap[k] ? "  ← выше бюджета" : ""
            if (mark != "") bad = 1
            printf "  %-16s %10d  на %s %.4f  предел %s%s\n", k, have[k], per[k] == "плитку" ? "плитку" : "пиксель", r, cap[k], mark
        }
        if (bad) print "работа кадра выше бюджета (docs/budget.md)"
        else print "работа кадра в бюджете (docs/budget.md)"
        exit bad
    }' "$lim" "$got"
