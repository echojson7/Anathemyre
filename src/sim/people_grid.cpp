// Сетка людей: сортировка по ключу клетки и поиск рядами (ключи клеток одного ряда идут подряд)
#include "people_grid.h"
#include "../core/sort.h"
#include <string.h>

void people_grid_build(const People &p, u32 shift, PeopleGrid &g, PeopleGridEntry *tmp) {
    g.shift = shift;
    g.n = 0;
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s)
        if (p.alive[s]) g.e[g.n++] = {people_grid_key(g, p.cx[s], p.cz[s], p.x[s], p.z[s], 0, 0), s};
    radix_sort_u64(g.e, tmp, g.n, [](const PeopleGridEntry &a) { return a.key; });
}

u32 people_grid_find(const PeopleGrid &g, u64 key) {
    u32 lo = 0, hi = g.n;
    while (lo < hi) {
        u32 m = (lo + hi) / 2;
        if (g.e[m].key < key) lo = m + 1;
        else hi = m;
    }
    return lo;
}

void people_grid_near(const PeopleGrid &g, i32 cx, i32 cz, f32 x, f32 z, f32 r, PeopleSet &out) {
    memset(out.bits, 0, sizeof out.bits);
    const f32 inv = 1.0f / (f32)(1u << g.shift);
    const i32 x0 = f_floor_i(x * inv), z0 = f_floor_i(z * inv);
    const i32 dx0 = f_floor_i((x - r) * inv) - x0, dx1 = f_floor_i((x + r) * inv) - x0;
    const i32 dz0 = f_floor_i((z - r) * inv) - z0, dz1 = f_floor_i((z + r) * inv) - z0;
    for (i32 dz = dz0; dz <= dz1; ++dz) {
        const u64 lo = people_grid_key(g, cx, cz, x, z, dx0, dz), hi = people_grid_key(g, cx, cz, x, z, dx1, dz);
        for (u32 k = people_grid_find(g, lo); k < g.n && g.e[k].key <= hi; ++k)
            out.bits[g.e[k].slot / 64] |= 1ull << (g.e[k].slot % 64);
    }
}

void people_set_alive(const People &p, PeopleSet &out) {
    memset(out.bits, 0, sizeof out.bits);
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s)
        if (p.alive[s]) out.bits[s / 64] |= 1ull << (s % 64);
}
