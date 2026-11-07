#!/bin/sh
# Сверка счётчиков работы освещения с эталоном (docs/tiles.md, «Сверка в CI»): work_check.sh эталон.txt кадр.txt [допуск %]
# Строки «имя значение». Больше эталона на допуск — работа выросла (ошибка); меньше — работа упала, эталон устарел
# (тоже ошибка: обнови tests/work_ref.txt в том же PR, чтобы следующий рост был виден). Код 0 — всё в допуске
ref=$1; got=$2; tol=${3:-2}
[ -f "$ref" ] && [ -f "$got" ] || { echo "work_check: нет $ref или $got"; exit 2; }
awk -v tol="$tol" '
    FNR == NR { want[$1] = $2; order[n++] = $1; next }
    { have[$1] = $2 }
    END {
        bad = 0
        for (i = 0; i < n; ++i) {
            k = order[i]
            if (!(k in have)) { printf "  %-14s нет в кадре\n", k; bad = 1; continue }
            w = want[k]; h = have[k]
            d = w > 0 ? (h - w) * 100.0 / w : (h > 0 ? 100 : 0)
            mark = d > tol ? "  ← больше работы" : d < -tol ? "  ← меньше: обнови эталон" : ""
            if (mark != "") bad = 1
            printf "  %-14s эталон %10d  кадр %10d  %+6.2f %%%s\n", k, w, h, d, mark
        }
        for (k in have) if (!(k in want)) { printf "  %-14s нет в эталоне\n", k; bad = 1 }
        if (bad) print "работа освещения не совпала с эталоном (допуск " tol " %)"
        else print "работа освещения совпала с эталоном (допуск " tol " %)"
        exit bad
    }' "$ref" "$got"
