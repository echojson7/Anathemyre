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
