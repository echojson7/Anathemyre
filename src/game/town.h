// Скудель — торговый посёлок к востоку от зала: три яруса на обломках древней стены,
// улицы с прилавками, дома в несколько этажей, мелочи быта, жители
#ifndef ANA_TOWN_H
#define ANA_TOWN_H

#include "../core/rng.h"
#include "../render/mesh.h"
#include "player.h"

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

typedef enum FolkKind { FOLK_STAND, FOLK_WALK } FolkKind;

// Жители: торговцы и зеваки стоят на местах, прохожие ходят по узлам улиц
typedef struct TownFolk {
    u32    count;
    Player body[TOWN_MAX_FOLK];
    f32    seed[TOWN_MAX_FOLK];
    f32    wait[TOWN_MAX_FOLK];
    f32    pace[TOWN_MAX_FOLK];    // свой темп шага, м/с
    u8     variant[TOWN_MAX_FOLK];
    u8     kind[TOWN_MAX_FOLK];    // FolkKind
    u8     node[TOWN_MAX_FOLK], prev[TOWN_MAX_FOLK];
    bool   armed[TOWN_MAX_FOLK];
} TownFolk;

// Геометрия посёлка (отбрасывает тени) и места жителей
void town_build(MeshBuilder *mb, Rng *rng, Town *t);
// Высота яруса под точкой, если она в посёлке; иначе base
f32  town_ground(f32 x, f32 z, f32 base);
// Выравнивание рельефа вокруг посёлка: 0 — ровно, 1 — дюны как обычно
f32  town_flatten(f32 x, f32 z);
void town_folk_init(TownFolk *f, const Town *t, Rng *rng, u32 walkers, u32 variants);
void town_folk_update(TownFolk *f, Rng *rng, f32 dt, GroundFn ground);

#endif
