// Звуки мира в игре: слушатель у камеры, фон по месту, шаги по фазе походки и покрытию,
// бой — по журналу CombatWorld (наблюдатель) и по виду бойца (достал, убрал, выронил).
// Только читает состояние; что какой звук значит — docs/sound.md
#ifndef ANA_SFX_H
#define ANA_SFX_H

#include "../sim/arena_ring.h"
#include "../sim/field.h"
#include "../sim/ground.h"
#include "../sim/phys.h"

// Звуки одного боя: поля или арены
typedef struct SfxFight {
    V3   origin;              // начало координат боя в мире
    const Field *field;       // для поля: чьи тела где
    GroundFn ground;
    u8   down[FIELD_MAX];     // лежит: падение уже прозвучало
    u8   awake[FIELD_MAX];    // был в бою в прошлом кадре
    u8   dropped[FIELD_MAX];
    f32  wield[FIELD_MAX];
    f32  phase[FIELD_MAX];    // фаза шага: в бою — вида бойца, иначе — ходьбы
    u32  salt;                // своя высота оружия у бойца: на арене меняется с каждым боем
    u32  roll;                // свой счётчик случая: мировой RNG звук не трогает
} SfxFight;

typedef struct WorldSound {
    V3   lis;
    V3   arena_center;
    u32  arena_bout;
    SfxFight field, arena;
    u32  hit_seen;            // удары физики: сколько уже прочитано из кольца
} WorldSound;

// Подключить наблюдателей к журналам поля и арены
void sfx_init(WorldSound *s, Field *field, ArenaSession *arena, V3 arena_center, GroundFn ground);
// Раз в кадр до остальных: слушатель от камеры (eye, точка взгляда, поворот) и фон места (зал, пустошь, долина)
void sfx_listener(WorldSound *s, V3 eye, V3 target, f32 yaw);
// Шаги, оружие в руках: все тела поля (отряд, бродящие, мобы, зрители, жители Скудели), бойцы арены.
// view — тела такими, какими их показывает кадр (Figures.view, по слоту): шаги звучат в такт ногам
void sfx_field(WorldSound *s, const Field *f, const Player *view);
void sfx_arena(WorldSound *s, const ArenaRing *v, const ArenaSession *a);
// Удары вещей с физикой (лом, оружие и вещи на земле) по материалу: о землю и друг о друга
void sfx_phys(WorldSound *s, const Phys *p, const Ground *g);
// Класс удара материала (Impact) → звук
u8   impact_sfx(u8 impact);

#endif
