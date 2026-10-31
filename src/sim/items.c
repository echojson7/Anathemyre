#include "items.h"

#include <string.h>

#include "ground.h"

const ItemDef    *g_items;
u32               g_item_count;
const LightDef   *g_lights;
const ContainerDef *g_containers;
const PhysCompDef *g_phys_comps;
const char       *g_gear_file[GEAR_CAP];
u32               g_gear_count;

const WeaponDef  *g_weapons;
u32               g_weapon_count;
const WeaponPart *g_weapon_parts;
const f32        *g_weapon_pts;
u8                g_weapon_pool[WEAPON_CAP];
u32               g_weapon_pool_count;
const ArmorDef   *g_armors;
u32               g_armor_count;
AttackDef         g_attacks[ATK_COUNT];
f32               g_quality_mul[Q_COUNT];
const char       *g_quality_name[Q_COUNT];
const char       *g_attack_name[ATK_COUNT];
const u8 g_armor_layer_order[5] = { SLOT_HEAD, SLOT_TORSO_ARMOR, SLOT_LEGS, SLOT_PANTS, SLOT_TORSO_UNDER };

u8 gear_find(const char *name)
{
    for (u32 i = 0; i < g_gear_count; i++)
        if (!strcmp(g_gear_file[i], name)) return (u8)i;
    return GEAR_NONE;
}

// ---------------------------------------------------------------- место вещи и переход

ItemStack item_at(const Ground *g, const ItemPlace *pl)
{
    const CharSheet *c = pl->sheet;
    switch (pl->kind) {
    case IP_NONE: return pl->it;
    case IP_GROUND: return ground_live(g, pl->at) ? g->item[pl->at] : (ItemStack){ 0 };
    case IP_HAND: return c ? c->equip[SLOT_WEAPON] : pl->it;
    case IP_BODY: return c && pl->slot < SLOT_COUNT ? c->equip[pl->slot] : (ItemStack){ 0 };
    case IP_BAG: return c && pl->at >= 0 && pl->at < c->inv.n ? c->inv.item[pl->at] : (ItemStack){ 0 };
    default: return (ItemStack){ 0 };  // ящики — с объектами шага 2б
    }
}

// Влезет ли вещь на место (проба без перемен)
static bool fits(const Ground *g, const ItemPlace *to, ItemStack it)
{
    const CharSheet *c = to->sheet;
    switch (to->kind) {
    case IP_GROUND: {
        u32 k = 0;
        while (k < g->count && item_live(g->item[k])) k++;
        return k < GROUND_MAX;
    }
    case IP_HAND: return !c || (!item_live(c->equip[SLOT_WEAPON]) && item_slot(it) == SLOT_WEAPON);
    case IP_BODY: return c && to->slot < SLOT_COUNT && !item_live(c->equip[to->slot]) && item_slot(it) == to->slot;
    case IP_BAG: {
        if (!c) return false;
        if (to->x != 255) return inv_fits(&c->inv, it, to->x, to->y);
        Inventory probe = c->inv;
        return inv_add(&probe, it);
    }
    default: return false;
    }
}

static ItemStack take(Ground *g, Phys *p, const ItemPlace *from)
{
    CharSheet *c = from->sheet;
    ItemStack it = item_at(g, from), old;
    switch (from->kind) {
    case IP_GROUND: return ground_take(g, p, (u32)from->at);
    case IP_HAND:
        if (c) sheet_equip(c, SLOT_WEAPON, (ItemStack){ 0 }, &old);
        return it;
    case IP_BODY: sheet_equip(c, from->slot, (ItemStack){ 0 }, &old); return it;
    case IP_BAG: return inv_take(&c->inv, from->at);
    default: return it;
    }
}

static i32 put(Ground *g, Phys *p, const ItemPlace *to, ItemStack it)
{
    CharSheet *c = to->sheet;
    ItemStack old;
    switch (to->kind) {
    case IP_GROUND: return ground_drop(g, p, it, to->xf, to->vel, to->spin, to->owner);
    case IP_HAND:
        if (c) sheet_equip(c, SLOT_WEAPON, it, &old);
        return 0;
    case IP_BODY: return sheet_equip(c, to->slot, it, &old) ? to->slot : -1;
    case IP_BAG:
        if (to->x != 255) return inv_put(&c->inv, it, to->x, to->y) ? (i32)c->inv.n - 1 : -1;
        return inv_add(&c->inv, it) ? 0 : -1;
    default: return -1;
    }
}

// Место заметно миру: земля или вещь сменила хозяина
static bool notable(const ItemPlace *from, const ItemPlace *to)
{
    return from->kind == IP_GROUND || to->kind == IP_GROUND || from->who != to->who;
}

i32 item_move(Ground *g, Phys *p, const ItemPlace *from, const ItemPlace *to)
{
    ItemStack it = item_at(g, from);
    // Внутри одного мешка на клетку: вещь может лечь поверх своего же прежнего места — проба после того, как взяли
    bool rebag = from->kind == IP_BAG && to->kind == IP_BAG && from->sheet == to->sheet && to->x != 255;
    if (!item_live(it) || from->kind >= IP_COUNT || to->kind == IP_NONE || (!rebag && !fits(g, to, it))) return -1;
    u8 ox = rebag ? from->sheet->inv.x[from->at] : 255, oy = rebag ? from->sheet->inv.y[from->at] : 0;
    V3 at = from->kind == IP_GROUND ? ground_pos(g, p, (u32)from->at) : to->kind == IP_GROUND ? v3(to->xf.m[12], to->xf.m[13], to->xf.m[14]) : v3(0, 0, 0);
    it = take(g, p, from);
    i32 k = put(g, p, to, it);
    if (k < 0) {
        // Некуда (физика полна, клетка занята): вещь возвращается — в мешке на свою клетку или в первое свободное
        ItemPlace back = *from;
        if (back.kind == IP_BAG) back.x = ox, back.y = oy;
        if (back.kind != IP_GROUND && back.kind != IP_NONE) put(g, p, &back, it);
        return -1;
    }
    if (notable(from, to))
        chron_add(g->chron, (ChronRec){ .kind = CH_ITEM, .from = 255, .to = 255, .a = from->who, .b = to->who, .n = it.def,
                                        .m = from->kind * 16 + to->kind, .x = at.x, .z = at.z });
    return k;
}
