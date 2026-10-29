#include "squad.h"

#include <string.h>

#include "arena.h"

#define FORMATION_STEP 1.1f

typedef struct MemberDef {
    const char *name;
    u8 tier, weapon, weapon_q;
    u8 armor[4];        // A_*, ARMOR_NONE — конец
    u8 gear[4];         // G_* для слотов лица, ног, рук, спины, пояса; G_COUNT — конец
    Item inv[6];
} MemberDef;

#define END_A ARMOR_NONE
#define END_G G_COUNT
#define GOODS(id, n) { IK_GOODS, id, Q_SOUND, n }

static const MemberDef g_start[] = {
    { "ГОСТОМЫСЛ", 1, W_RESSORA, Q_SOUND, { A_RAGS, A_JACKET, END_A }, { G_WRAP, G_BOOTS, G_PACK, END_G },
      { GOODS(G_WATER, 1), GOODS(G_RUSK, 6), GOODS(G_BANDAGE, 4), GOODS(G_TOKEN, 1) } },
    { "НИКАНДР", 1, W_BAGOR, Q_WORN, { A_QUILT, A_BUCKET, END_A }, { G_MITTS, G_BELT, END_G },
      { GOODS(G_WATER, 1), GOODS(G_MEAT, 3), GOODS(G_SCRAP, 2) } },
    { "АППИЙ", 0, W_ARMATURA, Q_JUNK, { A_RAGS, END_A }, { G_BOOTS, END_G },
      { GOODS(G_FUEL, 1), GOODS(G_PART, 1), GOODS(G_RUSK, 3) } },
    { "ДОБРОГОСТ", 2, W_SEKACH, Q_MASTER, { A_QUILT, A_PLATE, A_GREAVES, END_A }, { G_BOOTS, G_MITTS, G_PACK, END_G },
      { GOODS(G_WATER, 1), GOODS(G_BANDAGE, 6), GOODS(G_MEAT, 5), { IK_WEAPON, W_RESSORA, Q_WORN, 1 } } },
};

static void member_init(CharSheet *c, const MemberDef *m, Rng *rng)
{
    memset(c, 0, sizeof *c);
    c->name = m->name;
    arena_random_fighter(&c->d, rng, m->tier);
    for (u32 s = 0; s < SLOT_COUNT; s++) c->d.armor[s] = ARMOR_NONE;
    for (u32 p = 0; p < PART_COUNT; p++) c->d.hp[p] = g_part_hp[p];
    c->d.blood = 100.0f * c->d.mass / 75.0f;
    c->d.skill[SK_WEAPON + g_weapons[m->weapon].wclass] = fminf(100.0f, c->d.skill[SK_ATTACK] + 5.0f);

    Item old;
    sheet_equip(c, SLOT_WEAPON, item_make(IK_WEAPON, m->weapon, m->weapon_q, 1), &old);
    for (u32 k = 0; k < 4 && m->armor[k] != END_A; k++) {
        Item it = item_make(IK_ARMOR, m->armor[k], Q_WORN, 1);
        sheet_equip(c, item_slot(it), it, &old);
    }
    for (u32 k = 0; k < 4 && m->gear[k] != END_G; k++) {
        Item it = item_make(IK_GOODS, m->gear[k], Q_WORN, 1);
        sheet_equip(c, item_slot(it), it, &old);
    }
    for (u32 k = 0; k < 6 && m->inv[k].kind; k++) inv_add(&c->inv, m->inv[k]);
}

void squad_init(Squad *s, Rng *rng, V3 origin, f32 heading, GroundFn ground)
{
    memset(s, 0, sizeof *s);
    s->count = ARRAY_COUNT(g_start);
    for (u32 i = 0; i < s->count; i++) {
        member_init(&s->sheet[i], &g_start[i], rng);
        s->variant[i] = (u8)i;
        f32 x = origin.x + (f32)(i % 2) * 1.2f - 0.6f, z = origin.z - (f32)(i / 2) * 1.3f;
        player_init(&s->body[i], v3(x, ground(x, z), z), heading);
    }
    s->formation = true;
    // Старая рана: левая рука в бинтах, крови меньше
    CharSheet *hurt = &s->sheet[2];
    hurt->d.hp[PART_ARM_L] = 22.0f;
    hurt->d.hp[PART_STOMACH] = 61.0f;
    hurt->d.blood *= 0.82f;
    hurt->d.fracture = (u8)(1u << PART_ARM_L);
}

void squad_order_move(Squad *s, V3 target)
{
    u32 n = 0;
    V3 c = v3(0, 0, 0);
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) {
            c = v3_add(c, s->body[i].pos);
            n++;
        }
    if (!n) return;
    c = v3_mul(c, 1.0f / (f32)n);
    // Строй смотрит туда, куда идёт
    V3 fwd = v3(target.x - c.x, 0, target.z - c.z);
    f32 len = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    fwd = len > 0.01f ? v3_mul(fwd, 1.0f / len) : v3(0, 0, 1);
    V3 right = v3(fwd.z, 0, -fwd.x);
    u16 group = ++s->next_group;
    if (!group) group = ++s->next_group;
    u32 k = 0;
    for (u32 i = 0; i < s->count; i++) {
        if (!s->selected[i]) continue;
        u32 row = k / 3, in_row = MIN(3u, n - row * 3);
        f32 col = (f32)(k % 3) - 0.5f * (f32)(in_row - 1);
        V3 p = v3_add(target, v3_add(v3_mul(right, col * FORMATION_STEP), v3_mul(fwd, -(f32)row * FORMATION_STEP)));
        player_order_move(&s->body[i], p);
        s->body[i].group = group;
        k++;
    }
    // Метка там, куда щёлкнули, а не на месте первого в строю
    s->order_pos = target;
    s->order_marker = 1.2f;
}

void squad_set_gait(Squad *s, Gait g)
{
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) s->body[i].gait = (u8)g;
}

static f32 leg_factor(const FighterDesc *d, u32 part)
{
    if (d->severed & (1u << part)) return 0.2f;
    if (d->fracture & (1u << part)) return 0.4f;
    return 0.5f + 0.5f * clampf(d->hp[part] / g_part_hp[part], 0.0f, 1.0f);
}

f32 squad_member_speed(const Squad *s, u32 i, Gait g)
{
    const CharSheet *c = &s->sheet[i];
    // Хромает на худшую ногу
    f32 v = g_gait_speed[g] * fminf(leg_factor(&c->d, PART_LEG_L), leg_factor(&c->d, PART_LEG_R));
    // Атлетика решает на бегу, шагом все ходят одинаково
    if (g != GAIT_WALK) v *= 0.85f + 0.003f * c->d.skill[SK_ATHLETICS];
    // Ноша: до предела немного тормозит, сверх предела — сильно
    f32 over = sheet_load(c) / fmaxf(1.0f, sheet_carry_max(c));
    v *= 1.0f - 0.15f * clampf(over, 0.0f, 1.0f);
    if (over > 1.0f) v *= fmaxf(0.3f, 2.0f - over);
    return fmaxf(0.3f, v);
}

void squad_update(Squad *s, f32 dt, GroundFn ground)
{
    s->order_marker = fmaxf(0.0f, s->order_marker - dt);
    for (u32 i = 0; i < s->count; i++) s->body[i].vmax = squad_member_speed(s, i, (Gait)s->body[i].gait);
    // Строй: все идущие по одному приказу держат темп самого медленного из них
    if (s->formation) {
        f32 pace[SQUAD_MAX];
        for (u32 i = 0; i < s->count; i++) {
            pace[i] = s->body[i].vmax;
            if (!s->body[i].moving) continue;
            for (u32 j = 0; j < s->count; j++)
                if (s->body[j].moving && s->body[j].group == s->body[i].group) pace[i] = fminf(pace[i], s->body[j].vmax);
        }
        for (u32 i = 0; i < s->count; i++) s->body[i].vmax = pace[i];
    }
    for (u32 i = 0; i < s->count; i++) player_update(&s->body[i], dt, ground);
}

i32 squad_primary(const Squad *s)
{
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) return (i32)i;
    return -1;
}
