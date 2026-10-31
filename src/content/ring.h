// Ринг арены у зала: формы мачт и ламп, высота плиты. Раскладка — заготовка ring (data/defs/prefabs.txt),
// свет — объекты ring_spot и ring_lamp. Бои и зрители — sim/arena_ring.h
#ifndef ANA_RING_H
#define ANA_RING_H

#include "../core/mesh.h"
#include "../sim/arena_ring.h"


// Высота плиты арены под точкой; вне плиты — base
f32 ring_ground(f32 x, f32 z, f32 base);

#endif
