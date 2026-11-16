// Общее для mech.cpp (поза и кадр) и mech_move.cpp (поведение, шаги, пружины): размеры и мелкие функции
#pragma once
#include <math.h>
#include "../anim/pose.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include "mech.h"

constexpr f32 MECH_FOOT_H = 0.08f;       // ось лапы над подошвой (FOOT_H в tools/parts/vezha_leg.py)
constexpr f32 MECH_BELLY = 0.20f;        // оси бёдер над землёй, когда рама лежит на полозьях (BELLY в vezha.py)
constexpr f32 MECH_H_STAND = 0.86f;      // оси бёдер над опорой: стоит
constexpr f32 MECH_H_HIGH = 1.16f;       // привстала посмотреть
constexpr f32 MECH_H_LOW = 0.50f;        // присела
constexpr f32 MECH_THIGH_MIN = -1.536f;  // ход бедра к горизонту: −88°…+32° (ход поршня, vezha_leg.py)
constexpr f32 MECH_THIGH_MAX = 0.558f;
constexpr f32 MECH_PACK_YAW = -2.3561945f;   // пачка: бедро повёрнуто на −135° от диагонали, вдоль стороны рамы
constexpr f32 MECH_V_WALK = 1.5f, MECH_V_DASH = 3.3f;   // скорость шагом и рывком, м/с
constexpr f32 MECH_SEE_M = 16.0f;        // дальше людей не замечает
constexpr f32 MECH_JUMP_V = 3.6f;        // скорость тела вверх в толчке прыжка, м/с
constexpr f32 MECH_H_LAND = 0.74f;       // оси бёдер над опорой в миг касания после прыжка
constexpr f32 MECH_G = 9.8f;

inline f32 mech_wrap(f32 a) {
    while (a > PI_F) a -= TAU_F;
    while (a < -PI_F) a += TAU_F;
    return a;
}
inline f32 mech_smooth(f32 x) {
    x = f_clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}
inline f32 mech_mix(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
// Случайное 0..1 (своё зерно машины: поведение не зависит от шага мира)
inline f32 mech_rnd(u32 &s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (f32)(s >> 8) * (1.0f / 16777216.0f);
}
// Пружина с затуханием: x идёт к to с частотой w (рад/с) и долей затухания z
inline void mech_spring(f32 &x, f32 &v, f32 to, f32 w, f32 z, f32 dt) {
    v += (w * w * (to - x) - 2.0f * z * w * v) * dt;
    x += v * dt;
}
inline WPos mech_moved(const WPos &p, f32 dx, f32 dz) {
    WPos q = p;
    q.x += dx;
    q.z += dz;
    wpos_rebase(q);
    return q;
}
// Оси машины (x вправо, z вперёд) → мир при курсе yaw и обратно; высота не меняется
inline Vec3 mech_to_world(f32 yaw, Vec3 v) {
    f32 s = sinf(yaw), c = cosf(yaw);
    return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z};
}
inline Vec3 mech_to_body(f32 yaw, Vec3 v) {
    f32 s = sinf(yaw), c = cosf(yaw);
    return {c * v.x - s * v.z, v.y, s * v.x + c * v.z};
}
inline Quat mech_tilt(const Mech &m) {
    return quat_mul(quat_axis_angle(Vec3{1, 0, 0}, m.pitch), quat_axis_angle(Vec3{0, 0, 1}, m.roll));
}
// Вынос лап от оси поворота бедра по высоте тела: лёжа — шире (нога почти сложена и лежит плашмя, голеностоп не
// в земле), стоя — под собой
inline f32 mech_reach(const MechView &mv, f32 h) {
    return mv.u + mech_mix(0.48f, 0.40f, mech_smooth((h - 0.26f) / 0.5f));
}
// Опора под точкой: высота и нормаль
inline f32 mech_support(const Extract &c, const WPos &p, f32 from_y, Vec3 *n) {
    Support s = support_at(*c.terrain, *c.coll, p.cx, p.cz, p.x, p.z, from_y);
    if (n) *n = s.kind ? Vec3{0, 1, 0} : ground_at(*c.terrain, p).normal;
    return s.height;
}
// Точка в осях рамы (начало — на оси машины на высоте осей бёдер) → вынос от оси качания и высота в плоскости ноги
// k и поворот бедра от бинда
struct MechReach {
    f32 yaw, x, y;
};
inline MechReach mech_leg_reach(const MechView &mv, u32 k, Vec3 p) {
    Vec3 v = p - mv.corner[k];
    f32 phi0 = atan2f(-mv.dir[k].z, mv.dir[k].x);
    MechReach r;
    r.yaw = mech_wrap(atan2f(-v.z, v.x) - phi0);
    r.x = f_sqrt(v.x * v.x + v.z * v.z) - mv.u;
    r.y = v.y;
    return r;
}
// Где лапа сейчас: на опоре или на дуге шага (подошва)
inline WPos mech_leg_now(const MechLeg &l) {
    if (l.t < 0.0f) return l.at;
    f32 e = mech_smooth(l.t);
    Vec2 d = wpos_delta_xz(l.from, l.to);
    WPos p = mech_moved(l.from, d.x * e, d.y * e);
    p.y = mech_mix(l.from.y, l.to.y, e) + l.lift * 4.0f * l.t * (1.0f - l.t);
    return p;
}
// Самая короткая и самая длинная нога (от оси качания до оси лапы)
inline f32 mech_leg_min(const MechView &mv) { return mv.l1 + mv.l3 - mv.l2 + 0.0005f; }
inline f32 mech_leg_max(const MechView &mv) { return mv.l1 + mv.l3 + mv.l2 - 0.02f; }

// Человек, на которого машина смотрит
struct MechTarget {
    bool on;
    WPos at;
    f32 dist;
};
// Шаг жизни машины на dt секунд (dt ≤ 1/100): поведение, тело, лапы, пружины
void mech_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt);
// Куда встаёт лапа k, когда раскладывается: точка мира на опоре
WPos mech_leg_ground(const MechView &mv, const Mech &m, const Extract &c, u32 k, Vec3 pol);
