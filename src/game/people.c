#include "people.h"

#include <string.h>

u32 people_add(People *p, Role role, V3 pos, f32 heading, u8 team, u8 fflags)
{
    if (p->count >= PEOPLE_MAX) return PEOPLE_MAX - 1;
    u32 i = p->count++;
    player_init(&p->body[i], pos, heading);
    memset(&p->sheet[i], 0, sizeof p->sheet[i]);
    p->role[i] = (u8)role;
    p->team[i] = team;
    p->fflags[i] = fflags;
    p->variant[i] = 0;
    p->seed[i] = p->height[i] = p->width[i] = 0.0f;
    p->look_seed[i] = 0;
    p->look[i] = (M4){ 0 };
    p->ngear[i] = 0;
    p->torch[i] = p->stand[i] = false;
    p->home[i] = pos;
    p->home_h[i] = heading;
    p->wait[i] = p->pace[i] = 0.0f;
    p->node[i] = p->prev[i] = 0;
    return i;
}

void people_look_random(People *p, u32 i, u32 look_seed, u32 gear_seed)
{
    // Разброс сложения внутри класса H: рост ±6%, ширина ±8%
    f32 s = p->seed[i], hs = 1.0f + 0.06f * sinf(s * 7.31f), ws = 1.0f + 0.08f * sinf(s * 3.17f + 1.0f);
    p->height[i] = hs;
    p->width[i] = ws;
    p->look_seed[i] = look_seed;
    CharLook lk;
    look_make(&lk, p->variant[i], look_seed, 72.0f * hs * hs * ws * ws, 1.8f * hs, 30.0f + 25.0f * sinf(s * 2.1f));
    p->look[i] = look_matrix(&lk, p->variant[i]);
    p->ngear[i] = (u8)gear_random(gear_seed, p->gear[i]);
    p->ngear[i] += (u8)look_hair(&lk, p->gear[i], p->ngear[i]);
}

void people_dress(People *p, u32 i)
{
    const CharSheet *cs = &p->sheet[i];
    CharLook lk;
    look_make(&lk, p->variant[i], p->look_seed[i], cs->d.mass, cs->d.height, cs->d.skill[SK_STRENGTH]);
    p->look[i] = look_matrix(&lk, p->variant[i]);
    p->ngear[i] = (u8)gear_from_sheet(cs, p->gear[i]);
    p->ngear[i] += (u8)look_hair(&lk, p->gear[i], p->ngear[i]);
}

void people_dress_live(People *p)
{
    for (u32 i = 0; i < p->count; i++)
        if (p->role[i] == ROLE_SQUAD || p->role[i] == ROLE_MOB) people_dress(p, i);
}

void people_join_field(People *p, Field *f)
{
    for (u32 i = 0; i < p->count; i++) field_add(f, &p->body[i], &p->sheet[i], p->team[i], p->fflags[i]);
}

void people_update(People *p, Rng *rng, Rng *town_rng, f32 dt, GroundFn ground)
{
    for (u32 i = 0; i < p->count; i++) {
        switch (p->role[i]) {
        case ROLE_WANDER:
        case ROLE_WATCHER: crowd_step(p, i, rng, dt, ground); break;
        case ROLE_TOWN: town_folk_step(p, i, town_rng, dt, ground); break;
        case ROLE_MOB: mob_step(p, i, rng, dt, ground); break;
        default: break;
        }
    }
}

i32 people_nth(const People *p, Role role, u32 k)
{
    for (u32 i = 0; i < p->count; i++)
        if (p->role[i] == role && k-- == 0) return (i32)i;
    return -1;
}
