#include "people.h"

#include <string.h>

void people_init(People *p, Persons *ps)
{
    memset(p, 0, sizeof *p);
    p->ps = ps;
}

u32 people_add(People *p, Role role, V3 pos, f32 heading, u8 team, u8 fflags)
{
    if (p->count >= PEOPLE_MAX) return PEOPLE_MAX - 1;
    PersonId id = persons_add(p->ps, role, pos, heading, team, fflags);
    if (id == PERSON_NONE) return PEOPLE_MAX - 1;
    u32 k = p->count++;
    p->who[k] = id;
    p->stand[k] = false;
    p->home[k] = pos;
    p->home_h[k] = heading;
    p->wait[k] = p->pace[k] = 0.0f;
    p->node[k] = p->prev[k] = 0;
    return k;
}

void people_ser(Ser *s, People *p)
{
    ser_count(s, &p->count, PEOPLE_MAX);
    u32 n = p->count;
    SER_N(s, p->who, n);
    SER_N(s, p->stand, n);
    SER_N(s, p->home, n);
    SER_N(s, p->home_h, n);
    SER_N(s, p->wait, n);
    SER_N(s, p->pace, n);
    SER_N(s, p->node, n);
    SER_N(s, p->prev, n);
}

void people_update(People *p, Rng *rng, Rng *town_rng, f32 dt, GroundFn ground)
{
    for (u32 k = 0; k < p->count; k++) {
        switch (p->ps->role[people_place(p, k)]) {
        case ROLE_WANDER:
        case ROLE_WATCHER: crowd_step(p, k, rng, dt, ground); break;
        case ROLE_TOWN: town_folk_step(p, k, town_rng, dt, ground); break;
        case ROLE_MOB: mob_step(p, k, rng, dt, ground); break;
        default: break;
        }
    }
}

i32 people_nth(const People *p, Role role, u32 k)
{
    for (u32 j = 0; j < p->count; j++) {
        u32 i = people_place(p, j);
        if (p->ps->role[i] == role && k-- == 0) return (i32)i;
    }
    return -1;
}
