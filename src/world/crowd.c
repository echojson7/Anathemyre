// Люди зала: бродящие, зрители арены, мобы вокруг локации. Тестовые, как и сами локации
#include "crowd.h"

#include <string.h>

#include "../defs/defs.h"
#include "../sim/look.h"
#include "../sim/names.h"
#include "../core/hash.h"
#include "../sim/arena_ring.h"
#include "../sim/squad.h"

static const char *g_crowd_names[] = {
    "ЧУРИЛА", "ВЕЛИМИР", "ФЕОДУЛ", "ГОСТЯТА", "КАЛЛИСТ", "ТУЛЛ", "НЕЖДАН", "ПРОКЛ",
    "МАМЕРК", "ДОМАСЛАВ", "ЕВТИХ", "СЕВЕР", "ИЗЯСЛАВ", "ПАНКРАТ", "РУФ", "ВЫШЕСЛАВ",
};

void crowd_add(People *p, Rng *rng, u32 count, GroundFn ground)
{
    count = MIN(count, (u32)ARRAY_COUNT(g_crowd_names));
    u32 first = p->count;
    for (u32 k = 0; k < count; k++) {
        f32 x = rng_range(rng, -8.0f, 8.0f), z = rng_range(rng, -12.0f, 14.0f);
        // Каждый сам по себе: своя команда, за чужого не вступается
        u32 i = people_add(p, ROLE_WANDER, v3(x, ground(x, z), z), rng_range(rng, 0, 2 * PI_F), (u8)(TEAM_WANDER + k), 0);
        // Большинство новички с ломом в руках, изредка бывалый.
        // Свой поток случайных чисел: расстановка фигур та же, что без листов
        Rng sr;
        rng_seed(&sr, 0xC0DEu + k, 91);
        sheet_random(&p->sheet[i], &sr, g_crowd_names[k], k % 4 == 1 ? 1 : 0, g_weapon_pool[k % g_weapon_pool_count]);
        p->body[i].phase = rng_f32(rng);
        p->body[i].gait = GAIT_WALK;
        p->wait[i] = rng_range(rng, 0.0f, 3.0f);
        p->seed[i] = rng_range(rng, 0, 100.0f);
        p->variant[i] = (u8)(k % 4 % BODY_KIND_COUNT);
        p->stand[i] = k % 4 == 3;
        // Факел — у кого вторая рука свободна
        u8 wc = g_weapons[g_weapon_pool[k % g_weapon_pool_count]].wclass;
        p->torch[i] = k % 3 == 1 && wc != WC_POLEARM && wc != WC_HEAVY;
        people_look_random(p, i, k * 7919u + 101u, k * 131u + 9u);
    }
    // Первый всегда идёт к камере: сразу видно ходьбу
    if (count > 0) {
        Player *b = &p->body[first];
        player_init(b, v3(3.0f, ground(3.0f, 0.0f), 0.0f), 0.0f);
        player_order_move(b, v3(3.0f, 0.0f, 16.0f));
        b->marker = 0.0f;
        p->wait[first] = 0;
        p->stand[first] = false;
    }
}

void watchers_add(People *p, V3 center, f32 floor_y, u32 count, u8 team, GroundFn ground)
{
    // Места полукругом за оградой, кроме прохода к воротам: точка, куда смотрит, зерно позы
    V3 pos[RING_SPECTATORS];
    f32 heading[RING_SPECTATORS], seed[RING_SPECTATORS];
    u32 n = 0;
    for (u32 k = 0; n < RING_SPECTATORS && k < 64; k++) {
        f32 a = 0.35f + (f32)k * 0.41f + 0.12f * hashf(k * 7u);
        if (fabsf(wrap_angle(a - PI_F)) < 0.55f) continue;
        f32 r = 8.0f + 0.5f * hashf(k * 11u + 3u);
        f32 sx = sinf(a) * r, sz = cosf(a) * r;
        pos[n] = v3(center.x + sx, floor_y, center.z + sz);
        heading[n] = atan2f(-sx, -sz) + (hashf(k * 5u) - 0.5f) * 0.5f;
        seed[n] = 100.0f * hashf(k * 3u + 1u);
        n++;
    }
    for (u32 k = 0; k < count && k < n; k++) {
        // Одна толпа: не вступаются, только отбиваются
        u32 i = people_add(p, ROLE_WATCHER, v3(pos[k].x, ground(pos[k].x, pos[k].z), pos[k].z), heading[k], team, 0);
        p->variant[i] = (u8)((k + 2) % BODY_KIND_COUNT);
        // Глазеть на бои ходят с чем попало: у одних лом, у других ничего, у кого-то факел
        Rng sr;
        rng_seed(&sr, 0x5EE0u + k, 92);
        bool armed = k % 3 == 0;
        u8 weapon = def_weapon(k % 2 ? "sekach" : "ressora");
        sheet_random(&p->sheet[i], &sr, name_next(p->variant[i] == BODY_FEMALE), k % 5 == 2 ? 1 : 0, weapon);
        if (!armed) {
            Item old;
            sheet_equip(&p->sheet[i], SLOT_WEAPON, (Item){ 0 }, &old);
        }
        p->sheet[i].role = "ЗРИТЕЛЬ АРЕНЫ";
        p->body[i].phase = rng_f32(&sr);
        p->body[i].gait = GAIT_WALK;
        p->seed[i] = seed[k];
        p->stand[i] = true;
        p->torch[i] = k % 5 == 1;
        people_look_random(p, i, k * 7919u + 5000u, 7000u + k);
    }
}

void crowd_step(People *pp, u32 i, Rng *rng, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: поле боя ведёт тело, после — постоит и пойдёт дальше
        pp->wait[i] = rng_range(rng, 3.0f, 8.0f);
        player_update(p, dt, ground);
        return;
    }
    bool was = p->moving;
    if (!pp->stand[i] && !p->moving && (pp->wait[i] -= dt) <= 0.0f) {
        player_order_move(p, v3(rng_range(rng, -14.0f, 14.0f), 0.0f, rng_range(rng, -16.0f, 18.0f)));
        p->marker = 0.0f;
    }
    if (pp->stand[i] && !p->moving) {
        // Стоящий после боя возвращается на своё место и поворачивается как стоял
        V3 d = v3_sub(pp->home[i], p->pos);
        if (d.x * d.x + d.z * d.z > 0.5f * 0.5f) {
            if ((pp->wait[i] -= dt) <= 0.0f) {
                player_order_move(p, pp->home[i]);
                p->marker = 0.0f;
            }
        } else {
            f32 dh = pp->home_h[i] - p->heading;
            while (dh > PI_F) dh -= 2.0f * PI_F;
            while (dh < -PI_F) dh += 2.0f * PI_F;
            p->heading += dh * fminf(1.0f, dt * 1.5f);
        }
    }
    p->vmax = 1.15f;
    player_update(p, dt, ground);
    if (was && !p->moving) pp->wait[i] = rng_range(rng, 2.0f, 7.0f);
}

// ---------------------------------------------------------------- мобы

// Маршрут вокруг зала и арены, против часовой
#define MOB_ROUTE 6
static const V2 g_route[MOB_ROUTE] = {
    { -36.0f, -33.0f }, { 0.0f, -36.0f }, { 37.0f, -32.0f }, { 38.0f, 30.0f }, { 14.0f, 50.0f }, { -37.0f, 32.0f },
};

// Оружие и броня — имена описаний (data/defs), NULL — конец
typedef struct MobDef {
    const char *name;
    u8 tier, gait;
    const char *weapon, *armor[3];
} MobDef;

static const MobDef g_mob_defs[] = {
    { "ВЫШАТА", 1, GAIT_WALK, "ressora", { "rags", "jacket" } },
    { "ФЕОКТИСТ", 1, GAIT_JOG, "bagor", { "quilt" } },
    { "ГОРДИАН", 2, GAIT_WALK, "balka", { "quilt", "plate", "bucket" } },
    { "МИЛОНЕГ", 0, GAIT_RUN, "sekach", { "rags" } },
    { "КАССИЙ", 1, GAIT_JOG, "sekach", { "jacket", "greaves" } },
};

void mobs_add(People *p, Rng *rng, GroundFn ground)
{
    u32 n = ARRAY_COUNT(g_mob_defs);
    for (u32 k = 0; k < n; k++) {
        const MobDef *d = &g_mob_defs[k];
        // Персонаж раньше места: так шли случайные числа и прежде
        CharSheet c;
        memset(&c, 0, sizeof c);
        c.name = d->name;
        arena_random_fighter(&c.d, rng, d->tier);
        for (u32 s = 0; s < SLOT_COUNT; s++) c.d.armor[s] = ARMOR_NONE;
        for (u32 q = 0; q < PART_COUNT; q++) c.d.hp[q] = g_part_hp[q];
        c.d.blood = 100.0f * c.d.mass / 75.0f;
        Item old;
        c.d.weapon = def_weapon(d->weapon);
        sheet_equip(&c, SLOT_WEAPON, item_make(IK_WEAPON, c.d.weapon, Q_WORN, 1), &old);
        for (u32 a = 0; a < 3 && d->armor[a]; a++) {
            Item it = item_make(IK_ARMOR, def_armor(d->armor[a]), Q_WORN, 1);
            sheet_equip(&c, item_slot(it), it, &old);
        }
        sheet_equip(&c, SLOT_BACK, item_make(IK_GOODS, def_goods("pack"), Q_WORN, 1), &old);
        sheet_equip(&c, SLOT_PANTS, item_make(IK_ARMOR, def_armor("pants"), Q_WORN, 1), &old);
        // Разнесены по кругу, у каждого своя точка старта. Одна шайка: вступаются друг за друга
        u32 at = (k * 7u + 1u) % MOB_ROUTE;
        V2 a = g_route[at], b = g_route[(at + 1) % MOB_ROUTE];
        f32 t = rng_range(rng, 0.1f, 0.8f);
        f32 x = lerpf(a.x, b.x, t), z = lerpf(a.y, b.y, t);
        u32 i = people_add(p, ROLE_MOB, v3(x, ground(x, z), z), atan2f(b.x - a.x, b.y - a.y), TEAM_MOBS, FF_ASSIST);
        p->sheet[i] = c;
        p->body[i].gait = d->gait;
        p->body[i].phase = rng_f32(rng);
        p->node[i] = (u8)((at + 1) % MOB_ROUTE);
        p->variant[i] = (u8)(k % 4 % BODY_KIND_COUNT);
        p->seed[i] = rng_range(rng, 0.0f, 100.0f);
        p->height[i] = body_scale(c.d.height);
        p->width[i] = 1.0f + 0.08f * sinf(p->seed[i] * 3.17f + 1.0f);
        p->look_seed[i] = 2000u + k;
        // Ночью шайка ходит с огнём: первый и последний, у них вторая рука свободна
        p->torch[i] = k == 0 || k == n - 1;
        player_order_move(&p->body[i], v3(b.x, 0.0f, b.y));
        p->body[i].marker = 0.0f;
        people_dress(p, i);
    }
}

void mob_step(People *pp, u32 i, Rng *rng, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: поле боя ведёт тело, после — постоит и пойдёт дальше по кругу
        pp->wait[i] = rng_range(rng, 4.0f, 10.0f);
        player_update(p, dt, ground);
        return;
    }
    bool was = p->moving;
    p->vmax = g_gait_speed[p->gait] * (0.9f + 0.002f * pp->sheet[i].d.skill[SK_ATHLETICS]);
    if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) {
        V2 t = g_route[pp->node[i]];
        // Не точно в точку: каждый круг немного иначе
        player_order_move(p, v3(t.x + rng_range(rng, -2.0f, 2.0f), 0.0f, t.y + rng_range(rng, -2.0f, 2.0f)));
        p->marker = 0.0f;
        pp->node[i] = (u8)((pp->node[i] + 1) % MOB_ROUTE);
    }
    player_update(p, dt, ground);
    // Дошёл: иногда стоит, переводит дух, иногда сразу дальше
    if (was && !p->moving) pp->wait[i] = rng_f32(rng) < 0.55f ? rng_range(rng, 6.0f, 16.0f) : 0.0f;
}
