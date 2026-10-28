// Тестовая комната: бруталистский зал, руины, мегаструктура вдали, бродящие фигуры
#ifndef ANA_TESTROOM_H
#define ANA_TESTROOM_H

#include "../core/rng.h"
#include "../render/mesh.h"

#define TR_MAX_CHARS 16

// Толпа в виде структуры массивов
typedef struct Crowd {
    u32 count;
    f32 x[TR_MAX_CHARS], z[TR_MAX_CHARS];
    f32 heading[TR_MAX_CHARS];
    f32 phase[TR_MAX_CHARS];
    f32 walk[TR_MAX_CHARS];
    f32 target_x[TR_MAX_CHARS], target_z[TR_MAX_CHARS];
    f32 wait[TR_MAX_CHARS];
    f32 seed[TR_MAX_CHARS];
    u8  variant[TR_MAX_CHARS];
    bool stationary[TR_MAX_CHARS];
} Crowd;

void testroom_build(MeshBuilder *mb, Rng *rng);
void crowd_init(Crowd *c, Rng *rng, u32 count);
void crowd_update(Crowd *c, Rng *rng, f32 dt);

#endif
