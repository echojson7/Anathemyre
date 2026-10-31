// Ряд людей для сравнения (меню НАСТРОЙКИ, --humans): тела MakeHuman по уровням детализации рядом,
// левее — разная внешность и вещи по одной на тех же сетках
#ifndef ANA_HUMANS_ROW_H
#define ANA_HUMANS_ROW_H

#include "../view/figures.h"

#define HUMANS_N (BODY_KIND_COUNT * CHAR_LODS)
#define HUMANS_LOOKS (GEAR_COUNT + 1)

typedef struct HumansRow {
    bool    on;
    u32     count;
    GpuMesh mesh[HUMANS_N], hair[HUMANS_N];  // причёска — первый вид тела, у худого без неё
    bool    has_hair[HUMANS_N];
    M4     *bones;   // HUMANS_N наборов
    M4     *looks;   // HUMANS_LOOKS наборов
} HumansRow;

void humans_row_init(HumansRow *h, Arena *perm, GroundFn ground);
// Показать или убрать; сетки грузятся при первом показе
void humans_row_toggle(HumansRow *h, MeshBuilder *mb, Arena *perm, const char *data_dir, GroundFn ground);
u32  humans_row_draw(const HumansRow *h, const CharMeshes *m, DrawItem *items);

#endif
