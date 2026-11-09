// Математика для шага мира: только + - * / и точный sqrt, своё sin/cos.
// libm (<cmath>, sinf, powf...) в шаге мира запрещена: у CRT Windows и glibc разные приближения.
// Сборка с -ffp-contract=off, иначе компилятор сольёт a*b+c в FMA и результат поменяется.
#pragma once
#include "types.h"

constexpr f32 PI_F = 3.14159265358979323846f;
constexpr f32 TAU_F = 6.28318530717958647692f;

inline f32 f_abs(f32 x) { return x < 0.0f ? -x : x; }
inline f32 f_min(f32 a, f32 b) { return a < b ? a : b; }
inline f32 f_max(f32 a, f32 b) { return a > b ? a : b; }
inline f32 f_clamp(f32 x, f32 lo, f32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
// Точный корень по IEEE (sqrtss): одинаков на всех x86-64
inline f32 f_sqrt(f32 x) { return __builtin_sqrtf(x); }
// Целая часть вниз для |x| < 2^31
inline i32 f_floor_i(f32 x) {
    i32 i = (i32)x;
    return (x < (f32)i) ? i - 1 : i;
}

// Угол в долях оборота: u32, 2^32 = полный оборот. Складывается без потерь и без float-накопления
typedef u32 Turn;

// sin и cos угла в оборотах. Ошибка < 1e-6: четверть по старшим битам, внутри октанта — ряды Тейлора
inline void sincos_turn(Turn a, f32 *s, f32 *c) {
    u32 q = a >> 30;
    u32 r = a & 0x3FFFFFFFu;
    bool hi = r > 0x20000000u;
    if (hi) r = 0x40000000u - r;
    f32 x = (f32)(i32)r * (PI_F * 0.5f / 1073741824.0f);  // [0, pi/4]
    f32 x2 = x * x;
    f32 sn = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f))));
    f32 cs = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f))));
    if (hi) { f32 t = sn; sn = cs; cs = t; }
    switch (q) {
    case 0: *s = sn; *c = cs; break;
    case 1: *s = cs; *c = -sn; break;
    case 2: *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn; break;
    }
}

// Радианы → обороты. Для |rad| < ~3e8, дальше точность падает
inline Turn turn_from_rad(f32 rad) {
    f32 t = rad * (1.0f / TAU_F);
    t -= (f32)f_floor_i(t);
    return (Turn)(u32)(i64)(t * 4294967296.0f);
}

// Курс направления (dx, dz): 0 — на +z, четверть оборота — на +x (как quat_yaw: вперёд = (sin, cos)).
// atan — многочлен на [0, 1] (ошибка ~1e-5 рад), только + - * /: годится для шага мира
inline Turn turn_of_dir(f32 dx, f32 dz) {
    f32 ax = f_abs(dx), az = f_abs(dz);
    if (ax == 0.0f && az == 0.0f) return 0;
    bool swap = ax > az;
    f32 t = swap ? az / ax : ax / az;
    f32 t2 = t * t;
    f32 a = t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f + t2 * (-0.11643287f + t2 * (0.05265332f + t2 * -0.01172120f)))));
    if (swap) a = PI_F * 0.5f - a;
    if (dz < 0.0f) a = PI_F - a;
    if (dx < 0.0f) a = -a;
    return (Turn)(u32)(i64)(a * (4294967296.0f / TAU_F));
}
// Разность курсов b − a в радианах, [−π, π)
inline f32 turn_diff_rad(Turn a, Turn b) { return (f32)(i32)(b - a) * (TAU_F / 4294967296.0f); }
// Курс + угол в радианах (|rad| < π)
inline Turn turn_add_rad(Turn a, f32 rad) { return a + (u32)(i32)(i64)(rad * (4294967296.0f / TAU_F)); }

inline f32 sin_f(f32 rad) { f32 s, c; sincos_turn(turn_from_rad(rad), &s, &c); return s; }
inline f32 cos_f(f32 rad) { f32 s, c; sincos_turn(turn_from_rad(rad), &s, &c); return c; }

struct Vec2 {
    f32 x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, f32 k) { return {a.x * k, a.y * k}; }
inline Vec2 &operator+=(Vec2 &a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
inline f32 dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline f32 length(Vec2 a) { return f_sqrt(dot(a, a)); }
