// 64-битный хеш байтов для хеша шага и проверок. Не криптографический, но хорошо перемешивает
#pragma once
#include "types.h"
#include <string.h>

inline u64 hash_mix(u64 h) {
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return h;
}

inline u64 hash_bytes(const void *data, usize n, u64 seed = 0x9E3779B97F4A7C15ull) {
    const u8 *p = (const u8 *)data;
    u64 h = seed ^ (n * 0x100000001B3ull);
    while (n >= 8) {
        u64 w;
        memcpy(&w, p, 8);
        h = (h ^ hash_mix(w)) * 0x100000001B3ull;
        p += 8;
        n -= 8;
    }
    u64 tail = 0;
    memcpy(&tail, p, n);
    h = (h ^ hash_mix(tail ^ (n << 56))) * 0x100000001B3ull;
    return hash_mix(h);
}

inline u64 hash_combine(u64 h, u64 v) { return hash_mix(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2))); }

// Хеш строки для «содержимого по имени»: устойчив между запусками и платформами
inline u64 hash_str(const char *s) { return hash_bytes(s, strlen(s)); }
