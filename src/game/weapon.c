// Сетки оружия арены: в своей системе — рукоять в начале координат, острие по +Y, лезвие по +Z
#include "anim.h"

// Брусок вдоль оси от d0 до d1 (м от кулака); ширина к лезвию, толщина поперёк, сдвиг к лезвию,
// наклон бруска в плоскости лезвия
static void wbox(MeshBuilder *mb, f32 d0, f32 d1, f32 width, f32 thick, f32 off, f32 tilt)
{
    V3 a = v3_norm(v3(0.0f, 1.0f, tilt));
    V3 f = v3(0.0f, -a.z, a.y);
    V3 c = v3_add(v3_mul(v3(0, 1, 0), 0.5f * (d0 + d1)), v3(0, 0, off));
    M4 m = m4_identity();
    V3 sy = v3_mul(a, d1 - d0), sz = v3_mul(f, width);
    m.m[0] = thick;
    m.m[4] = sy.x; m.m[5] = sy.y; m.m[6] = sy.z;
    m.m[8] = sz.x; m.m[9] = sz.y; m.m[10] = sz.z;
    m.m[12] = c.x; m.m[13] = c.y; m.m[14] = c.z;
    mb_box(mb, m);
}

static void wcyl(MeshBuilder *mb, f32 d0, f32 d1, f32 r, f32 off)
{
    mb_cylinder(mb, v3(0, d0, off), v3(0, d1, off), r, 10);
}

void weapon_build(MeshBuilder *mb, u8 weapon)
{
    const u8 B = CHAR_BONE_WEAPON;
    const V3 wrap = v3(0.30f, 0.26f, 0.20f), steel = v3(0.34f, 0.33f, 0.31f), dark = v3(0.13f, 0.13f, 0.14f);
    const V3 rust = v3(0.42f, 0.24f, 0.13f), wood = v3(0.24f, 0.17f, 0.11f);
    switch (weapon) {
    case W_RESSORA:
        // Лист рессоры: длинная пологая дуга, рукоять в тряпье
        mb_set(mb, wrap, MAT_CLOTH, B);
        wcyl(mb, -0.12f, 0.10f, 0.02f, 0.0f);
        mb_set(mb, dark, MAT_METAL, B);
        wbox(mb, 0.10f, 0.13f, 0.12f, 0.03f, 0.01f, 0.0f);
        mb_set(mb, steel, MAT_METAL, B);
        wbox(mb, 0.13f, 0.50f, 0.065f, 0.012f, 0.012f, 0.03f);
        wbox(mb, 0.48f, 0.86f, 0.06f, 0.011f, 0.035f, 0.10f);
        wbox(mb, 0.84f, 0.98f, 0.04f, 0.010f, 0.07f, 0.25f);
        break;
    case W_SEKACH:
        // Тесак из обрезка листа: широкое тяжёлое лезвие, обух с зазубринами
        mb_set(mb, wood, MAT_LEATHER, B);
        wcyl(mb, -0.13f, 0.10f, 0.022f, 0.0f);
        mb_set(mb, rust, MAT_RUST, B);
        wbox(mb, 0.10f, 0.95f, 0.14f, 0.014f, 0.045f, 0.0f);
        wbox(mb, 0.93f, 1.05f, 0.10f, 0.013f, 0.03f, 0.5f);
        mb_set(mb, dark, MAT_METAL, B);
        for (int i = 0; i < 4; i++) wbox(mb, 0.25f + 0.15f * (f32)i, 0.31f + 0.15f * (f32)i, 0.03f, 0.02f, -0.03f, 0.0f);
        break;
    case W_ARMATURA:
        // Пук арматуры, на конце ком бетона
        mb_set(mb, wrap, MAT_CLOTH, B);
        wcyl(mb, -0.12f, 0.12f, 0.024f, 0.0f);
        mb_set(mb, rust, MAT_RUST, B);
        wcyl(mb, 0.0f, 0.90f, 0.014f, 0.012f);
        wcyl(mb, 0.0f, 0.85f, 0.013f, -0.012f);
        mb_set(mb, v3(0.38f, 0.36f, 0.33f), MAT_CONCRETE, B);
        wbox(mb, 0.66f, 0.92f, 0.14f, 0.12f, 0.0f, 0.15f);
        break;
    case W_BAGOR:
        // Багор: длинное древко, крюк и пика на конце; хват у нижней трети
        mb_set(mb, wood, MAT_LEATHER, B);
        wcyl(mb, -0.55f, 1.95f, 0.02f, 0.0f);
        mb_set(mb, wrap, MAT_CLOTH, B);
        wcyl(mb, -0.10f, 0.12f, 0.026f, 0.0f);
        wcyl(mb, 0.55f, 0.75f, 0.026f, 0.0f);
        mb_set(mb, rust, MAT_RUST, B);
        wcyl(mb, 1.90f, 2.20f, 0.012f, 0.0f);
        wbox(mb, 1.90f, 2.06f, 0.11f, 0.014f, 0.06f, -0.6f);
        break;
    case W_BALKA:
    default:
        // Обрезок двутавра: полки и стенка, рукоять обмотана
        mb_set(mb, wrap, MAT_CLOTH, B);
        wcyl(mb, -0.18f, 0.16f, 0.028f, 0.0f);
        mb_set(mb, v3(0.30f, 0.20f, 0.13f), MAT_RUST, B);
        wbox(mb, 0.15f, 1.40f, 0.16f, 0.012f, 0.0f, 0.0f);
        wbox(mb, 0.15f, 1.40f, 0.014f, 0.11f, 0.08f, 0.0f);
        wbox(mb, 0.15f, 1.40f, 0.014f, 0.11f, -0.08f, 0.0f);
        break;
    }
}

#define TORCH_FLAME_Y 0.56f  // середина пламени над кулаком, м

void torch_build(MeshBuilder *mb, bool flame)
{
    const u8 B = CHAR_BONE_TORCH;
    if (!flame) {
        // Обломок древка, сверху ветошь в мазуте, перетянутая проволокой
        mb_set(mb, v3(0.22f, 0.15f, 0.09f), MAT_LEATHER, B);
        wcyl(mb, -0.16f, 0.44f, 0.017f, 0.0f);
        mb_set(mb, v3(0.07f, 0.06f, 0.05f), MAT_CLOTH, B);
        wcyl(mb, 0.36f, 0.52f, 0.034f, 0.0f);
        mb_set(mb, v3(0.20f, 0.19f, 0.18f), MAT_METAL, B);
        wcyl(mb, 0.40f, 0.415f, 0.037f, 0.0f);
        wcyl(mb, 0.47f, 0.485f, 0.037f, 0.0f);
        return;
    }
    // Пламя: яркое ядро и языки вокруг, повёрнутые по-разному
    mb_set(mb, v3(0.55f, 0.32f, 0.10f), MAT_EMISSIVE, B);
    wbox(mb, 0.49f, 0.66f, 0.05f, 0.05f, 0.0f, 0.0f);
    mb_set(mb, v3(0.42f, 0.15f, 0.04f), MAT_EMISSIVE, B);
    wbox(mb, 0.48f, 0.62f, 0.075f, 0.03f, 0.008f, 0.12f);
    wbox(mb, 0.48f, 0.60f, 0.03f, 0.075f, -0.006f, -0.1f);
    wbox(mb, 0.58f, 0.74f, 0.028f, 0.028f, 0.01f, 0.2f);
}

V3 torch_flame_pos(const M4 *bones) { return m4_point(bones[CHAR_BONE_TORCH], v3(0.0f, TORCH_FLAME_Y, 0.0f)); }

void weapon_ground_pose(V3 pos, f32 yaw, M4 *out)
{
    // Ось оружия ложится горизонтально, лезвие на ребро не встаёт: плашмя
    M4 m = m4_mul(m4_translate(pos), m4_mul(m4_rot_y(yaw), m4_mul(m4_rot_x(PI_F * 0.5f), m4_rot_y(PI_F * 0.5f))));
    for (int i = 0; i < CHAR_BONES; i++) out[i] = m;
}
