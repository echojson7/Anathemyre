#include "items.h"

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
