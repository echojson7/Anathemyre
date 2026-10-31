// Скудель — торговый посёлок к востоку от зала: три яруса на обломках древней стены,
// улицы с прилавками, дома в несколько этажей, мелочи быта. Жители — world/town.c
#ifndef ANA_TOWN_BUILD_H
#define ANA_TOWN_BUILD_H

#include "../core/mesh.h"
#include "../core/rng.h"
#include "../world/town.h"

// Геометрия посёлка (отбрасывает тени) и места жителей
void town_build(MeshBuilder *mb, Rng *rng, Town *t);
// Высота яруса под точкой, если она в посёлке; иначе base
f32  town_ground(f32 x, f32 z, f32 base);

#endif
