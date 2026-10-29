#include "anim.h"

// ---------------------------------------------------------------- математика поз

// Аффинное преобразование: столбцы поворота и сдвиг
typedef struct Xf {
    V3 x, y, z, t;
} Xf;

static const Xf XF_ID = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 0 } };

static inline V3 xf_dir(const Xf *a, V3 d)
{
    return v3(a->x.x * d.x + a->y.x * d.y + a->z.x * d.z, a->x.y * d.x + a->y.y * d.y + a->z.y * d.z,
              a->x.z * d.x + a->y.z * d.y + a->z.z * d.z);
}
static inline V3 xf_point(const Xf *a, V3 p) { return v3_add(xf_dir(a, p), a->t); }
static inline Xf xf_mul(const Xf *a, const Xf *b)
{
    return (Xf){ xf_dir(a, b->x), xf_dir(a, b->y), xf_dir(a, b->z), xf_point(a, b->t) };
}
// Обратный поворот (ортонормированный) применить к вектору
static inline V3 xf_tdir(const Xf *a, V3 d) { return v3(v3_dot(a->x, d), v3_dot(a->y, d), v3_dot(a->z, d)); }
static inline M4 xf_m4(const Xf *a)
{
    return (M4){ { a->x.x, a->x.y, a->x.z, 0, a->y.x, a->y.y, a->y.z, 0, a->z.x, a->z.y, a->z.z, 0, a->t.x, a->t.y, a->t.z, 1 } };
}

// Синус и косинус многочленом: точности 1e-6 хватает, втрое быстрее libm
static inline void fsincos(f32 a, f32 *s, f32 *c)
{
    f32 kf = a * (2.0f / PI_F);
    i32 k = (i32)(kf + (kf >= 0.0f ? 0.5f : -0.5f));
    f32 x = a - (f32)k * (PI_F * 0.5f), x2 = x * x;
    f32 sn = x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f))));
    f32 cs = 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f))));
    switch (k & 3) {
    case 0: *s = sn; *c = cs; break;
    case 1: *s = cs; *c = -sn; break;
    case 2: *s = -sn; *c = -cs; break;
    default: *s = -cs; *c = sn; break;
    }
}
static inline f32 fsin(f32 a) { f32 s, c; fsincos(a, &s, &c); return s; }
static inline f32 fcos(f32 a) { f32 s, c; fsincos(a, &s, &c); return c; }

// Поворот Ry·Rz·Rx по углам e
static Xf xf_euler(V3 e, V3 t)
{
    f32 sx, cx, sy, cy, sz, cz;
    fsincos(e.x, &sx, &cx);
    fsincos(e.y, &sy, &cy);
    fsincos(e.z, &sz, &cz);
    return (Xf){ { cy * cz, sz, -sy * cz },
                 { -cy * sz * cx + sy * sx, cz * cx, sy * sz * cx + cy * sx },
                 { cy * sz * sx + sy * cx, -cz * sx, -sy * sz * sx + cy * cx },
                 t };
}

// Кратчайший поворот единичного a в единичный b
static Xf xf_between(V3 a, V3 b)
{
    V3 v = v3_cross(a, b);
    f32 c = v3_dot(a, b);
    if (c < -0.9999f) return (Xf){ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 0 } };
    f32 k = 1.0f / (1.0f + c);
    return (Xf){ { v.x * v.x * k + c, v.x * v.y * k + v.z, v.x * v.z * k - v.y },
                 { v.x * v.y * k - v.z, v.y * v.y * k + c, v.y * v.z * k + v.x },
                 { v.x * v.z * k + v.y, v.y * v.z * k - v.x, v.z * v.z * k + c },
                 { 0, 0, 0 } };
}

// Кватернионы только для смешивания поворотов кисти
typedef struct Quat { f32 x, y, z, w; } Quat;

static Quat q_from_xf(const Xf *m)
{
    f32 tr = m->x.x + m->y.y + m->z.z;
    Quat q;
    if (tr > 0.0f) {
        f32 s = sqrtf(tr + 1.0f) * 2.0f;
        q = (Quat){ (m->y.z - m->z.y) / s, (m->z.x - m->x.z) / s, (m->x.y - m->y.x) / s, 0.25f * s };
    } else if (m->x.x > m->y.y && m->x.x > m->z.z) {
        f32 s = sqrtf(1.0f + m->x.x - m->y.y - m->z.z) * 2.0f;
        q = (Quat){ 0.25f * s, (m->y.x + m->x.y) / s, (m->z.x + m->x.z) / s, (m->y.z - m->z.y) / s };
    } else if (m->y.y > m->z.z) {
        f32 s = sqrtf(1.0f + m->y.y - m->x.x - m->z.z) * 2.0f;
        q = (Quat){ (m->y.x + m->x.y) / s, 0.25f * s, (m->z.y + m->y.z) / s, (m->z.x - m->x.z) / s };
    } else {
        f32 s = sqrtf(1.0f + m->z.z - m->x.x - m->y.y) * 2.0f;
        q = (Quat){ (m->z.x + m->x.z) / s, (m->z.y + m->y.z) / s, 0.25f * s, (m->x.y - m->y.x) / s };
    }
    return q;
}

static Xf q_to_xf(Quat q, V3 t)
{
    f32 x = q.x, y = q.y, z = q.z, w = q.w;
    return (Xf){ { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w) },
                 { 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w) },
                 { 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) },
                 t };
}

static Quat q_nlerp(Quat a, Quat b, f32 t)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = (Quat){ -b.x, -b.y, -b.z, -b.w };
    Quat q = { lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t) };
    f32 l = 1.0f / sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return (Quat){ q.x * l, q.y * l, q.z * l, q.w * l };
}

static Xf xf_slerp_rot(const Xf *a, const Xf *b, f32 t, V3 pos)
{
    return q_to_xf(q_nlerp(q_from_xf(a), q_from_xf(b), t), pos);
}

static f32 smooth01(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static f32 ease_out(f32 t)
{
    t = clampf(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t);
}
static f32 ramp(f32 x, f32 a, f32 b) { return smooth01((x - a) / (b - a)); }

static f32 hashf(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (f32)(x & 0xFFFFFF) / 16777215.0f;
}

// Суставы тела, которое сейчас собирается (у каждого человека свои пропорции).
// Позы считаются в любом потоке, поэтому указатель свой у потока
static _Thread_local const V3 *g_joints;

static V3 joint(u32 b) { return g_joints ? g_joints[b] : g_body_bones[b].joint; }

// ---------------------------------------------------------------- поза

// Ноги и руки: 0 — левая, 1 — правая
enum { L, R };

typedef struct Pose {
    V3  off;              // сдвиг таза от позы привязки
    V3  r[BONE_COUNT];    // локальные повороты: x — наклон, y — разворот, z — крен
    V3  foot[2];          // цели лодыжек для IK (оси персонажа)
    f32 fpitch[2];        // наклон стопы к земле при IK: > 0 носок вниз
    f32 ik;               // 1 — ноги по целям, 0 — по углам r
    f32 plant[2];         // нога несёт вес: таз опускается, чтобы она доставала до цели
    f32 kout;             // колени врозь, рад
} Pose;

static void pose_lerp(Pose *a, const Pose *b, f32 t)
{
    if (t <= 0.0f) return;
    if (t >= 1.0f) {
        *a = *b;
        return;
    }
    const f32 *src = (const f32 *)b;
    f32 *dst = (f32 *)a;
    for (u32 i = 0; i < sizeof(Pose) / sizeof(f32); i++) dst[i] += (src[i] - dst[i]) * t;
}

// Рука в зеркальных углах: ux < 0 — вперёд и вверх, ac > 0 — к середине тела, out > 0 — в сторону,
// el < 0 — сгиб локтя, wx — сгиб кисти, wz — отведение кисти
typedef struct ArmK {
    f32 ux, ac, out, el, wx, wz;
} ArmK;

static ArmK arm_lerp(ArmK a, ArmK b, f32 t)
{
    return (ArmK){ lerpf(a.ux, b.ux, t), lerpf(a.ac, b.ac, t), lerpf(a.out, b.out, t), lerpf(a.el, b.el, t),
                   lerpf(a.wx, b.wx, t), lerpf(a.wz, b.wz, t) };
}

static void set_arm(Pose *p, u32 side, ArmK k)
{
    f32 m = side == L ? 1.0f : -1.0f;
    u8 base = side == L ? BONE_CLAVICLE_L : BONE_CLAVICLE_R;
    // Ключица следует за рукой: поднятая рука тянет плечо вверх, рука поперёк — вперёд
    f32 elev = 0.22f * clampf(-k.ux - 1.3f, 0.0f, 1.6f) + 0.12f * clampf(k.out - 0.6f, 0.0f, 1.0f);
    f32 prot = 0.18f * clampf(k.ac, 0.0f, 1.5f) - 0.12f * clampf(k.ux, 0.0f, 1.0f);
    p->r[base] = v3(0.0f, -m * prot, m * elev);
    p->r[base + 1] = v3(k.ux, -m * k.ac, m * k.out);
    p->r[base + 2] = v3(k.el, 0.0f, 0.0f);
    p->r[base + 3] = v3(k.wx, 0.0f, m * k.wz);
}

static void add_arm(Pose *p, u32 side, ArmK k, f32 t)
{
    f32 m = side == L ? 1.0f : -1.0f;
    u8 b = side == L ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
    p->r[b] = v3_add(p->r[b], v3_mul(v3(k.ux, -m * k.ac, m * k.out), t));
    p->r[b + 1].x += k.el * t;
    p->r[b + 2] = v3_add(p->r[b + 2], v3_mul(v3(k.wx, 0.0f, m * k.wz), t));
}

static ArmK get_arm(const Pose *p, u32 side)
{
    f32 m = side == L ? 1.0f : -1.0f;
    u8 b = side == L ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
    return (ArmK){ p->r[b].x, -m * p->r[b].y, m * p->r[b].z, p->r[b + 1].x, p->r[b + 2].x, m * p->r[b + 2].z };
}

// Стоя: ступни под тазом, чуть врозь; левая на полшага впереди
static V3 stand_foot(u32 side)
{
    V3 a = joint(side == L ? BONE_FOOT_L : BONE_FOOT_R);
    return v3(a.x + (side == L ? 0.015f : -0.015f), a.y, side == L ? 0.03f : -0.02f);
}

static void pose_stand(Pose *p)
{
    *p = (Pose){ .ik = 1.0f, .plant = { 1.0f, 1.0f } };
    p->foot[L] = stand_foot(L);
    p->foot[R] = stand_foot(R);
    set_arm(p, L, (ArmK){ 0.02f, 0.0f, 0.08f, -0.22f, 0.0f, 0.0f });
    set_arm(p, R, (ArmK){ 0.02f, 0.0f, 0.08f, -0.22f, 0.0f, 0.0f });
}

// ---------------------------------------------------------------- походка

// Траектория лодыжки за цикл; q — фаза этой ноги (0 — касание земли).
// Опора: стопа стоит, тело уходит вперёд со скоростью шага. Перенос: дуга вперёд;
// на бегу пятка захлёстывается к ягодице, колено выносится высоко и голень выбрасывается вперёд
typedef struct Gait {
    f32 duty, land, off;       // доля опоры; где стопа касается и отрывается, м от тазобедренного
    f32 kick, knee, reach;     // высота захлёста пятки, выноса колена, вынос голени вперёд
} Gait;

static void foot_track(const Gait *g, f32 q, f32 run, f32 *z, f32 *lift, f32 *pitch)
{
    f32 heel_up = lerpf(0.16f, 0.14f, run), toe_off = lerpf(1.1f, 1.2f, run);
    if (q < g->duty) {
        f32 t = q / g->duty;
        *z = lerpf(g->land, g->off, t);
        // Шаг: пятка, плоская стопа, подъём пятки и толчок носком. Бег: на середину стопы
        f32 heel = ramp(t, lerpf(0.55f, 0.45f, run), 1.0f);
        *lift = heel_up * heel;
        f32 strike = lerpf(-0.28f, -0.05f, run) * (1.0f - ramp(t, 0.0f, lerpf(0.18f, 0.12f, run)));
        *pitch = strike + toe_off * heel;
        return;
    }
    f32 u = (q - g->duty) / (1.0f - g->duty);
    // Вперёд — кривая Эрмита: на отрыве и перед касанием стопа идёт назад почти со скоростью земли,
    // без рывка при смене опоры
    f32 m = -0.2f * (g->land - g->off);
    f32 u2 = u * u, u3 = u2 * u;
    *z = (2.0f * u3 - 3.0f * u2 + 1.0f) * g->off + (u3 - 2.0f * u2 + u) * m + (-2.0f * u3 + 3.0f * u2) * g->land +
         (u3 - u2) * m;
    // Вверх — кривая Безье: пятка уходит вверх сразу после отрыва (колено сгибается), затем
    // колено выносится вперёд и голень выпрямляется к постановке
    f32 a = 1.0f - u;
    *lift = a * a * a * heel_up + 3.0f * a * a * u * g->kick + 3.0f * a * u2 * g->knee;
    // Стопа: после толчка носок вниз, к середине переноса — на себя, перед касанием на пятку
    f32 push = toe_off * (1.0f - smooth01(u / lerpf(0.4f, 0.5f, run)));
    f32 land = lerpf(-0.28f, -0.05f, run) * smooth01((u - 0.5f) / 0.45f);
    *pitch = push + land - 0.12f * fsin(PI_F * u) * (1.0f - run);
}

typedef struct Loco {
    f32 chest_yaw;   // скрутка плеч против таза, для головы
} Loco;

static void locomotion(const CharPose *p, Pose *o, Loco *lo)
{
    f32 w = clampf(p->walk, 0.0f, 1.0f);
    f32 run = clampf(p->run, 0.0f, 1.0f);
    f32 ph = p->phase - floorf(p->phase);
    const f32 tau = 2.0f * PI_F;
    // Бег по механике включается уже на трусце: пружинит, а не перекатывается
    f32 bounce = ramp(run, 0.05f, 0.4f);
    Gait g;
    g.duty = run < 0.5f ? lerpf(0.62f, 0.40f, run * 2.0f) : lerpf(0.40f, 0.29f, (run - 0.5f) * 2.0f);
    f32 L_ = char_stride(p->speed > 0.0f ? p->speed : lerpf(1.2f, 4.8f, run));
    f32 travel = g.duty * L_;
    f32 land_frac = lerpf(0.40f, 0.38f, bounce);
    g.land = land_frac * travel;
    g.off = g.land - travel;
    g.kick = lerpf(0.13f, lerpf(0.30f, 0.55f, ramp(run, 0.5f, 1.0f)), bounce);
    g.knee = lerpf(0.10f, lerpf(0.26f, 0.38f, ramp(run, 0.5f, 1.0f)), bounce);
    g.reach = lerpf(0.06f, 0.22f, bounce);

    // Таз: на шаге выше над опорной ногой, на беге ниже всего в опоре и выше в полёте
    f32 q2 = 2.0f * ph - floorf(2.0f * ph);
    f32 bob_amp = lerpf(0.017f, -0.035f, bounce);
    f32 bob = bob_amp * fcos(tau * (q2 - g.duty)) + lerpf(-0.045f, -0.060f, bounce);
    f32 sway = lerpf(0.026f, 0.010f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    f32 pyaw = -lerpf(0.08f, 0.13f, bounce) * fcos(tau * ph);
    f32 proll = lerpf(0.06f, 0.035f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    f32 ptilt = lerpf(0.05f, 0.14f, bounce);
    o->off = v3_add(o->off, v3_mul(v3(sway, bob, 0.0f), w));
    o->r[BONE_PELVIS] = v3_add(o->r[BONE_PELVIS], v3_mul(v3(ptilt, pyaw, proll), w));

    // Корпус: наклон вперёд, плечи крутятся навстречу тазу, грудь гасит крен таза и чуть
    // клонится к опорной ноге. Наклон дышит дважды за цикл: на шаге клюёт вперёд после касания
    // пятки, на беге глубже всего в середине опоры
    f32 sh_yaw = lerpf(0.12f, 0.22f, bounce) * fcos(tau * ph);
    f32 nod = lerpf(0.018f, 0.035f, bounce) * fcos(tau * (q2 - lerpf(0.08f, g.duty, bounce)));
    f32 side_lean = lerpf(0.025f, 0.01f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    o->r[BONE_SPINE] = v3_add(o->r[BONE_SPINE], v3_mul(v3(lerpf(0.0f, 0.04f, bounce) + nod * 0.6f, -pyaw * 0.6f + sh_yaw * 0.5f,
                                                          -proll * 0.6f - side_lean * 0.5f), w));
    o->r[BONE_CHEST] = v3_add(o->r[BONE_CHEST], v3_mul(v3(lerpf(0.02f, 0.03f, bounce) + nod * 0.4f, -pyaw * 0.4f + sh_yaw * 0.5f,
                                                          -proll * 0.4f - side_lean * 0.5f), w));
    lo->chest_yaw = sh_yaw * w;
    // Голова держит горизонт
    o->r[BONE_HEAD].x -= (ptilt + 0.05f * bounce + nod) * w;
    o->r[BONE_HEAD].z += (proll * 0.3f + side_lean) * w;

    // Руки против ног, мах от плеча, назад дальше, чем вперёд. Шагом локоть почти прям и сгибается,
    // когда рука идёт вперёд; на бегу локоть согнут, сзади раскрывается, спереди кисть идёт к груди
    f32 arm_ph = tau * (ph - 0.04f);
    f32 runk = ramp(run, 0.5f, 1.0f);
    f32 S = lerpf(0.30f, lerpf(0.48f, 0.55f, runk), bounce);
    f32 back = lerpf(-0.04f, 0.15f, bounce);
    f32 el_back = lerpf(-0.22f, lerpf(-1.10f, -1.20f, runk), bounce);
    f32 el_fwd = lerpf(-0.60f, lerpf(-1.55f, -1.75f, runk), bounce);
    for (u32 side = 0; side < 2; side++) {
        f32 c = side == L ? fcos(arm_ph) : -fcos(arm_ph);  // > 0 — рука идёт назад
        f32 fwd = 0.5f - 0.5f * c;
        fwd *= fwd;
        ArmK k = { back + S * c, lerpf(0.04f, 0.14f, bounce) * fwd, lerpf(0.08f, 0.10f, bounce), lerpf(el_back, el_fwd, fwd),
                   0.1f * bounce, 0.0f };
        ArmK cur = get_arm(o, side);
        set_arm(o, side, arm_lerp(cur, k, w));
    }

    // Ноги: цели стоп; шаг поворачивается по направлению движения
    f32 md_s = fsin(p->move_dir), md_c = fcos(p->move_dir);
    for (u32 side = 0; side < 2; side++) {
        f32 q = ph + (side == L ? 0.0f : 0.5f);
        q -= floorf(q);
        f32 z, lift, pitch;
        foot_track(&g, q, run, &z, &lift, &pitch);
        V3 base = o->foot[side];
        V3 aj = joint(side == L ? BONE_FOOT_L : BONE_FOOT_R);
        V3 walkp = v3(aj.x + md_s * z, aj.y + lift, md_c * z);
        // На бегу стопы ставятся ближе к средней линии
        walkp.x -= (side == L ? 1.0f : -1.0f) * 0.03f * bounce;
        o->foot[side] = v3_lerp(base, walkp, w);
        o->fpitch[side] = lerpf(o->fpitch[side], pitch, w);
        // Вес на ноге: с касания до подъёма пятки; к отрыву и до касания спадает плавно
        f32 pl = q < g.duty ? 1.0f - smooth01((q / g.duty - 0.6f) / 0.4f) : smooth01(((q - g.duty) / (1.0f - g.duty) - 0.6f) / 0.4f);
        o->plant[side] = lerpf(o->plant[side], pl, w);
    }
}

// ---------------------------------------------------------------- покой

// Покой в два слоя. Движения стоя идут всегда: дыхание, вес с ноги на ногу, взгляд по сторонам и
// короткие житейские движения раз в несколько секунд. Позы — редко и надолго, только если стоит
// давно: на корточках, сидит на земле, упёрся руками в колени, застыл и смотрит вверх
enum { MOVE_NONE, MOVE_NECK, MOVE_STRAP, MOVE_BROW, MOVE_HILT, MOVE_HANDS, MOVE_COUNT };
enum { POSE_NONE, POSE_SQUAT, POSE_SIT, POSE_BREATH, POSE_STARE, POSE_COUNT };

#define MOVE_SLOT 5.5f    // с на одно движение
#define POSE_SLOT 34.0f   // с на одну позу
#define POSE_FADE 2.2f    // вход в позу и выход
#define POSE_AFTER 4.0f   // поза не раньше, чем постоял столько

// Голова и корпус к точке: yaw — поворот, pitch > 0 — вниз
static void look_at(Pose *o, f32 yaw, f32 pitch)
{
    o->r[BONE_CHEST].y += yaw * 0.25f;
    o->r[BONE_NECK].y += yaw * 0.3f;
    o->r[BONE_HEAD].y += yaw * 0.45f;
    o->r[BONE_NECK].x += pitch * 0.4f;
    o->r[BONE_HEAD].x += pitch * 0.6f;
}

// Перенос веса на одну ногу (side > 0 — на левую): таз уходит к опорной, другая расслаблена в колене
static void weight_shift(Pose *o, f32 side, f32 amt)
{
    f32 sa = side * amt, aa = fabsf(sa);
    o->off.x += 0.035f * sa;
    o->off.y -= 0.010f * aa;
    o->r[BONE_PELVIS].z += 0.06f * sa;
    o->r[BONE_SPINE].z -= 0.035f * sa;
    o->r[BONE_CHEST].z -= 0.03f * sa;
    o->r[BONE_HEAD].z += 0.02f * sa;
    // Свободная нога чуть вперёд и наружу, колено мягкое
    for (u32 s = 0; s < 2; s++) {
        f32 f = s == L ? clampf(-sa, 0.0f, 1.0f) : clampf(sa, 0.0f, 1.0f);
        o->foot[s].z += 0.05f * f;
        o->foot[s].x += (s == L ? 0.02f : -0.02f) * f;
        o->foot[s].y += 0.012f * f;
        o->fpitch[s] += 0.10f * f;
        o->plant[s] *= 1.0f - 0.8f * f;
    }
}

// Плавная ступенька между значениями соседних отрезков: держит и быстро переходит
static f32 held(u32 sd, f32 t, f32 seg, f32 tr, u32 salt)
{
    f32 x = t / seg;
    u32 i = (u32)x;
    f32 a = hashf(sd * 7919u + i * 104729u + salt) * 2.0f - 1.0f;
    f32 b = hashf(sd * 7919u + (i + 1) * 104729u + salt) * 2.0f - 1.0f;
    return lerpf(a, b, smooth01((x - (f32)i - (1.0f - tr / seg)) / (tr / seg)));
}

// Движения стоя: всегда, поверх стойки
static void idle_moves(Pose *o, f32 t, u32 sd, f32 seed, bool armed)
{
    // Вес то на одной ноге, то ровно, то на другой; держит по 6–9 с
    f32 ws = held(sd, t, 6.0f + 3.0f * hashf(sd), 1.4f, 11u);
    weight_shift(o, ws > 0.0f ? 1.0f : -1.0f, smooth01(fabsf(ws) * 1.6f - 0.2f));
    // Взгляд: держит точку, переводит за полсекунды; изредка далеко в сторону
    f32 gy = held(sd, t, 2.6f + 1.2f * hashf(sd + 5u), 0.45f, 23u);
    f32 far = held(sd, t, 9.0f, 0.6f, 29u) > 0.55f ? 1.0f : 0.45f;
    look_at(o, 0.55f * gy * far, 0.04f + 0.06f * held(sd, t, 3.7f, 0.6f, 31u));
    // Руки висят не мёртво: чуть отходят от бедра и сгибаются
    for (u32 s = 0; s < 2; s++)
        add_arm(o, s, (ArmK){ 0.03f * fsin(t * 0.31f + seed + (f32)s), 0.0f, 0.02f, -0.10f - 0.06f * fsin(t * 0.23f + (f32)s * 2.0f), 0.08f, 0.0f }, 1.0f);

    // Житейские движения
    u32 slot = (u32)(t / MOVE_SLOT);
    f32 lt = t - (f32)slot * MOVE_SLOT;
    f32 hk = hashf(sd * 31u + slot * 977u);
    u32 k = hk < 0.45f ? MOVE_NONE : 1u + (u32)((hk - 0.45f) / 0.55f * (MOVE_COUNT - 1));
    if (k >= MOVE_COUNT) k = MOVE_COUNT - 1;
    if (k == MOVE_HILT && !armed) k = MOVE_HANDS;
    f32 dur = k == MOVE_HILT ? 5.0f : 3.2f;
    f32 g = smooth01(lt / 0.6f) * (1.0f - smooth01((lt - dur + 0.6f) / 0.6f));
    if (g <= 0.0f) return;
    switch (k) {
    case MOVE_NECK: {
        // Разминает шею: голова к одному плечу, к другому; плечи вверх-назад
        f32 n = fsin(lt * 2.2f);
        o->r[BONE_NECK].z += 0.22f * n * g;
        o->r[BONE_HEAD].z += 0.12f * n * g;
        f32 sh = smooth01((lt - 1.8f) / 0.5f) * (1.0f - smooth01((lt - 2.5f) / 0.5f));
        o->r[BONE_CLAVICLE_L].z += 0.12f * sh * g;
        o->r[BONE_CLAVICLE_R].z -= 0.12f * sh * g;
        o->r[BONE_CLAVICLE_L].y -= 0.08f * sh * g;
        o->r[BONE_CLAVICLE_R].y += 0.08f * sh * g;
        break;
    }
    case MOVE_STRAP: {
        // Поправляет лямку: большой палец под лямку на груди, дёргает вниз два раза
        f32 tug = fmaxf(0.0f, fsin(lt * 6.0f)) * smooth01((lt - 0.9f) / 0.3f);
        set_arm(o, R, arm_lerp(get_arm(o, R), (ArmK){ -0.55f, 0.55f, 0.05f, -2.05f + 0.25f * tug, 0.2f, 0.0f }, g));
        o->r[BONE_CLAVICLE_R].z -= 0.06f * tug * g;
        look_at(o, 0.0f, 0.10f * g);
        break;
    }
    case MOVE_BROW: {
        // Вытирает лоб тыльной стороной запястья: жара и пыль
        f32 wipe = fsin(smooth01((lt - 0.7f) / 1.2f) * PI_F);
        set_arm(o, L, arm_lerp(get_arm(o, L), (ArmK){ -1.55f - 0.15f * wipe, 0.85f - 0.35f * wipe, 0.0f, -2.20f, 0.3f, 0.0f }, g));
        look_at(o, -0.15f * wipe * g, -0.10f * g);
        break;
    }
    case MOVE_HILT:
        // Кладёт ладонь на рукоять оружия у левого бедра
        set_arm(o, R, arm_lerp(get_arm(o, R), (ArmK){ -0.28f, 0.62f, 0.0f, -1.05f, -0.2f, 0.0f }, g));
        break;
    case MOVE_HANDS: {
        // Растирает ладони перед собой: холод или пыль на руках
        f32 rub = fsin(lt * 8.0f) * smooth01((lt - 0.6f) / 0.3f);
        for (u32 s = 0; s < 2; s++) {
            f32 r = s == L ? rub : -rub;
            set_arm(o, s, arm_lerp(get_arm(o, s), (ArmK){ -0.30f + 0.05f * r, 0.62f, 0.0f, -1.45f + 0.08f * r, 0.0f, 0.0f }, g));
        }
        look_at(o, 0.0f, 0.25f * g);
        break;
    }
    default:
        break;
    }
}

// Поза k, t — с от начала позы
static void idle_pose(Pose *o, u32 k, f32 t, f32 seed)
{
    f32 br = fsin(t * 1.4f + seed);
    switch (k) {
    case POSE_SQUAT:
        // Глубоко на корточках: пятки на земле, таз у пяток, колени врозь и вперёд, корпус наклонён,
        // локти на коленях, кисти свисают между колен
        o->off = v3(0.0f, -0.64f, -0.12f);
        o->r[BONE_PELVIS] = v3(0.22f, 0.0f, 0.0f);
        o->r[BONE_SPINE] = v3(0.30f + 0.015f * br, 0.0f, 0.0f);
        o->r[BONE_CHEST] = v3(0.12f, 0.0f, 0.0f);
        o->r[BONE_NECK] = v3(-0.20f, 0.0f, 0.0f);
        o->r[BONE_HEAD] = v3(-0.30f, 0.0f, 0.0f);
        o->foot[L] = v3(0.17f, joint(BONE_FOOT_L).y, 0.06f);
        o->foot[R] = v3(-0.17f, joint(BONE_FOOT_R).y, 0.02f);
        o->fpitch[L] = o->fpitch[R] = 0.0f;
        o->kout = 0.30f;
        for (u32 s = 0; s < 2; s++) set_arm(o, s, (ArmK){ -0.85f, 0.30f, 0.25f, -0.95f, 0.80f, 0.0f });
        o->r[BONE_CLAVICLE_L].z += 0.02f * br;
        o->r[BONE_CLAVICLE_R].z -= 0.02f * br;
        break;
    case POSE_SIT:
        // Сидит на земле, колени подтянуты, руки обхватили колени
        o->off = v3(0.0f, -0.82f, -0.10f);
        o->r[BONE_PELVIS] = v3(-0.30f, 0.0f, 0.0f);
        o->r[BONE_SPINE] = v3(0.38f + 0.015f * br, 0.0f, 0.0f);
        o->r[BONE_CHEST] = v3(0.22f, 0.0f, 0.0f);
        o->r[BONE_NECK] = v3(-0.05f, 0.0f, 0.0f);
        o->r[BONE_HEAD] = v3(-0.12f, 0.0f, 0.0f);
        o->foot[L] = v3(0.13f, joint(BONE_FOOT_L).y, 0.40f);
        o->foot[R] = v3(-0.13f, joint(BONE_FOOT_R).y, 0.36f);
        o->fpitch[L] = o->fpitch[R] = 0.25f;
        o->kout = 0.05f;
        set_arm(o, L, (ArmK){ -1.05f, 0.45f, 0.10f, -0.70f, 0.2f, 0.0f });
        set_arm(o, R, (ArmK){ -1.10f, 0.50f, 0.10f, -0.60f, 0.2f, 0.0f });
        break;
    case POSE_BREATH: {
        // Упёрся руками в колени, тяжело дышит, смотрит вперёд из-под бровей
        f32 hb = fsin(t * 3.4f + seed);
        o->off.y -= 0.07f;
        o->off.z -= 0.06f;
        o->foot[L].x += 0.06f;
        o->foot[R].x -= 0.06f;
        o->r[BONE_PELVIS].x += 0.30f;
        o->r[BONE_SPINE].x += 0.30f + 0.03f * hb;
        o->r[BONE_CHEST].x += 0.20f + 0.03f * hb;
        o->r[BONE_NECK].x -= 0.25f;
        o->r[BONE_HEAD].x -= 0.35f;
        o->kout = 0.12f;
        for (u32 s = 0; s < 2; s++) set_arm(o, s, (ArmK){ -0.75f, 0.12f, 0.12f, -0.25f, 0.45f, 0.0f });
        o->r[BONE_CLAVICLE_L].z += 0.05f * hb;
        o->r[BONE_CLAVICLE_R].z -= 0.05f * hb;
        break;
    }
    case POSE_STARE: {
        // Застыл, смотрит вверх, на громаду вдали; вес на одной ноге, почти не дышит заметно
        f32 side = hashf((u32)(seed * 100.0f)) < 0.5f ? 1.0f : -1.0f;
        weight_shift(o, side, 1.0f);
        look_at(o, 0.25f * side, -0.32f);
        o->r[BONE_SPINE].x -= 0.04f;
        for (u32 s = 0; s < 2; s++) set_arm(o, s, (ArmK){ 0.04f, 0.0f, 0.06f, -0.08f, 0.0f, 0.0f });
        break;
    }
    default:
        break;
    }
}

static u32 pose_pick(u32 sd, u32 slot)
{
    // Поза — редкое событие: примерно в одном отрезке из шести
    f32 h = hashf(sd * 4801u + slot * 7349u);
    if (h > 0.17f || slot == 0) return POSE_NONE;
    u32 k = 1u + (u32)(hashf(sd * 53u + slot * 389u) * (POSE_COUNT - 1));
    return k < POSE_COUNT ? k : POSE_COUNT - 1;
}

// ---------------------------------------------------------------- бой

// Ключ боевой позы: руки, корпус, таз и стопы (левая впереди, правая сзади)
typedef struct FightK {
    ArmK r, l;
    f32 lean, twist, roll;   // корпус: наклон вперёд, скрутка груди влево, крен
    f32 pyaw, drop, pz;      // таз: разворот влево, присед, вынос вперёд
    f32 front, back;         // вынос передней и задней стопы вперёд, м
} FightK;

static FightK fk_lerp(FightK a, FightK b, f32 t)
{
    FightK k;
    const f32 *pa = (const f32 *)&a, *pb = (const f32 *)&b;
    f32 *pk = (f32 *)&k;
    for (u32 i = 0; i < sizeof(FightK) / sizeof(f32); i++) pk[i] = lerpf(pa[i], pb[i], t);
    return k;
}

// Стойка: вес на полусогнутых, корпус развёрнут правым плечом назад, оружие перед грудью
static const FightK K_GUARD = { { -0.55f, 0.35f, 0.15f, -1.25f, 0.55f, -0.1f }, { -0.75f, 0.35f, 0.15f, -1.55f, 0.1f, 0.0f },
                                0.10f, 0.20f, 0.0f, -0.30f, 0.07f, 0.0f, 0.0f, 0.0f };
static const FightK K_BLOCK = { { -1.55f, 0.55f, 0.35f, -1.35f, 0.65f, -0.6f }, { -1.05f, 0.40f, 0.25f, -1.55f, 0.1f, 0.0f },
                                -0.04f, 0.25f, 0.0f, -0.25f, 0.11f, -0.05f, 0.0f, -0.05f };
static const FightK K_WINDUP[ATK_COUNT] = {
    // Сверху: вес на заднюю ногу, оружие за голову, левая рука вперёд целится
    [ATK_OVERHEAD] = { { -2.85f, 0.05f, 0.35f, -1.75f, 0.35f, 0.0f }, { -1.25f, 0.30f, 0.10f, -0.65f, 0.0f, 0.0f },
                       -0.14f, -0.15f, 0.0f, -0.45f, 0.06f, -0.10f, 0.0f, -0.04f },
    // Размах: корпус закручен вправо, оружие отведено назад на уровне плеча
    [ATK_SWING]    = { { -1.35f, -1.25f, 0.30f, -0.95f, 0.45f, 0.3f }, { -1.20f, 0.70f, 0.10f, -1.05f, 0.0f, 0.0f },
                       0.04f, -0.55f, 0.0f, -0.55f, 0.08f, -0.06f, 0.0f, -0.02f },
    // Укол: оружие оттянуто к боку, остриё на противника, вес назад
    [ATK_THRUST]   = { { 0.30f, 0.20f, 0.20f, -1.85f, 1.25f, 0.0f }, { -1.05f, 0.35f, 0.15f, -1.20f, 0.0f, 0.0f },
                       0.0f, -0.30f, 0.0f, -0.50f, 0.07f, -0.12f, 0.0f, -0.05f },
};
static const FightK K_STRIKE[ATK_COUNT] = {
    // Шаг передней ногой, корпус складывается, оружие уходит вперёд-вниз
    [ATK_OVERHEAD] = { { -0.75f, 0.30f, 0.10f, -0.20f, 0.20f, 0.0f }, { 0.25f, 0.10f, 0.25f, -1.35f, 0.0f, 0.0f },
                       0.48f, 0.30f, 0.0f, 0.15f, 0.14f, 0.12f, 0.30f, 0.0f },
    // Таз ведёт, плечи догоняют, оружие проходит поперёк и уходит влево
    [ATK_SWING]    = { { -1.40f, 1.15f, 0.10f, -0.30f, 0.30f, -0.2f }, { 0.30f, -0.20f, 0.45f, -0.90f, 0.0f, 0.0f },
                       0.14f, 0.55f, 0.0f, 0.45f, 0.11f, 0.08f, 0.18f, 0.0f },
    // Выпад: длинный шаг, рука выпрямлена, левая уходит назад для равновесия
    [ATK_THRUST]   = { { -1.50f, 0.20f, 0.05f, -0.05f, 1.45f, 0.0f }, { 0.55f, -0.10f, 0.25f, -0.55f, 0.0f, 0.0f },
                       0.30f, 0.35f, 0.0f, 0.25f, 0.16f, 0.20f, 0.42f, 0.0f },
};

static void apply_fight(Pose *o, FightK k, f32 w, f32 *chest_yaw)
{
    if (w <= 0.0f) return;
    Pose f = *o;
    set_arm(&f, R, k.r);
    set_arm(&f, L, k.l);
    f.r[BONE_SPINE].x += k.lean * 0.5f;
    f.r[BONE_CHEST].x += k.lean * 0.5f;
    f.r[BONE_SPINE].y += k.twist * 0.4f - k.pyaw * 0.5f;
    f.r[BONE_CHEST].y += k.twist * 0.6f - k.pyaw * 0.3f;
    f.r[BONE_SPINE].z += k.roll;
    f.r[BONE_PELVIS].y += k.pyaw;
    f.r[BONE_PELVIS].x += 0.08f;
    f.off.y -= k.drop;
    f.off.z += k.pz;
    // Голова смотрит на противника: скрутку корпуса гасит
    f.r[BONE_HEAD].y -= (k.twist + k.pyaw * 0.2f) * 0.8f;
    f.r[BONE_HEAD].x -= k.lean * 0.6f;
    *chest_yaw += (k.twist + k.pyaw * 0.2f) * w;
    // Стопы: стойка шире, левая впереди
    V3 fl = joint(BONE_FOOT_L), fr = joint(BONE_FOOT_R);
    f.foot[L] = v3(fl.x + 0.07f, fl.y, 0.20f + k.front);
    f.foot[R] = v3(fr.x - 0.05f, fr.y, -0.20f + k.back);
    f.fpitch[L] = f.fpitch[R] = 0.0f;
    pose_lerp(o, &f, w);
}

// Подъём стопы при переступании: шаг в удар и обратно
static void step_lift(Pose *o, u32 side, f32 from_z, f32 to_z, f32 w)
{
    f32 d = fabsf(to_z - from_z);
    if (d < 0.02f) return;
    f32 at = clampf((o->foot[side].z - from_z) / (to_z - from_z), 0.0f, 1.0f);
    o->foot[side].y += 0.12f * fsin(PI_F * at) * fminf(1.0f, d / 0.3f) * w;
}

static void combat_layer(const CharAction *a, Pose *o, f32 *chest_yaw, f32 seed)
{
    f32 guard = clampf(a->guard, 0.0f, 1.0f);
    FightK k = K_GUARD;
    u8 at = a->attack < ATK_COUNT ? a->attack : ATK_SWING;
    f32 front_from = 0.0f, front_to = 0.0f;
    if (a->swing > 0.0f) {
        if (a->swing < 1.0f) {
            // Замах: сначала медленно, к концу набирает
            k = fk_lerp(K_GUARD, K_WINDUP[at], smooth01(a->swing));
        } else if (a->swing < 2.0f) {
            // Удар: резко, ноги и таз раньше рук
            f32 t = a->swing - 1.0f;
            FightK s = fk_lerp(K_WINDUP[at], K_STRIKE[at], ease_out(t));
            f32 legs = ease_out(fminf(1.0f, t * 1.6f));
            s.front = lerpf(K_WINDUP[at].front, K_STRIKE[at].front, legs);
            s.pyaw = lerpf(K_WINDUP[at].pyaw, K_STRIKE[at].pyaw, legs);
            s.pz = lerpf(K_WINDUP[at].pz, K_STRIKE[at].pz, legs);
            k = s;
            front_from = K_WINDUP[at].front;
            front_to = K_STRIKE[at].front;
        } else {
            // Проводка и возврат в стойку: передняя нога подтягивается назад
            f32 t = a->swing - 2.0f;
            f32 hold = ramp(t, 0.0f, 0.25f);
            k = fk_lerp(K_STRIKE[at], K_GUARD, smooth01((t - 0.15f) / 0.85f));
            (void)hold;
            front_from = K_STRIKE[at].front;
            front_to = 0.0f;
        }
    }
    k = fk_lerp(k, K_BLOCK, clampf(a->block, 0.0f, 1.0f));

    // Ошеломлён: корпус назад, руки врозь, задняя нога ловит равновесие
    f32 st = clampf(a->stagger, 0.0f, 1.0f);
    if (st > 0.0f) {
        FightK s = { { -0.35f, -0.2f, 0.75f, -0.6f, 0.2f, 0.0f }, { -0.55f, -0.2f, 0.85f, -0.5f, 0.0f, 0.0f },
                     -0.38f, 0.1f, 0.12f * fsin(seed), -0.1f, 0.13f, -0.18f, -0.05f, -0.32f };
        k = fk_lerp(k, s, st);
    }
    // Уворот: назад — корпус откинут, шаг назад; вбок — уход с линии; нырок — присед под удар
    f32 dg = clampf(a->dodge, 0.0f, 1.0f);
    if (dg > 0.0f) {
        FightK d = k;
        switch (a->dodge_kind) {
        case DODGE_SIDE:
            d.roll = -0.30f * a->dodge_side;
            d.lean = 0.05f;
            d.drop = 0.12f;
            d.twist = 0.25f * a->dodge_side;
            break;
        case DODGE_DUCK:
            d.lean = 0.65f;
            d.drop = 0.34f;
            d.pz = -0.06f;
            d.twist = 0.2f;
            d.l.ux = -0.6f;
            break;
        default:
            d.lean = -0.32f;
            d.pz = -0.22f;
            d.drop = 0.10f;
            d.back = -0.38f;
            d.front = -0.12f;
            break;
        }
        k = fk_lerp(k, d, dg);
    }
    apply_fight(o, k, guard, chest_yaw);
    if (guard > 0.0f && front_from != front_to) step_lift(o, L, 0.20f + front_from, 0.20f + front_to, guard);
    if (dg > 0.0f && a->dodge_kind == DODGE_SIDE) {
        // Шаг вбок той ногой, куда уходит; таз следом
        u32 s = a->dodge_side > 0.0f ? L : R;
        o->foot[s].x += 0.30f * a->dodge_side * dg;
        o->foot[s].y += 0.10f * fsin(PI_F * dg) * (1.0f - dg);
        o->off.x += 0.20f * a->dodge_side * dg;
    }
    if (st > 0.0f) o->foot[R].y += 0.08f * fsin(PI_F * st) * (1.0f - st);

    // Отдача от попадания: голова откидывается, корпус складывается, рука отдёргивается
    f32 fl = clampf(a->flinch, 0.0f, 1.0f);
    if (fl > 0.0f) {
        f32 e = fl * fl;
        switch (a->flinch_part) {
        case PART_HEAD:
            o->r[BONE_NECK].x -= 0.35f * e;
            o->r[BONE_HEAD].x -= 0.35f * e;
            o->r[BONE_HEAD].y += 0.30f * e;
            o->r[BONE_CHEST].x -= 0.15f * e;
            break;
        case PART_ARM_L:
        case PART_ARM_R: {
            u32 s = a->flinch_part == PART_ARM_L ? L : R;
            add_arm(o, s, (ArmK){ 0.4f, 0.3f, 0.2f, -0.4f, 0, 0 }, e);
            o->r[BONE_CHEST].y += (s == L ? -0.25f : 0.25f) * e;
            break;
        }
        case PART_LEG_L:
        case PART_LEG_R:
            o->off.y -= 0.09f * e;
            o->r[BONE_PELVIS].z += (a->flinch_part == PART_LEG_L ? -0.12f : 0.12f) * e;
            o->r[BONE_SPINE].x += 0.15f * e;
            break;
        default:
            o->r[BONE_SPINE].x += 0.30f * e;
            o->r[BONE_CHEST].x += 0.20f * e;
            o->off.z -= 0.08f * e;
            o->off.y -= 0.05f * e;
            break;
        }
    }
}

// ---------------------------------------------------------------- падение, подъём, лёжа

// Ключ позы всего тела: таз (высота над землёй, вперёд, вбок, повороты), корпус, руки, ноги.
// Ноги углами (ik = 0) или стопами на земле (ik = 1)
typedef struct BodyK {
    f32 t;
    f32 py, pz, px, ptx, pty, ptz;
    f32 sp, ch, hd, tw;
    ArmK al, ar;
    f32 lt[4], rt[4];        // бедро вперёд(<0), бедро в сторону, колено, стопа
    V3 fl, fr;               // цели стоп при ik
    f32 ik;
} BodyK;

#define ARM_LIMP_L { -0.35f, -0.1f, 0.95f, -0.45f, 0.2f, 0.0f }
#define ARM_LIMP_R { -0.30f, -0.1f, 1.10f, -0.25f, 0.2f, 0.0f }

// Лежит на спине
static const BodyK K_LIE_UP = { 1.0f, 0.13f, -0.62f, 0.0f, -1.53f, 0.0f, 0.0f, 0.02f, 0.0f, 0.15f, 0.0f,
                                ARM_LIMP_L, ARM_LIMP_R, { -0.75f, 0.10f, 1.30f, 0.20f }, { -0.15f, 0.12f, 0.25f, 0.35f },
                                { 0, 0, 0 }, { 0, 0, 0 }, 0.0f };
// Лежит ничком, голова набок
static const BodyK K_LIE_DOWN = { 1.0f, 0.15f, 0.62f, 0.0f, 1.50f, 0.0f, 0.0f, 0.02f, 0.0f, -0.25f, 0.0f,
                                  { -2.3f, -0.2f, 0.6f, -1.5f, 0.0f, 0.0f }, { 0.15f, 0.0f, 0.35f, -0.2f, 0.0f, 0.0f },
                                  { 0.05f, 0.18f, 0.35f, 0.9f }, { -0.05f, 0.08f, 0.10f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f };

// Сбит с ног ударом: удар назад, оступается, садится, валится на спину, затылок последним
static const BodyK K_FALL[] = {
    { 0.00f, 0.99f, -0.03f, 0.0f, -0.05f, 0.0f, 0.0f, -0.25f, -0.12f, 0.35f, 0.0f,
      { -1.1f, 0.0f, 0.5f, -0.6f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.5f, -0.7f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.10f }, { -0.115f, 0.09f, -0.05f }, 1.0f },
    { 0.30f, 0.86f, -0.24f, 0.0f, -0.20f, 0.1f, 0.0f, -0.30f, -0.15f, 0.30f, 0.0f,
      { -1.4f, -0.2f, 0.9f, -0.5f, 0.0f, 0.0f }, { -1.2f, -0.2f, 0.9f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.05f }, { -0.13f, 0.12f, -0.45f }, 1.0f },
    { 0.55f, 0.36f, -0.42f, 0.0f, -0.45f, 0.1f, 0.0f, -0.05f, 0.10f, 0.45f, 0.0f,
      { 0.55f, -0.1f, 0.35f, -0.2f, -0.6f, 0.0f }, { 0.60f, -0.1f, 0.35f, -0.2f, -0.6f, 0.0f },
      { -1.55f, 0.20f, 1.55f, 0.0f }, { -1.35f, 0.15f, 1.30f, 0.0f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.16f, -0.58f, 0.0f, -1.30f, 0.0f, 0.0f, -0.05f, 0.0f, 0.55f, 0.0f,
      { -0.2f, -0.1f, 0.9f, -0.3f, 0.0f, 0.0f }, { -0.1f, -0.1f, 1.0f, -0.2f, 0.0f, 0.0f },
      { -1.20f, 0.15f, 1.40f, 0.2f }, { -0.60f, 0.12f, 0.60f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_UP,
};

// Потерял сознание стоя: колени подламываются, оседает на колени и заваливается ничком
static const BodyK K_COLLAPSE_DOWN[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.10f, 0.55f, 0.0f,
      { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.03f }, { -0.115f, 0.09f, -0.02f }, 1.0f },
    { 0.40f, 0.48f, -0.05f, 0.0f, 0.15f, 0.0f, 0.0f, 0.30f, 0.20f, 0.60f, 0.0f,
      { 0.10f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f },
      { -0.15f, 0.05f, 1.75f, 0.6f }, { -0.10f, 0.05f, 1.70f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.68f, 0.40f, 0.20f, 0.0f, 0.95f, 0.0f, 0.0f, 0.30f, 0.15f, 0.30f, 0.0f,
      { -0.90f, 0.0f, 0.40f, -0.4f, 0.0f, 0.0f }, { -0.80f, 0.0f, 0.40f, -0.4f, 0.0f, 0.0f },
      { -0.80f, 0.05f, 1.55f, 0.6f }, { -0.75f, 0.05f, 1.50f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_DOWN,
};

// Тот же обморок, но заваливается на спину: оседает и падает назад набок
static const BodyK K_COLLAPSE_UP[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.10f, 0.55f, 0.0f,
      { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.03f }, { -0.115f, 0.09f, -0.02f }, 1.0f },
    { 0.38f, 0.55f, -0.10f, 0.0f, 0.05f, 0.2f, 0.0f, 0.25f, 0.15f, 0.60f, 0.0f,
      { 0.05f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f },
      { -0.90f, 0.10f, 1.60f, 0.2f }, { -0.60f, 0.10f, 1.40f, 0.2f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.70f, 0.20f, -0.40f, 0.0f, -0.90f, 0.3f, 0.2f, 0.10f, 0.05f, 0.50f, 0.0f,
      { -0.2f, 0.0f, 0.70f, -0.3f, 0.0f, 0.0f }, { 0.2f, 0.0f, 0.60f, -0.3f, 0.0f, 0.0f },
      { -1.10f, 0.15f, 1.30f, 0.2f }, { -0.70f, 0.10f, 0.90f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_UP,
};

// Встаёт со спины: садится, подтягивает ноги, перекатывается на колено, встаёт с опорой рукой о колено
static const BodyK K_GETUP_UP[] = {
    K_LIE_UP,
    { 0.22f, 0.17f, -0.45f, 0.0f, -0.55f, 0.0f, 0.0f, 0.45f, 0.20f, 0.20f, 0.0f,
      { 0.75f, 0.0f, 0.30f, -0.25f, -0.8f, 0.0f }, { 0.70f, 0.0f, 0.30f, -0.25f, -0.8f, 0.0f },
      { -1.30f, 0.20f, 2.00f, 0.3f }, { -1.10f, 0.15f, 1.80f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.45f, 0.40f, -0.20f, -0.05f, 0.10f, -0.35f, 0.10f, 0.45f, 0.10f, -0.10f, 0.0f,
      { 0.30f, 0.0f, 0.55f, -0.10f, -1.0f, 0.0f }, { -0.90f, 0.40f, 0.20f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.16f, 0.09f, 0.10f }, { -0.10f, 0.13f, -0.48f }, 1.0f },
    { 0.68f, 0.56f, -0.05f, 0.0f, 0.15f, -0.10f, 0.0f, 0.30f, 0.10f, -0.15f, 0.0f,
      { -0.70f, 0.45f, 0.05f, -0.35f, 0.0f, 0.0f }, { -0.50f, 0.20f, 0.20f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.14f, 0.09f, 0.30f }, { -0.12f, 0.13f, -0.30f }, 1.0f },
    { 0.86f, 0.86f, 0.05f, 0.0f, 0.12f, -0.05f, 0.0f, 0.20f, 0.05f, -0.10f, 0.0f,
      { -0.50f, 0.35f, 0.05f, -0.40f, 0.0f, 0.0f }, { -0.25f, 0.10f, 0.15f, -0.5f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.22f }, { -0.12f, 0.09f, -0.12f }, 1.0f },
    { 1.00f, 0.97f, 0.0f, 0.0f, 0.03f, 0.0f, 0.0f, 0.05f, 0.02f, 0.0f, 0.0f,
      { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
};

// Встаёт ничком: отжимается на руках, подтягивает колени, встаёт с колена
static const BodyK K_GETUP_DOWN[] = {
    K_LIE_DOWN,
    { 0.28f, 0.22f, 0.55f, 0.0f, 1.20f, 0.0f, 0.0f, -0.25f, -0.10f, -0.35f, 0.0f,
      { -1.35f, 0.15f, 0.25f, -0.10f, -1.0f, 0.0f }, { -1.35f, 0.15f, 0.25f, -0.10f, -1.0f, 0.0f },
      { 0.05f, 0.10f, 0.15f, 0.9f }, { 0.05f, 0.10f, 0.15f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.55f, 0.50f, 0.35f, 0.0f, 0.85f, 0.0f, 0.0f, 0.05f, 0.0f, -0.55f, 0.0f,
      { -1.00f, 0.10f, 0.20f, -0.10f, -1.0f, 0.0f }, { -1.00f, 0.10f, 0.20f, -0.10f, -1.0f, 0.0f },
      { -0.95f, 0.10f, 1.85f, 0.7f }, { -0.95f, 0.10f, 1.85f, 0.7f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.74f, 0.56f, -0.05f, 0.0f, 0.20f, -0.10f, 0.0f, 0.30f, 0.10f, -0.20f, 0.0f,
      { -0.70f, 0.45f, 0.05f, -0.35f, 0.0f, 0.0f }, { -0.30f, 0.10f, 0.20f, -0.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.14f, 0.09f, 0.30f }, { -0.12f, 0.13f, -0.30f }, 1.0f },
    { 0.88f, 0.86f, 0.05f, 0.0f, 0.12f, -0.05f, 0.0f, 0.20f, 0.05f, -0.10f, 0.0f,
      { -0.50f, 0.35f, 0.05f, -0.40f, 0.0f, 0.0f }, { -0.25f, 0.10f, 0.15f, -0.5f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.22f }, { -0.12f, 0.09f, -0.12f }, 1.0f },
    { 1.00f, 0.97f, 0.0f, 0.0f, 0.03f, 0.0f, 0.0f, 0.05f, 0.02f, 0.0f, 0.0f,
      { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
};

// Сдался: опускается на одно колено, потом на оба, руки поднимает пустыми ладонями
static const BodyK K_KNEEL[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.03f, 0.0f, 0.0f, 0.05f, 0.02f, 0.0f, 0.0f,
      { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.45f, 0.56f, -0.05f, 0.0f, 0.10f, -0.05f, 0.0f, 0.15f, 0.05f, 0.20f, 0.0f,
      { -0.6f, 0.2f, 0.3f, -1.4f, 0.0f, 0.0f }, { -0.6f, 0.2f, 0.3f, -1.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.14f, 0.09f, 0.30f }, { -0.12f, 0.13f, -0.32f }, 1.0f },
    { 1.00f, 0.50f, -0.12f, 0.0f, -0.05f, 0.0f, 0.0f, 0.10f, 0.05f, 0.35f, 0.0f,
      { -1.4f, 0.1f, 0.55f, -1.9f, 0.0f, 0.0f }, { -1.4f, 0.1f, 0.55f, -1.9f, 0.0f, 0.0f },
      { 0.05f, 0.10f, 1.70f, 0.75f }, { 0.05f, 0.10f, 1.70f, 0.75f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
};

static void body_key(Pose *o, const BodyK *k)
{
    Pose p = { 0 };
    f32 py0 = joint(BONE_PELVIS).y;
    p.off = v3(k->px, k->py - py0, k->pz);
    p.r[BONE_PELVIS] = v3(k->ptx, k->pty, k->ptz);
    p.r[BONE_SPINE] = v3(k->sp, k->tw * 0.5f, 0.0f);
    p.r[BONE_CHEST] = v3(k->ch, k->tw * 0.5f, 0.0f);
    p.r[BONE_NECK] = v3(k->hd * 0.4f, 0.0f, 0.0f);
    p.r[BONE_HEAD] = v3(k->hd * 0.6f, 0.0f, 0.0f);
    set_arm(&p, L, k->al);
    set_arm(&p, R, k->ar);
    for (u32 s = 0; s < 2; s++) {
        const f32 *lg = s == L ? k->lt : k->rt;
        f32 m = s == L ? 1.0f : -1.0f;
        u8 th = s == L ? BONE_THIGH_L : BONE_THIGH_R;
        p.r[th] = v3(lg[0], 0.0f, m * lg[1]);
        p.r[th + 1] = v3(lg[2], 0.0f, 0.0f);
        p.r[th + 2] = v3(lg[3], 0.0f, 0.0f);
    }
    p.foot[L] = k->fl;
    p.foot[R] = k->fr;
    p.ik = k->ik;
    *o = p;
}

// Поза в момент t по ключам: плавно через соседние (Катмулл — Ром по времени ключей)
static void body_track(Pose *o, const BodyK *keys, u32 n, f32 t)
{
    u32 i = 0;
    while (i + 2 < n && t > keys[i + 1].t) i++;
    f32 span = keys[i + 1].t - keys[i].t;
    f32 u = span > 0.0f ? clampf((t - keys[i].t) / span, 0.0f, 1.0f) : 1.0f;
    Pose p0, p1, pa, pb;
    body_key(&p0, &keys[i]);
    body_key(&p1, &keys[i + 1]);
    body_key(&pa, &keys[i > 0 ? i - 1 : i]);
    body_key(&pb, &keys[i + 2 < n ? i + 2 : i + 1]);
    // Катмулл — Ром: касательные по соседним ключам, без рывков на стыках
    f32 u2 = u * u, u3 = u2 * u;
    f32 c0 = -0.5f * u3 + u2 - 0.5f * u, c1 = 1.5f * u3 - 2.5f * u2 + 1.0f, c2 = -1.5f * u3 + 2.0f * u2 + 0.5f * u, c3 = 0.5f * u3 - 0.5f * u2;
    const f32 *a = (const f32 *)&pa, *b = (const f32 *)&p0, *c = (const f32 *)&p1, *d = (const f32 *)&pb;
    f32 *r = (f32 *)o;
    for (u32 j = 0; j < sizeof(Pose) / sizeof(f32); j++) r[j] = c0 * a[j] + c1 * b[j] + c2 * c[j] + c3 * d[j];
    // Цели стоп и доля IK не выходят за ключи
    o->ik = clampf(lerpf(p0.ik, p1.ik, u), 0.0f, 1.0f);
    if (p0.ik <= 0.0f) o->foot[L] = p1.foot[L], o->foot[R] = p1.foot[R];
    else if (p1.ik <= 0.0f) o->foot[L] = p0.foot[L], o->foot[R] = p0.foot[R];
}

// Лёжа: дышит, раненый шевелится; ползком: руки по очереди тянутся вперёд, ноги толкают
static void lie_pose(Pose *o, bool prone, f32 t, f32 limp, f32 seed)
{
    body_key(o, prone ? &K_LIE_DOWN : &K_LIE_UP);
    f32 br = (1.0f - 0.85f * limp) * fsin(t * 2.1f + seed);
    o->r[BONE_CHEST].x += 0.025f * br;
    o->r[BONE_HEAD].y += prone ? 1.1f : 0.25f * fsin(seed * 3.0f);
    // В сознании: перекатывает голову, подтягивает ногу
    f32 alive = 1.0f - limp;
    o->r[BONE_HEAD].y += 0.25f * fsin(t * 0.6f + seed) * alive;
    o->r[BONE_THIGH_R].x -= 0.35f * smooth01(fsin(t * 0.45f + seed) * 2.0f) * alive;
    o->r[BONE_SHIN_R].x += 0.6f * smooth01(fsin(t * 0.45f + seed) * 2.0f) * alive;
}

static void crawl_pose(Pose *o, f32 t, f32 seed)
{
    body_key(o, &K_LIE_DOWN);
    f32 c = fsin(t * 2.4f + seed), c2 = fcos(t * 2.4f + seed);
    o->r[BONE_HEAD].y = 0.0f;
    o->r[BONE_HEAD].x -= 0.45f;
    o->r[BONE_SPINE].z += 0.10f * c;
    o->r[BONE_PELVIS].y += 0.06f * c;
    // Подтягивается: рука выброшена вперёд, локоть упирается, тянет тело
    for (u32 s = 0; s < 2; s++) {
        f32 q = s == L ? c : -c;
        f32 reach = smooth01(q * 0.5f + 0.5f);
        ArmK a = { -2.6f + 0.6f * (1.0f - reach), 0.15f, 0.45f, -0.3f - 1.1f * (1.0f - reach), -0.3f, 0.0f };
        set_arm(o, s, a);
        u8 th = s == L ? BONE_THIGH_L : BONE_THIGH_R;
        f32 kick = smooth01((s == L ? c2 : -c2) * 0.5f + 0.5f);
        o->r[th].x = -0.45f * kick;
        o->r[th].z = (s == L ? 1.0f : -1.0f) * (0.15f + 0.35f * kick);
        o->r[th + 1].x = 0.9f * kick + 0.1f;
    }
    o->off.y += 0.02f * fabsf(c);
}

// ---------------------------------------------------------------- оружие: хват и ножны

typedef enum Carry { CARRY_BELT, CARRY_BACK_HIGH, CARRY_BACK_LOW } Carry;

static Carry weapon_carry(u8 w)
{
    u8 wc = g_weapons[w].wclass;
    if (wc == WC_HEAVY) return CARRY_BACK_HIGH;
    if (wc == WC_POLEARM) return CARRY_BACK_LOW;
    return CARRY_BELT;
}

static bool weapon_two_hand(u8 w)
{
    u8 wc = g_weapons[w].wclass;
    return wc == WC_POLEARM || wc == WC_HEAVY;
}

// Где вторая (левая) рука на оси оружия, м от правой
static f32 weapon_left_grip(u8 w) { return g_weapons[w].wclass == WC_POLEARM ? 0.62f : -0.15f; }

// Ось рукояти в кулаке (поза привязки): из кулака вперёд со стороны большого пальца, чуть вниз;
// лезвие к пальцам
static Xf grip_frame(void)
{
    V3 a = v3_norm(v3(0.0f, -0.25f, 1.0f));
    V3 e = v3_norm(v3(0.0f, -1.0f, -0.25f));
    return (Xf){ v3_cross(a, e), a, e, v3(0, 0, 0) };
}

// Рамка из оси острия и стороны лезвия
static Xf frame_axes(V3 tip, V3 edge, V3 pos)
{
    tip = v3_norm(tip);
    edge = v3_norm(v3_sub(edge, v3_mul(tip, v3_dot(edge, tip))));
    return (Xf){ v3_cross(tip, edge), tip, edge, pos };
}

// Ножны относительно кости-носителя, поза привязки (пространство модели)
static Xf sheath_frame(u8 w, u8 *bone)
{
    Carry c = weapon_carry(w);
    if (c == CARRY_BELT) {
        // Левое бедро, рукоять вперёд-вверх под правую руку
        *bone = BONE_PELVIS;
        V3 s = g_body_sockets[SOCKET_BELT].pos;
        return frame_axes(v3(0.18f, -1.0f, -0.62f), v3(0.0f, 0.3f, -1.0f), v3(fabsf(s.x) + 0.04f, s.y + 0.06f, s.z + 0.11f));
    }
    *bone = BONE_CHEST;
    V3 s = g_body_sockets[SOCKET_BACK].pos;
    if (c == CARRY_BACK_HIGH)
        // Рукоять над правым плечом, клинок наискосок к левому бедру — сквозь рюкзак
        return frame_axes(v3(0.42f, -1.0f, -0.04f), v3(0.0f, 0.0f, -1.0f), v3(s.x - 0.17f, s.y + 0.30f, s.z + 0.01f));
    // Древко наискосок вверх за левое плечо, хват у правой лопатки
    return frame_axes(v3(0.40f, 1.0f, -0.02f), v3(0.0f, 0.0f, -1.0f), v3(s.x - 0.16f, s.y - 0.12f, s.z + 0.03f));
}

// ---------------------------------------------------------------- оружие в бою

// Стойка и блок по типу оружия: рукоять и оси оружия относительно груди (поза привязки, от сустава груди)
typedef struct GuardK {
    V3 pos, tip, edge;     // стойка
    V3 bpos, btip, bedge;  // блок
} GuardK;

static const GuardK g_guard[3] = {
    // Одноручное: рукоять у пояса справа, остриё в лицо противнику; блок — клинок поперёк над головой
    { { -0.15f, -0.14f, 0.35f }, { 0.15f, 1.60f, 1.0f }, { 0.0f, -1.0f, 1.6f },
      { -0.12f, 0.38f, 0.30f }, { 1.0f, 0.30f, 0.15f }, { 0.0f, 1.0f, 0.25f } },
    // Древковое: правая у бедра, древко вперёд в грудь противнику; блок — древко поперёк груди
    { { -0.16f, -0.21f, 0.08f }, { 0.10f, 0.35f, 1.0f }, { 0.0f, -1.0f, 0.0f },
      { -0.25f, 0.05f, 0.25f }, { 1.0f, 0.35f, 0.10f }, { 0.0f, 0.0f, 1.0f } },
    // Тяжёлое двуручное: рукоять у живота, клинок стоймя чуть вперёд; блок — плашмя перед собой
    { { -0.10f, -0.17f, 0.31f }, { 0.18f, 1.0f, 0.30f }, { 0.0f, 0.0f, 1.0f },
      { -0.10f, 0.10f, 0.32f }, { 0.35f, 1.0f, 0.10f }, { 1.0f, 0.0f, 0.0f } },
};

static Xf frame_lerp(const Xf *a, const Xf *b, f32 t) { return xf_slerp_rot(a, b, t, v3_lerp(a->t, b->t, t)); }

// Оружие на доле удара q (0 — взведено, 1 — конец проводки). Размах и удар сверху: клинок от плеча P
// по дуге в плоскости следа, на ударе точка клинка проходит через цель T; укол — рукоять вперёд, остриё за T
static Xf strike_frame(const CharAction *a, V3 P, V3 T, f32 len, f32 reach, f32 q)
{
    if (a->attack == ATK_THRUST) {
        // Рукоять от бока толкается к цели, пока остриё не уйдёт за неё; дальше руки не пускают
        V3 gw = v3_add(P, v3(0.0f, -0.28f, -0.12f));
        V3 db = v3_sub(T, gw);
        f32 dbl = fmaxf(v3_len(db), 0.3f);
        f32 push = clampf(dbl - len + 0.10f - (a->aim_stop ? 0.30f : 0.0f), 0.10f, 0.60f);
        V3 gh = v3_add(gw, v3_mul(db, push / dbl));
        V3 gp = v3_sub(gh, P);
        f32 gl = v3_len(gp);
        if (gl > reach) gh = v3_add(P, v3_mul(gp, reach / gl));
        V3 g = v3_lerp(gw, gh, smooth01(q / 0.45f));
        V3 u = v3_norm(v3_sub(T, g));
        return frame_axes(u, v3_cross(u, v3(0, 1, 0)), g);
    }
    V3 d = v3_sub(T, P);
    f32 D = fmaxf(v3_len(d), 0.3f);
    V3 u0 = v3_mul(d, 1.0f / D);
    bool over = a->attack == ATK_OVERHEAD;
    f32 tw = over ? -2.2f : -1.9f, tf = over ? 0.9f : 1.3f;
    f32 th = lerpf(tw, tf, q);
    // Блок: клинок встаёт перед целью и отскакивает
    if (a->aim_stop && th > -0.35f) th = -0.35f - 0.4f * fminf(th + 0.35f, 1.0f);
    V3 m = over ? v3(a->aim_s, -1.0f, 0.0f) : v3(1.0f, a->aim_s, 0.0f);
    m = v3_norm(v3_sub(m, v3_mul(u0, v3_dot(m, u0))));
    f32 ah = clampf(D - 0.7f * len, 0.25f, reach);
    f32 r = th < 0.0f ? lerpf(ah, 0.38f, (th / tw) * (th / tw)) : lerpf(ah, 0.42f, (th / tf) * (th / tf));
    f32 sn, cs;
    fsincos(th, &sn, &cs);
    V3 u = v3_add(v3_mul(u0, cs), v3_mul(m, sn));
    V3 e = v3_sub(v3_mul(m, cs), v3_mul(u0, sn));
    return frame_axes(u, e, v3_add(P, v3_mul(u, r)));
}

// Рамка оружия в бою (пространство модели): стойка, блок, замах — удар — возврат
static Xf fight_frame(const CharAction *a, u8 wpn, const Xf *wld)
{
    u8 wc = g_weapons[wpn].wclass;
    const GuardK *k = &g_guard[wc == WC_POLEARM ? 1 : wc == WC_HEAVY ? 2 : 0];
    const Xf *c = &wld[BONE_CHEST];
    Xf f = frame_axes(xf_dir(c, k->tip), xf_dir(c, k->edge), xf_point(c, k->pos));
    f32 bl = clampf(a->block, 0.0f, 1.0f);
    if (bl > 0.0f) {
        Xf b = frame_axes(xf_dir(c, k->btip), xf_dir(c, k->bedge), xf_point(c, k->bpos));
        f = frame_lerp(&f, &b, smooth01(bl));
    }
    if (a->swing <= 0.0f || !a->aim_on) return f;
    V3 P = wld[BONE_UPPER_ARM_R].t;
    f32 reach = v3_len(v3_sub(joint(BONE_FOREARM_R), joint(BONE_UPPER_ARM_R))) +
                v3_len(v3_sub(joint(BONE_HAND_R), joint(BONE_FOREARM_R))) + 0.07f;
    f32 len = g_weapons[wpn].length;
    if (a->swing < 1.0f) {
        Xf s = strike_frame(a, P, a->aim, len, reach, 0.0f);
        return frame_lerp(&f, &s, smooth01(a->swing));
    }
    if (a->swing < 2.0f) return strike_frame(a, P, a->aim, len, reach, ease_out(a->swing - 1.0f));
    Xf s = strike_frame(a, P, a->aim, len, reach, 1.0f);
    return frame_lerp(&s, &f, smooth01((a->swing - 2.0f) / 0.8f));
}

// ---------------------------------------------------------------- IK руки

static void arm_ik(Xf *wld, u32 side, V3 target, const Xf *hand_rot)
{
    u8 up = side == L ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
    u8 fo = up + 1, ha = up + 2;
    f32 l1 = v3_len(v3_sub(joint(fo), joint(up))), l2 = v3_len(v3_sub(joint(ha), joint(fo)));
    V3 S = wld[up].t, E = wld[fo].t, W = wld[ha].t;
    V3 d = v3_sub(target, S);
    f32 dl = v3_len(d);
    if (dl < 1e-4f) return;
    V3 dn = v3_mul(d, 1.0f / dl);
    f32 len = clampf(dl, 0.3f * (l1 + l2), l1 + l2 - 1e-3f);
    // Локоть остаётся в плоскости, где был: вниз и наружу
    V3 pole = v3_sub(E, S);
    pole = v3_sub(pole, v3_mul(dn, v3_dot(pole, dn)));
    if (v3_dot(pole, pole) < 1e-6f) pole = v3(side == L ? 0.3f : -0.3f, -1.0f, -0.3f);
    pole = v3_norm(pole);
    f32 ca = clampf((l1 * l1 + len * len - l2 * l2) / (2.0f * l1 * len), -1.0f, 1.0f);
    f32 sa = sqrtf(1.0f - ca * ca);
    V3 E2 = v3_add(S, v3_add(v3_mul(dn, l1 * ca), v3_mul(pole, l1 * sa)));
    Xf r1 = xf_between(v3_norm(v3_sub(E, S)), v3_norm(v3_sub(E2, S)));
    Xf u = xf_mul(&r1, &wld[up]);
    u.t = S;
    V3 fdir = xf_dir(&r1, v3_sub(W, E));
    V3 W2 = v3_add(E2, v3_mul(dn, len));
    W2 = v3_add(E2, v3_mul(v3_norm(v3_sub(target, E2)), l2));
    Xf r2 = xf_between(v3_norm(fdir), v3_norm(v3_sub(W2, E2)));
    Xf rr = xf_mul(&r2, &r1);
    Xf f = xf_mul(&rr, &wld[fo]);
    f.t = E2;
    Xf h = hand_rot ? *hand_rot : xf_mul(&rr, &wld[ha]);
    h.t = W2;
    wld[up] = u;
    wld[fo] = f;
    wld[ha] = h;
}

// ---------------------------------------------------------------- сборка

void character_pose(const CharPose *p, const CharAction *act, M4 *out)
{
    static const CharAction none = { 0 };
    const CharAction *a = act ? act : &none;
    g_joints = p->joints;
    const bool armed = p->weapon > 0 && p->weapon <= W_COUNT;
    const u8 wpn = armed ? (u8)(p->weapon - 1) : 0;
    f32 guard = clampf(a->guard, 0.0f, 1.0f);
    f32 walk = clampf(p->walk, 0.0f, 1.0f);
    f32 wield = armed ? clampf(a->wield, 0.0f, 1.0f) : 0.0f;
    f32 breathe = fsin(p->time * 1.7f + p->seed);

    // Основа: стоит, дышит, переступает
    Pose o;
    pose_stand(&o);
    o.r[BONE_CHEST].x += 0.018f * breathe;
    o.r[BONE_CLAVICLE_L].z += 0.02f * breathe;
    o.r[BONE_CLAVICLE_R].z -= 0.02f * breathe;
    o.r[BONE_SPINE].x += 0.03f;

    // Покой: только стоя, вне боя и без клипа
    f32 idle_w = (1.0f - walk) * (1.0f - guard) * (a->clip == CLIP_NONE ? 1.0f : 0.0f) * (p->calm ? 0.0f : 1.0f);
    if (idle_w > 0.01f) {
        f32 t = p->time + p->seed * 37.0f;
        u32 sd = (u32)(p->seed * 1000.0f);
        Pose cur = o;
        idle_moves(&cur, t, sd, p->seed, armed);
        // Поза отрезка: входит не раньше, чем постоял POSE_AFTER, и выходит до конца отрезка
        u32 slot = (u32)(t / POSE_SLOT);
        f32 lt = fminf(t - (f32)slot * POSE_SLOT, p->idle - POSE_AFTER);
        u32 k = pose_pick(sd, slot);
        if (k != POSE_NONE && lt > 0.0f) {
            Pose ps = o;
            idle_pose(&ps, k, lt, p->seed);
            f32 tail = POSE_SLOT - (t - (f32)slot * POSE_SLOT);
            pose_lerp(&cur, &ps, smooth01(lt / POSE_FADE) * smooth01(tail / POSE_FADE));
        }
        // С оружием в руке руки не трогает
        Pose keep = o;
        pose_lerp(&o, &cur, idle_w);
        if (wield > 0.5f)
            for (u32 b = BONE_CLAVICLE_L; b <= BONE_HAND_R; b++) o.r[b] = keep.r[b];
    }

    // Оружие в руке вне стойки: опущено, остриё вперёд-вниз; двуручное — наперевес у бедра
    if (wield > 0.0f) {
        bool two = weapon_two_hand(wpn);
        ArmK carry = (ArmK){ -0.20f, 0.05f, 0.10f, -0.45f, 0.95f, 0.0f };
        if (g_weapons[wpn].wclass == WC_POLEARM) carry = (ArmK){ -0.45f, 0.35f, 0.10f, -1.25f, 0.75f, 0.0f };
        else if (two) carry = (ArmK){ -0.15f, 0.30f, 0.10f, -0.75f, 1.10f, 0.0f };
        set_arm(&o, R, arm_lerp(get_arm(&o, R), carry, smooth01((wield - 0.45f) / 0.55f)));
    }

    // Походка
    Loco lo = { 0 };
    locomotion(p, &o, &lo);
    if (wield > 0.5f) {
        // Рука с оружием машет вполсилы
        ArmK r = get_arm(&o, R);
        r.ux *= 0.5f;
        set_arm(&o, R, r);
    }

    // Бой поверх
    f32 chest_yaw = lo.chest_yaw;
    combat_layer(a, &o, &chest_yaw, p->seed);

    // Голова к цели, наклоны от разгона и поворота
    o.r[BONE_HEAD].y += 0.7f * p->look - chest_yaw * 0.6f * (1.0f - guard);
    o.r[BONE_SPINE].y += 0.3f * p->look * (1.0f - guard);
    o.r[BONE_HEAD].z += 0.5f * p->lean_side;
    o.r[BONE_PELVIS].x += p->lean_fwd * 0.6f;
    o.r[BONE_SPINE].x += p->lean_fwd * 0.6f;
    o.r[BONE_HEAD].x -= p->lean_fwd;
    o.off.z += p->lean_fwd * 0.35f;
    o.r[BONE_PELVIS].z -= p->lean_side * 0.6f;
    o.r[BONE_SPINE].z -= p->lean_side * 0.4f;
    o.off.x -= p->lean_side * 0.25f;

    // Всем телом: падение, лёжа, подъём, на колени, ползком
    f32 clip_w = 0.0f;
    if (a->clip != CLIP_NONE) {
        Pose c;
        f32 t = clampf(a->clip_t, 0.0f, 1.0f);
        switch (a->clip) {
        case CLIP_FALL:
            body_track(&c, K_FALL, ARRAY_COUNT(K_FALL), t);
            clip_w = ramp(t, 0.0f, 0.12f);
            break;
        case CLIP_COLLAPSE:
            if (a->prone) body_track(&c, K_COLLAPSE_DOWN, ARRAY_COUNT(K_COLLAPSE_DOWN), t);
            else body_track(&c, K_COLLAPSE_UP, ARRAY_COUNT(K_COLLAPSE_UP), t);
            clip_w = ramp(t, 0.0f, 0.15f);
            break;
        case CLIP_GETUP:
            if (a->prone) body_track(&c, K_GETUP_DOWN, ARRAY_COUNT(K_GETUP_DOWN), t);
            else body_track(&c, K_GETUP_UP, ARRAY_COUNT(K_GETUP_UP), t);
            clip_w = 1.0f - ramp(t, 0.86f, 1.0f);
            break;
        case CLIP_KNEEL:
            body_track(&c, K_KNEEL, ARRAY_COUNT(K_KNEEL), t);
            clip_w = ramp(t, 0.0f, 0.2f);
            break;
        case CLIP_CRAWL:
            crawl_pose(&c, a->clip_t, p->seed);
            clip_w = 1.0f;
            break;
        default:
            lie_pose(&c, a->prone, a->clip_t, a->limp, p->seed);
            clip_w = 1.0f;
            break;
        }
        // Обмякшие руки у выбывших
        f32 limp = clampf(a->limp, 0.0f, 1.0f);
        if (limp > 0.0f && a->clip != CLIP_LIE) {
            set_arm(&c, L, arm_lerp(get_arm(&c, L), (ArmK)ARM_LIMP_L, limp * 0.5f));
            set_arm(&c, R, arm_lerp(get_arm(&c, R), (ArmK)ARM_LIMP_R, limp * 0.5f));
        }
        pose_lerp(&o, &c, clip_w);
    }

    // Таз опускается, если опорной ноге не хватает длины до цели: длинный шаг, глубокий присед
    if (o.ik > 0.0f) {
        Xf pel = xf_euler(o.r[BONE_PELVIS], v3_add(joint(BONE_PELVIS), o.off));
        f32 drop = 0.0f;
        for (u32 s = 0; s < 2; s++) {
            if (o.plant[s] <= 0.0f) continue;
            u8 th = s == L ? BONE_THIGH_L : BONE_THIGH_R;
            V3 hip = xf_point(&pel, v3_sub(joint(th), joint(BONE_PELVIS)));
            f32 lg = 0.993f * (v3_len(v3_sub(joint(th + 1), joint(th))) + v3_len(v3_sub(joint(th + 2), joint(th + 1))));
            f32 dx = o.foot[s].x - hip.x, dz = o.foot[s].z - hip.z;
            f32 top = o.foot[s].y + sqrtf(fmaxf(lg * lg - dx * dx - dz * dz, 0.0f));
            drop = fminf(drop, (top - hip.y) * o.plant[s] * o.ik);
        }
        o.off.y += drop;
    }

    // Кинематика в пространстве модели: таз, корпус, руки углами; ноги — IK к целям
    Xf wld[BONE_COUNT];
    wld[BONE_ROOT] = XF_ID;
    for (u32 i = 1; i < BONE_COUNT; i++) {
        const BoneDef *b = &g_body_bones[i];
        V3 rel = v3_sub(joint(i), joint((u32)b->parent));
        if (i == BONE_PELVIS) rel = v3_add(rel, o.off);
        // Ноги по целям
        if ((i == BONE_THIGH_L || i == BONE_THIGH_R) && o.ik > 0.0f) {
            u32 s = i == BONE_THIGH_L ? L : R;
            f32 m = s == L ? 1.0f : -1.0f;
            const Xf *P = &wld[BONE_PELVIS];
            V3 hip = joint(i), knee = joint(i + 1), ank = joint(i + 2);
            V3 tl = xf_tdir(P, v3_sub(o.foot[s], P->t));
            V3 d = v3_sub(tl, v3_sub(hip, joint(BONE_PELVIS)));
            // Колено смотрит вперёд по земле и чуть наружу: разворот таза гасит
            f32 ty = -o.r[BONE_PELVIS].y + (0.10f + o.kout) * m;
            f32 sy, cy;
            fsincos(-ty, &sy, &cy);
            d = v3(cy * d.x + sy * d.z, d.y, -sy * d.x + cy * d.z);
            f32 l1 = v3_len(v3_sub(knee, hip)), l2 = v3_len(v3_sub(ank, knee));
            f32 bind_roll = atan2f(ank.x - hip.x, hip.y - ank.y);
            f32 roll = atan2f(d.x, -d.y) - bind_roll;
            f32 down = sqrtf(d.x * d.x + d.y * d.y);
            f32 len = clampf(sqrtf(down * down + d.z * d.z), 0.25f * (l1 + l2), l1 + l2 - 0.002f);
            f32 fa = atan2f(d.z, down);
            f32 a1 = acosf(clampf((l1 * l1 + len * len - l2 * l2) / (2.0f * l1 * len), -1.0f, 1.0f));
            f32 k = acosf(clampf((l1 * l1 + l2 * l2 - len * len) / (2.0f * l1 * l2), -1.0f, 1.0f));
            f32 tp = -(fa + a1), kn = PI_F - k;
            f32 ik = o.ik;
            V3 fk_th = o.r[i], fk_kn = o.r[i + 1], fk_ft = o.r[i + 2];
            o.r[i] = v3_lerp(fk_th, v3(tp, ty, roll), ik);
            o.r[i + 1] = v3_lerp(fk_kn, v3(kn, 0.0f, 0.0f), ik);
            // Стопа к земле с заданным наклоном
            V3 pr = o.r[BONE_PELVIS];
            V3 ft = v3(o.fpitch[s] - pr.x - tp - kn, 0.0f, -pr.z - roll);
            o.r[i + 2] = v3_lerp(fk_ft, ft, ik);
        }
        Xf local = xf_euler(o.r[i], rel);
        wld[i] = xf_mul(&wld[b->parent], &local);
    }

    // Не проваливаться сквозь землю: таз, грудь и голова не ниже своей толщины
    {
        static const u8 body_pts[3] = { BONE_PELVIS, BONE_CHEST, BONE_HEAD };
        static const f32 body_r[3] = { 0.12f, 0.13f, 0.12f };
        f32 lift = 0.0f;
        for (u32 k = 0; k < 3; k++) {
            V3 c = wld[body_pts[k]].t;
            if (body_pts[k] == BONE_HEAD) c = xf_point(&wld[BONE_HEAD], v3(0.0f, 0.10f, 0.0f));
            if (body_pts[k] == BONE_CHEST) c = xf_point(&wld[BONE_CHEST], v3(0.0f, 0.10f, 0.0f));
            lift = fmaxf(lift, body_r[k] - c.y);
        }
        if (lift > 0.0f && clip_w > 0.0f)
            for (u32 i = 1; i < BONE_COUNT; i++) wld[i].t.y += lift;
    }

    // Оружие: в руке или в ножнах; между — рука тянется к ножнам (IK)
    Xf weapon = XF_ID;
    if (armed) {
        u8 sb;
        Xf sh_local = sheath_frame(wpn, &sb);
        // Ножны в позе привязки -> с костью носителя
        Xf bind_inv = XF_ID;
        bind_inv.t = v3_mul(joint(sb), -1.0f);
        Xf sh_rel = xf_mul(&bind_inv, &sh_local);
        Xf sheath = xf_mul(&wld[sb], &sh_rel);
        Xf g = grip_frame();
        g.t = v3_sub(g_body_sockets[SOCKET_HAND_R].pos, joint(BONE_HAND_R));
        // Поворот кисти, при котором рукоять ложится в ножны
        Xf ginv = { v3(g.x.x, g.y.x, g.z.x), v3(g.x.y, g.y.y, g.z.y), v3(g.x.z, g.y.z, g.z.z), v3(0, 0, 0) };
        Xf hand_at_sheath = xf_mul(&sheath, &ginv);
        const f32 GRAB = 0.45f;
        if (wield > 0.0f && wield < 1.0f && a->clip == CLIP_NONE) {
            Xf hand_fk = wld[BONE_HAND_R];
            V3 grip_fk = xf_point(&hand_fk, g.t);
            Xf hrot;
            V3 grip_t;
            if (wield < GRAB) {
                f32 s = smooth01(wield / GRAB);
                hrot = xf_slerp_rot(&hand_fk, &hand_at_sheath, s, v3(0, 0, 0));
                grip_t = v3_lerp(grip_fk, sheath.t, s);
            } else {
                f32 s = smooth01((wield - GRAB) / (1.0f - GRAB));
                hrot = xf_slerp_rot(&hand_at_sheath, &hand_fk, s, v3(0, 0, 0));
                // Сначала вытягивает вдоль ножен, потом выносит вперёд
                grip_t = v3_add(v3_lerp(sheath.t, grip_fk, smooth01((s - 0.15f) / 0.85f)),
                                v3_mul(sheath.y, -0.35f * fsin(PI_F * fminf(1.0f, s * 1.4f))));
            }
            V3 wrist = v3_sub(grip_t, xf_dir(&hrot, g.t));
            arm_ik(wld, R, wrist, &hrot);
        }
        // В бою оружие ведёт рамка стойки, блока или удара, правая рука идёт за ним
        f32 wf = guard * smooth01((wield - 0.6f) / 0.4f) * (1.0f - 0.7f * clampf(a->stagger, 0.0f, 1.0f));
        if (wf > 0.0f && a->clip == CLIP_NONE) {
            Xf ff = fight_frame(a, wpn, wld);
            Xf hrot = xf_mul(&ff, &ginv);
            V3 wrist = v3_sub(ff.t, xf_dir(&hrot, g.t));
            Xf hb = xf_slerp_rot(&wld[BONE_HAND_R], &hrot, wf, v3(0, 0, 0));
            arm_ik(wld, R, v3_lerp(wld[BONE_HAND_R].t, wrist, wf), &hb);
        }
        bool in_hand = wield >= GRAB && a->clip == CLIP_NONE;
        // Выбывшие роняют оружие сами (вызывающий рисует его на земле); в клипе — в руке
        if (a->clip != CLIP_NONE && wield >= GRAB) in_hand = true;
        weapon = in_hand ? xf_mul(&wld[BONE_HAND_R], &g) : sheath;
        // Двуручное: левая рука на древке
        if (in_hand && weapon_two_hand(wpn)) {
            f32 w2 = smooth01((wield - 0.75f) / 0.25f) * (1.0f - clip_w) * (1.0f - 0.6f * clampf(a->stagger, 0.0f, 1.0f));
            if (w2 > 0.0f) {
                // Хват скользит по древку к месту, куда левая рука достаёт
                f32 lu = v3_len(v3_sub(joint(BONE_FOREARM_L), joint(BONE_UPPER_ARM_L)));
                f32 lf = v3_len(v3_sub(joint(BONE_HAND_L), joint(BONE_FOREARM_L)));
                f32 r = 0.96f * (lu + lf) + 0.06f;
                V3 os = v3_sub(weapon.t, wld[BONE_UPPER_ARM_L].t);
                f32 bb = v3_dot(weapon.y, os), cc = v3_dot(os, os) - r * r, disc = bb * bb - cc;
                f32 gs = weapon_left_grip(wpn);
                if (disc < 0.0f) gs = -bb;
                else gs = clampf(gs, -bb - sqrtf(disc), -bb + sqrtf(disc));
                V3 at = v3_add(weapon.t, v3_mul(weapon.y, clampf(gs, -0.35f, 1.2f)));
                Xf flip = { v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, -1), v3(0, 0, 0) };
                Xf wr = xf_mul(&weapon, &flip);
                wr.t = v3(0, 0, 0);
                Xf hl = xf_mul(&wr, &ginv);
                Xf gl = g;
                gl.t.x = -gl.t.x;
                V3 wrist_t = v3_sub(at, xf_dir(&hl, gl.t));
                V3 wrist_fk = wld[BONE_HAND_L].t;
                Xf hrot = xf_slerp_rot(&wld[BONE_HAND_L], &hl, w2, v3(0, 0, 0));
                arm_ik(wld, L, v3_lerp(wrist_fk, wrist_t, w2), &hrot);
            }
        }
    }

    // В мир: точка, поворот, рост и ширина
    f32 sh = p->height > 0.0f ? p->height : 1.0f;
    f32 sw = p->width > 0.0f ? p->width : 1.0f;
    f32 hs, hc;
    fsincos(p->heading, &hs, &hc);
    Xf root = { v3(hc * sw, 0, -hs * sw), v3(0, sh, 0), v3(hs * sw, 0, hc * sw), p->pos };
    for (u32 i = 0; i < BONE_COUNT; i++) {
        Xf skin = wld[i];
        skin.t = v3_sub(skin.t, xf_dir(&skin, joint(i)));
        Xf w = xf_mul(&root, &skin);
        out[i] = xf_m4(&w);
    }
    Xf ww = xf_mul(&root, &weapon);
    out[CHAR_BONE_WEAPON] = xf_m4(&ww);

    // Отсечённая конечность: вершины стягиваются в точку сустава
    static const u8 limb_bones[PART_COUNT][3] = {
        [PART_ARM_L] = { BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_HAND_L },
        [PART_ARM_R] = { BONE_UPPER_ARM_R, BONE_FOREARM_R, BONE_HAND_R },
        [PART_LEG_L] = { BONE_THIGH_L, BONE_SHIN_L, BONE_FOOT_L },
        [PART_LEG_R] = { BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_R },
    };
    for (u32 part = PART_ARM_L; part < PART_COUNT; part++) {
        if (!(a->severed & (1u << part))) continue;
        u8 b0 = limb_bones[part][0];
        Xf j = wld[b0];
        Xf stub = { v3_mul(j.x, 0.001f), v3_mul(j.y, 0.001f), v3_mul(j.z, 0.001f), j.t };
        stub.t = v3_sub(stub.t, xf_dir(&stub, joint(b0)));
        Xf w = xf_mul(&root, &stub);
        for (int k = 0; k < 3; k++) out[limb_bones[part][k]] = xf_m4(&w);
    }
}
