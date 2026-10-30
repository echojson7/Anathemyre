#include "places.h"

#include <math.h>

#include "../core/log.h"

u32 places_add(Places *p, const char *name, f32 x, f32 z)
{
    if (p->count >= PLACES_MAX) return PLACE_NONE;
    p->x[p->count] = x;
    p->z[p->count] = z;
    p->name[p->count] = name;
    return p->count++;
}

void places_road(Places *p, u32 a, u32 b)
{
    if (p->roads >= ROADS_MAX || a == b || a >= p->count || b >= p->count) return;
    p->road_a[p->roads] = (u8)a;
    p->road_b[p->roads] = (u8)b;
    p->roads++;
}

f32 places_dist(const Places *p, u32 a, u32 b)
{
    f32 dx = p->x[b] - p->x[a], dz = p->z[b] - p->z[a];
    return sqrtf(dx * dx + dz * dz);
}

// Флойд — Уоршелл: мест не больше 255, считается один раз при старте
bool places_build(Places *p, Arena *perm)
{
    u32 n = p->count;
    p->next = ARENA_PUSH_ARRAY(perm, u8, (size_t)n * n + 1);
    size_t mark = perm->used;
    f32 *d = ARENA_PUSH_ARRAY(perm, f32, (size_t)n * n + 1);
    if (!p->next || !d) {
        log_error("МЕСТА: нет памяти на пути");
        return false;
    }
    for (u32 i = 0; i < n * n; i++) {
        d[i] = INFINITY;
        p->next[i] = PLACE_NONE;
    }
    for (u32 i = 0; i < n; i++) {
        d[i * n + i] = 0.0f;
        p->next[i * n + i] = (u8)i;
    }
    for (u32 r = 0; r < p->roads; r++) {
        u32 a = p->road_a[r], b = p->road_b[r];
        f32 l = places_dist(p, a, b);
        d[a * n + b] = d[b * n + a] = l;
        p->next[a * n + b] = (u8)b;
        p->next[b * n + a] = (u8)a;
    }
    for (u32 k = 0; k < n; k++)
        for (u32 i = 0; i < n; i++) {
            f32 dik = d[i * n + k];
            if (dik == INFINITY) continue;
            for (u32 j = 0; j < n; j++)
                if (dik + d[k * n + j] < d[i * n + j]) {
                    d[i * n + j] = dik + d[k * n + j];
                    p->next[i * n + j] = p->next[i * n + k];
                }
        }
    perm->used = mark;
    return true;
}
