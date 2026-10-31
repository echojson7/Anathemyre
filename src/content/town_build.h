// Скудель — торговый посёлок к востоку от зала: три яруса на обломках древней стены,
// улицы с прилавками, дома в несколько этажей, мелочи быта. Жители — world/town.c
#ifndef ANA_TOWN_BUILD_H
#define ANA_TOWN_BUILD_H

#include "../world/town.h"

// Места жителей. Сам посёлок — объекты по заготовке skudel (data/defs/prefabs.txt), формы частей — town_build.c
void town_spots(Town *t);
// Высота яруса под точкой, если она в посёлке; иначе base
f32  town_ground(f32 x, f32 z, f32 base);

#endif
