#include "pose.h"
#include "../core/mathf.h"

struct M33 {
    f32 m[9];  // по строкам
};

static M33 quat_m33(f32 x, f32 y, f32 z, f32 w) {
    return {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
             2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
             2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
}

static M33 mul(const M33 &a, const M33 &b) {
    M33 r;
    for (u32 i = 0; i < 3; ++i)
        for (u32 j = 0; j < 3; ++j) r.m[i * 3 + j] = a.m[i * 3] * b.m[j] + a.m[i * 3 + 1] * b.m[3 + j] + a.m[i * 3 + 2] * b.m[6 + j];
    return r;
}

static M33 transpose(const M33 &a) {
    return {{a.m[0], a.m[3], a.m[6], a.m[1], a.m[4], a.m[7], a.m[2], a.m[5], a.m[8]}};
}

static Vec3 apply(const M33 &a, Vec3 v) {
    return {a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z, a.m[3] * v.x + a.m[4] * v.y + a.m[5] * v.z,
            a.m[6] * v.x + a.m[7] * v.y + a.m[8] * v.z};
}

Quat quat_axis_angle(Vec3 axis, f32 rad) {
    f32 s = sin_f(rad * 0.5f), c = cos_f(rad * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, c};
}

void pose_palette(const Skel &s, const Quat *rot, PoseMatrix *out, i32 trans_bone, const Vec3 *trans) {
    // Поза костей в осях сетки: поворот и голова. Кость — бинд-поза родителя в позе · местная бинд-поза · rot
    constexpr u32 MAX = SKEL_MAX_BONES;
    M33 wr[MAX];
    Vec3 wp[MAX];
    for (usize i = 0; i < s.bones.count && i < MAX; ++i) {
        const SkelBone &b = s.bones.data[i];
        M33 bind = quat_m33(b.rot[0], b.rot[1], b.rot[2], b.rot[3]);
        Vec3 head{b.pos[0], b.pos[1], b.pos[2]};
        M33 r = bind;
        Vec3 p = head;
        if (b.parent >= 0) {
            const SkelBone &pb = s.bones.data[b.parent];
            M33 pbind = quat_m33(pb.rot[0], pb.rot[1], pb.rot[2], pb.rot[3]);
            M33 pt = transpose(pbind);
            M33 lr = mul(pt, bind);
            Vec3 lp = apply(pt, head - Vec3{pb.pos[0], pb.pos[1], pb.pos[2]});
            r = mul(wr[b.parent], lr);
            p = wp[b.parent] + apply(wr[b.parent], lp);
        }
        if (trans && (i32)i == trans_bone) p = *trans;
        if (rot) r = mul(r, quat_m33(rot[i].x, rot[i].y, rot[i].z, rot[i].w));
        wr[i] = r;
        wp[i] = p;
        // Палитра: [r | p] · inv
        const f32 *v = b.inv;
        f32 *o = out[i].m;
        for (u32 row = 0; row < 3; ++row) {
            const f32 *a = &r.m[row * 3];
            for (u32 col = 0; col < 4; ++col) o[row * 4 + col] = a[0] * v[col] + a[1] * v[4 + col] + a[2] * v[8 + col];
            o[row * 4 + 3] += row == 0 ? p.x : row == 1 ? p.y : p.z;
        }
    }
}

Quat quat_between(Vec3 a, Vec3 b) {
    f32 d = dot(a, b);
    if (d < -0.9999f) {
        Vec3 ax = cross(f_abs(a.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0}, a);
        ax = normalize(ax);
        return {ax.x, ax.y, ax.z, 0.0f};
    }
    Vec3 c = cross(a, b);
    Quat q{c.x, c.y, c.z, 1.0f + d};
    f32 n = f_sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

void pose_world(const Skel &s, const Quat *rot, i32 trans_bone, const Vec3 *trans, Quat *wr, Vec3 *wp) {
    // Как pose_palette: кость = поза родителя · местная бинд-поза · rot
    for (usize i = 0; i < s.bones.count && i < SKEL_MAX_BONES; ++i) {
        const SkelBone &b = s.bones.data[i];
        Quat bind{b.rot[0], b.rot[1], b.rot[2], b.rot[3]};
        Vec3 head{b.pos[0], b.pos[1], b.pos[2]};
        Quat r = bind;
        Vec3 p = head;
        if (b.parent >= 0) {
            const SkelBone &pb = s.bones.data[b.parent];
            Quat pc = quat_conj(Quat{pb.rot[0], pb.rot[1], pb.rot[2], pb.rot[3]});
            Quat lr = quat_mul(pc, bind);
            Vec3 lp = quat_rotate(pc, head - Vec3{pb.pos[0], pb.pos[1], pb.pos[2]});
            r = quat_mul(wr[b.parent], lr);
            p = wp[b.parent] + quat_rotate(wr[b.parent], lp);
        }
        if (trans && (i32)i == trans_bone) p = *trans;
        if (rot) r = quat_mul(r, rot[i]);
        wr[i] = r;
        wp[i] = p;
    }
}

void pose_palette_world(const Skel &s, const Quat *wr, const Vec3 *wp, PoseMatrix *out) {
    for (usize i = 0; i < s.bones.count && i < SKEL_MAX_BONES; ++i) {
        const Quat &q = wr[i];
        M33 r = quat_m33(q.x, q.y, q.z, q.w);
        const f32 *v = s.bones.data[i].inv;
        f32 *o = out[i].m;
        for (u32 row = 0; row < 3; ++row) {
            const f32 *a = &r.m[row * 3];
            for (u32 col = 0; col < 4; ++col) o[row * 4 + col] = a[0] * v[col] + a[1] * v[4 + col] + a[2] * v[8 + col];
            o[row * 4 + 3] += row == 0 ? wp[i].x : row == 1 ? wp[i].y : wp[i].z;
        }
    }
}
