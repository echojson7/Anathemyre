#include "loot_view.h"

#include "../core/log.h"
#include "../sim/anim.h"

// ---------------------------------------------------------------- вид

void loot_view_init(LootView *v, MeshBuilder *mb, Arena *perm)
{
    // Узел: мешковина, перетянутая двумя ремнями
    size_t mark = perm->used;
    mb_init(mb, perm, 1024, 3072);
    mb_set(mb, v3(0.30f, 0.26f, 0.19f), MAT_CLOTH, 0);
    mb_box(mb, m4_trs(v3(0, 0, 0), v3(0.30f, 0.12f, 0.22f), 0, 0, 0));
    mb_set(mb, v3(0.16f, 0.10f, 0.06f), MAT_LEATHER, 0);
    mb_box(mb, m4_trs(v3(-0.07f, 0, 0), v3(0.03f, 0.125f, 0.225f), 0, 0, 0));
    mb_box(mb, m4_trs(v3(0.07f, 0, 0), v3(0.03f, 0.125f, 0.225f), 0, 0, 0));
    gpu_mesh_upload(&v->bundle, mb);
    perm->used = mark;
    v->bones = ARENA_PUSH_ARRAY(perm, M4, (size_t)LOOT_DRAW_MAX * RENDER_MAX_BONES);
}

u32 loot_draw(LootView *v, const Ground *g, const Phys *p, const GpuMesh *weapon, f32 alpha, V3 eye, DrawItem *out,
              u32 cap)
{
    u32 n = 0;
    for (u32 k = 0; k < g->count && n < cap && n < LOOT_DRAW_MAX; k++) {
        ItemStack it = g->item[k];
        if (!item_live(it)) continue;
        u32 h = (u32)g->body[k];
        V3 d = v3_sub(p->x[h], eye);
        if (v3_dot(d, d) > 200.0f * 200.0f) continue;
        M4 *b = &v->bones[(size_t)n * RENDER_MAX_BONES];
        M4 m = phys_pose(p, h, alpha);
        bool wpn = item_weapon(it) != COMP_NONE;
        // Размер узла — по форме тела (клетки мешка)
        if (!wpn) {
            PhysDesc pd = ground_desc(it);
            V3 s = v3_mul(pd.part[0].b, 2.0f);
            m = m4_mul(m, m4_trs(v3(0, 0, 0), v3(s.x / 0.30f, s.y / 0.12f, s.z / 0.22f), 0, 0, 0));
        }
        b[wpn ? CHAR_BONE_WEAPON : 0] = m;
        out[n++] = (DrawItem){ .mesh = wpn ? &weapon[weapon_mesh(item_weapon(it), it.quality)] : &v->bundle, .bones = b };
    }
    return n;
}
