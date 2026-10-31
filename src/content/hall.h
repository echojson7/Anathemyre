// Зал и пустошь вокруг: бруталистский зал с оружейной, обломки, мегаструктуры вдали. Раскладка — заготовки
// hall и horizon (data/defs/prefabs.txt), формы стоек и ламп — hall.c. Люди зала — world/crowd.c
#ifndef ANA_HALL_H
#define ANA_HALL_H

#include "../core/math3d.h"

// Колпак лампы над стойкой смотрит сюда (свет — объект hall_lamp)
#define HALL_LAMP_DIR v3(0.0f, -0.88f, -0.47f)
#define HALL_FLOOR_Y 0.1f

// Точка на плите пола зала (верх HALL_FLOOR_Y)
bool hall_floor(f32 x, f32 z);

#endif
