// ИИ бойца: цель, позиция, вид удара, мораль. Решения раз в COMBAT_THINK_TICKS тиков.
// Решение считается параллельно и пишет только в своего бойца. Замахи применяются
// вторым проходом: меньший номер не отменяет удар того, кто уже решил бить.
#ifndef ANA_AI_FIGHTER_H
#define ANA_AI_FIGHTER_H

#include "../sim/combat.h"

typedef struct FighterAI {
    u32 cap;
    u8  *act;     // ACT_* — что решил боец
    u8  *kind;    // вид удара
    i32 *target;  // выбранная цель
    f32 *next_morale; // мораль после решения, пишется в мир когда решили все
    u8  *morale_set;
    // Середина строя каждой команды (кто ещё в бою): куда идти, если врагов не видно
    f32 cx[COMBAT_MAX_TEAMS], cz[COMBAT_MAX_TEAMS];
    u32 cn[COMBAT_MAX_TEAMS];
} FighterAI;

void fighter_ai_init(FighterAI *ai, Arena *a, u32 cap);
// Сохранение: решения первых count бойцов и середины строя
void fighter_ai_ser(Ser *s, FighterAI *ai, u32 count);
// Вызывать перед каждым combat_tick
void fighter_think_all(FighterAI *ai, CombatWorld *w);

#endif
