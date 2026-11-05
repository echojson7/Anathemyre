// Положение в мире: целый номер чанка 256 м по X и Z плюс смещение f32 внутри чанка, высота f32 от нуля карты.
// Абсолютных f32-координат по карте нет: на 28 км шаг float ~2 мм (docs/invariants.md).
#pragma once
#include "mathf.h"

constexpr f32 CHUNK_M = 256.0f;

struct WPos {
    i32 cx = 0, cz = 0;   // номер чанка
    f32 x = 0, y = 0, z = 0;  // x, z в [0, 256), y — высота
};

// Перенести смещение в соседний чанк, если вышло за [0, 256)
inline void wpos_rebase(i32 &c, f32 &o) {
    if (o >= CHUNK_M || o < 0.0f) {
        i32 d = f_floor_i(o * (1.0f / CHUNK_M));
        c += d;
        o -= (f32)d * CHUNK_M;
        if (o >= CHUNK_M) { o -= CHUNK_M; c += 1; }  // защита от округления у края
        if (o < 0.0f) o = 0.0f;
    }
}
inline void wpos_rebase(WPos &p) { wpos_rebase(p.cx, p.x); wpos_rebase(p.cz, p.z); }

// Разность b - a по X и Z в метрах. Точна, пока чанки рядом (|разность| ≲ 64 км)
inline Vec2 wpos_delta_xz(i32 acx, i32 acz, f32 ax, f32 az, i32 bcx, i32 bcz, f32 bx, f32 bz) {
    return {(f32)(bcx - acx) * CHUNK_M + (bx - ax), (f32)(bcz - acz) * CHUNK_M + (bz - az)};
}
inline Vec2 wpos_delta_xz(const WPos &a, const WPos &b) {
    return wpos_delta_xz(a.cx, a.cz, a.x, a.z, b.cx, b.cz, b.x, b.z);
}
