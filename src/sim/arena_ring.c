#include "arena_ring.h"

#include <string.h>

#include "../core/hash.h"
void arena_ring_init(ArenaRing *v, V3 center, f32 floor_y)
{
    memset(v, 0, sizeof *v);
    v->center = center;
    v->floor_y = floor_y;
    // Зрители полукругом за оградой, кроме прохода к воротам
    u32 n = 0;
    for (u32 k = 0; n < RING_SPECTATORS && k < 64; k++) {
        f32 a = 0.35f + (f32)k * 0.41f + 0.12f * hashf(k * 7u);
        if (fabsf(wrap_angle(a - PI_F)) < 0.55f) continue;
        f32 r = 8.0f + 0.5f * hashf(k * 11u + 3u);
        f32 sx = sinf(a) * r, sz = cosf(a) * r;
        v->spos[n] = v3(center.x + sx, floor_y, center.z + sz);
        v->sh[n] = atan2f(-sx, -sz) + (hashf(k * 5u) - 0.5f) * 0.5f;
        v->sseed[n] = 100.0f * hashf(k * 3u + 1u);
        n++;
    }
}

static void bout_started(ArenaRing *v, const ArenaSession *s)
{
    const CombatWorld *w = &s->w;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) fight_anim_reset(&v->fa[i], w, i, (f32)s->idx[i] * 13.7f + 3.0f);
}

void arena_ring_tick(ArenaRing *v, ArenaSession *s)
{
    CombatWorld *w = &s->w;
    if (!v->in_bout) {
        // Все отдыхают: время идёт, пока кто-то не встанет
        for (u32 tries = 0; tries < 64 && !v->in_bout; tries++) v->in_bout = arena_bout_begin(s);
        if (v->in_bout) bout_started(v, s);
        return;
    }
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) fight_anim_pre_tick(&v->fa[i], w, i);
    if (arena_bout_tick(s)) {
        arena_bout_end(s);
        v->in_bout = false;
    }
}

void arena_ring_ser(Ser *s, ArenaRing *v)
{
    SER(s, v->in_bout);
}

void arena_ring_restore(ArenaRing *v, const ArenaSession *s)
{
    const CombatWorld *w = &s->w;
    if (!v->in_bout) return;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) fight_anim_settle(&v->fa[i], w, i, (f32)s->idx[i] * 13.7f + 3.0f);
}

void arena_ring_present(ArenaRing *v, const ArenaSession *s, f32 alpha, f32 dt)
{
    const CombatWorld *w = &s->w;
    v->alpha = alpha;
    if (!v->in_bout) return;
    for (u32 i = 0; i < w->count && i < ARENA_MAX_BOUT; i++) fight_anim_update(&v->fa[i], w, i, alpha, dt);
}

M4 arena_ring_look(const ArenaSession *s, u32 ri, CharLook *lk)
{
    const FighterDesc *d = &s->roster[ri].d;
    u32 kind = ri % BODY_KIND_COUNT;  // как сетка тела в figures.c
    look_make(lk, kind, ri * 31u + 7u, d->mass, d->height, d->skill[SK_STRENGTH]);
    return look_matrix(lk, kind);
}
