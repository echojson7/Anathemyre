#include "look.h"

const char *const g_body_file[BODY_KIND_COUNT] = { "mh_male", "mh_female", "mh_thin" };

const char *const g_gear_file[GEAR_COUNT] = {
    "rags", "quilt", "jacket", "plate", "bucket", "greaves", "wrap", "boots", "mitts", "pack", "belt",
};

// Порядок как HAIR_FEMALE и HAIR_MALE в tools/humanconv
const char *const g_hair_file[BODY_KIND_COUNT][HAIR_MAX] = {
    [BODY_MALE] = { "crop", "shag", "tied" },
    [BODY_FEMALE] = { "tail", "bun", "braid", "crop" },
    [BODY_THIN] = { "crop", "shag", "tied" },
};

u32 look_hair(const CharLook *l, u8 *out)
{
    if (!l->cut) return 0;
    out[0] = (u8)(GEAR_COUNT + l->cut - 1);
    return 1;
}

static const u8 g_armor_gear[A_COUNT] = {
    [A_RAGS] = GEAR_RAGS, [A_JACKET] = GEAR_JACKET, [A_QUILT] = GEAR_QUILT,
    [A_PLATE] = GEAR_PLATE, [A_BUCKET] = GEAR_BUCKET, [A_GREAVES] = GEAR_GREAVES,
};

u32 gear_from_armor(const u8 *armor, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (armor[s] < A_COUNT) out[n++] = g_armor_gear[armor[s]];
    return n;
}

u32 gear_from_sheet(const CharSheet *cs, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++) {
        const Item *it = &cs->equip[s];
        if (it->kind == IK_ARMOR && it->id < A_COUNT) out[n++] = g_armor_gear[it->id];
        else if (it->kind == IK_GOODS && it->id >= G_WRAP && it->id <= G_BELT) out[n++] = (u8)(GEAR_WRAP + (it->id - G_WRAP));
    }
    return n;
}

static f32 hash01(u32 *s);

u32 gear_random(u32 seed, u8 *out)
{
    u32 s = seed * 2246822519u + 5u, n = 0;
    f32 t = hash01(&s);
    out[n++] = t < 0.45f ? GEAR_RAGS : t < 0.75f ? GEAR_QUILT : GEAR_JACKET;
    if (hash01(&s) < 0.5f) out[n++] = GEAR_WRAP;
    if (hash01(&s) < 0.75f) out[n++] = GEAR_BOOTS;
    if (hash01(&s) < 0.25f) out[n++] = GEAR_PACK;
    if (hash01(&s) < 0.4f) out[n++] = GEAR_BELT;
    return n;
}

// Цвета, запечённые в сетках (PEOPLE в tools/humanconv): оттенок экземпляра — отношение к ним
static const V3 g_base_skin[BODY_KIND_COUNT] = { { 0.52f, 0.37f, 0.28f }, { 0.60f, 0.45f, 0.36f }, { 0.38f, 0.26f, 0.19f } };
static const V3 g_base_hair[BODY_KIND_COUNT] = { { 0.10f, 0.08f, 0.06f }, { 0.20f, 0.13f, 0.08f }, { 0.07f, 0.06f, 0.05f } };

// Кожа от бледной до тёмной, загар и пыль пустыни
static const V3 g_skin[] = {
    { 0.68f, 0.52f, 0.41f }, { 0.61f, 0.45f, 0.34f }, { 0.55f, 0.40f, 0.30f },
    { 0.47f, 0.33f, 0.24f }, { 0.39f, 0.27f, 0.19f }, { 0.29f, 0.20f, 0.14f },
};
// Волосы почти у всех тёмные: первые цвета выпадают чаще
static const V3 g_hair[] = {
    { 0.030f, 0.025f, 0.022f }, { 0.055f, 0.040f, 0.030f }, { 0.090f, 0.060f, 0.040f },
    { 0.120f, 0.085f, 0.055f }, { 0.110f, 0.060f, 0.038f }, { 0.150f, 0.125f, 0.095f },
};

static f32 hash01(u32 *s)
{
    u32 x = (*s += 0x9e3779b9u);
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (f32)(x & 0xFFFFFF) / 16777215.0f;
}

void look_make(CharLook *l, u32 body, u32 seed, f32 mass, f32 height, f32 strength)
{
    u32 s = seed * 2654435761u + 17u;
    for (u32 k = 0; k < LOOK_MORPHS; k++) l->morph[k] = 0.0f;
    // Полнота по индексу массы тела, сила по навыку
    f32 bmi = mass / (height * height);
    l->morph[LOOK_FAT] = clampf((bmi - 23.0f) / 7.0f, 0.0f, 0.85f);
    l->morph[LOOK_LEAN] = clampf((21.5f - bmi) / 4.0f, 0.0f, 0.9f);
    l->morph[LOOK_STRONG] = clampf((strength - 30.0f) / 40.0f + 0.2f * (hash01(&s) - 0.5f), 0.0f, 0.9f);
    l->morph[LOOK_WEAK] = clampf((18.0f - strength) / 14.0f, 0.0f, 0.6f);
    // Большинство молодые и средних лет, каждый седьмой старый
    f32 a = hash01(&s);
    l->morph[LOOK_OLD] = a < 0.86f ? 0.3f * a : 0.35f + 2.0f * (a - 0.86f);
    l->morph[LOOK_FACE_A] = 1.4f * (hash01(&s) - 0.5f);
    l->morph[LOOK_FACE_B] = 1.4f * (hash01(&s) - 0.5f);
    l->morph[LOOK_JAW] = body == BODY_FEMALE ? 0.8f * hash01(&s) - 0.6f : 1.2f * hash01(&s) - 0.4f;
    l->skin = g_skin[(u32)(hash01(&s) * (f32)ARRAY_COUNT(g_skin)) % ARRAY_COUNT(g_skin)];
    // Седина со старостью
    f32 hr = hash01(&s);
    V3 h = g_hair[(u32)(hr * hr * (f32)ARRAY_COUNT(g_hair)) % ARRAY_COUNT(g_hair)];
    l->hair = v3_lerp(h, v3(0.36f, 0.35f, 0.33f), clampf((l->morph[LOOK_OLD] - 0.25f) * 2.5f, 0.0f, 0.9f));
    // Причёска: у женщин всегда, мужчины через одного-двух бритые (тем чаще, чем старше)
    u32 nc = 0;
    while (nc < HAIR_MAX && g_hair_file[body][nc]) nc++;
    f32 c = hash01(&s);
    l->cut = body != BODY_FEMALE && c < 0.2f + 0.4f * l->morph[LOOK_OLD] ? 0 : (u8)(1 + (u32)(hash01(&s) * (f32)nc) % nc);
}

M4 look_matrix(const CharLook *l, u32 body)
{
    M4 m = { 0 };
    body = body < BODY_KIND_COUNT ? body : BODY_MALE;
    for (u32 k = 0; k < LOOK_MORPHS; k++) m.m[k] = l->morph[k];
    const V3 bs = g_base_skin[body], bh = g_base_hair[body];
    m.m[8] = l->skin.x / bs.x - 1.0f;
    m.m[9] = l->skin.y / bs.y - 1.0f;
    m.m[10] = l->skin.z / bs.z - 1.0f;
    m.m[12] = l->hair.x / bh.x - 1.0f;
    m.m[13] = l->hair.y / bh.y - 1.0f;
    m.m[14] = l->hair.z / bh.z - 1.0f;
    return m;
}
_Static_assert(GEAR_BELT - GEAR_WRAP == G_BELT - G_WRAP, "вещи G_WRAP..G_BELT идут подряд, как GEAR_WRAP..GEAR_BELT");
