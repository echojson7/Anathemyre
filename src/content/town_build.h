// Скудель — торговый посёлок к востоку от зала: три яруса на обломках древней стены,
// улицы с прилавками, дома в несколько этажей, мелочи быта, жители
#ifndef ANA_TOWN_H
#define ANA_TOWN_H

#include "../core/rng.h"
#include "../core/mesh.h"
#include "../sim/inventory.h"
#include "../sim/people.h"
#include "../sim/player.h"

#define TOWN_MAX_SPOTS 64
#define TOWN_MAX_FOLK  56

typedef enum SpotKind { SPOT_TRADER, SPOT_IDLE, SPOT_WORKER, SPOT_GUARD } SpotKind;

// Где житель стоит: за прилавком, на балконе, у края яруса, у кучи лома
typedef struct TownSpot {
    V3  pos;
    f32 heading;
    u8  kind;
} TownSpot;

typedef struct Town {
    u32      spot_count;
    TownSpot spot[TOWN_MAX_SPOTS];
    u32      stalls, houses;
} Town;

// Геометрия посёлка (отбрасывает тени) и места жителей
void town_build(MeshBuilder *mb, Rng *rng, Town *t);
// Высота яруса под точкой, если она в посёлке; иначе base
f32  town_ground(f32 x, f32 z, f32 base);
// Жители (People, ROLE_TOWN): торговцы, сторожа и зеваки стоят на местах, прохожие ходят по узлам улиц
void town_folk_add(People *p, const Town *t, Rng *rng, u32 walkers, u32 variants, u8 team);

#endif
