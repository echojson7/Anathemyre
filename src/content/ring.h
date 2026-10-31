// Ринг арены у зала: геометрия и высота плиты. Бои и зрители — sim/arena_ring.h, свет — view/scene.c
#ifndef ANA_RING_H
#define ANA_RING_H

#include "../core/mesh.h"
#include "../sim/arena_ring.h"

#define RING_FENCE_R 7.15f  // ограда: восьмиугольник, внутрь влезает круг ARENA_RADIUS
// Свет арены (view/scene.c): два прожектора на мачтах и две лампы на воротах
#define RING_LIGHTS 4

// Ринг, ограда, прожекторы, круг знаков: статика с тенями
void ring_build(MeshBuilder *mb, V3 center);
// Угол восьмиугольника с вписанным радиусом r_in; k = 0..7, отсчёт от +Z к +X
V3   ring_corner(V3 c, f32 r_in, int k, f32 y);
// Высота плиты арены под точкой; вне плиты — base
f32  ring_ground(f32 x, f32 z, f32 base);

#endif
