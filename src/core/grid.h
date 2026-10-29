// Равномерная сетка на плоскости XZ для поиска соседей. Перестраивается целиком
// (сортировка подсчётом). Внутри ячейки объекты разложены по меткам (например,
// команда или «выбыл»), а внутри метки по возрастанию номера: порядок обхода
// детерминирован и не зависит от числа потоков, а запрос по маске меток
// не перебирает лишних (своих, мёртвых).
#ifndef ANA_GRID_H
#define ANA_GRID_H

#include "arena.h"

#define GRID_MAX_TAGS 32
#define GRID_BLOCK 4      // грубый слой: блок из GRID_BLOCK×GRID_BLOCK ячеек
#define GRID_ALL_TAGS 0xffffffffu

typedef struct Grid {
    f32 cell, inv_cell;   // фактический размер ячейки (растёт, если поле не влезает)
    f32 min_cell;
    f32 ox, oz;           // угол сетки
    u32 w, h, ntags;
    u32 max_keys;         // предел ячеек × меток
    u32 cap, count;
    u32 *start;           // max_keys + 1: начало пары (ячейка, метка) в items
    u32 *items;           // номера объектов, отсортированные по ячейке и метке
    u32 *key_of;          // ключ каждого объекта
    // Грубый слой: маска меток в каждом блоке. Дешёвая проверка «есть ли вообще
    // кто-то нужный» перед дорогим запросом
    u32 bw, bh;
    u32 *block_mask;
} Grid;

void grid_init(Grid *g, Arena *a, u32 cap, u32 max_keys, f32 cell);
// tag — метка каждого объекта (< ntags <= GRID_MAX_TAGS), NULL — у всех метка 0
void grid_build(Grid *g, const f32 *x, const f32 *z, const u8 *tag, u32 ntags, u32 count);
// Номера с метками из mask из ячеек, задетых квадратом 2r вокруг точки
// (расстояние проверяет вызывающий). Возвращает сколько записано; при переполнении out обрезается.
u32  grid_query(const Grid *g, f32 x, f32 z, f32 r, u32 mask, u32 *out, u32 out_cap);
// Объединение меток в блоках, задетых квадратом 2r (с запасом до размера блока)
u32  grid_tags(const Grid *g, f32 x, f32 z, f32 r);

#endif
