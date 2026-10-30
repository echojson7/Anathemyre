#include "look.h"

#include "../render/mesh.h"

const char *const g_body_file[BODY_KIND_COUNT] = { "mh_male", "mh_female", "mh_thin" };

const V3 g_body_joints[BODY_KIND_COUNT][BONE_COUNT] = {
    [BODY_MALE] = {
        { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.970f, 0.003f }, { 0.000f, 1.061f, -0.030f }, { 0.000f, 1.197f, -0.030f },
        { 0.000f, 1.555f, 0.012f }, { 0.000f, 1.661f, 0.053f }, { 0.024f, 1.477f, 0.023f }, { 0.195f, 1.446f, 0.019f },
        { 0.228f, 1.141f, 0.019f }, { 0.240f, 0.881f, 0.031f }, { -0.024f, 1.477f, 0.023f }, { -0.195f, 1.446f, 0.019f },
        { -0.228f, 1.141f, 0.019f }, { -0.240f, 0.881f, 0.031f }, { 0.113f, 0.965f, -0.008f }, { 0.136f, 0.529f, 0.010f },
        { 0.159f, 0.075f, 0.029f }, { -0.113f, 0.965f, -0.008f }, { -0.136f, 0.529f, 0.010f }, { -0.159f, 0.075f, 0.029f },
    },
    [BODY_FEMALE] = {
        { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.940f, 0.027f }, { 0.000f, 1.022f, -0.025f }, { 0.000f, 1.196f, 0.013f },
        { 0.000f, 1.530f, -0.001f }, { 0.000f, 1.653f, 0.049f }, { 0.026f, 1.453f, 0.024f }, { 0.165f, 1.436f, 0.024f },
        { 0.197f, 1.135f, 0.024f }, { 0.209f, 0.882f, 0.036f }, { -0.026f, 1.453f, 0.024f }, { -0.165f, 1.436f, 0.024f },
        { -0.197f, 1.135f, 0.024f }, { -0.209f, 0.882f, 0.036f }, { 0.097f, 0.928f, 0.020f }, { 0.125f, 0.506f, 0.037f },
        { 0.153f, 0.077f, 0.054f }, { -0.097f, 0.928f, 0.020f }, { -0.125f, 0.506f, 0.037f }, { -0.153f, 0.077f, 0.054f },
    },
    [BODY_THIN] = {
        { 0.000f, 0.000f, 0.000f }, { 0.000f, 0.970f, 0.003f }, { 0.000f, 1.061f, -0.030f }, { 0.000f, 1.197f, -0.030f },
        { 0.000f, 1.555f, 0.012f }, { 0.000f, 1.661f, 0.053f }, { 0.024f, 1.477f, 0.023f }, { 0.195f, 1.446f, 0.019f },
        { 0.228f, 1.141f, 0.019f }, { 0.240f, 0.881f, 0.031f }, { -0.024f, 1.477f, 0.023f }, { -0.195f, 1.446f, 0.019f },
        { -0.228f, 1.141f, 0.019f }, { -0.240f, 0.881f, 0.031f }, { 0.113f, 0.965f, -0.008f }, { 0.136f, 0.529f, 0.010f },
        { 0.159f, 0.075f, 0.029f }, { -0.113f, 0.965f, -0.008f }, { -0.136f, 0.529f, 0.010f }, { -0.159f, 0.075f, 0.029f },
    },
};

const char *const g_gear_file[GEAR_COUNT] = {
    "rags", "quilt", "jacket", "plate", "bucket", "greaves", "wrap", "boots", "mitts", "pack", "belt", "pants",
};

// Порядок как HAIR_FEMALE и HAIR_MALE в tools/humanconv
const char *const g_hair_file[BODY_KIND_COUNT][HAIR_MAX] = {
    [BODY_MALE] = { "crop", "shag", "tied" },
    [BODY_FEMALE] = { "tail", "bun", "braid", "crop" },
    [BODY_THIN] = { "crop", "shag", "tied" },
};

u32 look_hair(const CharLook *l, u8 *gear, u32 n)
{
    if (!l->cut) return 0;
    for (u32 i = 0; i < n; i++)
        if (gear[i] == GEAR_BUCKET) return 0;
    gear[n] = (u8)(GEAR_COUNT + l->cut - 1);
    return 1;
}

static const u8 g_armor_gear[A_COUNT] = {
    [A_RAGS] = GEAR_RAGS, [A_JACKET] = GEAR_JACKET, [A_QUILT] = GEAR_QUILT,
    [A_PLATE] = GEAR_PLATE, [A_BUCKET] = GEAR_BUCKET, [A_GREAVES] = GEAR_GREAVES, [A_PANTS] = GEAR_PANTS,
};

// Штаны под комбинезоном не рисуются: их закрывает его штанина
static u32 drop_hidden(u8 *out, u32 n)
{
    bool quilt = false;
    for (u32 i = 0; i < n; i++) quilt |= out[i] == GEAR_QUILT;
    if (!quilt) return n;
    u32 m = 0;
    for (u32 i = 0; i < n; i++)
        if (out[i] != GEAR_PANTS) out[m++] = out[i];
    return m;
}

u32 gear_from_armor(const u8 *armor, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (armor[s] < A_COUNT) out[n++] = g_armor_gear[armor[s]];
    return drop_hidden(out, n);
}

u32 gear_from_sheet(const CharSheet *cs, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++) {
        const Item *it = &cs->equip[s];
        if (it->kind == IK_ARMOR && it->id < A_COUNT) out[n++] = g_armor_gear[it->id];
        else if (it->kind == IK_GOODS && it->id >= G_WRAP && it->id <= G_BELT) out[n++] = (u8)(GEAR_WRAP + (it->id - G_WRAP));
    }
    return drop_hidden(out, n);
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
    // Штаны почти у всех; под комбинезоном их не видно
    if (hash01(&s) < 0.85f && out[0] != GEAR_QUILT) out[n++] = GEAR_PANTS;
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
    // Большинство молодые и средних лет, каждый седьмой старый
    f32 a = hash01(&s);
    l->morph[LOOK_OLD] = a < 0.86f ? 0.3f * a : 0.35f + 2.0f * (a - 0.86f);
    l->morph[LOOK_NOSE] = 1.2f * (hash01(&s) - 0.5f);
    l->morph[LOOK_EYES] = 1.2f * (hash01(&s) - 0.5f);
    l->morph[LOOK_JAW] = body == BODY_FEMALE ? 0.8f * hash01(&s) - 0.6f : 1.2f * hash01(&s) - 0.4f;
    l->skin_t = (f32)((u32)(hash01(&s) * (f32)ARRAY_COUNT(g_skin)) % ARRAY_COUNT(g_skin)) / (f32)(ARRAY_COUNT(g_skin) - 1);
    // Волосы почти у всех тёмные, седина со старостью
    f32 hr = hash01(&s);
    l->hair_t = (f32)((u32)(hr * hr * (f32)ARRAY_COUNT(g_hair)) % ARRAY_COUNT(g_hair)) / (f32)(ARRAY_COUNT(g_hair) - 1);
    l->gray = clampf((l->morph[LOOK_OLD] - 0.25f) * 2.5f, 0.0f, 0.9f);
    look_paint(l);
    // Причёска: у женщин всегда, мужчины через одного-двух бритые (тем чаще, чем старше)
    u32 nc = look_cuts(body);
    f32 c = hash01(&s);
    l->cut = body != BODY_FEMALE && c < 0.2f + 0.4f * l->morph[LOOK_OLD] ? 0 : (u8)(1 + (u32)(hash01(&s) * (f32)nc) % nc);
    // Губы последними: кожа, волосы и причёска от того же зерна остались прежними
    l->morph[LOOK_LIPS] = body == BODY_FEMALE ? 0.9f * hash01(&s) - 0.3f : hash01(&s) - 0.6f;
}

// Точка на ряду цветов: t 0..1 от первого до последнего, между соседними плавно
static V3 palette_at(const V3 *pal, u32 n, f32 t)
{
    f32 k = clampf(t, 0.0f, 1.0f) * (f32)(n - 1);
    u32 a = MIN((u32)k, n - 2);
    return v3_lerp(pal[a], pal[a + 1], k - (f32)a);
}

void look_paint(CharLook *l)
{
    l->skin = palette_at(g_skin, ARRAY_COUNT(g_skin), l->skin_t);
    l->hair = v3_lerp(palette_at(g_hair, ARRAY_COUNT(g_hair), l->hair_t), v3(0.36f, 0.35f, 0.33f), clampf(l->gray, 0.0f, 1.0f));
}

u32 look_cuts(u32 body)
{
    u32 n = 0;
    while (body < BODY_KIND_COUNT && n < HAIR_MAX && g_hair_file[body][n]) n++;
    return n;
}

M4 look_matrix(const CharLook *l, u32 body)
{
    M4 m = { 0 };
    body = body < BODY_KIND_COUNT ? body : BODY_MALE;
    // Веса морфов: первые 8 чисел, 9-й и 10-й — в свободных m[11] и m[15] (scene.vert)
    for (u32 k = 0; k < LOOK_MORPHS; k++) m.m[k < 8 ? k : k == 8 ? 11 : 15] = l->morph[k];
    const V3 bs = g_base_skin[body], bh = g_base_hair[body];
    m.m[8] = l->skin.x / bs.x - 1.0f;
    m.m[9] = l->skin.y / bs.y - 1.0f;
    m.m[10] = l->skin.z / bs.z - 1.0f;
    m.m[12] = l->hair.x / bh.x - 1.0f;
    m.m[13] = l->hair.y / bh.y - 1.0f;
    m.m[14] = l->hair.z / bh.z - 1.0f;
    return m;
}
_Static_assert(LOOK_MORPHS <= MESH_MAX_MORPHS && MESH_MAX_MORPHS <= 10, "морфы внешности не влезают в матрицу");
_Static_assert(GEAR_BELT - GEAR_WRAP == G_BELT - G_WRAP, "вещи G_WRAP..G_BELT идут подряд, как GEAR_WRAP..GEAR_BELT");
