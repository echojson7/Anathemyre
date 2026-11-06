// Отладка: стенды, кнопки и подстановка ввода. Игра собирается без неё (NODEBUG=1 → ANA_NODEBUG), шов 7
#pragma once
#include "../core/mem.h"
#include "../core/rng.h"
#include "../platform/platform.h"
#include "../sim/intent.h"

struct DebugState {
    bool step_once = false;   // «.» на паузе — один шаг
    bool lines = true;        // L — отладочные отрезки
};

#ifndef ANA_NODEBUG
// Клавиши отладки за кадр
void debug_keys(const Input &in, DebugState &d);
// Подставной игрок для --sim: намерения из своего RNG (не RNG мира) по номеру шага
void autoplay_intents(u64 step, Rng &rng, FixedArray<Intent> &out);
void debug_print_help();
#else
inline void debug_keys(const Input &, DebugState &) {}
inline void autoplay_intents(u64, Rng &, FixedArray<Intent> &) {}
inline void debug_print_help() {}
#endif
