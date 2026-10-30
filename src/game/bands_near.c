#include "bands_near.h"

#include <math.h>
#include <string.h>

#include "../core/log.h"
#include "../sim/items.h"
#include "names.h"

bool near_init(NearBands *n, People *p, Field *f)
{
    memset(n, 0, sizeof *n);
    if (p->count != f->w.count || p->count + NEAR_BODIES > PEOPLE_MAX) {
        log_error("ОТРЯДЫ: нет мест под людей отрядов вдали (людей %u, на поле %u)", p->count, f->w.count);
        return false;
    }
    n->first = p->count;
    Rng rng;
    rng_seed(&rng, 0x0B5Eu, 3);
    for (u32 k = 0; k < NEAR_BODIES; k++) {
        u32 i = people_add(p, ROLE_FREE, v3(0, 0, 0), 0.0f, TEAM_FREE, 0);
        // Лист — любой годный: слот поля читает из него тело, пока свободен — не нужен
        sheet_random(&p->sheet[i], &rng, "", 0, 0);
        field_add(f, &p->body[i], &p->sheet[i], TEAM_FREE, FF_ABSENT);
        field_free(f, i);
        n->owner[k] = -1;
    }
    return true;
}

void near_ser(Ser *s, NearBands *n, u32 bands)
{
    SER(s, n->owner);
    SER(s, n->tier);
    SER(s, n->rank);
    SER_N(s, n->shown, bands);
    SER_N(s, n->heading, bands);
}

// Куда смотрит строй: по дороге, на месте — как стоял
static f32 band_heading(NearBands *n, const Bands *b, u32 bi)
{
    const Places *pl = b->places;
    u32 a = b->from[bi], c = b->to[bi];
    if (a != c) n->heading[bi] = atan2f(pl->x[c] - pl->x[a], pl->z[c] - pl->z[a]);
    return n->heading[bi];
}

// Место в строю: ведущий на точке отряда, за ним ряды по три
static V3 rank_pos(const NearBands *n, const Bands *b, u32 bi, u32 rank)
{
    f32 h = n->heading[bi], fx = sinf(h), fz = cosf(h);
    f32 side = 0.0f, back = 0.0f;
    if (rank) {
        side = (f32)((i32)((rank - 1) % 3) - 1) * 1.6f;
        back = (f32)(1 + (rank - 1) / 3) * 1.8f;
    }
    return v3(b->x[bi] + fz * side - fx * back, 0.0f, b->z[bi] - fx * side - fz * back);
}

static i32 free_body(const NearBands *n)
{
    for (u32 k = 0; k < NEAR_BODIES; k++)
        if (n->owner[k] < 0) return (i32)k;
    return -1;
}

static u8 field_team(const Bands *b, u32 bi) { return (u8)MIN(TEAM_BANDS + b->team[bi], TEAM_FREE - 1); }

// Отряд стал телами: ветераны впереди, новички сзади. Лица и листы — из зерна отряда: каждый раз те же
static void appear(NearBands *n, Bands *b, People *p, Field *f, u32 bi, GroundFn ground)
{
    u32 men = bands_men(b, bi), shown = 0;
    if (!men) return;
    f32 h = band_heading(n, b, bi);
    Rng rng;
    rng_seed(&rng, b->seed ^ ((u64)bi * 0x9E3779B97F4A7C15ull), 77);
    u8 team = field_team(b, bi);
    for (i32 t = BAND_TIERS - 1; t >= 0; t--)
        for (u32 m = 0; m < b->men[bi][t] && shown < NEAR_PER_BAND; m++) {
            i32 k = free_body(n);
            if (k < 0) break;
            u32 i = n->first + (u32)k, face = bi * 37u + shown;
            bool female = face % 3 == 1;
            V3 at = rank_pos(n, b, bi, shown);
            at.y = ground(at.x, at.z);
            people_reset(p, i, ROLE_BAND, at, h, team, FF_ASSIST | FF_AGGRO);
            sheet_random(&p->sheet[i], &rng, name_next(female), (u32)t, (u8)rng_below(&rng, W_COUNT));
            p->sheet[i].role = "ИЗ ОТРЯДА";
            p->body[i].gait = GAIT_WALK;
            p->body[i].phase = rng_f32(&rng);
            p->variant[i] = female ? BODY_FEMALE : face % 4 == 2 ? BODY_THIN : BODY_MALE;
            p->seed[i] = rng_range(&rng, 0.0f, 100.0f);
            p->height[i] = p->sheet[i].d.height / 1.8f;
            p->width[i] = 1.0f + 0.08f * sinf(p->seed[i] * 3.17f + 1.0f);
            p->look_seed[i] = 30000u + face;
            people_dress(p, i);
            p->last[i] = p->body[i];
            field_reuse(f, i, team, FF_ASSIST | FF_AGGRO);
            n->owner[k] = (i16)bi;
            n->tier[k] = (u8)t;
            n->rank[k] = (u8)shown++;
            n->shown[bi][t]++;
        }
    bands_set_near(b, bi, true);
    log_info("ОТРЯДЫ: отряд %u рядом, людей %u из %u", bi, shown, men);
    chron_add(b->chron, (ChronRec){ .kind = CH_BAND_NEAR, .from = b->from[bi], .to = b->to[bi], .a = bi, .b = CHRON_NOBODY,
                                    .n = (i32)shown, .m = (i32)men, .x = b->x[bi], .z = b->z[bi] });
}

// Отряд снова точка: в нём те, кто остался в живых, и те, кто не стал телами
static void leave(NearBands *n, Bands *b, People *p, Field *f, u32 bi)
{
    u16 men[BAND_TIERS];
    u32 was = 0, back = 0;
    for (u32 t = 0; t < BAND_TIERS; t++) {
        men[t] = (u16)(b->men[bi][t] - n->shown[bi][t]);
        was += n->shown[bi][t];
    }
    for (u32 k = 0; k < NEAR_BODIES; k++) {
        if (n->owner[k] != (i16)bi) continue;
        u32 i = n->first + k;
        if (f->w.state[i] != FS_DEAD) {
            men[n->tier[k]]++;
            back++;
        }
        p->role[i] = ROLE_FREE;
        p->torch[i] = false;
        p->body[i].moving = false;
        p->body[i].speed = 0.0f;
        field_free(f, i);
        n->owner[k] = -1;
    }
    memset(n->shown[bi], 0, sizeof n->shown[bi]);
    bands_set_near(b, bi, false);
    log_info("ОТРЯДЫ: отряд %u ушёл, тел вернулось %u из %u", bi, back, was);
    chron_add(b->chron, (ChronRec){ .kind = CH_BAND_FAR, .from = b->from[bi], .to = b->to[bi], .a = bi, .b = CHRON_NOBODY,
                                    .n = (i32)back, .m = (i32)was, .x = b->x[bi], .z = b->z[bi] });
    bands_set_men(b, bi, men);
}

static void decide(NearBands *n, Bands *b, People *p, Field *f, const Squad *sq, V3 focus, GroundFn ground)
{
    V3 at[SQUAD_MAX + 1];
    f32 r[SQUAD_MAX + 1];
    u32 na = 0;
    for (u32 k = 0; k < sq->count; k++) {
        at[na] = squad_body(sq, k)->pos;
        r[na++] = NEAR_R;
    }
    at[na] = focus;
    r[na++] = NEAR_CAM_R;
    for (u32 bi = 0; bi < b->count; bi++) {
        bool shown = b->near[bi];
        if (!shown && b->state[bi] == BAND_GONE) continue;
        f32 keep = shown ? NEAR_KEEP : 1.0f;
        bool want = false;
        for (u32 k = 0; k < na && !want; k++) {
            f32 dx = b->x[bi] - at[k].x, dz = b->z[bi] - at[k].z, rr = r[k] * keep;
            want = dx * dx + dz * dz < rr * rr;
        }
        if (want && !shown) appear(n, b, p, f, bi, ground);
        else if (!want && shown) leave(n, b, p, f, bi);
    }
}

void near_tick(NearBands *n, Bands *b, People *p, Field *f, const Squad *sq, V3 focus, f32 dt, GroundFn ground)
{
    if (!b->count) return;
    if (b->phase == 0) decide(n, b, p, f, sq, focus, ground);
    // Точка ждёт, пока тела дерутся или отстали, а если живых нет — стоит над убитыми, пока отряд не уйдёт
    // из виду. 2 — живых ещё не встретили
    for (u32 k = 0; k < NEAR_BODIES; k++)
        if (n->owner[k] >= 0) b->hold[n->owner[k]] = 2;
    for (u32 k = 0; k < NEAR_BODIES; k++) {
        i16 o = n->owner[k];
        u32 i = n->first + k;
        if (o < 0 || f->w.state[i] == FS_DEAD) continue;
        if (b->hold[o] == 2) b->hold[o] = 0;
        Player *pl = &p->body[i];
        // Хозяин тела — поле боя: бьётся или гонится
        if (pl->ctrl != CTRL_FREE) {
            b->hold[o] = 1;
            player_update(pl, dt, ground);
            continue;
        }
        band_heading(n, b, (u32)o);
        V3 t = rank_pos(n, b, (u32)o, n->rank[k]);
        f32 dx = t.x - pl->pos.x, dz = t.z - pl->pos.z, d = sqrtf(dx * dx + dz * dz);
        if (d > 12.0f) b->hold[o] = 1;
        if (d > 0.7f) {
            Gait g = d > 25.0f ? GAIT_RUN : d > 6.0f ? GAIT_JOG : GAIT_WALK;
            pl->gait = g;
            pl->vmax = g_gait_speed[g];
            player_order_move(pl, t);
            pl->marker = 0.0f;
        }
        player_update(pl, dt, ground);
    }
}
