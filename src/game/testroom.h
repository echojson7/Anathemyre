// Тестовая комната: бруталистский зал, руины, мегаструктура вдали, бродящие фигуры
#ifndef ANA_TESTROOM_H
#define ANA_TESTROOM_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "player.h"

#define TR_MAX_CHARS 16

// Бродящие фигуры: тела те же, что у отряда (Player), ходят шагом от точки к точке
typedef struct Crowd {
    u32    count;
    Player body[TR_MAX_CHARS];
    f32    wait[TR_MAX_CHARS];
    f32    seed[TR_MAX_CHARS];
    u8     variant[TR_MAX_CHARS];
    bool   stationary[TR_MAX_CHARS];
} Crowd;

// Зал и обломки: отбрасывают тени
void testroom_build_near(MeshBuilder *mb, Rng *rng);
// Рельеф и мегаструктуры: тени не отбрасывают, в теневую карту не попадают
void testroom_build_far(MeshBuilder *mb, Rng *rng);
// Высота поверхности под ногами: пол зала, ярусы посёлка или рельеф (без обломков)
f32  testroom_ground(f32 x, f32 z);
void crowd_init(Crowd *c, Rng *rng, u32 count);
void crowd_update(Crowd *c, Rng *rng, f32 dt);

#endif
