// Таблицы оружия и брони. Числа баланса — в data/defs (docs/content.md), читаются при старте, не в коде боя.
#ifndef ANA_ITEMS_H
#define ANA_ITEMS_H

#include "body.h"

typedef enum WeaponClass { WC_BLADE, WC_CLEAVER, WC_BLUNT, WC_POLEARM, WC_HEAVY, WC_COUNT } WeaponClass;
typedef enum AttackKind { ATK_OVERHEAD, ATK_SWING, ATK_THRUST, ATK_COUNT } AttackKind;

typedef struct WeaponDef {
    const char *id;   // имя в описаниях: по нему моды правят запись, сохранение сверяет номера
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
    u8  size_w, size_h;  // клетки инвентаря
    u8  voice, voice_strike;  // голос железа (SFX_WPN_* по порядку): закрылся им и ударил им
    u16 part, parts;  // форма: части в g_weapon_parts
    bool common;      // в общем запасе: отряды, арена, толпа берут случайное оружие отсюда
    bool unique;      // единственная вещь: вид не зависит от качества
} WeaponDef;

// Часть формы оружия в своей системе: рукоять в начале координат, острие по +Y, лезвие по +Z.
// Клинок и пластина — с точками в g_weapon_pts (pt — начало, npt — сколько чисел)
typedef enum WeaponPartKind { WP_ROD, WP_BOX, WP_BLADE, WP_PLATE, WP_HOLE, WP_WRAP, WP_KNOB } WeaponPartKind;
typedef struct WeaponPart {
    u8  kind, mat;
    u32 pt;
    u16 npt;
    f32 color[3];
    // стержень: от, до, радиус, сдвиг; брусок: от, до, ширина, толщина, сдвиг, наклон;
    // пластина: толщина; прорезь: центр по оси и к лезвию, полуоси; обмотка: от, до, радиус, витков;
    // навершие: от, до, радиус, сдвиг
    f32 v[6];
} WeaponPart;

#define WEAPON_CAP 64  // сеток оружия на GPU

typedef struct ArmorDef {
    const char *id;
    const char *name;
    u8  slot;
    f32 cover[PART_COUNT];
    f32 res_cut, res_blunt, res_pierce;
    f32 weight;
    f32 dodge_pen;
    u8  size_w, size_h;
    u8  look;   // сетка одежды (GEAR_*), GEAR_NONE — не видна
    bool metal; // удар по ней звенит железом
    u32 hides;  // какие сетки под ней не рисуются: биты GEAR_*, GEAR_HIDES_HAIR — причёска
} ArmorDef;

#define ARMOR_NONE 255
#define GEAR_NONE 255
#define GEAR_HIDES_HAIR (1u << 31)
#define WEAPON_PARTS 48  // частей формы на оружие
#define WEAPON_PTS 1536  // чисел точек клинков и пластин на оружие

// Качество: множитель урона оружия и сопротивлений брони
enum { Q_JUNK, Q_WORN, Q_SOUND, Q_MASTER, Q_ANCIENT, Q_COUNT };

typedef struct AttackDef {
    f32 dmg_mul;
    f32 windup_mul;
    f32 reach_add;
    f32 arc_deg;      // 0 = дуга оружия (только для размаха)
} AttackDef;

#define ARM_REACH 0.7f

// Заполняет defs_load при старте
extern const WeaponDef  *g_weapons;
extern u32               g_weapon_count;
extern const WeaponPart *g_weapon_parts;
extern const f32        *g_weapon_pts;
// Общий запас (common yes): из него случайное оружие отрядов, арены и толпы, по порядку записей
extern u8                g_weapon_pool[WEAPON_CAP];
extern u32               g_weapon_pool_count;
extern const ArmorDef   *g_armors;
extern u32               g_armor_count;
extern AttackDef         g_attacks[ATK_COUNT];
extern f32               g_quality_mul[Q_COUNT];
extern const char       *g_quality_name[Q_COUNT];
extern const char       *g_attack_name[ATK_COUNT];
extern const u8          g_armor_layer_order[5];  // слоты снаружи внутрь

#endif
