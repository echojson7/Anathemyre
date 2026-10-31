#include "persons.h"

#include "../core/log.h"

void persons_init(Persons *ps) { memset(ps, 0, sizeof *ps); }

PersonId persons_add(Persons *ps, Role role, V3 pos, f32 heading, u8 team, u8 fflags)
{
    u32 i = 0;
    while (i < ps->count && ps->live[i]) i++;
    if (i >= PERSONS_MAX) {
        log_error("ЛЮДИ: мест в реестре нет (%u)", PERSONS_MAX);
        return PERSON_NONE;
    }
    if (i == ps->count) ps->count++;
    ps->live[i] = true;
    ps->role[i] = (u8)role;
    ps->team[i] = team;
    ps->fflags[i] = fflags;
    ps->variant[i] = 0;
    Player b;
    player_init(&b, pos, heading);
    person_set_body(ps, i, &b);
    ps->last[i] = b;
    ps->mode[i] = BODY_ANIM;
    memset(&ps->sheet[i], 0, sizeof ps->sheet[i]);
    ps->seed[i] = ps->height[i] = ps->width[i] = 0.0f;
    ps->look_seed[i] = ps->gear_seed[i] = 0;
    ps->look_from[i] = LOOK_FROM_SEED;
    ps->lk_own[i] = ps->torch[i] = false;
    ps->look[i] = (M4){ 0 };
    memset(&ps->lk[i], 0, sizeof ps->lk[i]);
    ps->ngear[i] = 0;
    return person_id(ps, i);
}

void persons_free(Persons *ps, u32 i)
{
    if (i >= ps->count || !ps->live[i]) return;
    ps->live[i] = false;
    ps->gen[i]++;
    ps->role[i] = ROLE_FREE;
    ps->team[i] = TEAM_FREE;
    ps->torch[i] = false;
    ps->move[i].moving = false;
    ps->move[i].speed = 0.0f;
}

void person_walk(Persons *ps, u32 i, f32 dt, GroundFn ground)
{
    Player b = person_body(ps, i);
    player_update(&b, dt, ground);
    person_set_body(ps, i, &b);
}

void persons_keep_last(Persons *ps)
{
    for (u32 i = 0; i < ps->count; i++) ps->last[i] = person_body(ps, i);
}

void persons_look_random(Persons *ps, u32 i, u32 look_seed, u32 gear_seed)
{
    // Разброс сложения внутри класса H: рост ±6%, ширина ±8%
    f32 s = ps->seed[i], hs = 1.0f + 0.06f * sinf(s * 7.31f), ws = 1.0f + 0.08f * sinf(s * 3.17f + 1.0f);
    ps->height[i] = hs;
    ps->width[i] = ws;
    ps->look_seed[i] = look_seed;
    ps->gear_seed[i] = gear_seed;
    ps->look_from[i] = LOOK_FROM_SEED;
    CharLook *lk = &ps->lk[i];
    look_make(lk, ps->variant[i], look_seed, 72.0f * hs * hs * ws * ws, g_body.height * hs, 30.0f + 25.0f * sinf(s * 2.1f));
    ps->look[i] = look_matrix(lk, ps->variant[i]);
    ps->ngear[i] = (u8)gear_random(gear_seed, ps->gear[i]);
    ps->ngear[i] += (u8)look_hair(lk, ps->gear[i], ps->ngear[i]);
}

void persons_dress(Persons *ps, u32 i)
{
    const CharSheet *cs = &ps->sheet[i];
    CharLook *lk = &ps->lk[i];
    ps->look_from[i] = LOOK_FROM_SHEET;
    if (!ps->lk_own[i]) {
        look_make(lk, ps->variant[i], ps->look_seed[i], cs->d.mass, cs->d.height, cs->d.skill[SK_STRENGTH]);
        ps->look[i] = look_matrix(lk, ps->variant[i]);
    }
    ps->ngear[i] = (u8)gear_from_sheet(cs, ps->gear[i]);
    ps->ngear[i] += (u8)look_hair(lk, ps->gear[i], ps->ngear[i]);
}

void persons_relook(Persons *ps, u32 i)
{
    ps->look[i] = look_matrix(&ps->lk[i], ps->variant[i]);
    // Прежнюю причёску долой, новую в конец списка
    u32 n = 0;
    for (u32 k = 0; k < ps->ngear[i]; k++)
        if (ps->gear[i][k] < GEAR_CAP) ps->gear[i][n++] = ps->gear[i][k];
    ps->ngear[i] = (u8)(n + look_hair(&ps->lk[i], ps->gear[i], n));
}

void persons_dress_live(Persons *ps)
{
    for (u32 i = 0; i < ps->count; i++)
        if (ps->role[i] == ROLE_SQUAD || ps->role[i] == ROLE_MOB) persons_dress(ps, i);
}

void persons_relook_all(Persons *ps)
{
    for (u32 i = 0; i < ps->count; i++) {
        if (!ps->live[i]) continue;
        // Рост и ширину из зерна не трогает: у одетых по листу они свои
        f32 h = ps->height[i], w = ps->width[i];
        CharLook own = ps->lk[i];
        if (ps->look_from[i] == LOOK_FROM_SHEET) persons_dress(ps, i);
        else persons_look_random(ps, i, ps->look_seed[i], ps->gear_seed[i]);
        ps->height[i] = h, ps->width[i] = w;
        if (ps->lk_own[i]) {
            ps->lk[i] = own;
            persons_relook(ps, i);
        }
    }
}

void persons_ser(Ser *s, Persons *ps)
{
    ser_count(s, &ps->count, PERSONS_MAX);
    u32 n = ps->count;
    SER_N(s, ps->gen, n);
    SER_N(s, ps->live, n);
    SER_N(s, ps->role, n);
    SER_N(s, ps->team, n);
    SER_N(s, ps->fflags, n);
    SER_N(s, ps->variant, n);
    SER_N(s, ps->x, n);
    SER_N(s, ps->y, n);
    SER_N(s, ps->z, n);
    SER_N(s, ps->heading, n);
    SER_N(s, ps->move, n);
    SER_N(s, ps->last, n);
    SER_N(s, ps->mode, n);
    for (u32 i = 0; i < n; i++) {
        CharSheet *c = &ps->sheet[i];
        ser_str(s, &c->name);
        ser_str(s, &c->role);
        SER(s, c->d);
        SER(s, c->equip);
        SER(s, c->inv);
    }
    SER_N(s, ps->seed, n);
    SER_N(s, ps->height, n);
    SER_N(s, ps->width, n);
    SER_N(s, ps->look_seed, n);
    SER_N(s, ps->gear_seed, n);
    SER_N(s, ps->look_from, n);
    SER_N(s, ps->lk_own, n);
    SER_N(s, ps->torch, n);
    // Внешность из редактора не восстановить из зерна: пишется только она
    for (u32 i = 0; i < n; i++)
        if (ps->lk_own[i]) SER(s, ps->lk[i]);
}
