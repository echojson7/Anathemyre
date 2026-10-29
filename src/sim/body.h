// Стандарт тела гуманоида: скелет, части тела, хит-объёмы, слоты, классы телосложения.
// Одна топология для всех людей; крупные расы позже — тот же скелет с другими множителями.
// Не зависит от рендера: размеры в метрах, поза привязки класса H.
#ifndef ANA_BODY_H
#define ANA_BODY_H

#include "../core/math3d.h"

// Скелет: 20 костей, родитель всегда раньше ребёнка
typedef enum BodyBone {
    BONE_ROOT, BONE_PELVIS, BONE_SPINE, BONE_CHEST, BONE_NECK, BONE_HEAD,
    BONE_CLAVICLE_L, BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_HAND_L,
    BONE_CLAVICLE_R, BONE_UPPER_ARM_R, BONE_FOREARM_R, BONE_HAND_R,
    BONE_THIGH_L, BONE_SHIN_L, BONE_FOOT_L,
    BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_R,
    BONE_COUNT
} BodyBone;

typedef struct BoneDef {
    i32 parent;
    V3  joint;  // сустав в позе привязки, пространство модели
} BoneDef;

// Части тела, по которым считаются раны
typedef enum BodyPart {
    PART_HEAD, PART_CHEST, PART_STOMACH,
    PART_ARM_L, PART_ARM_R, PART_LEG_L, PART_LEG_R,
    PART_COUNT
} BodyPart;

// Хит-объём: капсула на кости (a == b — сфера)
typedef struct HitCapsule {
    u8  part, bone;
    V3  a, b;
    f32 r;
} HitCapsule;

typedef enum EquipSlot {
    SLOT_HEAD, SLOT_FACE, SLOT_TORSO_UNDER, SLOT_TORSO_ARMOR, SLOT_LEGS,
    SLOT_FEET, SLOT_HANDS, SLOT_BACK, SLOT_BELT, SLOT_PANTS,
    SLOT_COUNT
} EquipSlot;

typedef enum Socket {
    SOCKET_HAND_R, SOCKET_HAND_L, SOCKET_BACK, SOCKET_BELT,
    SOCKET_COUNT
} Socket;

typedef struct SocketDef {
    u8 bone;
    V3 pos;  // в позе привязки
} SocketDef;

typedef enum BodyClassId {
    BODY_H,  // человек: основные персонажи и почти всё население
    BODY_CLASS_COUNT
} BodyClassId;

typedef struct BodyClass {
    f32 height_min, height_max;  // м
    f32 mass_min, mass_max;      // кг
    f32 hp_mul;                  // множитель HP частей
    f32 radius;                  // общая капсула тела для толпы и сетки
    f32 stride;                  // длина полного цикла шага
    f32 knock_resist;            // множитель сопротивления сбиванию
} BodyClass;

// Поза привязки класса H, рост 1.80 м
#define BODY_BASE_HEIGHT 1.80f
#define BODY_HIP_Y   0.958f
#define BODY_KNEE_Y  0.526f
#define BODY_ANKLE_Y 0.074f
#define BODY_HIP_X   0.113f  // у бедра; колено и лодыжка чуть шире (g_body_bones)

#define BODY_MAX_HIT_CAPSULES 12

extern const BoneDef    g_body_bones[BONE_COUNT];
extern const BodyClass  g_body_classes[BODY_CLASS_COUNT];
extern const HitCapsule g_body_hit[BODY_MAX_HIT_CAPSULES];
extern const HitCapsule g_body_hit_guard[BODY_MAX_HIT_CAPSULES];  // в боевой стойке
extern const u32        g_body_hit_count;
extern const SocketDef  g_body_sockets[SOCKET_COUNT];
extern const f32        g_part_hp[PART_COUNT];      // HP класса H
extern const f32        g_part_hit_share[PART_COUNT]; // доля попаданий без учёта вида удара
extern const char      *g_part_name[PART_COUNT];

// Жизненная часть: при HP <= порога — нокаут, при <= -max — смерть
static inline bool body_part_vital(BodyPart p) { return p <= PART_STOMACH; }

#endif
