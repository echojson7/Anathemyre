#include "ik.h"
#include <math.h>

static f32 angle_between(Vec3 u, Vec3 v) { return acosf(f_clamp(dot(normalize(u), normalize(v)), -1.0f, 1.0f)); }

// Двухкостный IK по закону косинусов (как у D. Holden, «Simple Two Joint IK»): сначала угол в колене и бедре до
// нужного расстояния бедро — цель, потом поворот всей ноги на цель
void ik_two_bone(Quat *rot, const Quat *wr, const Vec3 *wp, u32 ia, u32 ib, u32 ic, Vec3 t, Vec3 pole) {
    Vec3 a = wp[ia], b = wp[ib], c = wp[ic];
    f32 lab = length(b - a), lcb = length(b - c);
    if (lab < 1e-4f || lcb < 1e-4f) return;
    f32 lat = f_clamp(length(t - a), 0.01f, (lab + lcb) * 0.999f);
    f32 ac_ab_0 = angle_between(c - a, b - a);
    f32 ba_bc_0 = angle_between(a - b, c - b);
    f32 ac_at_0 = angle_between(c - a, t - a);
    f32 ac_ab_1 = acosf(f_clamp((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
    f32 ba_bc_1 = acosf(f_clamp((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));
    Vec3 n0 = cross(c - a, b - a);
    if (length(n0) < 1e-5f) n0 = cross(c - a, pole);  // нога прямая: плоскость колена — по подсказке
    if (length(n0) < 1e-6f) return;
    Vec3 axis0 = normalize(n0);
    Vec3 n1 = cross(c - a, t - a);
    // Мир: бедро сначала сгибается (R0), потом вся нога поворачивается на цель (R2); колено — R1
    Quat r0 = quat_axis_angle(axis0, ac_ab_1 - ac_ab_0);
    Quat r1 = quat_axis_angle(axis0, ba_bc_1 - ba_bc_0);
    Quat q = r0;
    if (length(n1) > 1e-6f) q = quat_mul(quat_axis_angle(normalize(n1), ac_at_0), r0);
    pose_turn_world(rot, wr, ia, q);
    pose_turn_world(rot, wr, ib, r1);
}

void ik_foot_align(Quat *rot, const Quat *wr, u32 c, Vec3 n, f32 max_rad, f32 weight) {
    Vec3 up{0, 1, 0};
    f32 ang = angle_between(up, n);
    if (ang < 1e-4f || weight <= 0.0f) return;
    Vec3 ax = normalize(cross(up, n));
    pose_turn_world(rot, wr, c, quat_axis_angle(ax, f_min(ang, max_rad) * weight));
}
