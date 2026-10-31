// Зал и пустошь вокруг: бруталистский зал с оружейной, обломки, мегаструктуры вдали. Люди зала — world/crowd.c
#ifndef ANA_HALL_H
#define ANA_HALL_H

#include "../core/mesh.h"
#include "../core/rng.h"

// Лампы над стойками с оружием: горят всегда (свет — view/scene.c)
#define HALL_LAMPS 5
#define HALL_LAMP_DIR v3(0.0f, -0.88f, -0.47f)
extern const V3 g_hall_lamp[HALL_LAMPS];
#define HALL_FLOOR_Y 0.1f

// Зал и обломки: отбрасывают тени
void hall_build_near(MeshBuilder *mb, Rng *rng);
// Мегаструктуры у горизонта: тени не отбрасывают, в теневую карту не попадают
void hall_build_far(MeshBuilder *mb, Rng *rng);
// Точка на плите пола зала (верх HALL_FLOOR_Y)
bool hall_floor(f32 x, f32 z);

#endif
