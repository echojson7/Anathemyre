// Вид бойца: состояние боя (CombatWorld) -> поза и действие для character_pose.
// Общий для арены и боя в открытом мире; симуляция о нём ничего не знает.
#ifndef ANA_FIGHT_ANIM_H
#define ANA_FIGHT_ANIM_H

#include "../sim/combat.h"
#include "anim.h"

typedef struct FightAnim {
    f32  prev_x, prev_z, prev_h;      // позиция до последнего тика, для сглаживания
    f32  disp_x, disp_z;              // показанная позиция: отброшенного ведёт плавно
    f32  phase, walk, speed, move_dir;
    f32  guard, block, dodge, dodge_side, stagger, flinch, wield, limp;
    f32  calm;                        // сколько бой стоит (w->halted), с
    f32  vt;                          // время в текущем состоянии, с
    f32  seed;
    f32  hp_prev[PART_COUNT];
    u8   vst, dodge_kind, flinch_part;
    bool prone, from_stand, dropped;
    V3   drop_pos;                    // в координатах боя
    f32  drop_yaw;
    f32  lock_x, lock_z;              // где была цель в начале замаха: от уворота удар уходит в пустоту
} FightAnim;

// Боец вошёл в бой: оружие ещё убрано, достанет сам
void fight_anim_reset(FightAnim *f, const CombatWorld *w, u32 i, f32 seed);
// Перед каждым combat_tick: запомнить позицию
void fight_anim_pre_tick(FightAnim *f, const CombatWorld *w, u32 i);
// Раз в кадр; alpha — доля времени до следующего тика
void fight_anim_update(FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, f32 dt);
// Поза в мире: origin — начало координат боя, floor_y — высота земли.
// true — оружие выпало и лежит в drop_pos/drop_yaw (мир), в руке его нет
bool fight_anim_pose(const FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, V3 origin, f32 floor_y, f32 time,
                     CharPose *p, CharAction *a, V3 *drop_pos, f32 *drop_yaw);

#endif
