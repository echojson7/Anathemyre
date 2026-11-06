// Земля для шага мира — только через ground_at() (docs/invariants.md, docs/render.md «Земля»).
// Этап 1: плоскость y = 0. Этап 2 заменит тело функции на сетку высот 8 м + слой деталей 1 м,
// вызывающие (размещение объектов, потом ноги, путь и бой) не меняются
#pragma once
#include "../core/math3.h"
#include "../core/wpos.h"

struct World;

// Вид земли в точке: номер слоя (песок, сухая земля, щебень…), пока один
enum GroundKind : u32 {
    GROUND_PLAIN = 0,
};

struct GroundSample {
    f32 height = 0;          // метры от нуля карты
    Vec3 normal{0, 1, 0};
    u32 kind = GROUND_PLAIN;
};

inline GroundSample ground_at(const World &, i32 cx, i32 cz, f32 x, f32 z) {
    (void)cx; (void)cz; (void)x; (void)z;
    return GroundSample{};
}
inline GroundSample ground_at(const World &w, const WPos &p) { return ground_at(w, p.cx, p.cz, p.x, p.z); }
