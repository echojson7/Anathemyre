#include "ground.h"

#include "items.h"

void ground_init(Ground *g, Arena *perm)
{
    g->count = 0;
    g->item = ARENA_PUSH_ARRAY(perm, Item, GROUND_MAX);
    g->body = ARENA_PUSH_ARRAY(perm, i32, GROUND_MAX);
    g->owner = ARENA_PUSH_ARRAY(perm, u16, GROUND_MAX);
}

// Форма оружия по типу, пока у описаний нет своей (WeaponDef.col — тред оружия): рукоять до кулака,
// дальше клинок плашмя, древко или древко с тяжёлой головой. Острие по +Y, лезвие по +Z
static PhysDesc weapon_desc(const WeaponDef *wd)
{
    f32 len = clampf(wd->length, 0.2f, 3.0f);
    PhysDesc d = { .mass = fmaxf(wd->weight, 0.3f), .friction = 0.6f, .bounce = 0.15f };
    switch (wd->wclass) {
    case WC_BLADE:
        d.part[0] = (PhysPart){ PP_CAPSULE, { 0, -0.12f, 0 }, { 0, 0.06f, 0 }, 0.018f };
        d.part[1] = (PhysPart){ PP_BOX, { 0, 0.06f + 0.5f * len, 0 }, { 0.006f, 0.5f * len, 0.025f }, 0 };
        d.parts = 2;
        break;
    case WC_POLEARM:
        d.part[0] = (PhysPart){ PP_CAPSULE, { 0, -0.6f, 0 }, { 0, len - 0.3f, 0 }, 0.02f };
        d.part[1] = (PhysPart){ PP_BOX, { 0, len - 0.15f, 0 }, { 0.006f, 0.15f, 0.035f }, 0 };
        d.parts = 2;
        break;
    default:
        // Тесак, дубина, тяжёлое: древко и голова на конце, где почти вся масса
        d.part[0] = (PhysPart){ PP_CAPSULE, { 0, -0.12f, 0 }, { 0, len - 0.12f, 0 }, 0.02f };
        d.part[1] = (PhysPart){ PP_BOX, { 0, len - 0.1f, 0.02f },
                                { wd->wclass == WC_CLEAVER ? 0.008f : 0.035f, 0.1f, 0.06f }, 0 };
        d.parts = 2;
        break;
    }
    return d;
}

PhysDesc ground_desc(Item it)
{
    if (it.kind == IK_WEAPON && it.id < g_weapon_count) return weapon_desc(&g_weapons[it.id]);
    // Узел: одежда, броня, товар — свёрток размером по клеткам мешка
    u8 w = 1, h = 1;
    item_size(it, &w, &h);
    f32 hx = clampf(0.07f * (f32)w, 0.08f, 0.3f), hz = clampf(0.07f * (f32)h, 0.08f, 0.3f);
    return (PhysDesc){ .part = { { PP_BOX, { 0, 0, 0 }, { hx, 0.06f, hz }, 0 } }, .parts = 1,
                       .mass = fmaxf(item_weight(it), 0.3f), .friction = 0.9f, .bounce = 0.02f };
}

i32 ground_drop(Ground *g, Phys *p, Item it, M4 xf, V3 vel, V3 angvel, u16 owner)
{
    if (!it.kind) return -1;
    u32 k = 0;
    while (k < g->count && g->item[k].kind != IK_NONE) k++;
    if (k == GROUND_MAX) return -1;
    PhysDesc d = ground_desc(it);
    i32 b = phys_spawn(p, &d, xf, vel, angvel, PHYS_SETTLE, (u16)(GROUND_USER + k));
    if (b < 0) return -1;
    if (k == g->count) g->count++;
    g->item[k] = it;
    g->body[k] = b;
    g->owner[k] = owner;
    return (i32)k;
}

Item ground_take(Ground *g, Phys *p, u32 k)
{
    if (!ground_live(g, (i32)k)) return (Item){ 0 };
    Item it = g->item[k];
    // Что лежало на ней, падает
    phys_wake(p, p->x[g->body[k]], p->bound[g->body[k]] + 0.05f);
    phys_free(p, (u32)g->body[k]);
    g->item[k] = (Item){ 0 };
    g->owner[k] = 0;
    while (g->count && g->item[g->count - 1].kind == IK_NONE) g->count--;
    return it;
}

i32 ground_nearest(const Ground *g, const Phys *p, V3 at, f32 r)
{
    i32 best = -1;
    f32 bd = r * r;
    for (u32 k = 0; k < g->count; k++) {
        if (g->item[k].kind == IK_NONE) continue;
        V3 x = p->x[g->body[k]];
        f32 dx = x.x - at.x, dz = x.z - at.z, d = dx * dx + dz * dz;
        if (d < bd) {
            bd = d;
            best = (i32)k;
        }
    }
    return best;
}

u32 ground_live_count(const Ground *g)
{
    u32 n = 0;
    for (u32 k = 0; k < g->count; k++) n += g->item[k].kind != IK_NONE;
    return n;
}

void ground_ser(Ser *s, Ground *g)
{
    ser_count(s, &g->count, GROUND_MAX);
    SER_N(s, g->item, g->count);
    SER_N(s, g->body, g->count);
    SER_N(s, g->owner, g->count);
    for (u32 k = 0; k < g->count; k++)
        ser_check(s, g->item[k].kind == IK_NONE || (g->body[k] >= 0 && g->body[k] < PHYS_MAX));
}
