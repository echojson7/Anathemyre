// Детерминированный RNG (PCG32)
#ifndef ANA_RNG_H
#define ANA_RNG_H

#include "types.h"

typedef struct Rng {
    u64 state;
    u64 inc;
} Rng;

void rng_seed(Rng *r, u64 seed, u64 stream);
u32  rng_u32(Rng *r);
f32  rng_f32(Rng *r);                 // [0, 1)
f32  rng_range(Rng *r, f32 lo, f32 hi);
u32  rng_below(Rng *r, u32 n);        // [0, n)

#endif
