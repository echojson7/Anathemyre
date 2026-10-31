// Тестовая комната: бруталистский зал, руины, мегаструктура вдали, бродящие люди, зрители арены и мобы
#ifndef ANA_TESTROOM_H
#define ANA_TESTROOM_H

#include "../core/rng.h"
#include "../core/mesh.h"
#include "../render/render.h"
#include "../sim/inventory.h"
#include "../sim/people.h"
#include "../sim/player.h"

// Арена у зала, перед его открытым краем
#define ARENA_CENTER v3(14.0f, 0.0f, 34.0f)

// Зал и обломки: отбрасывают тени
void testroom_build_near(MeshBuilder *mb, Rng *rng);
// Мегаструктуры у горизонта: тени не отбрасывают, в теневую карту не попадают
void testroom_build_far(MeshBuilder *mb, Rng *rng);
// Лампы над стойками с оружием в зале: горят всегда
u32  testroom_lights(PointLight *out, u32 max);
// Высота поверхности под ногами: пол зала, ярусы посёлка, плита арены или земля карты (без обломков)
f32  testroom_ground(f32 x, f32 z);
// Бродящие по залу (People, ROLE_WANDER): ходят шагом от точки к точке, у каждого лист и оружие —
// на них можно напасть, и они отобьются
void crowd_add(People *p, Rng *rng, u32 count);
// Зрители арены (ROLE_WATCHER): стоят в заданных точках лицом к рингу, у некоторых факелы
void watchers_add(People *p, const V3 *pos, const f32 *heading, const f32 *seed, u32 count, u8 team);
// Мобы (ROLE_MOB): люди со снаряжением ходят кругом вокруг локации, на углах стоят;
// одни шагом, другие трусцой, один бегом. Лист у каждого полный, как у своего отряда
void mobs_add(People *p, Rng *rng);

#endif
