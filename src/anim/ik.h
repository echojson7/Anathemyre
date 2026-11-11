// IK для вида (docs/people.md, «Ходьба»): две кости ноги и стопа по опоре. Шаг мира IK не знает
#pragma once
#include "pose.h"

// Две кости: a (бедро) → b (голень) → c (стопа). Правит местные rot[a], rot[b] так, чтобы голова c пришла в target
// (оси сетки); колено остаётся в своей плоскости, при прямой ноге — в сторону pole. wr, wp — поза до правки
// (pose_world). Недостижимую цель нога тянется прямой
void ik_two_bone(Quat *rot, const Quat *wr, const Vec3 *wp, u32 a, u32 b, u32 c, Vec3 target, Vec3 pole);
// Повернуть стопу c так, чтобы её «верх» лёг по нормали опоры n (оси сетки), не круче max_rad; weight — доля
void ik_foot_align(Quat *rot, const Quat *wr, u32 c, Vec3 n, f32 max_rad, f32 weight);
