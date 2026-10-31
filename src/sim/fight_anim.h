// Вид бойца: состояние боя (CombatWorld) -> поза и действие для character_pose.
// Общий для арены и боя в открытом мире; симуляция о нём ничего не знает.
#ifndef ANA_FIGHT_ANIM_H
#define ANA_FIGHT_ANIM_H

#include "combat.h"
#include "anim.h"

typedef struct FightAnim {
    f32  prev_x, prev_z, prev_h;      // позиция до последнего тика, для сглаживания
    f32  disp_x, disp_z;              // показанная позиция: отброшенного ведёт плавно
    f32  phase, walk, speed, move_dir;
    f32  guard, block, dodge, dodge_side, stagger, flinch, wield, limp;
    f32  swing;                       // замах 0..1, удар 1..2, возврат 2..3 (CharAction.swing)
    f32  calm;                        // сколько бой стоит (w->halted), с
    f32  vt;                          // время в текущем состоянии, с
    f32  clock;                       // время жизни вида, с (дыхание лёжа)
    f32  seed;
    f32  hp_prev[PART_COUNT];
    u8   vst, dodge_kind, flinch_part;
    bool prone, from_stand, from_kneel, turn, dropped;
    // Клип всем телом сейчас и до смены состояния: из прежнего в новый — плавно за fade_s
    u8   clip, pclip;
    f32  clip_t, pclip_t, fade, fade_s;
    bool pprone;
    i8   side, pside;
    f32  lock_x, lock_z;              // где была цель в начале замаха: от уворота удар уходит в пустоту
    f32  knock_x, knock_z;            // откуда отбросило ударом
    // Боевой шаг: где стоят стопы (координаты боя), шаг из from в нынешнее место в стойке
    f32  foot_x[2], foot_z[2], from_x[2], from_z[2];
    f32  step_t[2];                   // доля шага; < 0 — стопа стоит
    f32  feet;                        // вес стойки у ног
    f32  untidy;                      // сколько стоит, поставив ноги не на место, с
    V3   feet_off[2];                 // для позы: сдвиг стоп от места в стойке, модель
    u32  steps;                       // шагов в стойке: по ним звук
} FightAnim;

// Боец вошёл в бой: оружие ещё убрано, достанет сам
void fight_anim_reset(FightAnim *f, const CombatWorld *w, u32 i, f32 seed);
// После чтения сохранения: вид не пишется в файл — заново и сразу в нынешнем состоянии (оружие в руке или на земле)
void fight_anim_settle(FightAnim *f, const CombatWorld *w, u32 i, f32 seed);
// Перед каждым combat_tick: запомнить позицию
void fight_anim_pre_tick(FightAnim *f, const CombatWorld *w, u32 i);
// Раз в кадр; alpha — доля времени до следующего тика
void fight_anim_update(FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, f32 dt);
// Через сколько секунд после события падения тело касается земли (звук удара о землю)
f32  fight_anim_contact(const CombatWorld *w, u32 i, u8 ev_kind);
// Поза в мире: origin — начало координат боя, floor_y — высота земли.
// true — оружия в руке нет: лежит на земле вещью мира (рисует её физика) или его забрали
bool fight_anim_pose(const FightAnim *f, const CombatWorld *w, u32 i, f32 alpha, V3 origin, f32 floor_y, f32 time,
                     CharPose *p, CharAction *a);

#endif
