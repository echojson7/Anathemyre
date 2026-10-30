#include "squad.h"
#include "../defs/defs.h"

#include <string.h>

#include "arena.h"

#define FORMATION_STEP 1.1f

typedef struct MemberDef {
    const char *name;
    u8 body;            // 0 мужчина, 1 женщина, 2 худой (main.c, g_body_file)
    u8 tier, weapon_q;
    const char *weapon;  // имена описаний (data/defs); NULL — конец списка
    const char *armor[4];
    const char *gear[4]; // вещи для слотов лица, ног, рук, спины, пояса
    struct { u8 kind; const char *id; u8 count; } inv[6];
} MemberDef;

#define GOODS(id, n) { IK_GOODS, id, n }

static const MemberDef g_start[] = {
    { "ГОСТОМЫСЛ", 0, 1, Q_SOUND, "ressora", { "rags", "jacket", "pants" }, { "wrap", "boots", "pack" },
      { GOODS("water", 1), GOODS("rusk", 6), GOODS("bandage", 4), GOODS("token", 1) } },
    { "НИКАНДР", 0, 1, Q_WORN, "bagor", { "quilt", "bucket", "pants" }, { "mitts", "belt" },
      { GOODS("water", 1), GOODS("meat", 3), GOODS("scrap", 2) } },
    { "АППИЙ", 2, 0, Q_JUNK, "armatura", { "rags", "pants" }, { "boots" },
      { GOODS("fuel", 1), GOODS("part", 1), GOODS("rusk", 3) } },
    { "КИНСЛАВА", 1, 2, Q_MASTER, "sekach", { "quilt", "plate", "greaves", "pants" }, { "boots", "mitts", "pack" },
      { GOODS("water", 1), GOODS("bandage", 6), GOODS("meat", 5), { IK_WEAPON, "ressora", 1 } } },
};

static void member_init(CharSheet *c, const MemberDef *m, u8 weapon, Rng *rng)
{
    memset(c, 0, sizeof *c);
    c->name = m->name;
    arena_random_fighter(&c->d, rng, m->tier);
    for (u32 s = 0; s < SLOT_COUNT; s++) c->d.armor[s] = ARMOR_NONE;
    for (u32 p = 0; p < PART_COUNT; p++) c->d.hp[p] = g_part_hp[p];
    c->d.blood = 100.0f * c->d.mass / 75.0f;
    c->d.skill[SK_WEAPON + g_weapons[weapon].wclass] = fminf(100.0f, c->d.skill[SK_ATTACK] + 5.0f);

    Item old;
    sheet_equip(c, SLOT_WEAPON, item_make(IK_WEAPON, weapon, m->weapon_q, 1), &old);
    for (u32 k = 0; k < 4 && m->armor[k]; k++) {
        Item it = item_make(IK_ARMOR, def_armor(m->armor[k]), Q_WORN, 1);
        sheet_equip(c, item_slot(it), it, &old);
    }
    for (u32 k = 0; k < 4 && m->gear[k]; k++) {
        Item it = item_make(IK_GOODS, def_goods(m->gear[k]), Q_WORN, 1);
        sheet_equip(c, item_slot(it), it, &old);
    }
    for (u32 k = 0; k < 6 && m->inv[k].kind; k++) {
        u8 id = m->inv[k].kind == IK_WEAPON ? def_weapon(m->inv[k].id) : def_goods(m->inv[k].id);
        inv_add(&c->inv, item_make(m->inv[k].kind, id, m->inv[k].kind == IK_WEAPON ? Q_WORN : Q_SOUND, m->inv[k].count));
    }
}

void sheet_random(CharSheet *c, Rng *rng, const char *name, u32 tier, u8 weapon)
{
    MemberDef m = { .name = name, .tier = (u8)tier, .weapon_q = tier ? Q_WORN : Q_JUNK, .armor = { "rags", "pants" } };
    if (tier >= 2) m.armor[2] = "quilt";
    member_init(c, &m, weapon, rng);
}

void squad_init(Squad *s, People *p, Rng *rng, V3 origin, f32 heading, GroundFn ground)
{
    memset(s, 0, sizeof *s);
    s->people = p;
    s->count = ARRAY_COUNT(g_start);
    for (u32 i = 0; i < s->count; i++) {
        f32 x = origin.x + (f32)(i % 2) * 1.2f - 0.6f, z = origin.z - (f32)(i / 2) * 1.3f;
        u32 k = people_add(p, ROLE_SQUAD, v3(x, ground(x, z), z), heading, TEAM_SQUAD, FF_PLAYER | FF_ASSIST);
        s->member[i] = k;
        member_init(&p->sheet[k], &g_start[i], def_weapon(g_start[i].weapon), rng);
        p->variant[k] = g_start[i].body;
        p->seed[k] = 7.0f + (f32)i * 5.3f;
        p->height[k] = p->sheet[k].d.height / 1.8f;
        p->look_seed[k] = 1000u + i;
        people_dress(p, k);
    }
    s->formation = true;
    // Старая рана: левая рука в бинтах, крови меньше
    CharSheet *hurt = squad_sheet(s, 2);
    hurt->d.hp[PART_ARM_L] = 22.0f;
    hurt->d.hp[PART_STOMACH] = 61.0f;
    hurt->d.blood *= 0.82f;
    hurt->d.fracture = (u8)(1u << PART_ARM_L);
}

void squad_place(Squad *s, V3 origin, f32 heading, GroundFn ground)
{
    V3 fwd = v3(sinf(heading), 0, cosf(heading)), right = v3(cosf(heading), 0, -sinf(heading));
    for (u32 i = 0; i < s->count; i++) {
        V3 p = v3_add(origin, v3_add(v3_mul(right, ((f32)(i % 2) - 0.5f) * 1.2f), v3_mul(fwd, -(f32)(i / 2) * 1.3f)));
        p.y = ground(p.x, p.z);
        Player *b = squad_body(s, i);
        u8 gait = b->gait;
        i32 slot = b->slot;
        player_init(b, p, heading);
        b->gait = gait;
        b->slot = slot;
    }
    s->order_marker = 0.0f;
}

void squad_order_move(Squad *s, V3 target)
{
    u32 n = 0;
    V3 c = v3(0, 0, 0);
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) {
            c = v3_add(c, squad_body(s, i)->pos);
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
        player_order_move(squad_body(s, i), p);
        squad_body(s, i)->group = group;
        k++;
    }
    // Метка там, куда щёлкнули, а не на месте первого в строю
    s->order_pos = target;
    s->order_marker = 1.2f;
}

void squad_set_gait(Squad *s, Gait g)
{
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) squad_body(s, i)->gait = (u8)g;
}

static f32 leg_factor(const FighterDesc *d, u32 part)
{
    if (d->severed & (1u << part)) return 0.2f;
    if (d->fracture & (1u << part)) return 0.4f;
    return 0.5f + 0.5f * clampf(d->hp[part] / g_part_hp[part], 0.0f, 1.0f);
}

f32 squad_member_speed(const Squad *s, u32 i, Gait g) { return sheet_speed(squad_sheet(s, i), g); }

f32 sheet_speed(const CharSheet *c, Gait g)
{
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
    for (u32 i = 0; i < s->count; i++) squad_body(s, i)->vmax = squad_member_speed(s, i, (Gait)squad_body(s, i)->gait);
    // Строй: все идущие по одному приказу держат темп самого медленного из них
    if (s->formation) {
        f32 pace[SQUAD_MAX];
        for (u32 i = 0; i < s->count; i++) {
            const Player *a = squad_body(s, i);
            pace[i] = a->vmax;
            if (!a->moving) continue;
            for (u32 j = 0; j < s->count; j++) {
                const Player *b = squad_body(s, j);
                if (b->moving && b->group == a->group) pace[i] = fminf(pace[i], b->vmax);
            }
        }
        for (u32 i = 0; i < s->count; i++) squad_body(s, i)->vmax = pace[i];
    }
    for (u32 i = 0; i < s->count; i++) player_update(squad_body(s, i), dt, ground);
}

i32 squad_primary(const Squad *s)
{
    for (u32 i = 0; i < s->count; i++)
        if (s->selected[i]) return (i32)i;
    return -1;
}

void squad_ser(Ser *s, Squad *sq)
{
    ser_count(s, &sq->count, SQUAD_MAX);
    SER(s, sq->member);
    SER(s, sq->selected);
    SER(s, sq->order_pos);
    SER(s, sq->order_marker);
    SER(s, sq->formation);
    SER(s, sq->next_group);
}
