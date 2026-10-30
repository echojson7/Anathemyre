// Хеши без состояния: одно и то же число на любом потоке и машине.
// Для случайностей, которые не должны зависеть от порядка обработки (бой, позы, внешность)
#ifndef ANA_HASH_H
#define ANA_HASH_H

#include "types.h"

// Перемешивание 32 бит (lowbias32)
static inline u32 hash_u32(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
// Число 0..1 из 24 младших бит
static inline f32 hashf(u32 x) { return (f32)(hash_u32(x) & 0xFFFFFF) / 16777215.0f; }

#endif
