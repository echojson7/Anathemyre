#include "look.h"

#include <stdio.h>

#include "../core/hash.h"
#include "../core/mesh.h"

const char *const g_body_file[BODY_KIND_COUNT] = { "mh_male", "mh_female", "mh_thin" };

V3 g_body_joints[BODY_KIND_COUNT][BONE_COUNT];

bool look_load_skeletons(const char *data_dir) {
    char path[512];
    for (int k = 0; k < BODY_KIND_COUNT; k++) {
        snprintf(path, sizeof path, "%s/meshes/%s.skel", data_dir, g_body_file[k]);
        if (!body_skeleton_load(path, g_body_joints[k])) return false;
    }
    return true;
}

const char *const g_gear_file[GEAR_COUNT] = {
    "rags", "quilt", "jacket", "plate", "bucket", "greaves", "wrap", "boots", "mitts", "pack", "belt", "pants",
};

// Порядок как HAIR_FEMALE и HAIR_MALE в tools/humanconv
#define HAIR_MH_MALE { "crop", "shag", "tied", "buzz", "slick", "long" }
const char *const g_hair_file[BODY_KIND_COUNT][HAIR_MAX] = {
    [BODY_MALE] = HAIR_MH_MALE,
    [BODY_FEMALE] = { "tail", "bun", "braid", "crop", "bob", "loose", "side" },
    [BODY_THIN] = HAIR_MH_MALE,
};

u32 g_gear_hides[GEAR_COUNT];

u32 look_hair(const CharLook *l, u8 *gear, u32 n)
{
    if (!l->cut) return 0;
    for (u32 i = 0; i < n; i++)
        if (gear[i] < GEAR_COUNT && (g_gear_hides[gear[i]] & GEAR_HIDES_HAIR)) return 0;
    gear[n] = (u8)(GEAR_COUNT + l->cut - 1);
    return 1;
}

// Закрытое другой вещью не рисуется: штаны под штаниной комбинезона
static u32 drop_hidden(u8 *out, u32 n)
{
    u32 hidden = 0;
    for (u32 i = 0; i < n; i++) hidden |= g_gear_hides[out[i]];
    if (!(hidden & ((1u << GEAR_COUNT) - 1u))) return n;
    u32 m = 0;
    for (u32 i = 0; i < n; i++)
        if (!(hidden & (1u << out[i]))) out[m++] = out[i];
    return m;
}

u32 gear_from_armor(const u8 *armor, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++)
        if (armor[s] < g_armor_count && g_armors[armor[s]].look < GEAR_COUNT) out[n++] = g_armors[armor[s]].look;
    return drop_hidden(out, n);
}

u32 gear_from_sheet(const CharSheet *cs, u8 *out)
{
    u32 n = 0;
    for (u32 s = 0; s < SLOT_COUNT; s++) {
        const Item *it = &cs->equip[s];
        u8 g = it->kind == IK_ARMOR ? g_armors[it->id].look : it->kind == IK_GOODS ? g_goods[it->id].look : GEAR_NONE;
        if (g < GEAR_COUNT) out[n++] = g;
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
    if (hash01(&s) < 0.85f && !(g_gear_hides[out[0]] & (1u << GEAR_PANTS))) out[n++] = GEAR_PANTS;
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

static f32 hash01(u32 *s) { return hashf(*s += 0x9e3779b9u); }

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
    l->morph[LOOK_JAW] = body == BODY_FEMALE ? 0.8f * hash01(&s) - 0.6f : 0.9f * hash01(&s) - 0.4f;  // челюсть не выше +50, как в редакторе
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
