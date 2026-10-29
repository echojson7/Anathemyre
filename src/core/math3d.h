// Векторы и матрицы. Матрицы по столбцам (как в OpenGL): m[col * 4 + row]
#ifndef ANA_MATH3D_H
#define ANA_MATH3D_H

#include <math.h>

#include "types.h"

#define PI_F 3.14159265358979f

typedef struct { f32 x, y; } V2;
typedef struct { f32 x, y, z; } V3;
typedef struct { f32 x, y, z, w; } V4;
typedef struct { f32 m[16]; } M4;

static inline V3 v3(f32 x, f32 y, f32 z) { V3 r = { x, y, z }; return r; }
static inline V3 v3_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3_mul(V3 a, f32 s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline V3 v3_hadamard(V3 a, V3 b) { return v3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline f32 v3_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3_cross(V3 a, V3 b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline f32 v3_len(V3 a) { return sqrtf(v3_dot(a, a)); }
static inline V3 v3_norm(V3 a)
{
    f32 l = v3_len(a);
    return l > 1e-12f ? v3_mul(a, 1.0f / l) : v3(0, 0, 0);
}
static inline V3 v3_lerp(V3 a, V3 b, f32 t) { return v3_add(a, v3_mul(v3_sub(b, a), t)); }
static inline f32 lerpf(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
static inline f32 clampf(f32 x, f32 lo, f32 hi) { return x < lo ? lo : (x > hi ? hi : x); }
// fminf/fmaxf из libm не встраиваются (правила NaN) и в горячих циклах стоят вызова; эти — одна инструкция
static inline f32 minf(f32 a, f32 b) { return a < b ? a : b; }
static inline f32 maxf(f32 a, f32 b) { return a > b ? a : b; }

static inline M4 m4_identity(void)
{
    M4 r = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };
    return r;
}

static inline M4 m4_mul(M4 a, M4 b)
{
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            f32 s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

static inline M4 m4_translate(V3 t)
{
    M4 r = m4_identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

static inline M4 m4_scale(V3 s)
{
    M4 r = m4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}

static inline M4 m4_rot_x(f32 a)
{
    M4 r = m4_identity();
    f32 c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}

static inline M4 m4_rot_y(f32 a)
{
    M4 r = m4_identity();
    f32 c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}

static inline M4 m4_rot_z(f32 a)
{
    M4 r = m4_identity();
    f32 c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}

static inline M4 m4_perspective(f32 fovy, f32 aspect, f32 n, f32 f)
{
    M4 r = { { 0 } };
    f32 t = 1.0f / tanf(fovy * 0.5f);
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = 2.0f * f * n / (n - f);
    return r;
}

static inline M4 m4_ortho(f32 l, f32 rr, f32 b, f32 t, f32 n, f32 f)
{
    M4 r = m4_identity();
    r.m[0] = 2.0f / (rr - l);
    r.m[5] = 2.0f / (t - b);
    r.m[10] = -2.0f / (f - n);
    r.m[12] = -(rr + l) / (rr - l);
    r.m[13] = -(t + b) / (t - b);
    r.m[14] = -(f + n) / (f - n);
    return r;
}

static inline M4 m4_look_at(V3 eye, V3 target, V3 up)
{
    V3 f = v3_norm(v3_sub(target, eye));
    V3 s = v3_norm(v3_cross(f, up));
    V3 u = v3_cross(s, f);
    M4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye);
    r.m[13] = -v3_dot(u, eye);
    r.m[14] = v3_dot(f, eye);
    return r;
}

static inline V3 m4_point(M4 a, V3 p)
{
    return v3(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
              a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
              a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]);
}

static inline V3 m4_dir(M4 a, V3 p)
{
    return v3(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z,
              a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z,
              a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z);
}

// Общая обратная матрица через алгебраические дополнения
static inline M4 m4_inverse(M4 a)
{
    const f32 *m = a.m;
    M4 r;
    f32 *inv = r.m;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    f32 id = fabsf(det) > 1e-20f ? 1.0f / det : 0.0f;
    for (int i = 0; i < 16; i++) inv[i] *= id;
    return r;
}

#endif
