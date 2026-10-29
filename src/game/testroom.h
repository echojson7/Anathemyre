// Тестовая комната: бруталистский зал, руины, мегаструктура вдали, бродящие фигуры
#ifndef ANA_TESTROOM_H
#define ANA_TESTROOM_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "../sim/inventory.h"
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

// Мобы: люди со снаряжением ходят кругом вокруг локации, на углах стоят;
// одни шагом, другие трусцой, один бегом. Лист у каждого полный, как у своего отряда
#define MOB_MAX 5
#define MOB_ROUTE 6

typedef struct Mobs {
    u32       count;
    Player    body[MOB_MAX];
    CharSheet sheet[MOB_MAX];
    u8        variant[MOB_MAX];
    u8        next[MOB_MAX];     // следующая точка маршрута
    f32       wait[MOB_MAX];     // стоит на месте ещё столько секунд
    f32       seed[MOB_MAX];
} Mobs;

void mobs_init(Mobs *m, Rng *rng);
void mobs_update(Mobs *m, Rng *rng, f32 dt);

// Зал и обломки: отбрасывают тени
void testroom_build_near(MeshBuilder *mb, Rng *rng);
// Рельеф и мегаструктуры: тени не отбрасывают, в теневую карту не попадают
void testroom_build_far(MeshBuilder *mb, Rng *rng);
// Высота поверхности под ногами: пол зала, ярусы посёлка или рельеф (без обломков)
f32  testroom_ground(f32 x, f32 z);
void crowd_init(Crowd *c, Rng *rng, u32 count);
void crowd_update(Crowd *c, Rng *rng, f32 dt);

#endif
