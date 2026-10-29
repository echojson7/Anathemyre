// Таблицы оружия и брони. Все числа баланса здесь, не в коде боя.
#ifndef ANA_ITEMS_H
#define ANA_ITEMS_H

#include "body.h"

typedef enum WeaponClass { WC_BLADE, WC_CLEAVER, WC_BLUNT, WC_POLEARM, WC_HEAVY, WC_COUNT } WeaponClass;
typedef enum AttackKind { ATK_OVERHEAD, ATK_SWING, ATK_THRUST, ATK_COUNT } AttackKind;

typedef struct WeaponDef {
    const char *name;
    u8  wclass;
    u8  attacks;      // маска (1 << AttackKind)
    u8  max_targets;  // предел целей размаха
    f32 cut, blunt, pierce;
    f32 length;       // досягаемость = рука 0.7 + длина
    f32 weight;       // кг
    f32 speed;        // 1 = быстрое, меньше = медленнее
    f32 arc_deg;      // дуга размаха
    f32 knock;        // сила сбивания
    f32 pen;          // пробой брони уколом, 0..1
} WeaponDef;

enum { W_RESSORA, W_SEKACH, W_ARMATURA, W_BAGOR, W_BALKA, W_COUNT };

typedef struct ArmorDef {
    const char *name;
    u8  slot;
    f32 cover[PART_COUNT];
    f32 res_cut, res_blunt, res_pierce;
    f32 weight;
    f32 dodge_pen;
} ArmorDef;

enum { A_RAGS, A_JACKET, A_QUILT, A_PLATE, A_BUCKET, A_GREAVES, A_COUNT };
#define ARMOR_NONE 255

// Качество: множитель урона оружия и сопротивлений брони
enum { Q_JUNK, Q_WORN, Q_SOUND, Q_MASTER, Q_ANCIENT, Q_COUNT };

typedef struct AttackDef {
    f32 dmg_mul;
    f32 windup_mul;
    f32 reach_add;
    f32 arc_deg;      // 0 = дуга оружия (только для размаха)
    f32 part_w[PART_COUNT];
} AttackDef;

#define ARM_REACH 0.7f

extern const WeaponDef g_weapons[W_COUNT];
extern const ArmorDef  g_armors[A_COUNT];
extern const AttackDef g_attacks[ATK_COUNT];
extern const f32       g_quality_mul[Q_COUNT];
extern const char     *g_quality_name[Q_COUNT];
extern const char     *g_attack_name[ATK_COUNT];
extern const u8        g_armor_layer_order[4];  // слоты снаружи внутрь

#endif
