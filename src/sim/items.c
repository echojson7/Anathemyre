#include "items.h"

#define ATK(k) (1u << (k))

const WeaponDef g_weapons[W_COUNT] = {
    [W_RESSORA]  = { "РЕССОРА", WC_BLADE, ATK(ATK_OVERHEAD) | ATK(ATK_SWING) | ATK(ATK_THRUST), 2,
                     24, 4, 12, 0.9f, 3, 1.00f, 70, 0.3f, 0.2f },
    [W_SEKACH]   = { "СЕКАЧ", WC_CLEAVER, ATK(ATK_OVERHEAD) | ATK(ATK_SWING), 2,
                     32, 12, 0, 1.0f, 7, 0.82f, 90, 0.6f, 0.0f },
    [W_ARMATURA] = { "АРМАТУРА", WC_BLUNT, ATK(ATK_OVERHEAD) | ATK(ATK_SWING), 2,
                     2, 30, 0, 0.9f, 6, 0.85f, 80, 1.0f, 0.0f },
    [W_BAGOR]    = { "БАГОР", WC_POLEARM, ATK(ATK_THRUST) | ATK(ATK_SWING), 3,
                     14, 8, 26, 2.0f, 6, 0.85f, 60, 0.6f, 0.4f },
    [W_BALKA]    = { "БАЛКА", WC_HEAVY, ATK(ATK_SWING) | ATK(ATK_OVERHEAD), 4,
                     30, 40, 0, 1.4f, 18, 0.60f, 140, 1.6f, 0.0f },
};

//                                  голова грудь живот рукаЛ рукаП ногаЛ ногаП
const ArmorDef g_armors[A_COUNT] = {
    [A_RAGS]    = { "РОБА", SLOT_TORSO_UNDER, { 0, 0.8f, 0.8f, 0.8f, 0.8f, 0, 0 }, 0.10f, 0.05f, 0.05f, 1, 0.00f },
    [A_JACKET]  = { "РЕЗИНОВАЯ КУРТКА", SLOT_TORSO_ARMOR, { 0, 0.85f, 0.85f, 0.85f, 0.85f, 0, 0 }, 0.25f, 0.15f, 0.20f, 4, 0.05f },
    [A_QUILT]   = { "КОМБИНЕЗОН", SLOT_TORSO_UNDER, { 0, 0.9f, 0.9f, 0, 0, 0, 0 }, 0.15f, 0.25f, 0.10f, 3, 0.03f },
    [A_PLATE]   = { "ОБШИВКА", SLOT_TORSO_ARMOR, { 0, 0.75f, 0.4f, 0, 0, 0, 0 }, 0.55f, 0.20f, 0.35f, 9, 0.15f },
    [A_BUCKET]  = { "КОЖУХ", SLOT_HEAD, { 0.9f, 0, 0, 0, 0, 0, 0 }, 0.60f, 0.25f, 0.50f, 3, 0.05f },
    [A_GREAVES] = { "ТРУБНЫЕ ПОНОЖИ", SLOT_LEGS, { 0, 0, 0, 0, 0, 0.7f, 0.7f }, 0.30f, 0.20f, 0.25f, 4, 0.08f },
    [A_PANTS]   = { "РАБОЧИЕ ШТАНЫ", SLOT_PANTS, { 0, 0, 0, 0, 0, 0.8f, 0.8f }, 0.10f, 0.05f, 0.05f, 1, 0.00f },
};

const AttackDef g_attacks[ATK_COUNT] = {
    [ATK_OVERHEAD] = { 1.15f, 1.00f, 0.0f, 40 },
    [ATK_SWING]    = { 1.00f, 1.00f, 0.0f, 0 },
    [ATK_THRUST]   = { 1.00f, 0.85f, 0.3f, 30 },
};

const f32 g_quality_mul[Q_COUNT] = { 0.6f, 0.8f, 1.0f, 1.15f, 1.3f };
const char *g_quality_name[Q_COUNT] = { "ХЛАМ", "ХОДОВОЕ", "ДОБРОТНОЕ", "МАСТЕРСКОЕ", "ДРЕВНЕЕ" };
const char *g_attack_name[ATK_COUNT] = { "УДАР СВЕРХУ", "РАЗМАХ", "УКОЛ" };
const u8 g_armor_layer_order[5] = { SLOT_HEAD, SLOT_TORSO_ARMOR, SLOT_LEGS, SLOT_PANTS, SLOT_TORSO_UNDER };
