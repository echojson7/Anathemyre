// Долина у реки: зелёная локация. Рельеф с уступами, русло и поля растительности (без графики)
#ifndef ANA_VALLEY_H
#define ANA_VALLEY_H

#include "../core/arena.h"
#include "../core/math3d.h"

#define VALLEY_HALF 320.0f   // половина стороны подробной карты высот, м
#define VALLEY_RES 641       // узлов карты на сторону, шаг 1 м
// Грубая карта вокруг подробной: склоны долины до края, где они сходят в пустошь (map.c)
#define VALLEY_FAR_HALF 1100.0f
#define VALLEY_FAR_STEP 4.0f
#define VALLEY_FAR_RES 551
// Где долина на карте мира (map.h): её начало координат. Функции ниже — в координатах долины
#define VALLEY_ORIGIN v3(1900.0f, 0.0f, 500.0f)

// Карта высот из постоянной арены (по потокам); до вызова valley_height считает по формуле
void valley_init(Arena *perm);
// Высота земли: внутри подробной карты — по её узлам (как сетка рельефа), вокруг — по грубой, дальше по формуле
f32  valley_height(f32 x, f32 z);
f32  valley_height_raw(f32 x, f32 z);
V3   valley_normal(f32 x, f32 z);
// Река течёт к +Z: ось русла, полуширина, уровень воды, расстояние от оси
f32  valley_river_x(f32 z);
f32  valley_river_w(f32 z);
f32  valley_water_y(f32 z);
f32  valley_river_dist(f32 x, f32 z);
// Где появляется отряд: поляна на восточном берегу ниже акведука
V3   valley_spawn(void);
// Поля 0..1: густота леса и сухость травы
f32  valley_forest(f32 x, f32 z);
f32  valley_dry(f32 x, f32 z);
// Шум значений 0..1 для разброса (общий с построением долины)
f32  valley_fbm(f32 x, f32 z, int octaves);
// Сухость земли 0..1 (с высотой суше, у воды сочнее) и цвет земли с нормалью n
f32  valley_ground_dry(f32 x, f32 z, f32 h);
V3   valley_color(f32 x, f32 z, f32 h, V3 n);

#endif
