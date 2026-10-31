// Жители Скудели: торговцы, сторожа и зеваки на местах посёлка, прохожие по узлам улиц.
// Места (Town) собирает content/town_build.c вместе с геометрией
#include "town.h"

#include "../defs/defs.h"
#include "../sim/look.h"
#include "../sim/names.h"
#include "../sim/squad.h"

// ---------------------------------------------------------------- жители

// Узлы улиц, по которым ходят прохожие; market — у прилавков, там задерживаются
typedef struct Node {
    f32 x, z, jitter;
    bool market;
} Node;

static const Node k_nodes[] = {
    { 50.0f, -8.0f, 1.0f, false },  // 0 у ворот снаружи
    { 58.5f, -8.0f, 0.8f, true },   // 1
    { 66.0f, -8.0f, 1.0f, true },   // 2
    { 78.0f, -8.0f, 0.8f, false },  // 3 перекрёсток
    { 88.0f, -8.0f, 1.0f, true },   // 4
    { 98.0f, -8.0f, 0.8f, true },   // 5
    { 78.0f, -19.0f, 1.0f, true },  // 6 переулок на юг
    { 78.0f, 1.5f, 0.8f, true },    // 7 под пандусом
    { 78.0f, 12.5f, 0.8f, false },  // 8 верх пандуса
    { 64.0f, 12.5f, 1.0f, true },   // 9
    { 84.5f, 12.5f, 0.6f, true },   // 10
    { 83.0f, 15.0f, 0.3f, false },  // 11 низ лестницы
    { 93.0f, 15.0f, 0.3f, false },  // 12 площадка наверху
    { 94.0f, 18.0f, 0.6f, false },  // 13
    { 82.0f, 18.0f, 1.0f, true },   // 14 у лотка со знаками
    { 68.0f, 17.8f, 0.8f, true },   // 15 у башни
};
#define NODE_COUNT ((u32)ARRAY_COUNT(k_nodes))

static const u8 k_edges[][2] = {
    { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 3, 6 }, { 3, 7 }, { 7, 8 },
    { 8, 9 }, { 8, 10 }, { 10, 11 }, { 11, 12 }, { 12, 13 }, { 13, 14 }, { 14, 15 },
};

static u32 next_node(Rng *r, u32 at, u32 prev)
{
    u32 opts[8], n = 0;
    for (u32 i = 0; i < ARRAY_COUNT(k_edges); i++) {
        u32 o = k_edges[i][0] == at ? k_edges[i][1] : k_edges[i][1] == at ? k_edges[i][0] : 255u;
        if (o != 255u && o != prev && n < 8) opts[n++] = o;
    }
    return n ? opts[rng_below(r, n)] : prev;
}

static void walk_to(People *p, Rng *r, u32 i, u32 node)
{
    const Node *nd = &k_nodes[node];
    p->prev[i] = p->node[i];
    p->node[i] = (u8)node;
    // Порядок случайных чисел задан явно: сначала z, потом x
    f32 jz = rng_range(r, -nd->jitter, nd->jitter), jx = rng_range(r, -nd->jitter, nd->jitter);
    player_order_move(&p->body[i], v3(nd->x + jx, 0, nd->z + jz));
    p->body[i].marker = 0.0f;
}

void town_folk_add(People *p, const Town *t, Rng *r, u32 walkers, u32 variants, u8 team)
{
    u32 first = p->count;
    // Сначала продавцы, сторожа и работники, потом прохожие, остаток мест — зеваки.
    // Один посёлок: за своих встают сторожа
    for (u32 pass = 0; pass < 3; pass++) {
        if (pass == 1) {
            for (u32 k = 0; k < walkers && p->count - first < TOWN_MAX_FOLK; k++) {
                u32 node = 1 + rng_below(r, NODE_COUNT - 1);
                const Node *nd = &k_nodes[node];
                f32 h = rng_range(r, 0, 2 * PI_F);
                f32 jz = rng_range(r, -nd->jitter, nd->jitter), jx = rng_range(r, -nd->jitter, nd->jitter);
                u32 i = people_add(p, ROLE_TOWN, v3(nd->x + jx, 0, nd->z + jz), h, team, 0);
                p->node[i] = p->prev[i] = (u8)node;
                p->wait[i] = rng_range(r, 0.0f, 4.0f);
            }
        }
        for (u32 s = 0; s < t->spot_count && p->count - first < TOWN_MAX_FOLK; s++) {
            const TownSpot *sp = &t->spot[s];
            bool want = pass == 0 ? sp->kind != SPOT_IDLE : pass == 2 && sp->kind == SPOT_IDLE;
            if (!want) continue;
            bool guard = sp->kind == SPOT_GUARD;
            u32 i = people_add(p, ROLE_TOWN, sp->pos, sp->heading, team, guard ? FF_ASSIST : 0);
            p->stand[i] = true;
            p->sheet[i].role = sp->kind == SPOT_TRADER ? "ТОРГОВЕЦ" : guard ? "СТОРОЖ СКУДЕЛИ"
                             : sp->kind == SPOT_WORKER ? "СБОРЩИК ЛОМА" : "ЖИТЕЛЬ СКУДЕЛИ";
        }
    }
    for (u32 i = first; i < p->count; i++) {
        p->seed[i] = rng_range(r, 0.0f, 100.0f);
        p->variant[i] = (u8)rng_below(r, variants);
        p->pace[i] = rng_range(r, 0.95f, 1.4f);
        p->body[i].phase = rng_f32(r);
        p->body[i].gait = GAIT_WALK;
        p->home[i] = p->body[i].pos;
        p->home_h[i] = p->body[i].heading;
    }
    // Листы жителей: свой поток случайных чисел, чтобы посёлок не зависел от их числа.
    // Сторожа бывалые, с ломом в руке (как рисуется), и у всех сторожей и у каждого пятого прохожего — факел
    for (u32 i = first; i < p->count; i++) {
        u32 k = i - first;
        bool armed = p->fflags[i] & FF_ASSIST;
        CharSheet *c = &p->sheet[i];
        const char *role = c->role ? c->role : "ПРОХОЖИЙ";
        Rng sr;
        rng_seed(&sr, 0x70C0u + k, 93);
        u8 weapon = def_weapon(p->variant[i] % 2 ? "sekach" : "ressora");
        sheet_random(c, &sr, name_next(p->variant[i] % BODY_KIND_COUNT == BODY_FEMALE), armed ? 1 : 0, weapon);
        if (!armed) {
            ItemStack old;
            sheet_equip(c, SLOT_WEAPON, (ItemStack){ 0 }, &old);
        }
        c->role = role;
        p->torch[i] = armed || (!p->stand[i] && k % 5 == 0);
        people_look_random(p, i, k * 6151u + 5003u, k * 977u + 31u);
    }
}

void town_folk_step(People *pp, u32 i, Rng *r, f32 dt, GroundFn ground)
{
    Player *p = &pp->body[i];
    if (p->ctrl != CTRL_FREE) {
        // Бой или погоня: телом правит поле боя; после — постоит и пойдёт дальше
        pp->wait[i] = rng_range(r, 3.0f, 8.0f);
        player_update(p, dt, ground);
        return;
    }
    if (pp->stand[i] && (p->moving || v3_len(v3_sub(pp->home[i], p->pos)) > 0.5f)) {
        // Стоящий после боя возвращается на место и встаёт как стоял
        if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) {
            player_order_move(p, pp->home[i]);
            p->marker = 0.0f;
        }
        p->vmax = pp->pace[i];
        player_update(p, dt, ground);
        if (!p->moving) p->heading = pp->home_h[i];
        return;
    }
    if (pp->stand[i]) {
        // Стоящие следят головой за ближайшим прохожим
        f32 best = 36.0f, goal = 0.0f;
        for (u32 j = 0; j < pp->count; j++) {
            if (pp->role[j] != ROLE_TOWN || pp->stand[j]) continue;
            const Player *o = &pp->body[j];
            f32 dx = o->pos.x - p->pos.x, dz = o->pos.z - p->pos.z, d2 = dx * dx + dz * dz;
            if (d2 < best && fabsf(o->pos.y - p->pos.y) < 2.0f) {
                f32 a = atan2f(dx, dz) - p->heading;
                while (a > PI_F) a -= 2 * PI_F;
                while (a < -PI_F) a += 2 * PI_F;
                if (fabsf(a) < 1.6f) {
                    best = d2;
                    goal = clampf(a, -1.0f, 1.0f);
                }
            }
        }
        p->look += (goal - p->look) * fminf(1.0f, dt * 2.5f);
        return;
    }
    bool was = p->moving;
    if (!p->moving && (pp->wait[i] -= dt) <= 0.0f) walk_to(pp, r, i, next_node(r, pp->node[i], pp->prev[i]));
    p->vmax = pp->pace[i];
    player_update(p, dt, ground);
    if (was && !p->moving) pp->wait[i] = k_nodes[pp->node[i]].market ? rng_range(r, 1.5f, 8.0f) : rng_range(r, 0.0f, 1.2f);
}
