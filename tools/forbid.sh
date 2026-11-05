#!/bin/sh
# Запреты из docs/invariants.md, которые ловит grep. Шаг мира — src/sim и src/core (его основа).
# Без вывода, если всё чисто; иначе строки нарушений и код 1
cd "$(dirname "$0")/.." || exit 2
bad=0
# check <описание> <regex> <файлы...>: строки с совпадением, кроме комментариев //
check() {
    what=$1; re=$2; shift 2
    out=$(grep -nE "$re" "$@" | grep -vE '^[^:]+:[0-9]+:\s*(//|#[^i])' )
    if [ -n "$out" ]; then
        echo "запрет: $what"
        echo "$out" | sed 's/^/  /'
        bad=1
    fi
}
SIM=$(find src/sim src/core -name '*.cpp' -o -name '*.h' | sort)
ALL=$(find src -name '*.cpp' -o -name '*.h' | sort)
[ -n "$SIM" ] || exit 0

check "libm и <cmath> в шаге мира (свои sin/cos, точный sqrt через f_sqrt)" \
    '#include <(cmath|math\.h)>|[^_a-z](sinf?|cosf?|tanf?|powf?|expf?|logf?|sqrtf?|atan2f?|floorf?|ceilf?|fmodf?|exp2f?|log2f?)\s*\(' $SIM
check "STL, new/delete, malloc/free в шаге мира (арены и свои контейнеры)" \
    'std::|[^_a-z](new|delete)[^_a-z]|[^_a-z](malloc|calloc|realloc|free)\s*\(' $SIM
out=$(grep -nE '#include <' $(find src/sim -name '*.cpp' -o -name '*.h') | grep -vE '<(stddef|stdint|string)\.h>')
[ -z "$out" ] || { echo "запрет: системные заголовки в sim (можно stddef, stdint, string)"; echo "$out" | sed 's/^/  /'; bad=1; }
check "случайность и время не из RNG мира" '[^_a-z](rand|srand|time|clock)\s*\(' $SIM
check "rsqrt/rcp (разные приближения у Intel и AMD)" '_mm[0-9]*_(rsqrt|rcp)' $ALL
check "long, wchar_t, long double в состоянии мира и форматах" '[^_a-z](long|wchar_t)[^_a-z]' $SIM
check "strtof/scanf/printf в шаге мира (свой разбор и печать)" '[^_a-z](strto[fdl]|ato[fil]|s?scanf|s?n?printf)\s*\(' $(find src/sim -name '*.cpp' -o -name '*.h')
check "исключения и RTTI" '[^_a-z](throw|try|catch|dynamic_cast|typeid)[^_a-z]' $ALL
check "std::pmr" 'std::pmr' $ALL
check "флаги, ломающие детерминизм float" '(-ffast-math|-march=native|-funsafe-math|-ffp-contract=(fast|on))' Makefile $(ls tools/*.sh | grep -v forbid.sh)

grep -q -- '-ffp-contract=off' Makefile || { echo "запрет: в Makefile нет -ffp-contract=off"; bad=1; }
grep -q -- '-march=x86-64-v3' Makefile || { echo "запрет: в Makefile нет -march=x86-64-v3"; bad=1; }

# Не запрет, а напоминание: модуль должен читаться целиком (AGENTS.md)
for f in $ALL; do
    n=$(wc -l < "$f")
    [ "$n" -gt 800 ] && echo "внимание: $f — $n строк, больше 800: повод разделить"
done
exit $bad
