// Места и дороги карты: граф для отрядов вдали (bands.h). Место — точка на карте с именем, дорога — прямая
// между двумя местами. Следующее место на кратчайшем пути считается для всех пар один раз (places_build)
#ifndef ANA_PLACES_H
#define ANA_PLACES_H

#include "../core/arena.h"

#define PLACES_MAX 255   // номер места — u8, 255 — «нет»
#define PLACE_NONE 255
#define ROADS_MAX 1024

typedef struct Places {
    u32         count, roads;
    f32         x[PLACES_MAX], z[PLACES_MAX];
    const char *name[PLACES_MAX];
    u8          road_a[ROADS_MAX], road_b[ROADS_MAX];
    u8         *next;   // count × count: следующее место по пути из a в b (PLACE_NONE — не дойти)
} Places;

u32  places_add(Places *p, const char *name, f32 x, f32 z);
void places_road(Places *p, u32 a, u32 b);
// Кратчайшие пути по дорогам между всеми парами; next — в perm (временные расстояния там же, после — снимаются)
bool places_build(Places *p, Arena *perm);
static inline u8 places_next(const Places *p, u32 from, u32 goal) { return p->next[from * p->count + goal]; }
f32  places_dist(const Places *p, u32 a, u32 b);

#endif
