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

// ---------------------------------------------------------------- оружие арены

// Рукоять в кулаке правой руки в позе покоя; оружие висит вниз и чуть вперёд
#define GRIP v3(-0.25f, 0.86f, 0.045f)

typedef struct WeaponFrame {
    V3 s, a, f;  // поперёк (плашмя), вдоль к острию, вперёд (к лезвию)
} WeaponFrame;

static WeaponFrame weapon_frame(void)
{
    WeaponFrame w;
    w.a = v3_norm(v3(0.0f, -1.0f, 0.12f));
    w.s = v3(1.0f, 0.0f, 0.0f);
    w.f = v3_cross(w.s, w.a);
    w.f = v3_mul(w.f, -1.0f);
    return w;
}

// Брусок вдоль оси оружия от d0 до d1 (м от кулака); ширина к лезвию, толщина поперёк, сдвиг к лезвию
static void wbox(MeshBuilder *mb, const WeaponFrame *wf, f32 d0, f32 d1, f32 width, f32 thick, f32 off, f32 tilt)
{
    V3 c = v3_add(GRIP, v3_add(v3_mul(wf->a, 0.5f * (d0 + d1)), v3_mul(wf->f, off)));
    // Наклон бруска в плоскости лезвия
    V3 a = v3_norm(v3_add(wf->a, v3_mul(wf->f, tilt)));
    V3 f = v3_norm(v3_cross(a, wf->s));
    f = v3_dot(f, wf->f) < 0.0f ? v3_mul(f, -1.0f) : f;
    M4 m = m4_identity();
    V3 sx = v3_mul(wf->s, thick), sy = v3_mul(a, d1 - d0), sz = v3_mul(f, width);
    m.m[0] = sx.x; m.m[1] = sx.y; m.m[2] = sx.z;
    m.m[4] = sy.x; m.m[5] = sy.y; m.m[6] = sy.z;
    m.m[8] = sz.x; m.m[9] = sz.y; m.m[10] = sz.z;
    m.m[12] = c.x; m.m[13] = c.y; m.m[14] = c.z;
    mb_box(mb, m);
}

static void wcyl(MeshBuilder *mb, const WeaponFrame *wf, f32 d0, f32 d1, f32 r, f32 off)
{
    V3 o = v3_add(GRIP, v3_mul(wf->f, off));
    mb_cylinder(mb, v3_add(o, v3_mul(wf->a, d0)), v3_add(o, v3_mul(wf->a, d1)), r, segs(10));
}

void weapon_build(MeshBuilder *mb, u8 weapon)
{
    g_lod = 0;
    const WeaponFrame wf = weapon_frame();
    const V3 wrap = v3(0.30f, 0.26f, 0.20f), steel = v3(0.34f, 0.33f, 0.31f), dark = v3(0.13f, 0.13f, 0.14f);
    const V3 rust = v3(0.42f, 0.24f, 0.13f), wood = v3(0.24f, 0.17f, 0.11f);
    switch (weapon) {
    case W_RESSORA:
        // Лист рессоры: длинная пологая дуга, рукоять в тряпье
        mb_set(mb, wrap, MAT_CLOTH, BONE_HAND_R);
        wcyl(mb, &wf, -0.12f, 0.10f, 0.02f, 0.0f);
        mb_set(mb, dark, MAT_METAL, BONE_HAND_R);
        wbox(mb, &wf, 0.10f, 0.13f, 0.12f, 0.03f, 0.01f, 0.0f);
        mb_set(mb, steel, MAT_METAL, BONE_HAND_R);
        wbox(mb, &wf, 0.13f, 0.50f, 0.065f, 0.012f, 0.012f, 0.03f);
        wbox(mb, &wf, 0.48f, 0.86f, 0.06f, 0.011f, 0.035f, 0.10f);
        wbox(mb, &wf, 0.84f, 0.98f, 0.04f, 0.010f, 0.07f, 0.25f);
        break;
    case W_SEKACH:
        // Тесак из обрезка листа: широкое тяжёлое лезвие, обух с зазубринами
        mb_set(mb, wood, MAT_LEATHER, BONE_HAND_R);
        wcyl(mb, &wf, -0.13f, 0.10f, 0.022f, 0.0f);
        mb_set(mb, rust, MAT_RUST, BONE_HAND_R);
        wbox(mb, &wf, 0.10f, 0.95f, 0.14f, 0.014f, 0.045f, 0.0f);
        wbox(mb, &wf, 0.93f, 1.05f, 0.10f, 0.013f, 0.03f, 0.5f);
        mb_set(mb, dark, MAT_METAL, BONE_HAND_R);
        for (int i = 0; i < 4; i++) wbox(mb, &wf, 0.25f + 0.15f * (f32)i, 0.31f + 0.15f * (f32)i, 0.03f, 0.02f, -0.03f, 0.0f);
        break;
    case W_ARMATURA:
        // Пук арматуры, на конце ком бетона
        mb_set(mb, wrap, MAT_CLOTH, BONE_HAND_R);
        wcyl(mb, &wf, -0.12f, 0.12f, 0.024f, 0.0f);
        mb_set(mb, rust, MAT_RUST, BONE_HAND_R);
        wcyl(mb, &wf, 0.0f, 0.90f, 0.014f, 0.012f);
        wcyl(mb, &wf, 0.0f, 0.85f, 0.013f, -0.012f);
        mb_set(mb, v3(0.38f, 0.36f, 0.33f), MAT_CONCRETE, BONE_HAND_R);
        wbox(mb, &wf, 0.66f, 0.92f, 0.14f, 0.12f, 0.0f, 0.15f);
        break;
    case W_BAGOR:
        // Багор: длинное древко, крюк и пика на конце; хват у нижней трети
        mb_set(mb, wood, MAT_LEATHER, BONE_HAND_R);
        wcyl(mb, &wf, -0.55f, 1.95f, 0.02f, 0.0f);
        mb_set(mb, wrap, MAT_CLOTH, BONE_HAND_R);
        wcyl(mb, &wf, -0.10f, 0.12f, 0.026f, 0.0f);
        wcyl(mb, &wf, 0.55f, 0.75f, 0.026f, 0.0f);
        mb_set(mb, rust, MAT_RUST, BONE_HAND_R);
        wcyl(mb, &wf, 1.90f, 2.20f, 0.012f, 0.0f);
        wbox(mb, &wf, 1.90f, 2.06f, 0.11f, 0.014f, 0.06f, -0.6f);
        break;
    case W_BALKA:
    default:
        // Обрезок двутавра: полки и стенка, рукоять обмотана
        mb_set(mb, wrap, MAT_CLOTH, BONE_HAND_R);
        wcyl(mb, &wf, -0.18f, 0.16f, 0.028f, 0.0f);
        mb_set(mb, v3(0.30f, 0.20f, 0.13f), MAT_RUST, BONE_HAND_R);
        wbox(mb, &wf, 0.15f, 1.40f, 0.16f, 0.012f, 0.0f, 0.0f);
        wbox(mb, &wf, 0.15f, 1.40f, 0.014f, 0.11f, 0.08f, 0.0f);
        wbox(mb, &wf, 0.15f, 1.40f, 0.014f, 0.11f, -0.08f, 0.0f);
        break;
    }
}

void weapon_ground_pose(V3 pos, f32 yaw, M4 *out)
{
    // Ось оружия ложится горизонтально, рукоять в pos
    M4 m = m4_mul(m4_translate(pos), m4_mul(m4_rot_y(yaw), m4_mul(m4_rot_x(PI_F * 0.5f - 0.12f), m4_translate(v3_mul(GRIP, -1.0f)))));
    for (int i = 0; i < BONE_COUNT; i++) out[i] = m;
}

// Двухзвенный IK ноги в плоскости шага: возвращает углы бедра (наклон и крен) и колена
static void leg_ik(V3 hip, V3 target, f32 *thigh_pitch, f32 *thigh_roll, f32 *knee)
{
    const f32 l1 = HIP_Y - KNEE_Y, l2 = KNEE_Y - ANKLE_Y;
    V3 d = v3_sub(target, hip);
    *thigh_roll = atan2f(d.x, -d.y);
    f32 down = sqrtf(d.x * d.x + d.y * d.y);
    f32 len = clampf(sqrtf(down * down + d.z * d.z), 0.2f, l1 + l2 - 0.002f);
    f32 a = atan2f(d.z, down);
    f32 a1 = acosf(clampf((l1 * l1 + len * len - l2 * l2) / (2.0f * l1 * len), -1.0f, 1.0f));
    f32 k = acosf(clampf((l1 * l1 + l2 * l2 - len * len) / (2.0f * l1 * l2), -1.0f, 1.0f));
    *thigh_pitch = -(a + a1);   // отрицательный поворот вокруг X выносит ногу вперёд
    *knee = PI_F - k;           // положительный сгибает колено назад
}

static f32 smooth01(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Траектория стопы за цикл: опора (стопа стоит на земле) и перенос (дуга вперёд)
static void foot_track(f32 q, f32 run, f32 stance, f32 stride, f32 *z, f32 *lift, f32 *pitch)
{
    const f32 half = stance * stride * 0.5f;
    f32 wl, wp, rl, rp;
    if (q < stance) {
        f32 t = q / stance;
        *z = half - 2.0f * half * t;
        f32 heel = smooth01((t - 0.7f) / 0.3f);
        // Шаг: удар пяткой (носок вверх) -> плоская стопа -> подъём пятки (носок вниз)
        wl = 0.06f * heel;
        wp = -0.25f * (1.0f - smooth01(t / 0.15f)) + 0.45f * heel;
        // Бег: ставит стопу плоско, сразу толкается носком
        rl = 0.05f * smooth01((t - 0.5f) / 0.5f);
        rp = 0.55f * smooth01((t - 0.3f) / 0.7f);
    } else {
        f32 t = (q - stance) / (1.0f - stance);
        *z = -half + 2.0f * half * smooth01(t);
        wl = 0.06f * (1.0f - t) + 0.10f * sinf(PI_F * t);
        wp = 0.45f * (1.0f - smooth01(t * 1.6f)) - 0.25f * smooth01((t - 0.6f) / 0.4f);
        // Бег: пятка захлёстывает назад и вверх, затем колено выносит голень вперёд
        *z -= run * 0.22f * sinf(PI_F * clampf(t / 0.6f, 0.0f, 1.0f));
        rl = 0.05f * (1.0f - t) + 0.40f * sinf(PI_F * powf(t, 0.7f));
        rp = 0.7f * (1.0f - smooth01(t * 1.4f)) - 0.1f * smooth01((t - 0.7f) / 0.3f);
    }
    *lift = lerpf(wl, rl, run);
    *pitch = lerpf(wp, rp, run);
}

// ---------------------------------------------------------------- позы боя

// Углы руки: плечо (наклон вперёд, разворот, отведение), локоть, кисть
typedef struct ArmAngles {
    f32 ux, uy, uz, fx, hx, hz;
} ArmAngles;

// Ключевая поза верха тела: правая и левая рука, наклон и скрутка корпуса
typedef struct UpperKey {
    ArmAngles r, l;
    f32 lean, twist;
} UpperKey;

static ArmAngles arm_lerp(ArmAngles a, ArmAngles b, f32 t)
{
    return (ArmAngles){ lerpf(a.ux, b.ux, t), lerpf(a.uy, b.uy, t), lerpf(a.uz, b.uz, t),
                        lerpf(a.fx, b.fx, t), lerpf(a.hx, b.hx, t), lerpf(a.hz, b.hz, t) };
}

static UpperKey key_lerp(UpperKey a, UpperKey b, f32 t)
{
    return (UpperKey){ arm_lerp(a.r, b.r, t), arm_lerp(a.l, b.l, t), lerpf(a.lean, b.lean, t), lerpf(a.twist, b.twist, t) };
}

// Отрицательный ux выносит руку вперёд и вверх; для правой руки отрицательный uy уводит её вправо
static const UpperKey K_GUARD = { { -0.55f, 0.15f, -0.12f, -1.15f, -0.40f, 0.0f }, { -0.70f, -0.30f, 0.15f, -1.30f, 0.0f, 0.0f }, 0.12f, 0.15f };
static const UpperKey K_BLOCK = { { -1.00f, 0.35f, -0.10f, -1.40f, -0.20f, 1.2f }, { -1.10f, -0.40f, 0.10f, -1.50f, 0.0f, 0.0f }, 0.05f, 0.10f };
static const UpperKey K_WINDUP[ATK_COUNT] = {
    [ATK_OVERHEAD] = { { -2.70f, 0.00f, -0.25f, -1.30f, -0.30f, 0.0f }, { -0.90f, -0.20f, 0.25f, -1.20f, 0.0f, 0.0f }, -0.10f, 0.10f },
    [ATK_SWING]    = { { -1.40f, -1.50f, -0.10f, -0.20f, 0.15f, 0.0f }, { -0.60f, -0.20f, 0.30f, -1.10f, 0.0f, 0.0f }, 0.05f, -0.60f },
    [ATK_THRUST]   = { { 0.25f, 0.00f, -0.15f, -1.90f, 0.20f, 0.0f }, { -0.90f, -0.30f, 0.20f, -1.20f, 0.0f, 0.0f }, 0.00f, -0.30f },
};
static const UpperKey K_STRIKE[ATK_COUNT] = {
    [ATK_OVERHEAD] = { { -0.75f, 0.25f, -0.10f, -0.15f, -0.20f, 0.0f }, { -0.40f, 0.00f, 0.30f, -0.80f, 0.0f, 0.0f }, 0.30f, 0.10f },
    [ATK_SWING]    = { { -1.40f, 0.90f, -0.10f, -0.20f, 0.15f, 0.0f }, { -0.30f, 0.00f, 0.50f, -0.80f, 0.0f, 0.0f }, 0.10f, 0.60f },
    [ATK_THRUST]   = { { -1.45f, 0.10f, -0.05f, -0.05f, -0.10f, 0.0f }, { -0.30f, 0.00f, 0.40f, -0.60f, 0.0f, 0.0f }, 0.25f, 0.35f },
};

static M4 arm_upper(ArmAngles a) { return m4_mul(m4_rot_y(a.uy), m4_mul(m4_rot_z(a.uz), m4_rot_x(a.ux))); }

static f32 ease_out(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

// Верх тела по действию; rest — руки при ходьбе
static UpperKey upper_from_action(const CharAction *a, UpperKey rest)
{
    UpperKey base = key_lerp(rest, K_GUARD, clampf(a->guard, 0.0f, 1.0f));
    UpperKey k = base;
    u8 at = a->attack < ATK_COUNT ? a->attack : ATK_SWING;
    if (a->swing > 0.0f) {
        if (a->swing < 1.0f) k = key_lerp(base, K_WINDUP[at], smooth01(a->swing));
        else if (a->swing < 2.0f) k = key_lerp(K_WINDUP[at], K_STRIKE[at], ease_out(a->swing - 1.0f));
        else k = key_lerp(K_STRIKE[at], base, smooth01(a->swing - 2.0f));
    }
    k = key_lerp(k, K_BLOCK, clampf(a->block, 0.0f, 1.0f));
    if (a->two_hand) {
        // Левая рука тянется к древку рядом с правой
        ArmAngles l = { k.r.ux * 0.9f, k.r.uy - 0.55f, 0.05f, k.r.fx * 0.8f, 0.0f, 0.0f };
        k.l = arm_lerp(k.l, l, clampf(a->guard, 0.0f, 1.0f));
    }
    // Ошеломлён: корпус назад, руки врозь
    f32 st = clampf(a->stagger, 0.0f, 1.0f);
    k.lean -= 0.35f * st;
    k.r.uz -= 0.6f * st;
    k.l.uz += 0.6f * st;
    k.lean -= 0.30f * clampf(a->dodge, 0.0f, 1.0f);
    // Без сознания и мёртвые: руки раскинуты
    f32 limp = clampf(a->limp, 0.0f, 1.0f);
    ArmAngles lr = { -0.2f, 0.0f, -0.9f, -0.2f, 0.0f, 0.0f }, ll = { -0.1f, 0.0f, 0.7f, -0.3f, 0.0f, 0.0f };
    k.r = arm_lerp(k.r, lr, limp);
    k.l = arm_lerp(k.l, ll, limp);
    return k;
}

void character_pose(const CharPose *p, const CharAction *act, M4 *out)
{
    static const CharAction none = { .down_dir = 1.0f };
    const CharAction *a = act ? act : &none;
    f32 w = clampf(p->walk, 0.0f, 1.0f);
    f32 run = clampf(p->run, 0.0f, 1.0f);
    f32 ph = p->phase - floorf(p->phase);
    // Доля опоры за цикл: шаг 0.6, бег 0.27 (между опорами полёт)
    f32 stance = lerpf(0.6f, 0.27f, run);
    f32 mid = stance * 0.5f;
    f32 tau = 2.0f * PI_F;
    f32 breathe = sinf(p->time * 1.6f + p->seed) * 0.5f + 0.5f;
    f32 guard = clampf(a->guard, 0.0f, 1.0f);
    f32 kneel = clampf(a->kneel, 0.0f, 1.0f);
    f32 down = clampf(a->down, 0.0f, 1.0f);
    f32 crawl = clampf(a->crawl, 0.0f, 1.0f);

    // Таз: на шаге выше всего над опорной ногой, на беге ниже всего (пружинит);
    // качается к опорной ноге; в стойке ниже, на коленях у земли
    f32 bob = lerpf(-0.05f, -0.10f, run) + lerpf(0.022f, -0.045f, run) * cosf(2.0f * tau * (ph - mid));
    f32 sway = 0.024f * (1.0f - 0.6f * run) * cosf(tau * (ph - mid));
    f32 idle_shift = 0.018f * sinf(p->time * 0.35f + p->seed);
    f32 dy = w * bob + (1.0f - w) * (-0.012f - 0.004f * breathe) - 0.06f * guard;
    f32 dx = w * sway + (1.0f - w) * idle_shift;
    f32 dz = -0.2f * clampf(a->dodge, 0.0f, 1.0f);
    // Выпад на ударе
    if (a->swing > 1.0f && a->swing < 3.0f) {
        f32 s = a->swing < 2.0f ? ease_out(a->swing - 1.0f) : 1.0f - smooth01(a->swing - 2.0f);
        dz += (a->attack == ATK_SWING ? 0.05f : 0.14f) * s;
        dy -= 0.04f * s;
    }
    dy = lerpf(dy, -0.44f, kneel);
    f32 pelvis_yaw = w * 0.07f * sinf(tau * ph);

    // Верх тела: ходьба, поверх неё стойка, удар, блок
    // На бегу руки согнуты в локтях и машут шире; правая с оружием сгибается меньше
    f32 swing = 0.32f * (1.0f + 0.9f * run) * cosf(tau * ph) * w;
    f32 elbow = 1.25f * run * w;
    UpperKey rest = {
        { -swing * 0.6f, 0.0f, -0.08f - 0.02f * breathe, -0.38f - 0.5f * elbow, 0.0f, 0.0f },
        { swing, 0.0f, 0.08f + 0.02f * breathe, -0.22f - 0.35f * fmaxf(0.0f, -cosf(tau * ph)) * w - elbow, 0.0f, 0.0f },
        0.0f, 0.0f,
    };
    UpperKey k = upper_from_action(a, rest);
    // На коленях: руки опущены, голова вниз
    k = key_lerp(k, (UpperKey){ { 0.05f, 0, -0.1f, -0.3f, 0, 0 }, { 0.05f, 0, 0.1f, -0.3f, 0, 0 }, 0.25f, 0.0f }, kneel);
    if (crawl > 0.0f) {
        // Ползком: руки по очереди тянутся вперёд по земле
        f32 c = sinf(tau * ph);
        ArmAngles rr = { -2.6f + 0.5f * c, 0.0f, -0.3f, -0.4f - 0.4f * fmaxf(0.0f, c), 0, 0 };
        ArmAngles rl = { -2.6f - 0.5f * c, 0.0f, 0.3f, -0.4f - 0.4f * fmaxf(0.0f, -c), 0, 0 };
        k.r = arm_lerp(k.r, rr, crawl);
        k.l = arm_lerp(k.l, rl, crawl);
    }

    M4 local[BONE_COUNT];
    for (int i = 0; i < BONE_COUNT; i++) local[i] = m4_identity();
    local[BONE_PELVIS] = m4_rot_y(pelvis_yaw);
    local[BONE_SPINE] = m4_mul(m4_rot_x((0.08f + 0.14f * run) * w + 0.02f * breathe + k.lean), m4_rot_y(-pelvis_yaw * 1.8f + k.twist + 0.3f * p->look));
    f32 look = sinf(p->time * 0.31f + p->seed * 3.0f) * 0.55f * (1.0f - w) * (1.0f - guard) * (1.0f - kneel);
    f32 head_x = -0.06f * w + 0.1f * (1.0f - w) - 0.4f * clampf(a->stagger, 0.0f, 1.0f) + 0.35f * kneel;
    // Голова держит противника: скрутку корпуса гасит
    // Голова держит горизонт: наклон тела гасит наполовину
    local[BONE_HEAD] = m4_mul(m4_rot_y(look + 0.7f * p->look + pelvis_yaw * 0.8f - k.twist * 0.7f),
                              m4_mul(m4_rot_z(0.5f * p->lean_side), m4_rot_x(head_x - k.lean * 0.6f - 0.5f * p->lean_fwd)));

    local[BONE_UPPER_ARM_R] = arm_upper(k.r);
    local[BONE_FOREARM_R] = m4_rot_x(k.r.fx);
    local[BONE_HAND_R] = m4_mul(m4_rot_x(k.r.hx), m4_rot_z(k.r.hz));
    local[BONE_UPPER_ARM_L] = arm_upper(k.l);
    local[BONE_FOREARM_L] = m4_rot_x(k.l.fx);
    local[BONE_HAND_L] = m4_mul(m4_rot_x(k.l.hx), m4_rot_z(k.l.hz));

    // Ноги: цель стопы в пространстве персонажа, IK от тазобедренного сустава.
    // Шаг поворачивается по направлению движения: вбок и назад ноги переступают правильно
    f32 md_s = sinf(p->move_dir), md_c = cosf(p->move_dir);
    // Тело наклонено от стоп: цели стоп переводятся в наклонённые координаты, чтобы стояли на земле
    M4 unlean = m4_mul(m4_rot_x(-p->lean_fwd), m4_rot_z(p->lean_side));
    for (int side = 0; side < 2; side++) {
        bool left = side == 0;
        f32 q = ph + (left ? 0.0f : 0.5f);
        q -= floorf(q);
        f32 z, lift, pitch;
        foot_track(q, run, stance, char_stride(run), &z, &lift, &pitch);
        // В стойке левая нога впереди, правая сзади, ступни шире
        f32 idle_z = lerpf(left ? 0.06f : -0.05f, left ? 0.20f : -0.18f, guard);
        f32 fx = (left ? 1.0f : -1.0f) * (HIP_X + 0.015f + 0.05f * guard);
        f32 step = z * w;
        V3 target = m4_point(unlean, v3(fx + md_s * step, ANKLE_Y + lift * w, idle_z * (1.0f - w) + md_c * step));
        V3 hip = v3((left ? HIP_X : -HIP_X) + dx, HIP_Y + dy, dz);
        f32 tp, tr, kn;
        leg_ik(hip, target, &tp, &tr, &kn);
        f32 foot = -(tp + kn) - p->lean_fwd + pitch * w;
        // На коленях: бедро отвесно, голень назад по земле
        tp = lerpf(tp, -0.1f, kneel);
        tr = lerpf(tr, 0.0f, kneel);
        kn = lerpf(kn, PI_F * 0.5f + 0.1f, kneel);
        foot = lerpf(foot, -0.5f, kneel);
        u8 thigh = left ? BONE_THIGH_L : BONE_THIGH_R;
        local[thigh] = m4_mul(m4_rot_z(tr), m4_rot_x(tp));
        local[thigh + 1] = m4_rot_x(kn);
        // Стопа параллельна земле плюс перекат с пятки на носок
        local[thigh + 2] = m4_mul(m4_rot_x(foot), m4_rot_z(p->lean_side - tr));
    }

    // Падение: поворот вокруг стоп, к концу таз приходит на место бойца
    f32 dir = a->down_dir < 0.0f ? -1.0f : 1.0f;
    f32 fall = smooth01(down);
    f32 sh = p->height > 0.0f ? p->height : 1.0f;
    f32 sw = p->width > 0.0f ? p->width : 1.0f;
    M4 lie = m4_mul(m4_translate(v3(0.0f, 0.13f * fall, 0.95f * sh * fall * dir)), m4_rot_x(-dir * PI_F * 0.5f * fall));
    M4 lean = m4_mul(m4_rot_z(-p->lean_side), m4_rot_x(p->lean_fwd));
    M4 root = m4_mul(m4_translate(p->pos), m4_mul(m4_rot_y(p->heading), m4_mul(lean, m4_mul(lie, m4_mul(m4_scale(v3(sw, sh, sw)), m4_translate(v3(dx, dy, dz)))))));
    M4 world[BONE_COUNT];
    for (int i = 0; i < BONE_COUNT; i++) {
        const BoneDef *b = &g_body_bones[i];
        V3 rel = b->parent < 0 ? b->joint : v3_sub(b->joint, g_body_bones[b->parent].joint);
        M4 parent = b->parent < 0 ? root : world[b->parent];
        world[i] = m4_mul(parent, m4_mul(m4_translate(rel), local[i]));
        out[i] = m4_mul(world[i], m4_translate(v3_mul(b->joint, -1.0f)));
    }

    // Отсечённая конечность: вершины стягиваются в точку сустава
    static const u8 limb_bones[PART_COUNT][3] = {
        [PART_ARM_L] = { BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_HAND_L },
        [PART_ARM_R] = { BONE_UPPER_ARM_R, BONE_FOREARM_R, BONE_HAND_R },
        [PART_LEG_L] = { BONE_THIGH_L, BONE_SHIN_L, BONE_FOOT_L },
        [PART_LEG_R] = { BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_R },
    };
    for (u32 part = PART_ARM_L; part < PART_COUNT; part++) {
        if (!(a->severed & (1u << part))) continue;
        u8 b0 = limb_bones[part][0];
        M4 stub = m4_mul(world[b0], m4_mul(m4_scale(v3(0.001f, 0.001f, 0.001f)), m4_translate(v3_mul(g_body_bones[b0].joint, -1.0f))));
        for (int j = 0; j < 3; j++) out[limb_bones[part][j]] = stub;
    }
}
