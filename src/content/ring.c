// Ринг арены у зала: плита, песок, ограда, прожекторы, круг знаков
#include "ring.h"

#include "../core/hash.h"
#include "props.h"

#define SLAB_R 8.6f     // вписанный радиус бетонной плиты
#define FLOOR_R 6.9f    // песчаный круг боя

// Восьмиугольная плита из четырёх полос: их объединение и есть восьмиугольник, грани плоские
static void oct_slab(MeshBuilder *mb, V3 c, f32 r_in, f32 y0, f32 y1)
{
    f32 side = 2.0f * r_in * tanf(PI_F / 8.0f);
    for (int k = 0; k < 4; k++)
        mb_box(mb, m4_trs(v3(c.x, 0.5f * (y0 + y1), c.z), v3(side, y1 - y0, 2.0f * r_in), 0, (f32)k * PI_F * 0.25f, 0));
}

V3 ring_corner(V3 c, f32 r_in, int k, f32 y)
{
    f32 a = PI_F / 8.0f + (f32)k * PI_F * 0.25f;
    f32 rc = r_in / cosf(PI_F / 8.0f);
    return v3(c.x + sinf(a) * rc, y, c.z + cosf(a) * rc);
}

// Плоский знак на полу, светится снизу вверх
static void floor_glyph(MeshBuilder *mb, V3 c, V3 radial, f32 half_len, f32 half_w)
{
    V3 tangent = v3(-radial.z, 0.0f, radial.x);
    mb_quad(mb, c, v3_mul(tangent, half_len), v3_mul(radial, half_w));
}

void ring_build(MeshBuilder *mb, V3 center)
{
    // Плита и песок
    prop_mat(mb, g_mat.concrete);
    oct_slab(mb, center, SLAB_R, -0.3f, 0.12f);
    prop_mat(mb, g_mat.arena_floor);
    oct_slab(mb, center, FLOOR_R, 0.0f, RING_FLOOR_Y);

    // Круг знаков на песке: им размечено место боя, смысла никто не помнит
    prop_mat(mb, g_mat.glyph_dim);
    for (int k = 0; k < 28; k++) {
        f32 a = (f32)k * 2.0f * PI_F / 28.0f;
        V3 radial = v3(sinf(a), 0.0f, cosf(a));
        floor_glyph(mb, v3(center.x + radial.x * 6.1f, RING_FLOOR_Y + 0.004f, center.z + radial.z * 6.1f), radial, 0.55f, 0.16f);
    }

    // Ограда: стойки из двутавра по углам, три трубы, частые прутья арматуры.
    // Сторона к залу (−Z) открыта: ворота с перекладиной
    const int gate = 4;  // сторона между углами 3 и 4 смотрит на −Z
    for (int k = 0; k < 8; k++) {
        V3 p = ring_corner(center, RING_FENCE_R, k, 0.0f);
        f32 a = PI_F / 8.0f + (f32)k * PI_F * 0.25f;
        bool gate_post = k == gate - 1 || k == gate;
        f32 h = gate_post ? 3.1f : 2.3f;
        prop_mat(mb, g_mat.rust_dark);
        mb_box(mb, m4_trs(v3(p.x, h * 0.5f, p.z), v3(0.22f, h, 0.06f), 0, a, 0));
        mb_box(mb, m4_trs(v3(p.x, h * 0.5f, p.z), v3(0.05f, h, 0.2f), 0, a, 0));
        prop_mat(mb, g_mat.concrete_dark);
        mb_box(mb, m4_trs(v3(p.x, 0.25f, p.z), v3(0.5f, 0.3f, 0.5f), 0, a, 0));
    }
    for (int k = 0; k < 8; k++) {
        int k1 = (k + 1) % 8;
        if (k1 == gate) continue;
        V3 a = ring_corner(center, RING_FENCE_R, k, 0.0f), b = ring_corner(center, RING_FENCE_R, k1, 0.0f);
        prop_mat(mb, g_mat.rust);
        static const f32 rails[3] = { 0.55f, 1.15f, 1.85f };
        for (int r = 0; r < 3; r++)
            mb_cylinder(mb, v3(a.x, rails[r], a.z), v3(b.x, rails[r] - (r == 2 ? 0.06f : 0.0f), b.z), 0.04f, 8);
        prop_mat(mb, g_mat.rust_dark);
        for (int i = 1; i < 12; i++) {
            f32 t = (f32)i / 12.0f;
            V3 q = v3_lerp(a, b, t);
            // Прутья кривые и разной высоты: ограду латали чем было
            f32 top = 1.85f + 0.25f * hashf((u32)(k * 31 + i));
            f32 lean = (hashf((u32)(k * 57 + i)) - 0.5f) * 0.12f;
            mb_cylinder(mb, v3(q.x, 0.12f, q.z), v3(q.x + lean, top, q.z + lean * 0.5f), 0.016f, 6);
        }
    }
    {
        // Перекладина ворот и знак над ними
        V3 a = ring_corner(center, RING_FENCE_R, gate - 1, 2.95f), b = ring_corner(center, RING_FENCE_R, gate, 2.95f);
        prop_mat(mb, g_mat.rust_dark);
        mb_cylinder(mb, a, b, 0.09f, 10);
        V3 m = v3_lerp(a, b, 0.5f);
        prop_mat(mb, g_mat.concrete_dark);
        mb_box(mb, m4_trs(v3(m.x, 2.45f, m.z - 0.02f), v3(1.6f, 0.75f, 0.08f), 0, 0, 0));
        prop_mat(mb, g_mat.glyph_red);
        mb_quad(mb, v3(m.x, 2.45f, m.z - 0.07f), v3(-0.7f, 0, 0), v3(0, 0.3f, 0));
    }

    // Прожекторные мачты по диагонали: свет болезненно-жёлтый
    for (int side = 0; side < 2; side++) {
        f32 a = PI_F * (side ? 1.375f : 0.375f);
        V3 dir = v3(sinf(a), 0.0f, cosf(a));
        V3 base = v3(center.x + dir.x * 10.2f, 0.0f, center.z + dir.z * 10.2f);
        prop_mat(mb, g_mat.rust_dark);
        mb_box(mb, m4_trs(v3(base.x, 5.2f, base.z), v3(0.32f, 10.4f, 0.32f), 0, a, 0));
        for (int i = 0; i < 6; i++)
            mb_box(mb, m4_trs(v3(base.x, 1.2f + (f32)i * 1.6f, base.z), v3(0.5f, 0.06f, 0.5f), 0, a, 0));
        prop_mat(mb, g_mat.concrete_dark);
        mb_box(mb, m4_trs(v3(base.x, 0.3f, base.z), v3(1.1f, 0.6f, 1.1f), 0, a, 0));
        // Корпус фонаря смотрит в центр и вниз
        V3 lamp = v3(base.x - dir.x * 0.5f, 10.2f, base.z - dir.z * 0.5f);
        f32 yaw = atan2f(-dir.x, -dir.z);
        mb_set(mb, v3(0.16f, 0.16f, 0.17f), MAT_METAL, BONE_NONE);
        mb_box(mb, m4_trs(lamp, v3(1.2f, 0.7f, 0.5f), 0.55f, yaw, 0));
        prop_mat(mb, g_mat.lamp_white);
        V3 fwd = v3(sinf(yaw), 0.0f, cosf(yaw));
        V3 lens = v3_add(lamp, v3_add(v3_mul(fwd, 0.22f), v3(0, -0.14f, 0)));
        V3 right = v3(cosf(yaw), 0.0f, -sinf(yaw));
        V3 up = v3_norm(v3_add(v3(0, 1, 0), v3_mul(fwd, 0.6f)));
        mb_quad(mb, lens, v3_mul(right, -0.5f), v3_mul(up, 0.25f));
    }

    // Лампы в сетках на стойках ворот: свет на проход и на песок у ворот
    for (int k = gate - 1; k <= gate; k++) {
        V3 p = ring_corner(center, RING_FENCE_R, k, 2.62f);
        V3 in = v3_norm(v3(center.x - p.x, 0.0f, center.z - p.z));
        V3 c = v3_add(p, v3_mul(in, 0.2f));
        prop_mat(mb, g_mat.rust_dark);
        mb_box(mb, m4_trs(v3_add(p, v3_mul(in, 0.1f)), v3(0.2f, 0.04f, 0.04f), 0, atan2f(in.x, in.z) + PI_F * 0.5f, 0));
        mb_box(mb, m4_trs(v3_add(c, v3(0, 0.1f, 0)), v3(0.16f, 0.025f, 0.16f), 0, 0.3f, 0));
        for (int i = 0; i < 4; i++) {
            f32 a = (f32)i * PI_F * 0.5f + 0.3f;
            mb_box(mb, m4_trs(v3(c.x + sinf(a) * 0.07f, c.y - 0.02f, c.z + cosf(a) * 0.07f), v3(0.012f, 0.22f, 0.012f), 0, a, 0));
        }
        mb_set(mb, v3_mul(mat_c(g_mat.lamp_white), 0.5f), MAT_EMISSIVE, BONE_NONE);
        mb_box(mb, m4_trs(v3_add(c, v3(0, -0.02f, 0)), v3(0.07f, 0.11f, 0.07f), 0, 0.3f, 0));
    }

    // Бетонные блоки, на которых стоят и сидят зрители
    prop_mat(mb, g_mat.concrete_dark);
    for (int i = 0; i < 9; i++) {
        f32 a = 0.5f + (f32)i * 0.62f;
        if (fabsf(wrap_angle(a - PI_F)) < 0.6f) continue;  // проход к воротам
        f32 r = 9.4f + 0.5f * hashf((u32)i * 13u);
        mb_box(mb, m4_trs(v3(center.x + sinf(a) * r, 0.25f, center.z + cosf(a) * r), v3(1.6f, 0.5f, 0.6f), 0, a, 0.03f));
    }
}

f32 ring_ground(f32 x, f32 z, f32 base)
{
    f32 dx = x - ARENA_CENTER.x, dz = z - ARENA_CENTER.z;
    return dx * dx + dz * dz < 8.5f * 8.5f ? fmaxf(base, RING_FLOOR_Y) : base;
}
