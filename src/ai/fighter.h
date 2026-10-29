// ИИ бойца: цель, позиция, вид удара, мораль. Решения раз в COMBAT_THINK_TICKS тиков.
#ifndef ANA_AI_FIGHTER_H
#define ANA_AI_FIGHTER_H

#include "../sim/combat.h"

// Вызывать перед каждым combat_tick
void fighter_think_all(CombatWorld *w);

#endif
