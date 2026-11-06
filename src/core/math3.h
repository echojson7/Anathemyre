// Векторы 3D и кватернион. Только + - * / и точный sqrt,
// углы — через свои sin/cos (mathf.h), поэтому годится и для шага мира, и для кадра
#pragma once
#include "mathf.h"

struct Vec3 {
    f32 x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, f32 k) { return {a.x * k, a.y * k, a.z * k}; }
inline Vec3 &operator+=(Vec3 &a, Vec3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline f32 dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline f32 length(Vec3 a) { return f_sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    f32 l = length(a);
    return l > 0.0f ? a * (1.0f / l) : Vec3{0, 1, 0};
}

// Кватернион поворота (x, y, z — ось·sin, w — cos половины угла)
struct Quat {
    f32 x = 0, y = 0, z = 0, w = 1;
};
// Поворот вокруг вертикали (y) на угол в оборотах
inline Quat quat_yaw(Turn yaw) {
    f32 s, c;
    sincos_turn(yaw >> 1, &s, &c);  // половина угла; yaw ≥ пол-оборота — тот же поворот с минусом
    return {0.0f, s, 0.0f, c};
}

