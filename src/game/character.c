#include "character.h"

#define HIP_Y   BODY_HIP_Y
#define KNEE_Y  BODY_KNEE_Y
#define ANKLE_Y BODY_ANKLE_Y
#define HIP_X   BODY_HIP_X

typedef struct Palette {
    V3 cloak, jacket, trousers, leather, boots, wrap, rust, dark_metal, lens;
} Palette;

static const Palette g_palettes[] = {
    // Пыльный странник
    { { 0.42f, 0.36f, 0.27f }, { 0.25f, 0.24f, 0.20f }, { 0.22f, 0.21f, 0.18f }, { 0.30f, 0.18f, 0.10f },
      { 0.12f, 0.10f, 0.08f }, { 0.48f, 0.43f, 0.35f }, { 0.50f, 0.28f, 0.14f }, { 0.14f, 0.14f, 0.15f },
      { 0.10f, 0.35f, 0.18f } },
    // Серо-зелёный, бывший охранник
    { { 0.26f, 0.30f, 0.24f }, { 0.20f, 0.22f, 0.19f }, { 0.17f, 0.18f, 0.16f }, { 0.22f, 0.15f, 0.10f },
      { 0.09f, 0.09f, 0.08f }, { 0.36f, 0.36f, 0.31f }, { 0.46f, 0.27f, 0.15f }, { 0.12f, 0.12f, 0.13f },
      { 0.40f, 0.06f, 0.04f } },
    // Выцветший ржаво-красный, культист знаков
    { { 0.44f, 0.20f, 0.12f }, { 0.22f, 0.16f, 0.13f }, { 0.20f, 0.17f, 0.14f }, { 0.28f, 0.17f, 0.10f },
      { 0.10f, 0.08f, 0.07f }, { 0.44f, 0.37f, 0.29f }, { 0.48f, 0.27f, 0.14f }, { 0.13f, 0.13f, 0.13f },
      { 0.35f, 0.30f, 0.10f } },
    // Тёмный, почти чёрный: наёмник
    { { 0.16f, 0.15f, 0.14f }, { 0.13f, 0.13f, 0.12f }, { 0.12f, 0.12f, 0.11f }, { 0.18f, 0.12f, 0.08f },
      { 0.07f, 0.06f, 0.06f }, { 0.30f, 0.28f, 0.24f }, { 0.42f, 0.25f, 0.14f }, { 0.10f, 0.10f, 0.11f },
      { 0.45f, 0.08f, 0.05f } },
};

#define SEG 16

// Уровень детализации текущей сборки: 0 — полный, дальше грубее
static u32 g_lod;

// Сегментов по кругу на текущем уровне
static u32 segs(u32 n)
{
    static const f32 k[CHAR_LODS] = { 1.0f, 0.5f, 0.3f };
    u32 s = (u32)((f32)n * k[g_lod] + 0.5f);
    return s < 4 ? 4 : s;
}

// Мелкие детали, которых на самом грубом уровне не видно
static bool fine(void) { return g_lod + 1 < CHAR_LODS; }

static V3 mirror(V3 p) { return v3(-p.x, p.y, p.z); }
static V3 side_x(V3 p, bool left) { return left ? p : mirror(p); }

static void caps(MeshBuilder *mb, V3 a, V3 b, f32 r0, f32 r1, f32 ex, f32 ez)
{
    mb_capsule(mb, a, b, r0, r1, segs(SEG), ex, ez);
}

static void box_at(MeshBuilder *mb, V3 c, V3 size, f32 rx, f32 ry, f32 rz)
{
    M4 xf = m4_mul(m4_translate(c), m4_mul(m4_rot_y(ry), m4_mul(m4_rot_x(rx), m4_mul(m4_rot_z(rz), m4_scale(size)))));
    mb_box(mb, xf);
}

static f32 hashf(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (f32)(x & 0xFFFFFF) / 16777215.0f;
}

// Складки ткани и рваный подол: смещение вершин детали после построения.
// folds — число складок по кругу, hem_y — высота подола
static void cloth_folds(MeshBuilder *mb, u32 v0, u32 i0, f32 top_y, f32 hem_y, u32 folds, f32 amp, f32 rag, u32 seed)
{
    for (u32 k = v0; k < mb->vcount; k++) {
        Vertex *v = &mb->v[k];
        f32 x = v->pos[0], y = v->pos[1], z = v->pos[2];
        f32 t = clampf((top_y - y) / (top_y - hem_y), 0.0f, 1.0f);
        f32 ang = atan2f(x, z);
        f32 r = sqrtf(x * x + z * z);
        if (r < 1e-4f) continue;
        f32 wave = sinf(ang * (f32)folds + hashf(seed) * 6.0f) * 0.6f + sinf(ang * (f32)(folds * 2 + 1) + 1.3f) * 0.4f;
        f32 nr = r + amp * wave * t * t;
        v->pos[0] = x / r * nr;
        v->pos[2] = z / r * nr;
        if (t > 0.98f) {
            // Рваный край: соседние лоскуты разной длины
            u32 cell = (u32)((ang + PI_F) / (2.0f * PI_F) * 23.0f);
            v->pos[1] -= rag * hashf(cell * 7919u + seed);
        }
    }
    mb_smooth_normals(mb, v0, i0);
}

// Рука: плечо в куртке, предплечье в обмотках, кисть в перчатке с большим пальцем
static void arm(MeshBuilder *mb, const Palette *pal, bool left)
{
    u8 upper = left ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
    u8 fore = left ? BONE_FOREARM_L : BONE_FOREARM_R;
    f32 sx = left ? 1.0f : -1.0f;
    mb_set(mb, pal->jacket, MAT_CLOTH, upper);
    caps(mb, side_x(v3(0.21f, 1.45f, 0), left), side_x(v3(0.24f, 1.17f, 0), left), 0.068f, 0.054f, 1.0f, 1.0f);
    mb_set(mb, pal->wrap, MAT_CLOTH, fore);
    caps(mb, side_x(v3(0.24f, 1.16f, 0), left), side_x(v3(0.25f, 0.93f, 0.01f), left), 0.052f, 0.04f, 1.0f, 1.0f);
    // Витки обмоток
    for (int i = 0; i < 4 && fine(); i++) {
        f32 y = 1.10f - (f32)i * 0.05f;
        V2 ring[] = { { 0.0f, 0.052f - (f32)i * 0.003f }, { 0.018f, 0.052f - (f32)i * 0.003f } };
        mb_lathe(mb, side_x(v3(0.245f, y, 0.004f), left), side_x(v3(0.25f, y - 1.0f, 0.04f), left), ring, 2, segs(SEG), 1.0f, 1.0f);
    }
    u8 hand = left ? BONE_HAND_L : BONE_HAND_R;
    mb_set(mb, pal->leather, MAT_LEATHER, hand);
    caps(mb, side_x(v3(0.25f, 0.95f, 0.012f), left), side_x(v3(0.25f, 0.905f, 0.015f), left), 0.046f, 0.046f, 1.0f, 1.0f);
    box_at(mb, side_x(v3(0.252f, 0.85f, 0.025f), left), v3(0.045f, 0.10f, 0.085f), 0.05f, 0, 0.0f);
    box_at(mb, side_x(v3(0.25f, 0.79f, 0.03f), left), v3(0.04f, 0.04f, 0.075f), 0.25f, 0, 0.0f);
    caps(mb, side_x(v3(0.24f, 0.87f, 0.06f), left), side_x(v3(0.23f, 0.82f, 0.075f), left), 0.016f, 0.013f, 1.0f, 1.0f);
    (void)sx;
}

// Нога: бедро, наколенник, голень, сапог со шнуровкой и подошвой
static void leg(MeshBuilder *mb, const Palette *pal, bool left)
{
    u8 thigh = left ? BONE_THIGH_L : BONE_THIGH_R;
    u8 shin = left ? BONE_SHIN_L : BONE_SHIN_R;
    u8 foot = left ? BONE_FOOT_L : BONE_FOOT_R;
    f32 sx = left ? 1.0f : -1.0f;
    mb_set(mb, pal->trousers, MAT_CLOTH, thigh);
    caps(mb, side_x(v3(HIP_X, HIP_Y, 0.0f), left), side_x(v3(HIP_X + 0.005f, KNEE_Y + 0.02f, 0.0f), left), 0.092f, 0.066f, 1.0f, 1.05f);
    // Набедренный карман
    box_at(mb, side_x(v3(HIP_X + 0.075f, 0.78f, 0.01f), left), v3(0.04f, 0.13f, 0.11f), 0, 0, 0.06f * sx);
    mb_set(mb, pal->trousers, MAT_CLOTH, shin);
    caps(mb, side_x(v3(HIP_X, KNEE_Y, 0.0f), left), side_x(v3(HIP_X, 0.30f, -0.005f), left), 0.062f, 0.052f, 1.0f, 1.0f);
    mb_set(mb, pal->rust, MAT_RUST, shin);
    box_at(mb, side_x(v3(HIP_X, KNEE_Y, 0.065f), left), v3(0.095f, 0.12f, 0.035f), -0.12f, 0, 0);
    box_at(mb, side_x(v3(HIP_X, KNEE_Y + 0.045f, 0.06f), left), v3(0.08f, 0.03f, 0.03f), -0.5f, 0, 0);
    // Голенище
    mb_set(mb, pal->boots, MAT_LEATHER, shin);
    caps(mb, side_x(v3(HIP_X, 0.33f, -0.004f), left), side_x(v3(HIP_X, 0.13f, 0.0f), left), 0.066f, 0.064f, 1.0f, 1.08f);
    {
        V2 cuff[] = { { 0.0f, 0.072f }, { 0.035f, 0.072f } };
        mb_lathe(mb, side_x(v3(HIP_X, 0.31f, -0.004f), left), side_x(v3(HIP_X, 1.0f, -0.004f), left), cuff, 2, segs(SEG), 1.0f, 1.1f);
    }
    // Стопа: носок, подъём, каблук, подошва
    mb_set(mb, pal->boots, MAT_LEATHER, foot);
    caps(mb, side_x(v3(HIP_X, 0.075f, 0.0f), left), side_x(v3(HIP_X, 0.06f, 0.13f), left), 0.06f, 0.048f, 1.05f, 0.85f);
    box_at(mb, side_x(v3(HIP_X, 0.045f, 0.06f), left), v3(0.105f, 0.07f, 0.2f), 0, 0, 0);
    mb_set(mb, v3(0.05f, 0.045f, 0.04f), MAT_LEATHER, foot);
    box_at(mb, side_x(v3(HIP_X, 0.012f, 0.055f), left), v3(0.115f, 0.024f, 0.29f), 0, 0, 0);
    box_at(mb, side_x(v3(HIP_X, 0.03f, -0.055f), left), v3(0.1f, 0.04f, 0.07f), 0, 0, 0);
    // Ремешки на подъёме
    mb_set(mb, pal->rust, MAT_METAL, foot);
    for (int i = 0; i < 2 && fine(); i++)
        box_at(mb, side_x(v3(HIP_X, 0.1f + (f32)i * 0.04f, 0.035f + (f32)i * 0.01f), left), v3(0.125f, 0.014f, 0.06f), -0.3f, 0, 0);
}

void character_build(MeshBuilder *mb, u32 variant, bool armed, u32 lod)
{
    g_lod = lod < CHAR_LODS ? lod : CHAR_LODS - 1;
    const Palette *pal = &g_palettes[variant % ARRAY_COUNT(g_palettes)];
    u32 seed = variant * 977u + 13u;

    // Таз, пояс, подсумки
    mb_set(mb, pal->trousers, MAT_CLOTH, BONE_PELVIS);
    caps(mb, v3(0, 0.92f, 0), v3(0, 1.06f, 0), 0.145f, 0.15f, 1.0f, 0.8f);
    mb_set(mb, pal->leather, MAT_LEATHER, BONE_PELVIS);
    {
        V2 belt[] = { { 0.0f, 0.162f }, { 0.055f, 0.162f } };
        mb_lathe(mb, v3(0, 0.995f, 0), v3(0, 1.2f, 0), belt, 2, segs(SEG), 1.0f, 0.82f);
    }
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_PELVIS);
    box_at(mb, v3(0, 1.02f, 0.135f), v3(0.07f, 0.05f, 0.02f), 0, 0, 0);
    mb_set(mb, pal->leather, MAT_LEATHER, BONE_PELVIS);
    for (int i = 0; i < 3; i++) {
        f32 a = -0.7f + (f32)i * 0.55f;
        box_at(mb, v3(sinf(a) * 0.17f, 0.97f, cosf(a) * 0.13f), v3(0.075f, 0.1f, 0.05f), 0.05f, a, 0);
    }
    // Фляга на боку
    mb_set(mb, pal->rust, MAT_RUST, BONE_PELVIS);
    caps(mb, v3(-0.18f, 0.90f, -0.04f), v3(-0.18f, 1.0f, -0.04f), 0.045f, 0.04f, 1.0f, 0.6f);

    // Торс: грудь шире талии, сплюснут спереди назад
    mb_set(mb, pal->jacket, MAT_CLOTH, BONE_CHEST);
    {
        V2 torso[] = {
            { 0.00f, 0.00f }, { 0.00f, 0.145f }, { 0.10f, 0.152f }, { 0.22f, 0.175f }, { 0.32f, 0.195f },
            { 0.40f, 0.20f }, { 0.45f, 0.185f }, { 0.49f, 0.15f }, { 0.52f, 0.09f }, { 0.53f, 0.0f },
        };
        mb_lathe(mb, v3(0, 0.97f, 0), v3(0, 2.0f, 0), torso, ARRAY_COUNT(torso), segs(SEG + 4), 1.0f, 0.66f);
    }
    // Нагрудник и ремни крест-накрест
    mb_set(mb, pal->rust, MAT_RUST, BONE_CHEST);
    box_at(mb, v3(0, 1.30f, 0.125f), v3(0.24f, 0.2f, 0.035f), -0.1f, 0, 0);
    box_at(mb, v3(0, 1.17f, 0.12f), v3(0.2f, 0.07f, 0.03f), 0.05f, 0, 0);
    mb_set(mb, pal->leather, MAT_LEATHER, BONE_CHEST);
    box_at(mb, v3(0.0f, 1.25f, 0.145f), v3(0.04f, 0.5f, 0.012f), -0.08f, 0, 0.62f);
    box_at(mb, v3(0.0f, 1.25f, -0.135f), v3(0.04f, 0.5f, 0.012f), 0.08f, 0, -0.62f);

    // Шарф-горловина
    mb_set(mb, pal->wrap, MAT_CLOTH, BONE_HEAD);
    {
        V2 scarf[] = { { 0.0f, 0.10f }, { 0.04f, 0.115f }, { 0.08f, 0.10f }, { 0.11f, 0.075f } };
        mb_lathe(mb, v3(0, 1.45f, 0.0f), v3(0, 2.0f, 0.01f), scarf, ARRAY_COUNT(scarf), segs(SEG), 1.0f, 1.0f);
    }

    // Накидка на плечах со складками и рваным подолом
    mb_set(mb, pal->cloak, MAT_CLOTH, BONE_CHEST);
    {
        V2 mantle[] = {
            { 0.00f, 0.08f }, { 0.03f, 0.17f }, { 0.07f, 0.245f }, { 0.13f, 0.285f }, { 0.22f, 0.30f },
            { 0.32f, 0.31f }, { 0.40f, 0.32f }, { 0.46f, 0.325f },
        };
        u32 v0 = mb->vcount, i0 = mb->icount;
        mb_lathe(mb, v3(0, 1.56f, 0), v3(0, 0.0f, 0), mantle, ARRAY_COUNT(mantle), segs(40), 1.0f, 0.82f);
        cloth_folds(mb, v0, i0, 1.50f, 1.10f, 9, fine() ? 0.018f : 0.0f, 0.08f, seed);
    }

    // Капюшон, респиратор и линзы
    mb_set(mb, pal->cloak, MAT_CLOTH, BONE_HEAD);
    {
        V2 hood[] = {
            { 0.00f, 0.10f }, { 0.05f, 0.13f }, { 0.11f, 0.142f }, { 0.17f, 0.14f }, { 0.23f, 0.118f },
            { 0.28f, 0.075f }, { 0.31f, 0.03f }, { 0.32f, 0.0f },
        };
        u32 v0 = mb->vcount, i0 = mb->icount;
        mb_lathe(mb, v3(0, 1.50f, -0.02f), v3(0.0f, 2.0f, -0.08f), hood, ARRAY_COUNT(hood), segs(24), 1.0f, 1.14f);
        cloth_folds(mb, v0, i0, 1.80f, 1.52f, 5, fine() ? 0.008f : 0.0f, 0.0f, seed + 1);
    }
    // Тень лица внутри капюшона
    mb_set(mb, v3(0.03f, 0.03f, 0.03f), MAT_LEATHER, BONE_HEAD);
    caps(mb, v3(0, 1.62f, 0.07f), v3(0, 1.72f, 0.07f), 0.085f, 0.075f, 1.0f, 0.8f);
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HEAD);
    caps(mb, v3(0, 1.605f, 0.12f), v3(0, 1.585f, 0.19f), 0.056f, 0.045f, 1.0f, 1.0f);
    for (int i = 0; i < 3 && fine(); i++)
        box_at(mb, v3(0, 1.575f + (f32)i * 0.017f, 0.193f), v3(0.05f, 0.006f, 0.008f), 0, 0, 0);
    mb_set(mb, pal->rust, MAT_RUST, BONE_HEAD);
    mb_cylinder(mb, v3(0.05f, 1.59f, 0.16f), v3(0.11f, 1.565f, 0.18f), 0.036f, segs(12));
    mb_cylinder(mb, v3(-0.05f, 1.59f, 0.16f), v3(-0.11f, 1.565f, 0.18f), 0.036f, segs(12));
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HEAD);
    mb_cylinder(mb, v3(0.046f, 1.685f, 0.125f), v3(0.046f, 1.685f, 0.16f), 0.032f, segs(12));
    mb_cylinder(mb, v3(-0.046f, 1.685f, 0.125f), v3(-0.046f, 1.685f, 0.16f), 0.032f, segs(12));
    box_at(mb, v3(0, 1.685f, 0.13f), v3(0.17f, 0.03f, 0.03f), 0, 0, 0);
    mb_set(mb, pal->lens, MAT_EMISSIVE, BONE_HEAD);
    mb_cylinder(mb, v3(0.046f, 1.685f, 0.16f), v3(0.046f, 1.685f, 0.166f), 0.025f, segs(12));
    mb_cylinder(mb, v3(-0.046f, 1.685f, 0.16f), v3(-0.046f, 1.685f, 0.166f), 0.025f, segs(12));

    arm(mb, pal, true);
    arm(mb, pal, false);
    leg(mb, pal, true);
    leg(mb, pal, false);

    // Наплечник из обрезка листа на левом плече
    mb_set(mb, pal->rust, MAT_RUST, BONE_CHEST);
    box_at(mb, v3(0.25f, 1.535f, 0.0f), v3(0.2f, 0.035f, 0.27f), 0, 0, -0.5f);
    box_at(mb, v3(0.32f, 1.44f, 0.0f), v3(0.035f, 0.15f, 0.24f), 0, 0, -0.18f);
    mb_set(mb, pal->dark_metal, MAT_METAL, BONE_CHEST);
    for (int i = -1; i <= 1 && fine(); i += 2) box_at(mb, v3(0.26f, 1.55f, 0.09f * (f32)i), v3(0.025f, 0.02f, 0.025f), 0, 0, -0.5f);

    if (variant % 3 != 1) {
        // Рюкзак с каркасом, скаткой и котелком
        mb_set(mb, pal->leather, MAT_LEATHER, BONE_CHEST);
        box_at(mb, v3(0, 1.27f, -0.25f), v3(0.30f, 0.38f, 0.15f), 0.05f, 0, 0);
        box_at(mb, v3(0, 1.36f, -0.165f + -0.16f), v3(0.27f, 0.12f, 0.04f), 0.15f, 0, 0);
        box_at(mb, v3(0.17f, 1.22f, -0.25f), v3(0.06f, 0.16f, 0.11f), 0.05f, 0, 0);
        box_at(mb, v3(-0.17f, 1.22f, -0.25f), v3(0.06f, 0.16f, 0.11f), 0.05f, 0, 0);
        mb_set(mb, pal->cloak, MAT_CLOTH, BONE_CHEST);
        caps(mb, v3(-0.16f, 1.51f, -0.25f), v3(0.16f, 1.51f, -0.25f), 0.075f, 0.075f, 1.0f, 1.0f);
        mb_set(mb, pal->rust, MAT_RUST, BONE_CHEST);
        if (fine()) {
            mb_cylinder(mb, v3(0.18f, 1.06f, -0.26f), v3(0.18f, 1.52f, -0.26f), 0.013f, segs(8));
            mb_cylinder(mb, v3(-0.18f, 1.06f, -0.26f), v3(-0.18f, 1.52f, -0.26f), 0.013f, segs(8));
        }
        mb_cylinder(mb, v3(-0.07f, 1.05f, -0.29f), v3(0.07f, 1.05f, -0.29f), 0.06f, segs(12));
    }

    // Оружие в правой руке
    if (!armed) return;
    if (variant % 2 == 0) {
        // Длинный клинок из обрезка листа
        mb_set(mb, pal->leather, MAT_LEATHER, BONE_HAND_R);
        mb_cylinder(mb, v3(-0.25f, 0.93f, 0.05f), v3(-0.25f, 0.76f, 0.05f), 0.019f, segs(10));
        mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HAND_R);
        box_at(mb, v3(-0.25f, 0.755f, 0.05f), v3(0.035f, 0.025f, 0.16f), 0, 0, 0);
        mb_cylinder(mb, v3(-0.25f, 0.95f, 0.05f), v3(-0.25f, 0.93f, 0.05f), 0.028f, segs(10));
        mb_set(mb, v3(0.36f, 0.35f, 0.33f), MAT_METAL, BONE_HAND_R);
        box_at(mb, v3(-0.25f, 0.37f, 0.085f), v3(0.012f, 0.76f, 0.07f), -0.08f, 0, 0);
        box_at(mb, v3(-0.25f, -0.02f, 0.115f), v3(0.011f, 0.06f, 0.05f), -0.5f, 0, 0);
    } else {
        // Дубина из трубы с намотанной проволокой
        mb_set(mb, pal->rust, MAT_RUST, BONE_HAND_R);
        mb_cylinder(mb, v3(-0.25f, 0.96f, 0.03f), v3(-0.26f, 0.22f, 0.12f), 0.024f, segs(10));
        mb_cylinder(mb, v3(-0.26f, 0.40f, 0.098f), v3(-0.26f, 0.20f, 0.124f), 0.05f, segs(10));
        mb_set(mb, pal->dark_metal, MAT_METAL, BONE_HAND_R);
        for (int i = 0; i < 4; i++) {
            f32 t = 0.22f + (f32)i * 0.045f;
            mb_cylinder(mb, v3(-0.26f, t + 0.01f, 0.098f + (0.4f - t) * 0.13f), v3(-0.26f, t, 0.1f + (0.4f - t) * 0.13f), 0.056f, segs(10));
        }
        mb_set(mb, pal->leather, MAT_LEATHER, BONE_HAND_R);
        mb_cylinder(mb, v3(-0.25f, 0.93f, 0.035f), v3(-0.252f, 0.76f, 0.055f), 0.03f, segs(10));
    }
}
