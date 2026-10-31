// Скудель в мире: места жителей и сами жители. Геометрия посёлка — content/town_build.c
#ifndef ANA_TOWN_H
#define ANA_TOWN_H

#include "../core/rng.h"
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

// Жители (People, ROLE_TOWN): торговцы, сторожа и зеваки стоят на местах, прохожие ходят по узлам улиц
void town_folk_add(People *p, const Town *t, Rng *rng, u32 walkers, u32 variants, u8 team);

#endif
