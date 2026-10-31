// Полевой бой двух отрядов без окна: нагрузка на симуляцию и проверка детерминизма
#ifndef ANA_BATTLE_H
#define ANA_BATTLE_H

#include "../core/types.h"

// threads: всего потоков, 0 — по числу ядер. verify: повторить на 1, 2, 4, 8 потоках
// и сравнить контрольные суммы
int battle_bench(u32 side_a, u32 side_b, u64 seed, u32 threads, bool verify);

#endif
