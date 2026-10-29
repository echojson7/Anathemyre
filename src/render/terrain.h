// Рельеф кольцами вокруг камеры (clipmap) для render.c: уровни с шагом 1, 2, 4… м, у каждого сетка
// TERRAIN_CELLS² клеток, у внешних без середины — её закрывает уровень мельче
#ifndef ANA_TERRAIN_H
#define ANA_TERRAIN_H

#include "render.h"

#define TERRAIN_LEVELS 7     // шаг до 64 м, край колец ~4 км от центра
#define TERRAIN_CELLS 128    // клеток на сторону уровня

bool terrain_init(Arena *scratch);
void terrain_shutdown(void);
void terrain_source(TerrainFill fill, void *user);
// Центр колец: точка взгляда, но не дальше FOCUS_MAX от глаза. Дописывает новые полосы уровней
void terrain_update(V3 eye, V3 target);
// Видимые куски уровней 0..levels−1 по плоскостям пирамиды (нормаль внутрь xyz, смещение w), кроме
// кусков с перепадом высот меньше min_relief (ровное тени не отбрасывает). Программа сцены или теней
// уже выбрана; draws и tris — прибавить к счётчикам прохода
void terrain_draw(const V4 planes[6], u32 levels, f32 min_relief, u32 *draws, f64 *tris);

#endif
