// Поза скелета → палитра скиннинга (docs/people.md, «Поза и скиннинг»). Считает только вид, шаг мира поз не
// знает. Выборка клипов и смешивание — clip.h
#pragma once
#include "../asset/skel.h"
#include "../core/math3.h"

// Матрица 3×4 по строкам: точка сетки в бинд-позе → точка сетки в позе (как RenderBone)
struct PoseMatrix {
    f32 m[12];
};

// Поворот вокруг оси (единичной) на угол в радианах
Quat quat_axis_angle(Vec3 axis, f32 rad);

// Кватернионы вида (шаг мира их не знает)
inline Quat quat_mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Quat quat_conj(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Vec3 quat_rotate(Quat q, Vec3 v) {
    Vec3 u{q.x, q.y, q.z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// Наименьший поворот единичного a в единичное b (a ≈ −b — вокруг любой поперечной оси)
Quat quat_between(Vec3 a, Vec3 b);

// Поза в осях сетки (прямая кинематика): на кость поворот и голова. rot, trans_bone, trans — как у pose_palette
void pose_world(const Skel &s, const Quat *rot, i32 trans_bone, const Vec3 *trans, Quat *wr, Vec3 *wp);
// Палитра из позы в осях сетки (pose_world)
void pose_palette_world(const Skel &s, const Quat *wr, const Vec3 *wp, PoseMatrix *out);
// Повернуть кость i в осях сетки на q (поворот вокруг её головы, дети — за ней): правит местный rot[i] по
// позе wr этой же кости
inline void pose_turn_world(Quat *rot, const Quat *wr, u32 i, Quat q) {
    rot[i] = quat_mul(rot[i], quat_mul(quat_conj(wr[i]), quat_mul(q, wr[i])));
}
// Палитра: на кость — поза · обратная бинд-матрица. rot — на кость поворот в её осях после бинд-позы
// (nullptr — бинд-поза у всех); дети идут за родителем. trans — голова кости trans_bone в осях сетки (кость
// переноса клипа, таз), nullptr — от родителя, как в бинде. out — s.bones.count матриц
void pose_palette(const Skel &s, const Quat *rot, PoseMatrix *out, i32 trans_bone = -1, const Vec3 *trans = nullptr);
