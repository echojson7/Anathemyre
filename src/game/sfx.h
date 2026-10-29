// Звуки мира в игре: слушатель у камеры, фон по месту, шаги по фазе походки и покрытию,
// бой — по журналу CombatWorld (наблюдатель) и по виду бойца (достал, убрал, выронил).
// Только читает состояние; что какой звук значит — docs/sound.md
#ifndef ANA_SFX_H
#define ANA_SFX_H

#include "arena_view.h"
#include "field.h"

// Звуки одного боя: поля или арены
typedef struct SfxFight {
    V3   origin;              // начало координат боя в мире
    bool valley;              // слушатель в долине: в пустоши ничего не слышно, на поле — только свой отряд
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
    bool valley;
    V3   arena_center;
    u32  arena_bout;
    SfxFight field, arena;
} WorldSound;

// Подключить наблюдателей к журналам поля и арены
void sfx_init(WorldSound *s, Field *field, ArenaSession *arena, V3 arena_center, GroundFn ground);
// Раз в кадр до остальных: слушатель от камеры (eye, точка взгляда, поворот) и фон места
void sfx_listener(WorldSound *s, V3 eye, V3 target, f32 yaw, bool valley);
// Шаги, оружие в руках: все тела поля (отряд, бродящие, мобы, зрители, жители Скудели), бойцы арены
void sfx_field(WorldSound *s, const Field *f);
void sfx_arena(WorldSound *s, const ArenaView *v, const ArenaSession *a);

#endif
