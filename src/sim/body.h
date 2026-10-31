// Стандарт тела гуманоида: роли костей, части тела, хит-объёмы, слоты, крепления.
// Одна топология для всех людей; числа тела — в описании (data/defs/bodies.txt, docs/body.md).
// Не зависит от рендера: размеры в метрах, поза привязки.
#ifndef ANA_BODY_H
#define ANA_BODY_H

#include "../core/math3d.h"

// Роли костей: 20, родитель всегда раньше ребёнка. Код знает роли, имена костей в файле — описание тела
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

// Капсула в шаблоне тела: концы — суставы костей from и to со сдвигом, м. Числа не зависят от
// пропорций: другой скелет — капсулы на его суставах (как шаблоны тела Dwarf Fortress)
typedef struct HitDef {
    u8  part, bone, from, to;
    V3  da, db;
    f32 r;
} HitDef;

typedef enum EquipSlot {
    SLOT_HEAD, SLOT_FACE, SLOT_TORSO_UNDER, SLOT_TORSO_ARMOR, SLOT_LEGS,
    SLOT_FEET, SLOT_HANDS, SLOT_BACK, SLOT_BELT, SLOT_PANTS,
    SLOT_COUNT
} EquipSlot;

// Точки крепления на теле. Точка хвата на самом оружии — в описании вещи
typedef enum Socket {
    SOCKET_GRIP,    // хват: правый кулак
    SOCKET_GRIP2,   // второй хват: левый кулак
    SOCKET_SHEATH,  // ножны на поясе
    SOCKET_BACK,    // за спиной
    SOCKET_TORCH,   // факел в левом кулаке
    SOCKET_COUNT
} Socket;

typedef struct SocketDef {
    u8 bone;
    V3 pos;  // в позе привязки
} SocketDef;

// Часть ragdoll: капсула от сустава bone до сустава to, доля массы тела и сустав к родительской части.
// Пределы — в осях сустава (sim/phys.h, PhysJointDesc), радианы: шар — отклонение не больше cone, кручение lo..hi;
// петля — сгиб lo..hi, 0 — как в позе привязки
typedef enum RagJoint { RAG_ROOT, RAG_BALL, RAG_HINGE } RagJoint;  // по номерам как PhysJointKind
typedef struct RagPart {
    u8  bone, to, parent, joint;  // parent — номер части выше по списку
    f32 r, mass;
    f32 lo, hi, cone;
} RagPart;

#define BODY_MAX_HIT_CAPSULES 12
#define BODY_RAG_MAX 16

// Описание тела (data/defs/bodies.txt, вид body): скелет, роли костей, крепления, хит-капсулы, ragdoll.
// Заполняет defs при старте; docs/body.md
typedef enum BodyClassId {
    BODY_H,  // человек: основные персонажи и почти всё население
    BODY_CLASS_COUNT
} BodyClassId;

typedef struct BodyDef {
    f32 height;                      // рост скелета в позе привязки, м: от него множитель роста человека
    const char *skeleton;            // общий скелет боя и креплений: data/meshes/<skeleton>.skel
    char bone_name[BONE_COUNT][16];  // имя кости в .skel для каждой роли BONE_*
    SocketDef socket[SOCKET_COUNT];
    HitDef hit[BODY_MAX_HIT_CAPSULES];
    u32 hits;
    RagPart rag[BODY_RAG_MAX];
    u32 rags;
} BodyDef;

extern BodyDef g_body;
// Родители — стандарт гуманоида; суставы — из g_body.skeleton (body_load)
extern BoneDef          g_body_bones[BONE_COUNT];
// Капсулы в позе привязки (body_load) и в боевой стойке с одноручным (ставит вид при старте,
// anim_bake_hit_guard). По стойке считается, куда пришёлся удар по стоящему
extern HitCapsule       g_body_hit[BODY_MAX_HIT_CAPSULES];
extern HitCapsule       g_body_hit_guard[BODY_MAX_HIT_CAPSULES];
extern f32              g_part_hp[PART_COUNT];      // HP частей (описание тела)
extern const char      *g_part_name[PART_COUNT];

// Множитель роста человека от роста в метрах
static inline f32 body_scale(f32 height_m) { return height_m / g_body.height; }

// Скелет из файла .skel (tools/humanconv): суставы в позе привязки по ролям BONE_*. Кости находят роль по имени
// (g_body.bone_name), родители сверяются со стандартом g_body_bones. false — файла нет или он не по стандарту
bool body_skeleton_load(const char *path, V3 *joints);
// Общий скелет и капсулы позы привязки: после описаний (defs_load)
bool body_load(const char *data_dir);

// Жизненная часть: при HP <= порога — нокаут, при <= -max — смерть
static inline bool body_part_vital(BodyPart p) { return p <= PART_STOMACH; }

#endif
