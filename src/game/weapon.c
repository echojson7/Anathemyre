// Сетки оружия по описаниям: в своей системе — рукоять в начале координат, острие по +Y, лезвие по +Z
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
    // Форма из описания (data/defs/weapons.txt): стержни и бруски по порядку
    const WeaponDef *d = &g_weapons[weapon];
    for (u32 k = 0; k < d->parts; k++) {
        const WeaponPart *p = &g_weapon_parts[d->part + k];
        mb_set(mb, v3(p->color[0], p->color[1], p->color[2]), (Material)p->mat, CHAR_BONE_WEAPON);
        if (p->kind == WP_ROD) wcyl(mb, p->v[0], p->v[1], p->v[2], p->v[3]);
        else wbox(mb, p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5]);
    }
}


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
    // Пламя капли: жёлтый низ у ветоши, выше оранжевый язык, по краям тёмно-красные отрывы.
    // Своя кость CHAR_BONE_FLAME: начало у ветоши, ось +Y всегда вверх
    const u8 F = CHAR_BONE_FLAME;
    static const V2 base[] = { { -0.01f, 0.0f }, { 0.01f, 0.030f }, { 0.04f, 0.042f }, { 0.11f, 0.038f } };
    static const V2 top[] = { { 0.10f, 0.040f }, { 0.18f, 0.030f }, { 0.26f, 0.013f }, { 0.33f, 0.0f } };
    static const V2 lick[] = { { 0.0f, 0.016f }, { 0.07f, 0.011f }, { 0.13f, 0.0f } };
    mb_set(mb, v3(0.52f, 0.28f, 0.06f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0, 0, 0), v3(0, 1, 0), base, 4, 6, 1.0f, 1.0f);
    mb_set(mb, v3(0.40f, 0.13f, 0.03f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0, 0, 0), v3(0.05f, 1, 0.02f), top, 4, 6, 1.0f, 1.0f);
    mb_set(mb, v3(0.24f, 0.05f, 0.01f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0.022f, 0.15f, 0.01f), v3(0.022f + 0.25f, 1.15f, 0.11f), lick, 3, 5, 1.0f, 1.0f);
    mb_lathe(mb, v3(-0.02f, 0.13f, -0.015f), v3(-0.02f - 0.2f, 1.13f, -0.215f), lick, 3, 5, 1.0f, 1.0f);
}

V3 torch_flame_pos(const M4 *bones) { return m4_point(bones[CHAR_BONE_FLAME], v3(0.0f, 0.12f, 0.0f)); }

void weapon_ground_pose(V3 pos, f32 yaw, M4 *out)
{
    // Ось оружия ложится горизонтально, лезвие на ребро не встаёт: плашмя
    M4 m = m4_mul(m4_translate(pos), m4_mul(m4_rot_y(yaw), m4_mul(m4_rot_x(PI_F * 0.5f), m4_rot_y(PI_F * 0.5f))));
    for (int i = 0; i < CHAR_BONES; i++) out[i] = m;
}
