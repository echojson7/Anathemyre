// RNG мира: PCG32 (O'Neill). Состояние — два u64, входит в хеш шага и в сохранение.
// Других источников случайности в шаге мира нет (docs/invariants.md)
#pragma once
#include "types.h"

struct Rng {
    u64 state = 0;
    u64 inc = 1;
};

inline u32 rng_u32(Rng &r) {
    u64 old = r.state;
    r.state = old * 6364136223846793005ull + r.inc;
    u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    u32 rot = (u32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

inline void rng_seed(Rng &r, u64 seed, u64 stream) {
    r.state = 0;
    r.inc = (stream << 1u) | 1u;
    rng_u32(r);
    r.state += seed;
    rng_u32(r);
}

// [0, n), n > 0. Умножение вместо деления: смещение < n / 2^32, для игры неважно
inline u32 rng_below(Rng &r, u32 n) { return (u32)(((u64)rng_u32(r) * n) >> 32); }

// [0, 1): 24 старших бита, точно представимо в f32
inline f32 rng_f01(Rng &r) { return (f32)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

// [lo, hi)
inline f32 rng_range(Rng &r, f32 lo, f32 hi) { return lo + (hi - lo) * rng_f01(r); }
