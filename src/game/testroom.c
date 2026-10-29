#include "testroom.h"

#include <string.h>

#include "arena.h"
#include "character.h"

// Цвета поверхностей (альбедо, линейные)
#define C_CONCRETE v3(0.42f, 0.40f, 0.37f)
#define C_CONCRETE_DARK v3(0.30f, 0.29f, 0.27f)
#define C_RUST v3(0.42f, 0.24f, 0.13f)
#define C_SAND v3(0.40f, 0.34f, 0.26f)
#define C_GLYPH_GREEN v3(0.25f, 1.0f, 0.40f)
#define C_GLYPH_RED v3(0.95f, 0.18f, 0.10f)

static f32 hash2(i32 x, i32 z)
{
    u32 h = (u32)x * 374761393u + (u32)z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (f32)(h & 0xFFFFFF) / 16777215.0f;
}

static f32 vnoise(f32 x, f32 z)
{
    i32 ix = (i32)floorf(x), iz = (i32)floorf(z);
    f32 fx = x - (f32)ix, fz = z - (f32)iz;
    fx = fx * fx * (3 - 2 * fx);
    fz = fz * fz * (3 - 2 * fz);
    f32 a = hash2(ix, iz), b = hash2(ix + 1, iz), c = hash2(ix, iz + 1), d = hash2(ix + 1, iz + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

static f32 terrain_height(f32 x, f32 z, void *user)
{
    (void)user;
    f32 r = sqrtf(x * x + z * z);
    f32 dunes = 0.0f, amp = 1.0f, freq = 0.012f;
    for (int i = 0; i < 5; i++) {
        dunes += vnoise(x * freq, z * freq) * amp;
        amp *= 0.5f;
        freq *= 2.1f;
    }
    // Площадка у зала ровная, дальше дюны, у горизонта гряды
    f32 flat = clampf((r - 45.0f) / 60.0f, 0.0f, 1.0f);
    f32 far = clampf((r - 400.0f) / 900.0f, 0.0f, 1.0f);
    return -0.05f + flat * flat * (dunes - 0.6f) * 9.0f + far * dunes * 70.0f;
}

static M4 xf_box(V3 c, V3 size, f32 rx, f32 ry, f32 rz)
{
    return m4_mul(m4_translate(c), m4_mul(m4_rot_y(ry), m4_mul(m4_rot_x(rx), m4_mul(m4_rot_z(rz), m4_scale(size)))));
}

static void box(MeshBuilder *mb, V3 c, V3 size)
{
    mb_box(mb, xf_box(c, size, 0, 0, 0));
}

static void glyph_panel(MeshBuilder *mb, V3 c, V3 normal, f32 hw, f32 hh, V3 color)
{
    mb_set(mb, color, MAT_GLYPH, BONE_NONE);
    V3 right = v3_norm(v3_cross(v3(0, 1, 0), normal));
    mb_quad(mb, v3_add(c, v3_mul(normal, 0.03f)), v3_mul(right, hw), v3(0, hh, 0));
}

static void hall(MeshBuilder *mb, Rng *rng)
{
    mb_set(mb, C_CONCRETE, MAT_CONCRETE, BONE_NONE);

    // Плита пола
    box(mb, v3(0, -0.3f, -2), v3(64, 0.8f, 52));

    // Две колоннады
    for (int row = -1; row <= 1; row += 2)
        for (int k = 0; k < 7; k++) {
            f32 x = (f32)row * 9.0f, z = -18.0f + (f32)k * 6.0f;
            bool broken = row == 1 && k == 6;
            f32 h = broken ? 5.5f : 15.0f;
            box(mb, v3(x, h * 0.5f, z), v3(1.6f, h, 1.6f));
            // Утолщение у основания
            box(mb, v3(x, 0.5f, z), v3(2.2f, 1.0f, 2.2f));
            if (broken) {
                mb_box(mb, xf_box(v3(x + 3.5f, 0.9f, z + 2.0f), v3(1.6f, 1.6f, 7.5f), 0.08f, 0.9f, 0.1f));
                mb_box(mb, xf_box(v3(x + 1.5f, 0.6f, z - 1.5f), v3(1.3f, 1.2f, 1.6f), 0.3f, 0.4f, 0.2f));
            }
        }

    // Балки поверх колонн и перекрытие полосами: свет проходит в щели
    for (int k = 0; k < 6; k++) {
        f32 z = -18.0f + (f32)k * 6.0f;
        box(mb, v3(0, 15.6f, z), v3(20.0f, 1.2f, 1.4f));
    }
    box(mb, v3(-9, 16.7f, -3), v3(1.8f, 1.0f, 38.0f));
    box(mb, v3(9, 16.7f, -6), v3(1.8f, 1.0f, 32.0f));
    for (int k = 0; k < 6; k++) {
        f32 z = -19.0f + (f32)k * 2.9f;
        box(mb, v3(0, 17.5f, z), v3(19.0f, 0.6f, 1.9f));
    }

    // Задняя стена с проёмами
    f32 wz = -22.0f, wh = 22.0f;
    f32 gates[] = { -14.0f, 0.0f, 14.0f };
    f32 gw = 6.0f, gh = 10.0f;
    f32 x0 = -34.0f;
    for (u32 g = 0; g <= ARRAY_COUNT(gates); g++) {
        f32 x1 = g < ARRAY_COUNT(gates) ? gates[g] - gw * 0.5f : 34.0f;
        box(mb, v3((x0 + x1) * 0.5f, wh * 0.5f, wz), v3(x1 - x0, wh, 2.4f));
        if (g < ARRAY_COUNT(gates)) {
            box(mb, v3(gates[g], (gh + wh) * 0.5f, wz), v3(gw, wh - gh, 2.4f));
            x0 = gates[g] + gw * 0.5f;
        }
    }
    // Контрфорсы
    for (int k = -3; k <= 3; k++) {
        f32 x = (f32)k * 9.5f + 4.7f;
        if (fabsf(x) > 33.0f) continue;
        mb_box(mb, xf_box(v3(x, 7.0f, wz - 3.5f), v3(1.4f, 16.0f, 5.0f), -0.28f, 0, 0));
    }

    // Обрушенная боковая стена: ступенчатые обломки
    mb_set(mb, C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
    for (int k = 0; k < 12; k++) {
        f32 z = -20.0f + (f32)k * 2.6f;
        f32 h = fmaxf(1.0f, 13.0f - (f32)k * 1.1f + rng_range(rng, -1.5f, 1.5f));
        box(mb, v3(-25.0f, h * 0.5f, z), v3(2.0f, h, 2.7f));
    }

    // Обломки и арматура
    for (int i = 0; i < 70; i++) {
        f32 ang = rng_range(rng, 0, 2 * PI_F);
        f32 r = rng_range(rng, 2.0f, 30.0f);
        f32 s = rng_range(rng, 0.25f, 1.4f);
        V3 c = v3(cosf(ang) * r, s * 0.3f, sinf(ang) * r - 4.0f);
        if (fabsf(c.x) < 6.5f && c.z > -16.0f) continue; // проход по центру
        mb_set(mb, rng_f32(rng) < 0.5f ? C_CONCRETE : C_CONCRETE_DARK, MAT_CONCRETE, BONE_NONE);
        mb_box(mb, xf_box(c, v3(s * rng_range(rng, 0.8f, 2.0f), s, s * rng_range(rng, 0.7f, 1.5f)),
                          rng_range(rng, -0.4f, 0.4f), rng_range(rng, 0, PI_F), rng_range(rng, -0.4f, 0.4f)));
        if (rng_f32(rng) < 0.35f) {
            mb_set(mb, C_RUST, MAT_RUST, BONE_NONE);
            V3 tip = v3_add(c, v3(rng_range(rng, -0.6f, 0.6f), rng_range(rng, 0.8f, 1.8f), rng_range(rng, -0.6f, 0.6f)));
            mb_cylinder(mb, c, tip, 0.025f, 6);
        }
    }

    // Трубы вдоль задней стены
    mb_set(mb, C_RUST, MAT_RUST, BONE_NONE);
    for (int i = 0; i < 2; i++) {
        f32 y = 4.0f + (f32)i * 1.3f, r = 0.45f - (f32)i * 0.1f;
        mb_cylinder(mb, v3(-33, y, wz + 1.9f), v3(33, y, wz + 1.9f), r, 14);
        for (f32 x = -32.0f; x <= 32.0f; x += 4.0f)
            mb_cylinder(mb, v3(x - 0.1f, y, wz + 1.9f), v3(x + 0.1f, y, wz + 1.9f), r + 0.12f, 14);
        for (f32 x = -26.0f; x <= 26.0f; x += 13.0f)
            box(mb, v3(x, y - 0.3f - (f32)i * 0.2f, wz + 1.5f), v3(0.3f, 0.2f, 0.9f));
    }
    mb_cylinder(mb, v3(20, 0, wz + 1.9f), v3(20, 22, wz + 1.9f), 0.6f, 14);
    mb_cylinder(mb, v3(-6, 0, wz + 1.9f), v3(-6, 5.3f, wz + 1.9f), 0.3f, 10);

    // Лежащий резервуар
    mb_set(mb, v3(0.26f, 0.19f, 0.14f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(16, 3.0f, 10), v3(22, 3.4f, -2), 3.0f, 24);
    for (int i = 0; i < 4; i++) {
        f32 t = 0.15f + 0.23f * (f32)i;
        V3 c = v3_lerp(v3(16, 3.0f, 10), v3(22, 3.4f, -2), t);
        V3 d = v3_mul(v3_norm(v3(6, 0.4f, -12)), 0.15f);
        mb_cylinder(mb, v3_sub(c, d), v3_add(c, d), 3.15f, 24);
    }

    // Контейнеры
    mb_set(mb, v3(0.30f, 0.18f, 0.11f), MAT_RUST, BONE_NONE);
    mb_box(mb, xf_box(v3(-15, 1.3f, 8), v3(2.4f, 2.6f, 6.0f), 0, 0.15f, 0));
    mb_box(mb, xf_box(v3(-14.2f, 3.4f, 4.5f), v3(2.4f, 2.6f, 6.0f), 0.12f, -0.35f, 0.25f));

    // Машина неизвестного назначения со знаками
    mb_set(mb, v3(0.22f, 0.21f, 0.20f), MAT_METAL, BONE_NONE);
    box(mb, v3(-4, 1.6f, -15), v3(5.0f, 3.2f, 3.0f));
    mb_set(mb, C_RUST, MAT_RUST, BONE_NONE);
    box(mb, v3(-4, 3.6f, -15.5f), v3(3.6f, 0.8f, 2.0f));
    mb_cylinder(mb, v3(-6, 4.0f, -15.5f), v3(-6, 7.5f, -16.5f), 0.25f, 10);
    glyph_panel(mb, v3(-4, 1.8f, -13.5f), v3(0, 0, 1), 1.8f, 0.7f, C_GLYPH_GREEN);

    // Знаки на стене и колоннах
    glyph_panel(mb, v3(0, 13.5f, wz + 1.2f), v3(0, 0, 1), 6.0f, 1.6f, C_GLYPH_GREEN);
    glyph_panel(mb, v3(-24.0f, 16.0f, wz + 1.2f), v3(0, 0, 1), 3.0f, 0.4f, C_GLYPH_RED);
    glyph_panel(mb, v3(-9, 4.5f, 6.8f), v3(0, 0, 1), 0.65f, 1.6f, C_GLYPH_RED);
    glyph_panel(mb, v3(9, 6.0f, -5.2f), v3(0, 0, 1), 0.65f, 2.2f, C_GLYPH_GREEN);

    // Красные огни на колоннах
    mb_set(mb, v3(0.9f, 0.12f, 0.06f), MAT_EMISSIVE, BONE_NONE);
    box(mb, v3(-9, 8.0f, 6.85f), v3(0.18f, 0.18f, 0.1f));
    box(mb, v3(9, 8.0f, -5.15f), v3(0.18f, 0.18f, 0.1f));
    mb_set(mb, v3(0.9f, 0.6f, 0.25f), MAT_EMISSIVE, BONE_NONE);
    box(mb, v3(-4, 3.25f, -13.4f), v3(0.4f, 0.12f, 0.05f));
}

// Мегаструктуры у горизонта: стена, башня, пилоны, мост
static void megastructures(MeshBuilder *mb, Rng *rng)
{
    mb_set(mb, v3(0.30f, 0.29f, 0.27f), MAT_CONCRETE, BONE_NONE);
    f32 wz = -720.0f;
    box(mb, v3(0, 260.0f, wz), v3(3600.0f, 560.0f, 70.0f));
    for (int k = -14; k <= 14; k++) {
        f32 x = (f32)k * 130.0f;
        f32 h = 620.0f + rng_range(rng, -40.0f, 90.0f);
        box(mb, v3(x, h * 0.5f - 20.0f, wz + 45.0f), v3(42.0f, h, 40.0f));
        if (rng_f32(rng) < 0.4f) box(mb, v3(x, h + 10.0f, wz + 30.0f), v3(12.0f, 60.0f, 12.0f));
    }
    // Башня с кольцами
    box(mb, v3(420.0f, 450.0f, -560.0f), v3(90.0f, 1000.0f, 90.0f));
    for (int i = 0; i < 5; i++)
        box(mb, v3(420.0f, 180.0f + (f32)i * 150.0f, -560.0f), v3(150.0f, 14.0f, 150.0f));
    // Наклонный пилон
    mb_box(mb, xf_box(v3(-520.0f, 220.0f, -420.0f), v3(60.0f, 600.0f, 60.0f), 0.0f, 0.3f, -0.35f));
    // Мост на опорах над пустыней
    mb_set(mb, v3(0.27f, 0.26f, 0.25f), MAT_CONCRETE, BONE_NONE);
    box(mb, v3(-200.0f, 140.0f, -330.0f), v3(1400.0f, 22.0f, 40.0f));
    for (int k = -3; k <= 2; k++)
        box(mb, v3((f32)k * 220.0f, 65.0f, -330.0f), v3(28.0f, 150.0f, 28.0f));
    // Огромная труба
    mb_set(mb, v3(0.30f, 0.20f, 0.13f), MAT_RUST, BONE_NONE);
    mb_cylinder(mb, v3(-1500.0f, 60.0f, -600.0f), v3(1500.0f, 90.0f, -650.0f), 32.0f, 24);
}

void testroom_build_near(MeshBuilder *mb, Rng *rng)
{
    hall(mb, rng);
}

void testroom_build_far(MeshBuilder *mb, Rng *rng)
{
    mb_set(mb, C_SAND, MAT_SAND, BONE_NONE);
    mb_terrain(mb, 3000.0f, 220, terrain_height, NULL);
    megastructures(mb, rng);
}

f32 testroom_ground(f32 x, f32 z)
{
    // Плита пола зала: верх на 0.1
    if (fabsf(x) <= 32.0f && z >= -28.0f && z <= 24.0f) return 0.1f;
    return terrain_height(x, z, NULL);
}

static void pick_target(Crowd *c, Rng *rng, u32 i)
{
    player_order_move(&c->body[i], v3(rng_range(rng, -14.0f, 14.0f), 0.0f, rng_range(rng, -16.0f, 18.0f)));
    c->body[i].marker = 0.0f;
}

void crowd_init(Crowd *c, Rng *rng, u32 count)
{
    c->count = MIN(count, TR_MAX_CHARS);
    for (u32 i = 0; i < c->count; i++) {
        f32 x = rng_range(rng, -8.0f, 8.0f), z = rng_range(rng, -12.0f, 14.0f);
        player_init(&c->body[i], v3(x, testroom_ground(x, z), z), rng_range(rng, 0, 2 * PI_F));
        c->body[i].phase = rng_f32(rng);
        c->body[i].gait = GAIT_WALK;
        c->wait[i] = rng_range(rng, 0.0f, 3.0f);
        c->seed[i] = rng_range(rng, 0, 100.0f);
        c->variant[i] = (u8)(i % 4);
        c->stationary[i] = i % 4 == 3;
    }
    // Первый всегда идёт к камере: сразу видно ходьбу
    if (c->count > 0) {
        player_init(&c->body[0], v3(3.0f, testroom_ground(3.0f, 0.0f), 0.0f), 0.0f);
        player_order_move(&c->body[0], v3(3.0f, 0.0f, 16.0f));
        c->body[0].marker = 0.0f;
        c->wait[0] = 0;
        c->stationary[0] = false;
    }
}

void crowd_update(Crowd *c, Rng *rng, f32 dt)
{
    for (u32 i = 0; i < c->count; i++) {
        Player *p = &c->body[i];
        bool was = p->moving;
        if (!c->stationary[i] && !p->moving && (c->wait[i] -= dt) <= 0.0f) pick_target(c, rng, i);
        p->vmax = 1.15f;
        player_update(p, dt, testroom_ground);
        if (was && !p->moving) c->wait[i] = rng_range(rng, 2.0f, 7.0f);
    }
}

// ---------------------------------------------------------------- мобы

// Маршрут вокруг зала и арены, против часовой
static const V2 g_route[MOB_ROUTE] = {
    { -36.0f, -33.0f }, { 0.0f, -36.0f }, { 37.0f, -32.0f }, { 38.0f, 30.0f }, { 14.0f, 50.0f }, { -37.0f, 32.0f },
};

typedef struct MobDef {
    const char *name;
    u8 tier, weapon, gait;
    u8 armor[3];
} MobDef;

static const MobDef g_mob_defs[MOB_MAX] = {
    { "ВЫШАТА", 1, W_RESSORA, GAIT_WALK, { A_RAGS, A_JACKET, ARMOR_NONE } },
    { "ФЕОКТИСТ", 1, W_BAGOR, GAIT_JOG, { A_QUILT, ARMOR_NONE, ARMOR_NONE } },
    { "ГОРДИАН", 2, W_BALKA, GAIT_WALK, { A_QUILT, A_PLATE, A_BUCKET } },
    { "МИЛОНЕГ", 0, W_ARMATURA, GAIT_RUN, { A_RAGS, ARMOR_NONE, ARMOR_NONE } },
    { "КАССИЙ", 1, W_SEKACH, GAIT_JOG, { A_JACKET, A_GREAVES, ARMOR_NONE } },
};

void mobs_init(Mobs *m, Rng *rng)
{
    memset(m, 0, sizeof *m);
    m->count = MOB_MAX;
    for (u32 i = 0; i < m->count; i++) {
        const MobDef *d = &g_mob_defs[i];
        CharSheet *c = &m->sheet[i];
        c->name = d->name;
        arena_random_fighter(&c->d, rng, d->tier);
        for (u32 s = 0; s < SLOT_COUNT; s++) c->d.armor[s] = ARMOR_NONE;
        for (u32 p = 0; p < PART_COUNT; p++) c->d.hp[p] = g_part_hp[p];
        c->d.blood = 100.0f * c->d.mass / 75.0f;
        Item old;
        c->d.weapon = d->weapon;
        sheet_equip(c, SLOT_WEAPON, item_make(IK_WEAPON, d->weapon, Q_WORN, 1), &old);
        for (u32 k = 0; k < 3 && d->armor[k] != ARMOR_NONE; k++) {
            Item it = item_make(IK_ARMOR, d->armor[k], Q_WORN, 1);
            sheet_equip(c, item_slot(it), it, &old);
        }
        sheet_equip(c, SLOT_BACK, item_make(IK_GOODS, G_PACK, Q_WORN, 1), &old);
        // Разнесены по кругу, у каждого своя точка старта
        u32 at = (i * 7u + 1u) % MOB_ROUTE;
        V2 a = g_route[at], b = g_route[(at + 1) % MOB_ROUTE];
        f32 t = rng_range(rng, 0.1f, 0.8f);
        f32 x = lerpf(a.x, b.x, t), z = lerpf(a.y, b.y, t);
        player_init(&m->body[i], v3(x, testroom_ground(x, z), z), atan2f(b.x - a.x, b.y - a.y));
        m->body[i].gait = d->gait;
        m->body[i].phase = rng_f32(rng);
        m->next[i] = (u8)((at + 1) % MOB_ROUTE);
        m->variant[i] = (u8)(i % 4);
        m->seed[i] = rng_range(rng, 0.0f, 100.0f);
        player_order_move(&m->body[i], v3(b.x, 0.0f, b.y));
        m->body[i].marker = 0.0f;
    }
}

void mobs_update(Mobs *m, Rng *rng, f32 dt)
{
    for (u32 i = 0; i < m->count; i++) {
        Player *p = &m->body[i];
        bool was = p->moving;
        p->vmax = g_gait_speed[p->gait] * (0.9f + 0.002f * m->sheet[i].d.skill[SK_ATHLETICS]);
        if (!p->moving && (m->wait[i] -= dt) <= 0.0f) {
            V2 t = g_route[m->next[i]];
            // Не точно в точку: каждый круг немного иначе
            player_order_move(p, v3(t.x + rng_range(rng, -2.0f, 2.0f), 0.0f, t.y + rng_range(rng, -2.0f, 2.0f)));
            p->marker = 0.0f;
            m->next[i] = (u8)((m->next[i] + 1) % MOB_ROUTE);
        }
        player_update(p, dt, testroom_ground);
        // Дошёл: иногда стоит, переводит дух, иногда сразу дальше
        if (was && !p->moving) m->wait[i] = rng_f32(rng) < 0.55f ? rng_range(rng, 6.0f, 16.0f) : 0.0f;
    }
}
