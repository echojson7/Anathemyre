#include "rng.h"

void rng_seed(Rng *r, u64 seed, u64 stream)
{
    r->state = 0;
    r->inc = (stream << 1u) | 1u;
    rng_u32(r);
    r->state += seed;
    rng_u32(r);
}

u32 rng_u32(Rng *r)
{
    u64 old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    u32 rot = (u32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

f32 rng_f32(Rng *r)
{
    return (f32)(rng_u32(r) >> 8) * (1.0f / 16777216.0f);
}

f32 rng_range(Rng *r, f32 lo, f32 hi)
{
    return lo + (hi - lo) * rng_f32(r);
}

u32 rng_below(Rng *r, u32 n)
{
    return (u32)(((u64)rng_u32(r) * n) >> 32);
}
