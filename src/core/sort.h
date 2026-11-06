// Своя устойчивая сортировка по 64-битному ключу (std::sort в шаге мира запрещён: порядок равных разный
// у libstdc++ и libc++). Поразрядная LSD по 8 бит, разряды с одинаковой цифрой у всех пропускаются
#pragma once
#include "types.h"
#include <string.h>

// a — данные, tmp — место на n элементов; key(x) — u64. Результат в a
template <typename T, typename KeyFn>
void radix_sort_u64(T *a, T *tmp, usize n, KeyFn key) {
    if (n < 2) return;
    T *src = a, *dst = tmp;
    for (u32 shift = 0; shift < 64; shift += 8) {
        usize count[256] = {};
        for (usize i = 0; i < n; ++i) count[(key(src[i]) >> shift) & 0xFF]++;
        if (count[(key(src[0]) >> shift) & 0xFF] == n) continue;  // у всех одна цифра
        usize pos = 0;
        for (usize &c : count) { usize t = c; c = pos; pos += t; }
        for (usize i = 0; i < n; ++i) dst[count[(key(src[i]) >> shift) & 0xFF]++] = src[i];
        T *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy((void *)a, (const void *)src, n * sizeof(T));
}
