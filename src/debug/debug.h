// Отладка: стенды, кнопки и подстановка ввода. Игра собирается без неё (NODEBUG=1 → ANA_NODEBUG), шов 7
#pragma once
#include "../core/mem.h"
#include "../core/rng.h"
#include "../platform/platform.h"
#include "../sim/intent.h"
#include "../sim/terrain.h"

struct DebugState {
    bool step_once = false;   // «.» на паузе — один шаг
    bool lines = true;        // L — отладочные отрезки
    i32 clock_hours = 0;      // [ ] — часы назад и вперёд на час (за этот кадр)
    bool clock_speed = false; // T — следующая скорость суток (за этот кадр)
};
// Скорости суток по T: тиков часов за шаг (1 — обычный ход: сутки за 48 минут; 60 — за 48 секунд)
constexpr u32 CLOCK_SPEEDS[] = {1, 20, 120, 0};

#ifndef ANA_NODEBUG
// Клавиши отладки за кадр
void debug_keys(const Input &in, DebugState &d);
// Подставной игрок для --sim: намерения из своего RNG (не RNG мира) по номеру шага
void autoplay_intents(u64 step, Rng &rng, FixedArray<Intent> &out);
void debug_print_help();
// Снимок кадра (render_capture_next перед кадром) → файл .bmp
bool debug_write_shot(const char *path, Arena &scratch);
// Карта земли сверху (отмывка рельефа и маски) → .bmp
bool debug_write_terrain_map(const Terrain &t, const char *path, Arena &scratch);
#else
inline void debug_keys(const Input &, DebugState &) {}
inline void autoplay_intents(u64, Rng &, FixedArray<Intent> &) {}
inline void debug_print_help() {}
inline bool debug_write_shot(const char *, Arena &) { return false; }
inline bool debug_write_terrain_map(const Terrain &, const char *, Arena &) { return false; }
#endif
