#include "items.h"

#include <string.h>

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
