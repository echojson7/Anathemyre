// Замеры без окна: один ключ --bench <имя>, сценарии — data/debug/bench.txt (docs/debug.md).
// Каждый замер печатает итог и сумму в консоль; одинаковое зерно — одинаковая сумма
#ifndef ANA_DEBUG_BENCH_H
#define ANA_DEBUG_BENCH_H

#include "../core/types.h"

// Сколько боёв арены подряд; log_bouts — сколько первых печатать подробно
int arena_bench(u32 bouts, u64 seed, u32 log_bouts);
// Полевой бой двух отрядов. threads: всего потоков, 0 — по числу ядер. verify: повторить на 1, 2, 4, 8 потоках
// и сравнить суммы
int battle_bench(u32 side_a, u32 side_b, u64 seed, u32 threads, bool verify);
// Сценка атаки с журналом, затем нагрузка на bodies тел (0 — без неё)
int field_bench(u32 bodies, u64 seed);
// Бой людей реестра side_a на side_b через поле боя, с позами без кадра: время ходьбы, боя и поз
int people_bench(u32 side_a, u32 side_b, u64 seed, u32 threads);
// Развороты: шаг, трусца, бег; 90°, 135°, 180°; trace_path — CSV траекторий или NULL
int player_bench(const char *trace_path);
// count отрядов вдали на случайных местах и дорогах, hours часов дальнего мира
int bands_bench(u32 count, f32 hours, u64 seed, u32 phases, u32 log_events);

#endif
