// Высота под ногами во всём мире: земля карты и полы построек. Мир берёт её как GroundFn (World.floor):
// какие постройки стоят в мире, решает сборка игры, а не мир
#ifndef ANA_APP_GROUND_H
#define ANA_APP_GROUND_H

#include "../core/types.h"

f32 app_ground(f32 x, f32 z);

#endif
