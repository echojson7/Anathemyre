#include "ground.h"

#include "../content/hall.h"
#include "../content/ostov.h"
#include "../content/ring.h"
#include "../content/town_build.h"
#include "../view/showhall.h"
#include "../world/map.h"

f32 app_ground(f32 x, f32 z)
{
    if (hall_floor(x, z)) return HALL_FLOOR_Y;
    f32 h = showhall_ground(x, z, ostov_ground(x, z, town_ground(x, z, map_height(x, z))));
    return ring_ground(x, z, h);
}
