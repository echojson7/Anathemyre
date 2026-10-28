#include "character.h"

typedef struct BoneDef {
    i32 parent;
    V3  joint;  // сустав в позе привязки, пространство модели
} BoneDef;

static const BoneDef g_bones[BONE_COUNT] = {
    [BONE_PELVIS]      = { -1, { 0.0f, 1.00f, 0.0f } },
    [BONE_CHEST]       = { BONE_PELVIS, { 0.0f, 1.05f, 0.0f } },
    [BONE_HEAD]        = { BONE_CHEST, { 0.0f, 1.50f, 0.0f } },
    [BONE_UPPER_ARM_L] = { BONE_CHEST, { 0.21f, 1.44f, 0.0f } },
    [BONE_FOREARM_L]   = { BONE_UPPER_ARM_L, { 0.24f, 1.16f, 0.0f } },
    [BONE_UPPER_ARM_R] = { BONE_CHEST, { -0.21f, 1.44f, 0.0f } },
    [BONE_FOREARM_R]   = { BONE_UPPER_ARM_R, { -0.24f, 1.16f, 0.0f } },
    [BONE_THIGH_L]     = { BONE_PELVIS, { 0.10f, 0.97f, 0.0f } },
    [BONE_SHIN_L]      = { BONE_THIGH_L, { 0.11f, 0.53f, 0.0f } },
    [BONE_THIGH_R]     = { BONE_PELVIS, { -0.10f, 0.97f, 0.0f } },
    [BONE_SHIN_R]      = { BONE_THIGH_R, { -0.11f, 0.53f, 0.0f } },
};

typedef struct Palette {
    V3 cloak, jacket, trousers, leather, boots, wrap, rust, dark_metal, lens;
} Palette;

static const Palette g_palettes[] = {
    // Пыльный странник
    { { 0.32f, 0.28f, 0.22f }, { 0.20f, 0.19f, 0.16f }, { 0.17f, 0.16f, 0.14f }, { 0.22f, 0.14f, 0.09f },
      { 0.10f, 0.08f, 0.07f }, { 0.36f, 0.32f, 0.26f }, { 0.45f, 0.28f, 0.17f }, { 0.12f, 0.12f, 0.12f },
      { 0.10f, 0.30f, 0.16f } },
    // Серо-зелёный, бывший охранник
    { { 0.20f, 0.22f, 0.19f }, { 0.15f, 0.16f, 0.14f }, { 0.13f, 0.14f, 0.12f }, { 0.16f, 0.12f, 0.09f },
      { 0.08f, 0.08f, 0.07f }, { 0.28f, 0.28f, 0.24f }, { 0.40f, 0.26f, 0.16f }, { 0.10f, 0.10f, 0.11f },
      { 0.35f, 0.06f, 0.04f } },
    // Выцветший ржаво-красный, культист знаков
    { { 0.30f, 0.16f, 0.11f }, { 0.17f, 0.13f, 0.11f }, { 0.15f, 0.13f, 0.11f }, { 0.20f, 0.13f, 0.09f },
      { 0.09f, 0.07f, 0.06f }, { 0.33f, 0.28f, 0.22f }, { 0.42f, 0.26f, 0.15f }, { 0.11f, 0.11f, 0.11f },
      { 0.30f, 0.26f, 0.10f } },
};

#define SEG 12

static V3 mirror(V3 p) { return v3(-p.x, p.y, p.z); }

static void limb(MeshBuilder *mb, bool left, u8 bone, V3 a, V3 b, f32 r0, f32 r1, V3 col, Material m)
{
    mb_set(mb, col, m, bone);
    if (left) mb_capsule(mb, a, b, r0, r1, SEG, 1.0f, 1.0f);
    else mb_capsule(mb, mirror(a), mirror(b), r0, r1, SEG, 1.0f, 1.0f);
}

static void box_at(MeshBuilder *mb, V3 c, V3 size, f32 rx, f32 ry, f32 rz)
{
    M4 xf = m4_mul(m4_translate(c), m4_mul(m4_rot_y(ry), m4_mul(m4_rot_x(rx), m4_mul(m4_rot_z(rz), m4_scale(size)))));
    mb_box(mb, xf);
}

void character_build(MeshBuilder *mb, u32 variant)
{
    const Palette *pal = &g_palettes[variant % ARRAY_COUNT(g_palettes)];

    // Таз и пояс
    mb_set(mb, pal->trousers, MAT_CLOTH, BONE_PELVIS);
    mb_capsule(mb, v3(0, 0.92f, 0), v3(0, 1.06f, 0), 0.14f, 0.15f, SEG, 1.0f, 0.78f);
    mb_set(mb, pal->leather, MAT_LEATHER, BONE_PELVIS);
    {
        V2 belt[] = { { 0.0f, 0.158f }, { 0.07f, 0.158f } };
        mb_lathe(mb, v3(0, 0.99f, 0), v3(0, 1.2f, 0), belt, 2, SEG, 1.0f, 0.8f);
    }
    box_at(mb, v3(0.12f, 0.97f, 0.09f), v3(0.08f, 0.1f, 0.05f), 0, 0.5f, 0);
    box_at(mb, v3(-0.13f, 0.97f, 0.06f), v3(0.07f, 0.12f, 0.06f), 0, -0.7f, 0);

    // Торс
    mb_set(mb, pal->jacket, MAT_CLOTH, BONE_CHEST);
    {
        V2 torso[] = {
            { 0.00f, 0.00f }, { 0.00f, 0.14f }, { 0.12f, 0.15f }, { 0.28f, 0.18f },
            { 0.40f, 0.19f }, { 0.47f, 0.16f }, { 0.52f, 0.08f }, { 0.53f, 0.0f },
        };
        mb_lathe(mb, v3(0, 0.97f, 0), v3(0, 2.0f, 0), torso, ARRAY_COUNT(torso), SEG + 2, 1.0f, 0.68f);
    }
    // Накидка на плечах
    mb_set(mb, pal->cloak, MAT_CLOTH, BONE_CHEST);
    {
        V2 mantle[] = {
            { 0.00f, 0.07f }, { 0.03f, 0.16f }, { 0.08f, 0.24f }, { 0.18f, 0.28f },
            { 0.34f, 0.30f }, { 0.42f, 0.31f },
        };
        mb_lathe(mb, v3(0, 1.55f, 0), v3(0, 0.0f, 0), mantle, ARRAY_COUNT(mantle), SEG + 4, 1.0f, 0.82f);
    }

    // Капюшон, респиратор и линзы
    mb_set(mb, pal->cloak, MAT_CLOTH, BONE_HEAD);
    {
        V2 hood[] = {
            { 0.00f, 0.10f }, { 0.06f, 0.135f }, { 0.15f, 0.14f }, { 0.23f, 0.115f },
            { 0.29f, 0.06f }, { 0.31f, 0.0f },
        };
        mb_lathe(mb, v3(0, 1.50f, -0.015f), v3(0, 2.0f, -0.03f), hood, ARRAY_COUNT(hood), SEG, 1.0f, 1.12f);
    }
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HEAD);
    mb_capsule(mb, v3(0, 1.60f, 0.10f), v3(0, 1.585f, 0.19f), 0.055f, 0.045f, 10, 1.0f, 1.0f);
    mb_set(mb, pal->rust, MAT_RUST, BONE_HEAD);
    mb_cylinder(mb, v3(0.05f, 1.585f, 0.15f), v3(0.11f, 1.56f, 0.17f), 0.035f, 10);
    mb_cylinder(mb, v3(-0.05f, 1.585f, 0.15f), v3(-0.11f, 1.56f, 0.17f), 0.035f, 10);
    mb_set(mb, pal->lens, MAT_EMISSIVE, BONE_HEAD);
    mb_cylinder(mb, v3(0.048f, 1.685f, 0.12f), v3(0.048f, 1.685f, 0.165f), 0.026f, 10);
    mb_cylinder(mb, v3(-0.048f, 1.685f, 0.12f), v3(-0.048f, 1.685f, 0.165f), 0.026f, 10);
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HEAD);
    box_at(mb, v3(0, 1.685f, 0.125f), v3(0.16f, 0.035f, 0.03f), 0, 0, 0);

    // Руки: плечо в куртке, предплечье в обмотках, перчатка
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        u8 upper = left ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
        u8 fore = left ? BONE_FOREARM_L : BONE_FOREARM_R;
        limb(mb, left, upper, v3(0.21f, 1.44f, 0), v3(0.24f, 1.17f, 0), 0.066f, 0.055f, pal->jacket, MAT_CLOTH);
        limb(mb, left, fore, v3(0.24f, 1.16f, 0), v3(0.25f, 0.92f, 0.01f), 0.052f, 0.043f, pal->wrap, MAT_CLOTH);
        limb(mb, left, fore, v3(0.25f, 0.89f, 0.015f), v3(0.25f, 0.81f, 0.03f), 0.046f, 0.036f, pal->leather, MAT_LEATHER);
    }

    // Ноги: бедро, голень, сапог, ступня, наколенник
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        u8 thigh = left ? BONE_THIGH_L : BONE_THIGH_R;
        u8 shin = left ? BONE_SHIN_L : BONE_SHIN_R;
        f32 sx = left ? 1.0f : -1.0f;
        limb(mb, left, thigh, v3(0.10f, 0.97f, 0), v3(0.11f, 0.54f, 0), 0.088f, 0.066f, pal->trousers, MAT_CLOTH);
        limb(mb, left, shin, v3(0.11f, 0.53f, 0), v3(0.11f, 0.20f, 0), 0.062f, 0.05f, pal->trousers, MAT_CLOTH);
        limb(mb, left, shin, v3(0.11f, 0.32f, 0), v3(0.11f, 0.07f, 0), 0.068f, 0.07f, pal->boots, MAT_LEATHER);
        mb_set(mb, pal->boots, MAT_LEATHER, shin);
        box_at(mb, v3(0.11f * sx, 0.045f, 0.055f), v3(0.11f, 0.09f, 0.25f), 0, 0, 0);
        mb_set(mb, pal->rust, MAT_RUST, shin);
        box_at(mb, v3(0.11f * sx, 0.53f, 0.06f), v3(0.1f, 0.12f, 0.04f), -0.15f, 0, 0);
    }

    // Снаряжение по варианту
    mb_set(mb, pal->rust, MAT_RUST, BONE_CHEST);
    box_at(mb, v3(0.25f, 1.53f, 0.0f), v3(0.2f, 0.04f, 0.26f), 0, 0, -0.5f);
    box_at(mb, v3(0.31f, 1.43f, 0.0f), v3(0.04f, 0.14f, 0.22f), 0, 0, -0.15f);

    if (variant % 3 != 1) {
        // Рюкзак со скаткой
        mb_set(mb, pal->leather, MAT_LEATHER, BONE_CHEST);
        box_at(mb, v3(0, 1.28f, -0.25f), v3(0.30f, 0.36f, 0.15f), 0.05f, 0, 0);
        mb_set(mb, pal->cloak, MAT_CLOTH, BONE_CHEST);
        mb_capsule(mb, v3(-0.15f, 1.50f, -0.24f), v3(0.15f, 1.50f, -0.24f), 0.07f, 0.07f, SEG, 1.0f, 1.0f);
        mb_set(mb, pal->rust, MAT_RUST, BONE_CHEST);
        mb_cylinder(mb, v3(0.17f, 1.12f, -0.24f), v3(0.17f, 1.36f, -0.24f), 0.045f, 8);
    }

    // Оружие в правой руке
    if (variant % 2 == 0) {
        // Длинный клинок из обрезка листа
        mb_set(mb, pal->leather, MAT_LEATHER, BONE_FOREARM_R);
        mb_cylinder(mb, v3(-0.25f, 0.92f, 0.04f), v3(-0.25f, 0.76f, 0.05f), 0.02f, 8);
        mb_set(mb, pal->dark_metal, MAT_METAL, BONE_FOREARM_R);
        box_at(mb, v3(-0.25f, 0.76f, 0.05f), v3(0.03f, 0.03f, 0.14f), 0, 0, 0);
        mb_set(mb, v3(0.30f, 0.29f, 0.27f), MAT_METAL, BONE_FOREARM_R);
        box_at(mb, v3(-0.25f, 0.36f, 0.09f), v3(0.012f, 0.78f, 0.075f), -0.08f, 0, 0);
    } else {
        // Дубина из трубы
        mb_set(mb, pal->rust, MAT_RUST, BONE_FOREARM_R);
        mb_cylinder(mb, v3(-0.25f, 0.95f, 0.03f), v3(-0.26f, 0.25f, 0.12f), 0.025f, 8);
        mb_cylinder(mb, v3(-0.26f, 0.38f, 0.10f), v3(-0.26f, 0.22f, 0.125f), 0.05f, 8);
    }
}

void character_pose(const CharPose *p, M4 *out)
{
    f32 s = sinf(p->phase), c = cosf(p->phase), w = p->walk;
    f32 breathe = sinf(p->time * 1.7f + p->seed) * 0.02f;
    f32 look = sinf(p->time * 0.31f + p->seed * 3.0f) * 0.5f * (1.0f - w);
    f32 bob = w * (fabsf(c) * 0.045f - 0.03f);

    M4 local[BONE_COUNT];
    local[BONE_PELVIS] = m4_rot_y(0.10f * s * w);
    local[BONE_CHEST] = m4_mul(m4_rot_x(0.10f * w + breathe), m4_rot_y(-0.14f * s * w));
    local[BONE_HEAD] = m4_mul(m4_rot_y(look), m4_rot_x(-0.06f * w + 0.08f * (1.0f - w)));
    local[BONE_UPPER_ARM_L] = m4_mul(m4_rot_z(0.07f + breathe), m4_rot_x(0.45f * s * w));
    local[BONE_FOREARM_L] = m4_rot_x(-0.25f - 0.25f * fmaxf(0.0f, -s) * w);
    local[BONE_UPPER_ARM_R] = m4_mul(m4_rot_z(-0.07f - breathe), m4_rot_x(-0.35f * s * w));
    local[BONE_FOREARM_R] = m4_rot_x(-0.35f);
    local[BONE_THIGH_L] = m4_rot_x(-0.5f * s * w);
    local[BONE_SHIN_L] = m4_rot_x((0.05f + 0.85f * fmaxf(0.0f, c)) * w + 0.03f);
    local[BONE_THIGH_R] = m4_rot_x(0.5f * s * w);
    local[BONE_SHIN_R] = m4_rot_x((0.05f + 0.85f * fmaxf(0.0f, -c)) * w + 0.03f);

    M4 root = m4_mul(m4_translate(v3_add(p->pos, v3(0, bob, 0))), m4_rot_y(p->heading));
    M4 world[BONE_COUNT];
    for (int i = 0; i < BONE_COUNT; i++) {
        const BoneDef *b = &g_bones[i];
        V3 rel = b->parent < 0 ? b->joint : v3_sub(b->joint, g_bones[b->parent].joint);
        M4 parent = b->parent < 0 ? root : world[b->parent];
        world[i] = m4_mul(parent, m4_mul(m4_translate(rel), local[i]));
        out[i] = m4_mul(world[i], m4_translate(v3_mul(b->joint, -1.0f)));
    }
}
