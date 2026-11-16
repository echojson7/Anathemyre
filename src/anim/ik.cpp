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

void ik_arm(Quat *rot, const Quat *wr, const Vec3 *wp, u32 ia, u32 ib, u32 ic, Vec3 t, Vec3 pole) {
    Vec3 a = wp[ia], b = wp[ib], c = wp[ic];
    f32 lab = length(b - a), lbc = length(c - b);
    if (lab < 1e-4f || lbc < 1e-4f || length(t - a) < 1e-4f) return;
    Vec3 d = normalize(t - a);
    f32 lat = f_clamp(length(t - a), f_abs(lab - lbc) + 0.01f, (lab + lbc) * 0.999f);
    Vec3 side = pole - d * dot(pole, d);
    if (length(side) < 1e-4f) return;
    // Локоть: вдоль «плечо — цель» на x и в сторону pole на h (закон косинусов)
    f32 x = (lab * lab - lbc * lbc + lat * lat) / (2.0f * lat);
    f32 hh = lab * lab - x * x;
    Vec3 e = a + d * x + normalize(side) * (hh > 0.0f ? f_sqrt(hh) : 0.0f);
    Quat q1 = quat_between(normalize(b - a), normalize(e - a));
    Quat q2 = quat_between(normalize(quat_rotate(q1, c - b)), normalize(a + d * lat - e));
    pose_turn_world(rot, wr, ia, q1);
    Quat wb = quat_mul(q1, wr[ib]);  // предплечье после поворота плеча
    rot[ib] = quat_mul(rot[ib], quat_mul(quat_conj(wb), quat_mul(q2, wb)));
}

void ik_foot_align(Quat *rot, const Quat *wr, u32 c, Vec3 n, f32 max_rad, f32 weight) {
    Vec3 up{0, 1, 0};
    f32 ang = angle_between(up, n);
    if (ang < 1e-4f || weight <= 0.0f) return;
    Vec3 ax = normalize(cross(up, n));
    pose_turn_world(rot, wr, c, quat_axis_angle(ax, f_min(ang, max_rad) * weight));
}
