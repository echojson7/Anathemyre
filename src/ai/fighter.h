// ИИ бойца: цель, позиция, вид удара, мораль. Решения раз в COMBAT_THINK_TICKS тиков.
// Решение считается параллельно и пишет только в своего бойца; всё, что задевает
// других (замах, сдача, журнал), применяется потом в одном потоке по номерам.
#ifndef ANA_AI_FIGHTER_H
#define ANA_AI_FIGHTER_H

#include "../sim/combat.h"

typedef struct FighterAI {
    u32 cap;
    u8  *act;     // ACT_* — что решил боец
    u8  *kind;    // вид удара
    i32 *target;  // выбранная цель
    // Середина строя каждой команды (кто ещё в бою): куда идти, если врагов не видно
    f32 cx[COMBAT_MAX_TEAMS], cz[COMBAT_MAX_TEAMS];
    u32 cn[COMBAT_MAX_TEAMS];
} FighterAI;

void fighter_ai_init(FighterAI *ai, Arena *a, u32 cap);
// Вызывать перед каждым combat_tick
void fighter_think_all(FighterAI *ai, CombatWorld *w);

#endif
