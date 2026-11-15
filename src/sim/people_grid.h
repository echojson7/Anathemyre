// Сетка людей (docs/people.md «Сетка людей»): кто рядом — без проверок «все со всеми» (docs/invariants.md).
// Записи по ключу клетки (ряд z, потом x), внутри клетки — по номеру места: устойчивая сортировка из порядка
// номеров. Ответ «кто в круге» — набор номеров битами, обход — по возрастанию номера, поэтому итог проверок
// тот же, что у перебора всех мест по порядку
#pragma once
#include "people.h"

constexpr u32 PEOPLE_GRID_FINE = 1;    // клетка 2 м: расталкивание, удар (сдвиг 1 — 1/2 м⁻¹)
constexpr u32 PEOPLE_GRID_COARSE = 3;  // клетка 8 м: выбор цели ИИ

struct PeopleGridEntry {
    u64 key;
    u32 slot;
};

struct PeopleGrid {
    PeopleGridEntry e[PEOPLE_CAPACITY];
    u32 n = 0;
    u32 shift = PEOPLE_GRID_FINE;  // клетка 2^shift м
};

// Набор номеров мест битами
struct PeopleSet {
    u64 bits[PEOPLE_CAPACITY / 64];
};

// Ключ клетки точки (cx, cz, x, z), сдвинутой на dx, dz клеток
inline u64 people_grid_key(const PeopleGrid &g, i32 cx, i32 cz, f32 x, f32 z, i32 dx, i32 dz) {
    const i32 per = (i32)CHUNK_M >> g.shift;
    const f32 inv = 1.0f / (f32)(1u << g.shift);  // степень двух: умножение точно
    i32 gx = cx * per + f_floor_i(x * inv) + dx, gz = cz * per + f_floor_i(z * inv) + dz;
    return ((u64)(u32)(gz + (1 << 30)) << 32) | (u64)(u32)(gx + (1 << 30));
}

// Живые люди в сетку с клеткой 2^shift м; tmp — место на PEOPLE_CAPACITY записей
void people_grid_build(const People &p, u32 shift, PeopleGrid &g, PeopleGridEntry *tmp);
// Первая запись с ключом ≥ key
u32 people_grid_find(const PeopleGrid &g, u64 key);
// Все, чьи клетки задевают квадрат ±r вокруг точки (точное расстояние проверяет вызывающий)
void people_grid_near(const PeopleGrid &g, i32 cx, i32 cz, f32 x, f32 z, f32 r, PeopleSet &out);
// Все живые (без сетки: шаг без арены)
void people_set_alive(const People &p, PeopleSet &out);

// Обход набора по возрастанию номера: f(slot)
template <typename F>
void people_set_each(const PeopleSet &s, F f) {
    for (u32 w = 0; w < PEOPLE_CAPACITY / 64; ++w)
        for (u64 b = s.bits[w]; b; b &= b - 1) f(w * 64 + (u32)__builtin_ctzll(b));
}
