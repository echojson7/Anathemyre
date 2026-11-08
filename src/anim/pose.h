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
// Палитра: на кость — поза · обратная бинд-матрица. rot — на кость поворот в её осях после бинд-позы
// (nullptr — бинд-поза у всех); дети идут за родителем. trans — голова кости trans_bone в осях сетки (кость
// переноса клипа, таз), nullptr — от родителя, как в бинде. out — s.bones.count матриц
void pose_palette(const Skel &s, const Quat *rot, PoseMatrix *out, i32 trans_bone = -1, const Vec3 *trans = nullptr);
