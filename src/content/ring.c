// Ринг арены у зала: формы мачты прожектора и лампы в сетке. Раскладка (плита, песок, ограда, знаки) —
// заготовка ring в data/defs/prefabs.txt
#include "ring.h"

#include "props.h"
#include "shapes.h"

// Мачта прожектора: точка — под фонарём, +z — к середине ринга. Свет — объект ring_spot
static void sh_ring_mast(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    u32 v0 = mb->vcount;
    V3 base = v3(0, 0, -0.9f);
    prop_mat(mb, g_mat.rust_dark);
    mb_box(mb, m4_trs(v3(base.x, 5.2f, base.z), v3(0.32f, 10.4f, 0.32f), 0, 0, 0));
    for (int i = 0; i < 6; i++) mb_box(mb, m4_trs(v3(base.x, 1.2f + (f32)i * 1.6f, base.z), v3(0.5f, 0.06f, 0.5f), 0, 0, 0));
    prop_mat(mb, g_mat.concrete_dark);
    mb_box(mb, m4_trs(v3(base.x, 0.3f, base.z), v3(1.1f, 0.6f, 1.1f), 0, 0, 0));
    // Корпус фонаря смотрит в середину и вниз
    V3 lamp = v3(0, 10.2f, -0.4f);
    mb_set(mb, v3(0.16f, 0.16f, 0.17f), MAT_METAL, BONE_NONE);
    mb_box(mb, m4_trs(lamp, v3(1.2f, 0.7f, 0.5f), 0.55f, 0, 0));
    prop_mat(mb, g_mat.lamp_white);
    V3 lens = v3_add(lamp, v3(0, -0.14f, 0.22f));
    mb_quad(mb, lens, v3(-0.5f, 0, 0), v3_mul(v3_norm(v3(0, 1, 0.6f)), 0.25f));
    shape_to_world(mb, v0, c->L);
}

// Лампа в сетке на стойке ворот: точка — лампа, +z — к середине ринга. Свет — объект ring_lamp
static void sh_ring_cage(const ShapeCtx *c)
{
    MeshBuilder *mb = c->mb;
    u32 v0 = mb->vcount;
    V3 m = v3(0, 0.02f, 0);
    prop_mat(mb, g_mat.rust_dark);
    mb_box(mb, m4_trs(v3(0, 0.02f, -0.1f), v3(0.2f, 0.04f, 0.04f), 0, PI_F * 0.5f, 0));
    mb_box(mb, m4_trs(v3_add(m, v3(0, 0.1f, 0)), v3(0.16f, 0.025f, 0.16f), 0, 0.3f, 0));
    for (int i = 0; i < 4; i++) {
        f32 a = (f32)i * PI_F * 0.5f + 0.3f;
        mb_box(mb, m4_trs(v3(m.x + sinf(a) * 0.07f, m.y - 0.02f, m.z + cosf(a) * 0.07f), v3(0.012f, 0.22f, 0.012f), 0, a, 0));
    }
    mb_set(mb, v3_mul(mat_c(g_mat.lamp_white), 0.5f), MAT_EMISSIVE, BONE_NONE);
    mb_box(mb, m4_trs(v3_add(m, v3(0, -0.02f, 0)), v3(0.07f, 0.11f, 0.07f), 0, 0.3f, 0));
    shape_to_world(mb, v0, c->L);
}

const Shape g_ring_shapes[] = {
    { "ring_mast", sh_ring_mast, 0 },
    { "ring_cage", sh_ring_cage, 0 },
};
const u32 g_ring_shape_count = ARRAY_COUNT(g_ring_shapes);

f32 ring_ground(f32 x, f32 z, f32 base)
{
    f32 dx = x - ARENA_CENTER.x, dz = z - ARENA_CENTER.z;
    return dx * dx + dz * dz < 8.5f * 8.5f ? fmaxf(base, RING_FLOOR_Y) : base;
}
