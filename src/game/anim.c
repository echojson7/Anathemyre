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

// Стоя: ступни под тазобедренными, лодыжки на 21 см врозь; левая чуть впереди
static V3 stand_foot(u32 side)
{
    V3 a = joint(side == L ? BONE_FOOT_L : BONE_FOOT_R);
    return v3(side == L ? 0.105f : -0.105f, a.y, side == L ? 0.03f : -0.02f);
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

// Сдвиг лодыжки от плоской стопы, когда стопа наклонена на a (> 0 носок вниз) и катится по земле:
// носком вниз — через подушечку (12 см вперёд), носком вверх — через пятку (6 см назад); подошва на 7.4 см
// ниже сустава. По сеткам людей: подушечка 0.115–0.128, пятка 0.057–0.063
static void foot_roll(f32 a, f32 *dz, f32 *dy)
{
    f32 rz = a >= 0.0f ? 0.12f : -0.06f, ry = -0.074f;
    f32 s, c;
    fsincos(a, &s, &c);
    *dz = rz * (1.0f - c) - ry * s;
    *dy = rz * s + ry * (1.0f - c);
}

static void foot_track(const Gait *g, f32 q, f32 run, f32 *z, f32 *lift, f32 *pitch)
{
    f32 toe_off = lerpf(1.1f, 1.2f, run), strike0 = lerpf(-0.28f, -0.05f, run);
    if (q < g->duty) {
        f32 t = q / g->duty;
        // Шаг: пятка, плоская стопа, подъём пятки и толчок носком. Бег: на середину стопы.
        // Точка опоры стоит на земле: стопа перекатывается через пятку, потом через носок
        f32 heel = ramp(t, lerpf(0.55f, 0.45f, run), 1.0f);
        f32 strike = strike0 * (1.0f - ramp(t, 0.0f, lerpf(0.18f, 0.12f, run)));
        *pitch = strike + toe_off * heel;
        f32 dz, dy;
        foot_roll(*pitch, &dz, &dy);
        *z = lerpf(g->land, g->off, t) + dz;
        *lift = dy;
        return;
    }
    // Перенос начинается, где опора кончилась, и приходит туда, где она начнётся
    f32 off, heel_up, land, land_up;
    foot_roll(toe_off, &off, &heel_up);
    foot_roll(strike0, &land, &land_up);
    off += g->off;
    land += g->land;
    f32 u = (q - g->duty) / (1.0f - g->duty);
    // Вперёд — кривая Эрмита: носок отрывается, ещё уходя назад вместе с землёй, а перед касанием
    // стопа уже тянется назад навстречу земле — без рывка при смене опоры
    f32 gu = (g->land - g->off) * (1.0f - g->duty) / g->duty;  // скорость земли в долях переноса
    // На бегу нога после толчка сразу складывается: назад почти не уходит
    f32 m0 = -lerpf(0.3f, 0.12f, smooth01(run * 2.0f)) * gu, m1 = -0.25f * gu;
    f32 u2 = u * u, u3 = u2 * u;
    *z = (2.0f * u3 - 3.0f * u2 + 1.0f) * off + (u3 - 2.0f * u2 + u) * m0 + (-2.0f * u3 + 3.0f * u2) * land +
         (u3 - u2) * m1;
    // Вверх — кривая Безье: пятка уходит вверх сразу после отрыва (колено сгибается), затем
    // колено выносится вперёд и голень выпрямляется к постановке
    f32 a = 1.0f - u;
    *lift = a * a * a * heel_up + 3.0f * a * a * u * g->kick + 3.0f * a * u2 * g->knee + u3 * land_up;
    // Стопа: после толчка носок вниз, к середине переноса — на себя, перед касанием на пятку
    f32 push = toe_off * (1.0f - smooth01(u / lerpf(0.3f, 0.5f, run)));
    f32 heel_dn = strike0 * smooth01((u - 0.5f) / 0.45f);
    *pitch = push + heel_dn - 0.12f * fsin(PI_F * u) * (1.0f - run);
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
    f32 v = p->speed > 0.0f ? p->speed : lerpf(1.2f, 4.8f, run);
    // Размах шага: медленно — всё мельче, к быстрому шагу — шире
    f32 sf = clampf(0.4f + 0.375f * v, 0.4f, 1.2f);
    // Модель растянута вдоль в sw раз (рост на ширину): шаг в модели короче, чтобы стопа не ехала
    f32 sw = (p->width > 0.0f ? p->width : 1.0f) * (p->height > 0.0f ? p->height : 1.0f);
    Gait g;
    // Доля опоры: медленный шаг 0.63, быстрый 0.59, трусца 0.40, бег 0.29
    f32 duty_walk = clampf(0.66f - 0.045f * v, 0.59f, 0.64f);
    g.duty = run < 0.5f ? lerpf(duty_walk, 0.40f, run * 2.0f) : lerpf(0.40f, 0.29f, (run - 0.5f) * 2.0f);
    f32 L_ = char_stride(v) / sw;
    f32 travel = g.duty * L_;
    f32 land_frac = lerpf(0.35f, 0.27f, bounce);
    g.land = land_frac * travel;
    g.off = g.land - travel;
    g.kick = lerpf(0.20f, lerpf(0.30f, 0.55f, ramp(run, 0.5f, 1.0f)), bounce);
    g.knee = lerpf(0.10f, lerpf(0.26f, 0.38f, ramp(run, 0.5f, 1.0f)), bounce);
    g.reach = lerpf(0.06f, 0.22f, bounce);

    f32 runk = ramp(run, 0.5f, 1.0f);
    // Таз: на шаге выше над опорной ногой, на беге ниже всего в опоре и выше в полёте
    f32 q2 = 2.0f * ph - floorf(2.0f * ph);
    f32 bob_amp = lerpf(0.020f * sf, lerpf(-0.042f, -0.036f, runk), bounce);
    f32 bob = bob_amp * fcos(tau * (q2 - g.duty)) + lerpf(-0.030f - 0.010f * v, -0.060f, bounce);
    f32 sway = lerpf(0.026f, 0.010f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    f32 pyaw = -lerpf(0.08f * sf, 0.12f, bounce) * fcos(tau * ph);
    f32 proll = lerpf(0.06f, 0.035f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    // Наклон всего корпуса растёт со скоростью: шагом едва заметен, на бегу ~10°.
    // Таз берёт большую часть, позвоночник и грудь — остальное
    f32 lean = 0.008f + 0.026f * v;
    f32 ptilt = lean * 0.6f;
    o->off = v3_add(o->off, v3_mul(v3(sway, bob, 0.0f), w));
    o->r[BONE_PELVIS] = v3_add(o->r[BONE_PELVIS], v3_mul(v3(ptilt, pyaw, proll), w));

    // Корпус: наклон вперёд, плечи крутятся навстречу тазу, грудь гасит крен таза и чуть
    // клонится к опорной ноге. Наклон дышит дважды за цикл: на шаге клюёт вперёд после касания
    // пятки, на беге глубже всего в середине опоры
    f32 sh_yaw = lerpf(0.12f * sf, 0.20f, bounce) * fcos(tau * ph);
    f32 nod = lerpf(0.018f, 0.035f, bounce) * fcos(tau * (q2 - lerpf(0.08f, g.duty, bounce)));
    f32 side_lean = lerpf(0.025f, 0.01f, bounce) * fcos(tau * (ph - 0.5f * g.duty));
    o->r[BONE_SPINE] = v3_add(o->r[BONE_SPINE], v3_mul(v3(lean * 0.25f + nod * 0.6f, -pyaw * 0.6f + sh_yaw * 0.5f,
                                                          -proll * 0.6f - side_lean * 0.5f), w));
    o->r[BONE_CHEST] = v3_add(o->r[BONE_CHEST], v3_mul(v3(lean * 0.15f + nod * 0.4f, -pyaw * 0.4f + sh_yaw * 0.5f,
                                                          -proll * 0.4f - side_lean * 0.5f), w));
    lo->chest_yaw = sh_yaw * w;
    // Голова держит горизонт
    o->r[BONE_HEAD].x -= (lean * 0.85f + nod) * w;
    o->r[BONE_HEAD].z += (proll * 0.3f + side_lean) * w;

    // Руки против ног, мах от плеча, назад дальше, чем вперёд. Шагом локоть почти прям и сгибается,
    // когда рука идёт вперёд; на бегу локоть согнут, сзади раскрывается, спереди кисть идёт к груди
    f32 arm_ph = tau * (ph - 0.04f);
    // Мах руки растёт со скоростью шага: на медленном шаге руки почти висят
    f32 S = lerpf(fminf(0.10f + 0.13f * v, 0.36f), lerpf(0.46f, 0.54f, runk), bounce);
    f32 back = lerpf(0.0f, 0.08f, bounce);
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
        // Ширина шага: шагом лодыжки на 13 см врозь, на бегу ставятся почти на одну линию (6 см).
        // В переносе стопа чуть огибает опорную ногу снаружи
        f32 sx = (side == L ? 1.0f : -1.0f) * lerpf(0.065f, 0.03f, bounce);
        if (q >= g.duty) sx *= 1.0f + 0.5f * fsin(PI_F * (q - g.duty) / (1.0f - g.duty));
        V3 walkp = v3(sx + md_s * z, aj.y + lift, md_c * z);
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
// Ноги углами (ik = 0) или стопами на земле (ik = 1).
// Темп по тяжести: таз нигде не опускается быстрее свободного падения, обмякшее тело падает с ним
// почти вровень, живой притормаживает ногами и руками. Подъём через силу — вдвое дольше падения
typedef struct BodyK {
    f32 t;
    f32 py, pz, px, ptx, pty, ptz;
    f32 sp, ch, hd, tw;
    ArmK al, ar;
    f32 lt[4], rt[4];        // бедро вперёд(<0), бедро в сторону, колено, стопа
    V3 fl, fr;               // цели стоп при ik
    f32 ik;
} BodyK;

#define ARM_HANG { 0.05f, 0.0f, 0.12f, -0.20f, 0.0f, 0.0f }

// Стоит прямо
#define K_STAND(t) { t, 0.97f, 0.0f, 0.0f, 0.03f, 0.0f, 0.0f, 0.05f, 0.02f, 0.0f, 0.0f, \
    { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.08f, -0.25f, 0.0f, 0.0f }, \
    { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f }
// Лежит на спине: руки на земле вдоль тела, левое колено приподнято
#define K_LIE_UP(t) { t, 0.13f, -0.62f, 0.0f, -1.53f, 0.0f, 0.0f, 0.02f, 0.0f, 0.15f, 0.0f, \
    { 0.25f, -0.1f, 0.45f, -0.35f, 0.2f, 0.0f }, { 0.20f, -0.1f, 0.55f, -0.20f, 0.2f, 0.0f }, \
    { -0.75f, 0.10f, 1.30f, 0.20f }, { -0.15f, 0.12f, 0.25f, 0.35f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f }
// Лежит ничком, левая рука у головы, правая вдоль тела, ноги вытянуты
#define K_LIE_DOWN(t) { t, 0.15f, 0.62f, 0.0f, 1.50f, 0.0f, 0.0f, 0.02f, 0.0f, -0.25f, 0.0f, \
    { -2.3f, -0.2f, 0.6f, -1.5f, 0.0f, 0.0f }, { 0.15f, 0.0f, 0.35f, -0.2f, 0.0f, 0.0f }, \
    { 0.0f, 0.16f, 0.10f, 0.9f }, { -0.05f, 0.08f, 0.10f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f }
// Лежит на левом боку (влево — к +X): верхняя нога согнута вперёд, руки перед собой
#define K_LIE_SIDE(t, x) { t, 0.17f, -0.05f, x, 0.05f, 0.0f, -1.45f, 0.10f, 0.05f, 0.25f, 0.0f, \
    { -1.20f, 0.0f, 0.15f, -0.45f, 0.0f, 0.0f }, { -0.70f, 0.35f, 0.10f, -0.60f, 0.0f, 0.0f }, \
    { -0.30f, 0.0f, 0.45f, 0.30f }, { -0.85f, 0.05f, 1.00f, 0.30f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f }

static const BodyK K_LIE_UP_K = K_LIE_UP(1.0f);
static const BodyK K_LIE_DOWN_K = K_LIE_DOWN(1.0f);
static const BodyK K_LIE_SIDE_K = K_LIE_SIDE(1.0f, 0.0f);

// Сбит ударом спереди, 1.1 с: отшатывается, руки вперёд на уровне плеч, второй шаг назад, садится,
// руки назад ловят землю, перекатывается на лопатки, затылок последним
static const BodyK K_FALL[] = {
    { 0.00f, 0.97f, -0.03f, 0.0f, -0.08f, 0.0f, 0.0f, -0.20f, -0.10f, 0.30f, 0.0f,
      { -0.6f, 0.0f, 0.35f, -0.5f, 0.0f, 0.0f }, { -0.5f, 0.0f, 0.35f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.08f }, { -0.115f, 0.09f, -0.05f }, 1.0f },
    { 0.18f, 0.92f, -0.20f, 0.0f, -0.20f, 0.08f, 0.0f, -0.22f, -0.10f, 0.30f, 0.0f,
      { -0.9f, -0.1f, 0.70f, -0.5f, 0.0f, 0.0f }, { -0.8f, -0.1f, 0.70f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.02f }, { -0.13f, 0.10f, -0.45f }, 1.0f },
    { 0.38f, 0.80f, -0.40f, 0.0f, -0.30f, 0.10f, 0.0f, -0.15f, -0.05f, 0.35f, 0.0f,
      { -0.6f, -0.1f, 0.80f, -0.6f, 0.0f, 0.0f }, { -0.5f, -0.1f, 0.80f, -0.7f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.15f, 0.09f, -0.10f }, { -0.14f, 0.10f, -0.40f }, 1.0f },
    { 0.50f, 0.55f, -0.48f, 0.0f, -0.35f, 0.08f, 0.0f, -0.05f, 0.05f, 0.40f, 0.0f,
      { 0.20f, -0.1f, 0.60f, -0.3f, -0.3f, 0.0f }, { 0.25f, -0.1f, 0.60f, -0.3f, -0.3f, 0.0f },
      { 0 }, { 0 }, { 0.15f, 0.09f, -0.05f }, { -0.14f, 0.10f, -0.30f }, 1.0f },
    { 0.64f, 0.20f, -0.55f, 0.0f, -0.55f, 0.05f, 0.0f, -0.05f, 0.10f, 0.45f, 0.0f,
      { 0.55f, -0.1f, 0.40f, -0.2f, -0.6f, 0.0f }, { 0.60f, -0.1f, 0.40f, -0.2f, -0.6f, 0.0f },
      { -1.45f, 0.20f, 1.45f, 0.0f }, { -1.30f, 0.15f, 1.25f, 0.0f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.77f, 0.15f, -0.60f, 0.0f, -1.00f, 0.0f, 0.0f, -0.05f, 0.05f, 0.50f, 0.0f,
      { 0.80f, -0.1f, 0.50f, -0.3f, -0.4f, 0.0f }, { 0.85f, -0.1f, 0.50f, -0.3f, -0.4f, 0.0f },
      { -1.35f, 0.15f, 1.40f, 0.1f }, { -0.90f, 0.12f, 0.80f, 0.2f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.91f, 0.13f, -0.62f, 0.0f, -1.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.45f, 0.0f,
      { 0.40f, -0.1f, 0.60f, -0.3f, 0.0f, 0.0f }, { 0.40f, -0.1f, 0.65f, -0.2f, 0.0f, 0.0f },
      { -0.95f, 0.12f, 1.35f, 0.2f }, { -0.35f, 0.12f, 0.40f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_UP(1.0f),
};

// Сбит ударом в спину, 1.1 с: корпус бросает вперёд, шаг-другой, падает на колени, руки ловят землю,
// ложится грудью
static const BodyK K_CHAR_FALL_FRONT[] = {
    { 0.00f, 0.97f, 0.03f, 0.0f, 0.10f, 0.0f, 0.0f, 0.15f, 0.05f, -0.20f, 0.0f,
      { 0.30f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f }, { 0.30f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.18f, 0.92f, 0.20f, 0.0f, 0.25f, 0.0f, 0.0f, 0.20f, 0.05f, -0.10f, 0.0f,
      { -0.60f, 0.0f, 0.30f, -0.4f, 0.0f, 0.0f }, { -0.50f, 0.0f, 0.30f, -0.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, -0.05f }, { -0.13f, 0.10f, 0.45f }, 1.0f },
    { 0.38f, 0.66f, 0.35f, 0.0f, 0.45f, 0.0f, 0.0f, 0.20f, 0.10f, -0.05f, 0.0f,
      { -1.20f, 0.0f, 0.30f, -0.2f, 0.0f, 0.0f }, { -1.10f, 0.0f, 0.30f, -0.2f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.10f, -0.10f }, { -0.13f, 0.09f, 0.45f }, 1.0f },
    { 0.55f, 0.47f, 0.40f, 0.0f, 0.75f, 0.0f, 0.0f, 0.15f, 0.10f, -0.15f, 0.0f,
      { -1.45f, 0.0f, 0.30f, -0.2f, -0.6f, 0.0f }, { -1.40f, 0.0f, 0.30f, -0.2f, -0.6f, 0.0f },
      { -0.70f, 0.08f, 1.57f, 0.6f }, { -0.85f, 0.08f, 1.45f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.72f, 0.36f, 0.55f, 0.0f, 1.10f, 0.0f, 0.0f, 0.10f, 0.05f, -0.30f, 0.0f,
      { -1.60f, 0.0f, 0.30f, -0.9f, -0.8f, 0.0f }, { -1.55f, 0.0f, 0.30f, -0.9f, -0.8f, 0.0f },
      { -0.50f, 0.10f, 0.97f, 0.7f }, { -0.55f, 0.08f, 0.95f, 0.7f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.86f, 0.20f, 0.62f, 0.0f, 1.40f, 0.0f, 0.0f, 0.05f, 0.0f, -0.35f, 0.0f,
      { -1.30f, 0.10f, 0.50f, -1.6f, -0.6f, 0.0f }, { -1.20f, 0.10f, 0.50f, -1.6f, -0.6f, 0.0f },
      { -0.20f, 0.12f, 0.40f, 0.9f }, { -0.25f, 0.08f, 0.35f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_DOWN(1.0f),
};

// Сбит ударом сбоку (валится влево, к +X; вправо — зеркало), 1.1 с: левая нога отступает, подламывается,
// левая рука ловит землю, садится на бедро, перекатывается на спину
static const BodyK K_FALL_SIDE[] = {
    { 0.00f, 0.97f, 0.0f, 0.02f, 0.03f, 0.0f, -0.10f, 0.05f, 0.0f, 0.10f, 0.0f,
      { -0.1f, 0.0f, 0.40f, -0.3f, 0.0f, 0.0f }, { -0.1f, 0.0f, 0.40f, -0.3f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.20f, 0.90f, 0.0f, 0.10f, 0.05f, 0.0f, -0.25f, 0.05f, 0.0f, 0.15f, 0.0f,
      { -0.2f, 0.0f, 0.60f, -0.3f, 0.0f, 0.0f }, { -0.3f, 0.0f, 0.90f, -0.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.32f, 0.10f, 0.0f }, { -0.10f, 0.09f, -0.02f }, 1.0f },
    { 0.40f, 0.64f, -0.02f, 0.20f, 0.10f, 0.0f, -0.45f, 0.10f, 0.05f, 0.20f, 0.0f,
      { -0.3f, 0.0f, 0.55f, -0.2f, -0.4f, 0.0f }, { -0.4f, 0.0f, 0.90f, -0.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.35f, 0.09f, 0.05f }, { -0.05f, 0.10f, -0.05f }, 1.0f },
    { 0.58f, 0.22f, -0.10f, 0.25f, -0.20f, 0.0f, -1.00f, 0.0f, 0.05f, 0.30f, 0.0f,
      { 0.10f, 0.0f, 0.70f, -0.1f, -0.6f, 0.0f }, { -0.6f, 0.2f, 0.60f, -0.5f, 0.0f, 0.0f },
      { -0.70f, 0.10f, 1.10f, 0.2f }, { -0.50f, 0.10f, 0.70f, 0.2f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.16f, -0.35f, 0.15f, -0.90f, 0.0f, -0.70f, 0.0f, 0.0f, 0.40f, 0.0f,
      { 0.40f, 0.0f, 0.60f, -0.2f, 0.0f, 0.0f }, { -0.2f, 0.1f, 0.60f, -0.4f, 0.0f, 0.0f },
      { -0.85f, 0.12f, 1.25f, 0.2f }, { -0.40f, 0.12f, 0.50f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_UP(1.0f),
};

// Убит ударом спереди, 0.8 с: тело обмякает сразу, колени подламываются, валится навзничь,
// руки разлетаются по инерции, голова отстаёт
static const BodyK K_DIE_BACK[] = {
    { 0.00f, 0.97f, -0.02f, 0.0f, -0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.30f, 0.72f, -0.10f, 0.0f, -0.25f, 0.0f, 0.0f, -0.10f, 0.0f, 0.45f, 0.0f,
      { -0.25f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f }, { -0.20f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.08f }, { -0.12f, 0.09f, 0.02f }, 1.0f },
    { 0.55f, 0.22f, -0.35f, 0.0f, -0.70f, 0.0f, 0.0f, -0.05f, 0.0f, 0.50f, 0.0f,
      { -0.75f, 0.0f, 0.35f, -0.3f, 0.0f, 0.0f }, { -0.65f, 0.0f, 0.40f, -0.2f, 0.0f, 0.0f },
      { -1.30f, 0.18f, 1.60f, 0.2f }, { -1.20f, 0.14f, 1.50f, 0.2f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.14f, -0.55f, 0.0f, -1.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.0f,
      { -0.3f, 0.0f, 1.00f, -0.2f, 0.0f, 0.0f }, { -0.3f, 0.0f, 1.05f, -0.2f, 0.0f, 0.0f },
      { -0.40f, 0.15f, 0.70f, 0.3f }, { -0.30f, 0.12f, 0.50f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 1.00f, 0.13f, -0.62f, 0.0f, -1.53f, 0.0f, 0.0f, 0.02f, 0.0f, 0.10f, 0.0f,
      { 0.10f, -0.1f, 0.90f, -0.30f, 0.2f, 0.0f }, { 0.10f, -0.1f, 1.00f, -0.20f, 0.2f, 0.0f },
      { -0.25f, 0.14f, 0.40f, 0.3f }, { -0.15f, 0.12f, 0.25f, 0.35f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
};

// Убит ударом в спину, 0.8 с: колени подламываются вперёд, падает на колени и лицом в землю, руками не ловит
static const BodyK K_DIE_FRONT[] = {
    { 0.00f, 0.97f, 0.02f, 0.0f, 0.10f, 0.0f, 0.0f, 0.05f, 0.0f, -0.15f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.30f, 0.70f, 0.05f, 0.0f, 0.30f, 0.0f, 0.0f, 0.25f, 0.10f, 0.35f, 0.0f,
      { 0.10f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.0f }, { -0.12f, 0.09f, -0.06f }, 1.0f },
    { 0.52f, 0.48f, 0.15f, 0.0f, 0.55f, 0.0f, 0.0f, 0.25f, 0.10f, 0.40f, 0.0f,
      { -0.30f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f }, { -0.30f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f },
      { -0.55f, 0.08f, 1.60f, 0.6f }, { -0.55f, 0.08f, 1.55f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.25f, 0.45f, 0.0f, 1.20f, 0.0f, 0.0f, 0.10f, 0.0f, 0.20f, 0.0f,
      { 0.30f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f }, { 0.30f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f },
      { -0.20f, 0.10f, 0.57f, 0.8f }, { -0.20f, 0.08f, 0.50f, 0.8f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 1.00f, 0.15f, 0.62f, 0.0f, 1.50f, 0.0f, 0.0f, 0.02f, 0.0f, -0.25f, 0.0f,
      { 0.30f, 0.0f, 0.25f, -0.2f, 0.0f, 0.0f }, { 0.25f, 0.0f, 0.30f, -0.2f, 0.0f, 0.0f },
      { 0.0f, 0.14f, 0.10f, 0.9f }, { 0.0f, 0.08f, 0.10f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
};

// Убит ударом сбоку (влево, к +X), 0.8 с: ноги подламываются, падает на бедро и плечо, остаётся на боку
static const BodyK K_DIE_SIDE[] = {
    { 0.00f, 0.97f, 0.0f, 0.02f, 0.03f, 0.0f, -0.08f, 0.0f, 0.0f, 0.20f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.30f, 0.70f, 0.0f, 0.06f, 0.10f, 0.0f, -0.20f, 0.10f, 0.0f, 0.35f, 0.0f,
      { -0.1f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f }, { -0.2f, 0.0f, 0.25f, -0.3f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.55f, 0.30f, -0.02f, 0.18f, 0.05f, 0.0f, -0.65f, 0.10f, 0.05f, 0.35f, 0.0f,
      { -0.3f, 0.0f, 0.15f, -0.2f, 0.0f, 0.0f }, { -0.4f, 0.1f, 0.40f, -0.3f, 0.0f, 0.0f },
      { -1.00f, 0.05f, 1.40f, 0.3f }, { -0.90f, 0.05f, 1.30f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.18f, -0.04f, 0.24f, 0.05f, 0.0f, -1.25f, 0.10f, 0.05f, 0.30f, 0.0f,
      { -0.9f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f }, { -0.6f, 0.3f, 0.40f, -0.5f, 0.0f, 0.0f },
      { -0.50f, 0.02f, 0.80f, 0.3f }, { -0.85f, 0.05f, 1.10f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_SIDE(1.0f, 0.24f),
};

// Истёк кровью стоя, 2 с: шатается, опускается на правое колено, потом на оба, заваливается на левый бок
static const BodyK K_BLEED[] = {
    { 0.00f, 0.96f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.05f, 0.25f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.20f, 0.90f, 0.0f, -0.02f, 0.08f, 0.0f, 0.06f, 0.25f, 0.10f, 0.40f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.12f, 0.09f, 0.05f }, { -0.12f, 0.09f, -0.03f }, 1.0f },
    { 0.40f, 0.56f, 0.02f, 0.0f, 0.15f, 0.0f, 0.0f, 0.25f, 0.10f, 0.45f, 0.0f,
      { -0.9f, 0.3f, 0.20f, -0.8f, 0.0f, 0.0f }, ARM_HANG,
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.28f }, { -0.12f, 0.12f, -0.38f }, 1.0f },
    { 0.60f, 0.48f, -0.05f, 0.0f, 0.30f, 0.0f, 0.0f, 0.35f, 0.15f, 0.55f, 0.0f,
      { 0.10f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f },
      { -0.35f, 0.06f, 1.65f, 0.6f }, { -0.35f, 0.06f, 1.65f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.80f, 0.30f, -0.05f, 0.12f, 0.30f, 0.0f, -0.70f, 0.30f, 0.10f, 0.45f, 0.0f,
      { -0.5f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f }, { 0.10f, 0.0f, 0.05f, -0.3f, 0.0f, 0.0f },
      { -0.60f, 0.04f, 1.50f, 0.5f }, { -0.50f, 0.04f, 1.40f, 0.5f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.92f, 0.19f, -0.05f, 0.20f, 0.15f, 0.0f, -1.20f, 0.20f, 0.05f, 0.35f, 0.0f,
      { -1.0f, 0.0f, 0.25f, -0.4f, 0.0f, 0.0f }, { -0.5f, 0.3f, 0.20f, -0.5f, 0.0f, 0.0f },
      { -0.40f, 0.0f, 0.70f, 0.3f }, { -0.80f, 0.05f, 1.10f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_SIDE(1.0f, 0.20f),
};

// Без сознания стоя, 1.1 с: колени подламываются, оседает на колени, корпус валится вперёд, руки не ловят
static const BodyK K_COLLAPSE_DOWN[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.10f, 0.45f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.115f, 0.09f, 0.03f }, { -0.115f, 0.09f, -0.02f }, 1.0f },
    { 0.36f, 0.58f, 0.0f, 0.0f, 0.15f, 0.0f, 0.0f, 0.30f, 0.15f, 0.60f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.115f, 0.09f, 0.0f }, { -0.115f, 0.09f, -0.04f }, 1.0f },
    { 0.50f, 0.48f, 0.0f, 0.0f, 0.30f, 0.0f, 0.0f, 0.35f, 0.15f, 0.60f, 0.0f,
      { -0.10f, 0.0f, 0.15f, -0.2f, 0.0f, 0.0f }, { -0.10f, 0.0f, 0.15f, -0.2f, 0.0f, 0.0f },
      { -0.35f, 0.05f, 1.65f, 0.6f }, { -0.35f, 0.05f, 1.65f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.75f, 0.30f, 0.30f, 0.0f, 1.00f, 0.0f, 0.0f, 0.20f, 0.10f, 0.30f, 0.0f,
      { -0.50f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f }, { -0.45f, 0.0f, 0.20f, -0.2f, 0.0f, 0.0f },
      { -0.20f, 0.08f, 0.77f, 0.8f }, { -0.20f, 0.06f, 0.70f, 0.8f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 1.00f, 0.15f, 0.62f, 0.0f, 1.50f, 0.0f, 0.0f, 0.02f, 0.0f, -0.25f, 0.0f,
      { 0.20f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f }, { 0.25f, 0.0f, 0.30f, -0.2f, 0.0f, 0.0f },
      { 0.0f, 0.14f, 0.15f, 0.9f }, { 0.0f, 0.08f, 0.10f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
};

// То же навзничь: колени подламываются, садится, корпус падает назад
static const BodyK K_COLLAPSE_UP[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.10f, 0.45f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.115f, 0.09f, 0.03f }, { -0.115f, 0.09f, -0.02f }, 1.0f },
    { 0.36f, 0.62f, -0.08f, 0.0f, 0.0f, 0.0f, 0.0f, 0.25f, 0.10f, 0.55f, 0.0f,
      ARM_HANG, ARM_HANG, { 0 }, { 0 }, { 0.115f, 0.09f, 0.08f }, { -0.115f, 0.09f, 0.02f }, 1.0f },
    { 0.55f, 0.22f, -0.25f, 0.0f, -0.40f, 0.1f, 0.0f, 0.15f, 0.05f, 0.55f, 0.0f,
      { -0.2f, 0.0f, 0.50f, -0.3f, 0.0f, 0.0f }, { -0.1f, 0.0f, 0.45f, -0.3f, 0.0f, 0.0f },
      { -1.30f, 0.15f, 1.55f, 0.2f }, { -1.20f, 0.12f, 1.45f, 0.2f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.78f, 0.15f, -0.50f, 0.0f, -1.30f, 0.0f, 0.0f, 0.05f, 0.0f, 0.45f, 0.0f,
      { 0.0f, 0.0f, 0.80f, -0.3f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.75f, -0.3f, 0.0f, 0.0f },
      { -1.00f, 0.15f, 1.10f, 0.2f }, { -0.70f, 0.10f, 0.80f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 1.00f, 0.13f, -0.62f, 0.0f, -1.53f, 0.0f, 0.0f, 0.02f, 0.0f, 0.10f, 0.0f,
      { 0.15f, -0.1f, 0.70f, -0.30f, 0.2f, 0.0f }, { 0.15f, -0.1f, 0.75f, -0.20f, 0.2f, 0.0f },
      { -0.45f, 0.14f, 0.70f, 0.3f }, { -0.20f, 0.12f, 0.30f, 0.35f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
};

// Ноги не держат, в сознании, 1.1 с: колени подламываются, ловит себя руками, ложится ничком (потом ползёт)
static const BodyK K_SINK[] = {
    { 0.00f, 0.97f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.15f, 0.10f, 0.40f, 0.0f,
      { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f }, { 0.05f, 0.0f, 0.10f, -0.15f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.03f }, { -0.115f, 0.09f, -0.02f }, 1.0f },
    { 0.30f, 0.62f, -0.03f, 0.0f, 0.12f, 0.0f, 0.0f, 0.25f, 0.15f, 0.45f, 0.0f,
      { -0.30f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f }, { -0.30f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.115f, 0.09f, 0.0f }, { -0.115f, 0.09f, -0.04f }, 1.0f },
    { 0.45f, 0.48f, -0.05f, 0.0f, 0.15f, 0.0f, 0.0f, 0.30f, 0.20f, 0.45f, 0.0f,
      { -0.40f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f }, { -0.40f, 0.0f, 0.20f, -0.3f, 0.0f, 0.0f },
      { -0.15f, 0.05f, 1.75f, 0.6f }, { -0.10f, 0.05f, 1.70f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.72f, 0.40f, 0.20f, 0.0f, 0.95f, 0.0f, 0.0f, 0.30f, 0.15f, 0.30f, 0.0f,
      { -0.90f, 0.0f, 0.40f, -0.4f, 0.0f, 0.0f }, { -0.80f, 0.0f, 0.40f, -0.4f, 0.0f, 0.0f },
      { -0.45f, 0.05f, 1.10f, 0.6f }, { -0.45f, 0.05f, 1.05f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_DOWN(1.0f),
};

// Встаёт со спины, 2.2 с: садится, перекатывается на бок с опорой на левую руку, встаёт на колено,
// с упором рукой о колено — на ноги. Правая рука свободна: в ней может быть оружие
static const BodyK K_GETUP_UP[] = {
    K_LIE_UP(0.0f),
    { 0.25f, 0.17f, -0.45f, 0.0f, -0.55f, 0.0f, 0.0f, 0.45f, 0.20f, 0.20f, 0.0f,
      { 0.75f, 0.0f, 0.30f, -0.25f, -0.8f, 0.0f }, { 0.20f, 0.0f, 0.40f, -0.60f, 0.0f, 0.0f },
      { -1.30f, 0.20f, 2.00f, 0.3f }, { -1.10f, 0.15f, 1.80f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.48f, 0.40f, -0.20f, -0.05f, 0.10f, -0.35f, 0.10f, 0.45f, 0.10f, -0.10f, 0.0f,
      { 0.30f, 0.0f, 0.55f, -0.10f, -1.0f, 0.0f }, { -0.50f, 0.20f, 0.30f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.16f, 0.09f, 0.10f }, { -0.10f, 0.13f, -0.48f }, 1.0f },
    { 0.68f, 0.56f, -0.05f, 0.0f, 0.15f, -0.10f, 0.0f, 0.30f, 0.10f, -0.15f, 0.0f,
      { -0.70f, 0.45f, 0.05f, -0.35f, 0.0f, 0.0f }, { -0.50f, 0.20f, 0.20f, -0.6f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.14f, 0.09f, 0.30f }, { -0.12f, 0.13f, -0.30f }, 1.0f },
    { 0.86f, 0.86f, 0.05f, 0.0f, 0.12f, -0.05f, 0.0f, 0.20f, 0.05f, -0.10f, 0.0f,
      { -0.50f, 0.35f, 0.05f, -0.40f, 0.0f, 0.0f }, { -0.25f, 0.10f, 0.15f, -0.5f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.22f }, { -0.12f, 0.09f, -0.12f }, 1.0f },
    K_STAND(1.0f),
};

// Встаёт ничком, 2 с: отжимается на руках, подтягивает колени, встаёт с колена
static const BodyK K_GETUP_DOWN[] = {
    K_LIE_DOWN(0.0f),
    { 0.25f, 0.22f, 0.55f, 0.0f, 1.20f, 0.0f, 0.0f, -0.25f, -0.10f, -0.35f, 0.0f,
      { -1.35f, 0.15f, 0.25f, -0.10f, -1.0f, 0.0f }, { -1.35f, 0.15f, 0.25f, -0.10f, -1.0f, 0.0f },
      { 0.05f, 0.10f, 0.15f, 0.9f }, { 0.05f, 0.10f, 0.15f, 0.9f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.50f, 0.50f, 0.35f, 0.0f, 0.85f, 0.0f, 0.0f, 0.05f, 0.0f, -0.55f, 0.0f,
      { -1.00f, 0.10f, 0.20f, -0.10f, -1.0f, 0.0f }, { -1.00f, 0.10f, 0.20f, -0.10f, -1.0f, 0.0f },
      { -0.95f, 0.10f, 1.85f, 0.7f }, { -0.95f, 0.10f, 1.85f, 0.7f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.72f, 0.56f, -0.05f, 0.0f, 0.20f, -0.10f, 0.0f, 0.30f, 0.10f, -0.20f, 0.0f,
      { -0.70f, 0.45f, 0.05f, -0.35f, 0.0f, 0.0f }, { -0.30f, 0.10f, 0.20f, -0.4f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.14f, 0.09f, 0.30f }, { -0.12f, 0.13f, -0.30f }, 1.0f },
    { 0.88f, 0.86f, 0.05f, 0.0f, 0.12f, -0.05f, 0.0f, 0.20f, 0.05f, -0.10f, 0.0f,
      { -0.50f, 0.35f, 0.05f, -0.40f, 0.0f, 0.0f }, { -0.25f, 0.10f, 0.15f, -0.5f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, 0.22f }, { -0.12f, 0.09f, -0.12f }, 1.0f },
    K_STAND(1.0f),
};

// Перевернуться со спины на живот, 1.2 с (ноги не держат, дальше ползёт): садится, опора на руку, ничком
static const BodyK K_TURN_OVER[] = {
    K_LIE_UP(0.0f),
    { 0.30f, 0.17f, -0.45f, 0.0f, -0.55f, 0.0f, 0.0f, 0.45f, 0.20f, 0.20f, 0.0f,
      { 0.75f, 0.0f, 0.30f, -0.25f, -0.8f, 0.0f }, { 0.20f, 0.0f, 0.40f, -0.60f, 0.0f, 0.0f },
      { -1.10f, 0.20f, 0.60f, 0.3f }, { -1.00f, 0.15f, 0.45f, 0.3f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    { 0.62f, 0.36f, -0.05f, -0.05f, 0.60f, -0.35f, 0.10f, 0.30f, 0.10f, -0.20f, 0.0f,
      { -0.60f, 0.0f, 0.40f, -0.30f, -0.8f, 0.0f }, { -0.90f, 0.10f, 0.30f, -0.3f, -0.8f, 0.0f },
      { -0.40f, 0.10f, 1.60f, 0.6f }, { -0.30f, 0.10f, 1.50f, 0.6f }, { 0, 0, 0 }, { 0, 0, 0 }, 0.0f },
    K_LIE_DOWN(1.0f),
};

// Поднимает выроненное оружие, 0.9 с: шаг правой, присед, правая рука к земле впереди справа, встаёт
static const BodyK K_PICKUP[] = {
    K_STAND(0.0f),
    { 0.45f, 0.60f, 0.05f, 0.0f, 0.35f, 0.0f, 0.0f, 0.35f, 0.20f, 0.30f, 0.0f,
      { -0.70f, 0.40f, 0.05f, -0.40f, 0.0f, 0.0f }, { -0.95f, 0.05f, 0.25f, -0.10f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, -0.10f }, { -0.14f, 0.09f, 0.25f }, 1.0f },
    { 0.60f, 0.58f, 0.05f, 0.0f, 0.38f, 0.0f, 0.0f, 0.35f, 0.20f, 0.35f, 0.0f,
      { -0.70f, 0.40f, 0.05f, -0.40f, 0.0f, 0.0f }, { -1.00f, 0.05f, 0.25f, -0.15f, 0.0f, 0.0f },
      { 0 }, { 0 }, { 0.13f, 0.09f, -0.10f }, { -0.14f, 0.09f, 0.25f }, 1.0f },
    K_STAND(1.0f),
};

// Сдался, 1.6 с: опускается на одно колено, потом на оба, руки поднимает пустыми ладонями
static const BodyK K_KNEEL[] = {
    K_STAND(0.0f),
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

// Зеркало позы слева направо: клипы, сделанные в одну сторону, играются и в другую
static void pose_mirror(Pose *p)
{
    static const u8 pairs[][2] = { { BONE_CLAVICLE_L, BONE_CLAVICLE_R }, { BONE_UPPER_ARM_L, BONE_UPPER_ARM_R },
                                   { BONE_FOREARM_L, BONE_FOREARM_R },   { BONE_HAND_L, BONE_HAND_R },
                                   { BONE_THIGH_L, BONE_THIGH_R },       { BONE_SHIN_L, BONE_SHIN_R },
                                   { BONE_FOOT_L, BONE_FOOT_R } };
    for (u32 k = 0; k < ARRAY_COUNT(pairs); k++) {
        V3 t = p->r[pairs[k][0]];
        p->r[pairs[k][0]] = p->r[pairs[k][1]];
        p->r[pairs[k][1]] = t;
    }
    for (u32 b = 0; b < BONE_COUNT; b++) p->r[b] = v3(p->r[b].x, -p->r[b].y, -p->r[b].z);
    p->off.x = -p->off.x;
    V3 f = p->foot[L];
    p->foot[L] = v3(-p->foot[R].x, p->foot[R].y, p->foot[R].z);
    p->foot[R] = v3(-f.x, f.y, f.z);
    f32 q = p->fpitch[L];
    p->fpitch[L] = p->fpitch[R];
    p->fpitch[R] = q;
    q = p->plant[L];
    p->plant[L] = p->plant[R];
    p->plant[R] = q;
}

// Лежит: на спине, ничком или на боку (side ±1, -1 — правый). Живой дышит, раненый шевелится.
// Мёртвый не дышит; still — сколько он уже мёртв, с: первые секунды — последние слабые движения.
// Мёртвые лежат каждый по-своему (по seed): зеркало, руки и ноги, поворот головы
static void lie_pose(Pose *o, bool prone, i32 side, f32 t, f32 limp, bool dead, f32 still, f32 seed)
{
    body_key(o, side ? &K_LIE_SIDE_K : prone ? &K_LIE_DOWN_K : &K_LIE_UP_K);
    u32 sd = (u32)(seed * 1000.0f);
    if (dead) {
        f32 h0 = hashf(sd * 13u + 1u) * 2.0f - 1.0f, h1 = hashf(sd * 13u + 2u) * 2.0f - 1.0f;
        f32 h2 = hashf(sd * 13u + 3u) * 2.0f - 1.0f, h3 = hashf(sd * 13u + 4u) * 2.0f - 1.0f;
        f32 h4 = hashf(sd * 13u + 6u) * 2.0f - 1.0f;
        if (!side && !prone) {
            // Навзничь: руки раскинуты, одна бывает на животе; одна нога бывает согнута и завалена наружу
            o->r[BONE_THIGH_L].x = -0.25f + 0.20f * h0;
            o->r[BONE_SHIN_L].x = 0.40f + 0.25f * h0;
            if (h0 > 0.3f) {
                // колено завалено наружу (поворот бедра вокруг оси тела)
                o->r[BONE_THIGH_L] = v3(-0.70f, 0.95f, 0.25f);
                o->r[BONE_SHIN_L].x = 1.10f;
            }
            if (h1 < -0.4f) set_arm(o, L, (ArmK){ -0.50f, 0.90f, 0.10f, -1.30f, 0.0f, 0.0f });
            else add_arm(o, L, (ArmK){ 0.0f, 0.0f, 0.35f + 0.45f * h1, 0.25f * h2, 0, 0 }, 1.0f);
            add_arm(o, R, (ArmK){ 0.0f, 0.0f, 0.10f + 0.55f * h2, -0.3f + 0.3f * h3, 0, 0 }, 1.0f);
            o->r[BONE_SPINE].y += 0.15f * h4;
        } else if (prone) {
            // Ничком: левая рука то у головы, то вдоль тела, правая бывает подвёрнута под себя;
            // одна нога бывает подтянута вбок
            if (h0 > 0.0f) set_arm(o, L, (ArmK){ 0.20f, 0.0f, 0.30f, -0.3f, 0.0f, 0.0f });
            if (h1 > 0.4f) set_arm(o, R, (ArmK){ -0.30f, 0.80f, 0.05f, -1.40f, 0.0f, 0.0f });
            else add_arm(o, R, (ArmK){ 0.0f, 0.0f, 0.45f * h1, 0.30f * h2, 0, 0 }, 1.0f);
            if (h3 > 0.2f) {
                // колено подтянуто вбок по земле
                o->r[BONE_THIGH_R] = v3(-0.90f, -1.30f, -0.10f);
                o->r[BONE_SHIN_R].x = 1.30f;
            } else {
                o->r[BONE_THIGH_R].z -= 0.20f * h3;
            }
            o->r[BONE_SPINE].z += 0.12f * h4;
        } else {
            add_arm(o, R, (ArmK){ 0.40f * h1, 0.0f, 0.0f, 0.30f * h2, 0, 0 }, 1.0f);
            o->r[BONE_THIGH_R].x += 0.40f * h3;
            o->r[BONE_SHIN_R].x += 0.30f * h4;
        }
        o->r[BONE_HEAD].y += 0.35f * h3;
        if (!side && hashf(sd * 13u + 5u) < 0.5f) pose_mirror(o);
    }
    if (side < 0) pose_mirror(o);
    // Дыхание: у мёртвого нет; последний выдох и затихающие движения в первые 1.5 с
    f32 br = (dead ? 0.0f : 1.0f - 0.85f * limp) * fsin(t * 2.1f + seed);
    o->r[BONE_CHEST].x += 0.025f * br;
    o->r[BONE_HEAD].y += prone ? 1.1f : 0.25f * fsin(seed * 3.0f);
    if (dead) {
        f32 k = 1.0f - smooth01(still / 1.5f);
        o->r[BONE_CHEST].x += 0.05f * fsin(PI_F * clampf(still / 0.8f, 0.0f, 1.0f)) * k;
        o->r[BONE_FOREARM_R].x -= 0.25f * k * k * (0.5f + 0.5f * fsin(still * 7.0f + seed));
        o->r[BONE_HEAD].y += 0.3f * (1.0f - k) * (side ? 0.0f : 1.0f);
    }
    // В сознании: перекатывает голову, подтягивает ногу
    f32 alive = dead ? 0.0f : 1.0f - limp;
    o->r[BONE_HEAD].y += 0.25f * fsin(t * 0.6f + seed) * alive;
    o->r[BONE_THIGH_R].x -= 0.35f * smooth01(fsin(t * 0.45f + seed) * 2.0f) * alive;
    o->r[BONE_SHIN_R].x += 0.6f * smooth01(fsin(t * 0.45f + seed) * 2.0f) * alive;
}

static void crawl_pose(Pose *o, f32 t, f32 seed)
{
    body_key(o, &K_LIE_DOWN_K);
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

// Поза клипа всем телом и её вес поверх боевой. settle — насколько руки и ноги лежат на земле
static f32 clip_pose(Pose *c, const CharAction *a, u8 clip, f32 clip_t, bool prone, i32 side, f32 seed, f32 *settle)
{
    f32 t = clampf(clip_t, 0.0f, 1.0f), w = 1.0f;
    bool mirror = false;
    *settle = 0.0f;
    switch (clip) {
    case CLIP_FALL:
        switch (a->fall_dir) {
        case CHAR_FALL_FRONT: body_track(c, K_CHAR_FALL_FRONT, ARRAY_COUNT(K_CHAR_FALL_FRONT), t); break;
        case CHAR_FALL_LEFT:
        case CHAR_FALL_RIGHT:
            body_track(c, K_FALL_SIDE, ARRAY_COUNT(K_FALL_SIDE), t);
            mirror = a->fall_dir == CHAR_FALL_RIGHT;
            break;
        default: body_track(c, K_FALL, ARRAY_COUNT(K_FALL), t); break;
        }
        w = ramp(t, 0.0f, 0.10f);
        *settle = ramp(t, 0.85f, 1.0f);
        break;
    case CLIP_DIE:
        switch (a->fall_dir) {
        case CHAR_FALL_FRONT: body_track(c, K_DIE_FRONT, ARRAY_COUNT(K_DIE_FRONT), t); break;
        case CHAR_FALL_LEFT:
        case CHAR_FALL_RIGHT:
            body_track(c, K_DIE_SIDE, ARRAY_COUNT(K_DIE_SIDE), t);
            mirror = a->fall_dir == CHAR_FALL_RIGHT;
            break;
        default: body_track(c, K_DIE_BACK, ARRAY_COUNT(K_DIE_BACK), t); break;
        }
        w = ramp(t, 0.0f, 0.08f);
        *settle = ramp(t, 0.8f, 1.0f);
        break;
    case CLIP_BLEED:
        body_track(c, K_BLEED, ARRAY_COUNT(K_BLEED), t);
        mirror = side < 0;
        w = ramp(t, 0.0f, 0.15f);
        *settle = ramp(t, 0.85f, 1.0f);
        break;
    case CLIP_COLLAPSE:
        if (prone) body_track(c, K_COLLAPSE_DOWN, ARRAY_COUNT(K_COLLAPSE_DOWN), t);
        else body_track(c, K_COLLAPSE_UP, ARRAY_COUNT(K_COLLAPSE_UP), t);
        w = ramp(t, 0.0f, 0.15f);
        *settle = ramp(t, 0.85f, 1.0f);
        break;
    case CLIP_SINK:
        body_track(c, K_SINK, ARRAY_COUNT(K_SINK), t);
        w = ramp(t, 0.0f, 0.15f);
        break;
    case CLIP_GETUP:
        if (prone) body_track(c, K_GETUP_DOWN, ARRAY_COUNT(K_GETUP_DOWN), t);
        else body_track(c, K_GETUP_UP, ARRAY_COUNT(K_GETUP_UP), t);
        w = 1.0f - ramp(t, 0.86f, 1.0f);
        break;
    case CLIP_TURN:
        body_track(c, K_TURN_OVER, ARRAY_COUNT(K_TURN_OVER), t);
        break;
    case CLIP_PICKUP:
        body_track(c, K_PICKUP, ARRAY_COUNT(K_PICKUP), t);
        w = ramp(t, 0.0f, 0.15f) * (1.0f - ramp(t, 0.80f, 1.0f));
        break;
    case CLIP_KNEEL:
        body_track(c, K_KNEEL, ARRAY_COUNT(K_KNEEL), t);
        w = ramp(t, 0.0f, 0.2f);
        break;
    case CLIP_CRAWL:
        crawl_pose(c, clip_t, seed);
        break;
    default:
        lie_pose(c, prone, side, clip_t, a->limp, a->dead, a->still, seed);
        *settle = 1.0f;
        break;
    }
    if (mirror) pose_mirror(c);
    return w;
}

// Обмякшие руки и ноги ложатся на землю: каждая конечность целиком поворачивается вокруг плеча или
// бедра, пока кисть или лодыжка не коснётся земли. Ладонь и пятка не висят в воздухе и не уходят в землю
static void rot_about(Xf *x, V3 pivot, V3 axis, f32 ang)
{
    f32 s, c;
    fsincos(ang, &s, &c);
    V3 *v[4] = { &x->x, &x->y, &x->z, &x->t };
    for (u32 k = 0; k < 4; k++) {
        V3 p = k == 3 ? v3_sub(*v[k], pivot) : *v[k];
        V3 r = v3_add(v3_add(v3_mul(p, c), v3_mul(v3_cross(axis, p), s)), v3_mul(axis, v3_dot(axis, p) * (1.0f - c)));
        *v[k] = k == 3 ? v3_add(r, pivot) : r;
    }
}

static void settle_limbs(Xf *wld, f32 k)
{
    static const u8 chain[4][3] = { { BONE_UPPER_ARM_L, BONE_FOREARM_L, BONE_HAND_L },
                                    { BONE_UPPER_ARM_R, BONE_FOREARM_R, BONE_HAND_R },
                                    { BONE_THIGH_L, BONE_SHIN_L, BONE_FOOT_L },
                                    { BONE_THIGH_R, BONE_SHIN_R, BONE_FOOT_R } };
    static const f32 rest_y[4] = { 0.05f, 0.05f, 0.08f, 0.08f };
    V3 mid = wld[BONE_PELVIS].t;
    for (u32 c = 0; c < 4; c++) {
        V3 piv = wld[chain[c][0]].t, end = wld[chain[c][2]].t;
        V3 v = v3_sub(end, piv);
        f32 len = v3_len(v);
        if (len < 0.05f) continue;
        f32 ty = clampf(rest_y[c] - piv.y, -len * 0.98f, len * 0.98f);
        f32 dy = lerpf(v.y, ty, k);
        if (fabsf(dy - v.y) < 0.005f) continue;
        V3 hz = v3(v.x, 0.0f, v.z);
        f32 hl = v3_len(hz);
        if (hl < 0.02f) {
            hz = v3(piv.x - mid.x, 0.0f, piv.z - mid.z);
            hl = v3_len(hz);
            if (hl < 1e-4f) hz = v3(1.0f, 0.0f, 0.0f), hl = 1.0f;
        }
        hz = v3_mul(hz, sqrtf(fmaxf(len * len - dy * dy, 0.0f)) / hl);
        V3 to = v3(hz.x, dy, hz.z);
        V3 ax = v3_cross(v, to);
        f32 al = v3_len(ax);
        if (al < 1e-6f) continue;
        f32 ang = atan2f(al, v3_dot(v, to));
        ax = v3_mul(ax, 1.0f / al);
        for (u32 b = 0; b < 3; b++) rot_about(&wld[chain[c][b]], piv, ax, ang);
    }
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

// Где вторая (левая) рука на оси оружия, м от правой, и докуда может по ней скользить
static f32 weapon_left_grip(u8 w) { return g_weapons[w].wclass == WC_POLEARM ? 0.62f : -0.15f; }
static f32 wpn_grip_lo(u8 w) { return g_weapons[w].wclass == WC_POLEARM ? 0.12f : -0.16f; }
static f32 wpn_grip_hi(u8 w) { return g_weapons[w].wclass == WC_POLEARM ? 1.10f : -0.11f; }

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

#define DRAW_MID 0.4f  // доля вынимания до промежуточного положения

// Промежуточное положение при доставании (пространство модели, грудь c)
static Xf draw_mid(u8 w, const Xf *sheath, const Xf *c)
{
    Carry k = weapon_carry(w);
    if (k == CARRY_BELT) {
        // Вытянуто из ножен вверх-вперёд по их оси, клинок ещё в их направлении
        Xf m = *sheath;
        m.t = v3_add(v3_sub(sheath->t, v3_mul(sheath->y, 0.32f)), v3_mul(c->z, 0.08f));
        return m;
    }
    if (k == CARRY_BACK_HIGH)
        // Над правым плечом, клинок назад и чуть вверх: дальше переходит на плечо
        return frame_axes(v3_add(v3_mul(c->z, -0.85f), v3_mul(c->y, 0.5f)), v3_mul(c->y, 1.0f),
                          v3_add(sheath->t, v3_add(v3_mul(c->y, 0.12f), v3_mul(c->z, 0.10f))));
    // Древко стоймя у правого бока
    return frame_axes(c->y, c->z,
                      v3_add(c->t, v3_add(v3_add(v3_mul(c->x, -0.34f), v3_mul(c->y, -0.25f)), v3_mul(c->z, 0.02f))));
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
    { { -0.18f, -0.14f, 0.32f }, { 0.36f, 0.30f, 1.0f }, { 0.0f, -1.0f, 0.0f },
      { -0.25f, 0.05f, 0.33f }, { 1.0f, 0.35f, 0.10f }, { 0.0f, 0.0f, 1.0f } },
    // Тяжёлое двуручное: рукоять у живота, клинок стоймя чуть вперёд; блок — плашмя перед собой
    { { -0.17f, -0.13f, 0.38f }, { 0.18f, 1.0f, 0.30f }, { 0.0f, 0.0f, 1.0f },
      { -0.10f, 0.10f, 0.32f }, { 0.35f, 1.0f, 0.10f }, { 1.0f, 0.0f, 0.0f } },
};

static Xf frame_lerp(const Xf *a, const Xf *b, f32 t) { return xf_slerp_rot(a, b, t, v3_lerp(a->t, b->t, t)); }

// То же, но рукоять идёт вокруг корпуса, а не сквозь него: поворот, удаление от оси груди c и высота
// меняются по отдельности
static Xf frame_around(const Xf *a, const Xf *b, f32 t, const Xf *c)
{
    V3 ra = v3_sub(a->t, c->t), rb = v3_sub(b->t, c->t);
    f32 ya = v3_dot(ra, c->y), yb = v3_dot(rb, c->y);
    V3 ha = v3_sub(ra, v3_mul(c->y, ya)), hb = v3_sub(rb, v3_mul(c->y, yb));
    f32 la = v3_len(ha), lb = v3_len(hb);
    V3 h = v3_lerp(v3_mul(ha, 1.0f / fmaxf(la, 1e-4f)), v3_mul(hb, 1.0f / fmaxf(lb, 1e-4f)), t);
    f32 hl = v3_len(h);
    V3 pos = c->t;
    if (hl > 1e-3f) pos = v3_add(pos, v3_mul(h, lerpf(la, lb, t) / hl));
    else pos = v3_add(pos, v3_lerp(ha, hb, t));
    pos = v3_add(pos, v3_mul(c->y, lerpf(ya, yb, t)));
    return xf_slerp_rot(a, b, t, pos);
}

// Оружие на доле удара q (0 — взведено, 1 — конец проводки). Размах и удар сверху: клинок от плеча P
// по дуге в плоскости следа, на ударе точка клинка проходит через цель T; укол — рукоять вперёд, остриё за T
static Xf strike_frame(const CharAction *a, const Xf *c, V3 P, V3 T, f32 len, f32 reach, f32 q, bool pole, bool two)
{
    if (a->attack == ATK_THRUST) {
        // Рукоять от бедра толкается к цели, пока остриё не уйдёт за неё; дальше руки не пускают.
        // Взведено: рукоять у правого бедра снаружи, локоть отведён назад мимо корпуса
        V3 gw = v3_add(P, v3_add(v3_add(v3_mul(c->x, -0.07f), v3_mul(c->y, -0.36f)), v3_mul(c->z, 0.03f)));
        V3 db = v3_sub(T, gw);
        f32 dbl = fmaxf(v3_len(db), 0.3f);
        f32 push = clampf(dbl - len + 0.10f - (a->aim_stop ? 0.30f : 0.0f), 0.10f, 0.60f);
        V3 gh = v3_add(gw, v3_mul(db, push / dbl));
        V3 gp = v3_sub(gh, P);
        f32 gl = v3_len(gp);
        if (gl > reach) gh = v3_add(P, v3_mul(gp, reach / gl));
        V3 g = v3_lerp(gw, gh, smooth01(q / 0.45f));
        V3 u = v3_norm(v3_sub(T, g));
        return frame_axes(u, v3_cross(v3(0, 1, 0), u), g);
    }
    V3 d = v3_sub(T, P);
    f32 D = fmaxf(v3_len(d), 0.3f);
    V3 u0 = v3_mul(d, 1.0f / D);
    bool over = a->attack == ATK_OVERHEAD;
    // Древко не заводят далеко за спину: другой конец прошёл бы сквозь тело; двуручное сверху — над головой,
    // а не за ней: левой руке туда не достать
    f32 tw = over ? (two ? -1.55f : -2.2f) : pole ? -1.1f : -1.9f, tf = over ? 0.9f : 1.3f;
    f32 th = lerpf(tw, tf, q);
    // Блок: клинок встаёт перед целью и отскакивает
    if (a->aim_stop && th > -0.35f) th = -0.35f - 0.4f * fminf(th + 0.35f, 1.0f);
    V3 m = over ? v3(a->aim_s, -1.0f, 0.0f) : v3(1.0f, a->aim_s, 0.0f);
    m = v3_norm(v3_sub(m, v3_mul(u0, v3_dot(m, u0))));
    f32 ah = clampf(D - 0.7f * len, 0.25f, reach);
    f32 rw = pole ? 0.56f : two ? 0.60f : 0.46f, rf = pole ? 0.58f : 0.50f;
    f32 r = th < 0.0f ? lerpf(ah, rw, (th / tw) * (th / tw)) : lerpf(ah, rf, (th / tf) * (th / tf));
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
    // Двуручным машут от середины плеч: оружие перед телом, обе руки достают
    V3 P = wld[BONE_UPPER_ARM_R].t;
    if (weapon_two_hand(wpn) && a->attack != ATK_THRUST) P = v3_lerp(P, wld[BONE_UPPER_ARM_L].t, 0.5f);
    f32 reach = v3_len(v3_sub(joint(BONE_FOREARM_R), joint(BONE_UPPER_ARM_R))) +
                v3_len(v3_sub(joint(BONE_HAND_R), joint(BONE_FOREARM_R))) + 0.07f;
    f32 len = g_weapons[wpn].length;
    bool pole = wc == WC_POLEARM, two = weapon_two_hand(wpn);
    if (a->swing < 1.0f) {
        Xf s = strike_frame(a, c, P, a->aim, len, reach, 0.0f, pole, two);
        return frame_around(&f, &s, smooth01(a->swing), c);
    }
    if (a->swing < 2.0f) return strike_frame(a, c, P, a->aim, len, reach, ease_out(a->swing - 1.0f), pole, two);
    Xf s = strike_frame(a, c, P, a->aim, len, reach, 1.0f, pole, two);
    return frame_around(&s, &f, smooth01((a->swing - 2.0f) / 0.8f), c);
}

// ---------------------------------------------------------------- IK руки

// Ортонормальный базис: ось кости, шарнир, их произведение (столбцы)
static Xf basis(V3 axis, V3 hinge)
{
    hinge = v3_norm(v3_sub(hinge, v3_mul(axis, v3_dot(hinge, axis))));
    return (Xf){ axis, hinge, v3_cross(axis, hinge), v3(0, 0, 0) };
}

// Поворот, переводящий базис b в базис w
static Xf basis_map(const Xf *b, const Xf *w)
{
    Xf r;
    r.x = v3_add(v3_add(v3_mul(w->x, b->x.x), v3_mul(w->y, b->y.x)), v3_mul(w->z, b->z.x));
    r.y = v3_add(v3_add(v3_mul(w->x, b->x.y), v3_mul(w->y, b->y.y)), v3_mul(w->z, b->z.y));
    r.z = v3_add(v3_add(v3_mul(w->x, b->x.z), v3_mul(w->y, b->y.z)), v3_mul(w->z, b->z.z));
    r.t = v3(0, 0, 0);
    return r;
}

#define SHAFT_R 0.22f     // древко двуручного не ближе к оси груди, м
#define SHIFT_MAX 0.32f   // на сколько двуручное тянут к левой руке, дальше она отпускает, м
#define THRUST_SHIFT 0.06f // то же в уколе
#define ELBOW_HABIT 0.35f // насколько локоть тянется вниз-наружу-назад против продолжения кисти
#define ELBOW_R 0.21f     // подсказка локтя не ближе к оси груди, м: корпус 0.16 и толщина руки
#define FORE_R 0.19f      // предплечье не ближе к оси груди, м
#define CHEST_FWD 0.03f   // ось корпуса впереди сустава груди, м
#define HEAD_R 0.14f      // и к середине головы, м
#define WRIST_MAX 1.30f   // излом запястья, рад (~75°): дальше кисть доворачивается к предплечью
#define WRIST_COS 0.2675f // cos WRIST_MAX
#define WRIST_SIN 0.9636f

// Два звена: локоть и запястье для цели target и полюса локтя pole
typedef struct ArmFit {
    V3 u, f, n, E, W;
} ArmFit;

static ArmFit arm_fit(V3 S, V3 target, V3 Ei, u32 side, f32 l1, f32 l2)
{
    ArmFit r;
    V3 d = v3_sub(target, S);
    f32 dl = fmaxf(v3_len(d), 1e-4f);
    V3 dn = v3_mul(d, 1.0f / dl);
    f32 len = clampf(dl, 0.3f * (l1 + l2), l1 + l2 - 1e-3f);
    // К локтю без излома — привычный: вниз, наружу и назад
    V3 pole = v3_sub(Ei, S), anat = v3(side == L ? 0.45f : -0.45f, -1.0f, -0.5f);
    pole = v3_sub(pole, v3_mul(dn, v3_dot(pole, dn)));
    anat = v3_sub(anat, v3_mul(dn, v3_dot(anat, dn)));
    f32 pl = v3_len(pole), al = v3_len(anat);
    pole = v3_add(pl > 1e-5f ? v3_mul(pole, 1.0f / pl) : v3(0, 0, 0), v3_mul(anat, ELBOW_HABIT / fmaxf(al, 1e-5f)));
    if (v3_dot(pole, pole) < 1e-8f) pole = v3_cross(dn, v3(1, 0, 0));
    pole = v3_norm(pole);
    f32 ca = clampf((l1 * l1 + len * len - l2 * l2) / (2.0f * l1 * len), -1.0f, 1.0f);
    f32 sa = sqrtf(1.0f - ca * ca);
    r.u = v3_add(v3_mul(dn, ca), v3_mul(pole, sa));
    r.E = v3_add(S, v3_mul(r.u, l1));
    r.W = v3_add(S, v3_mul(dn, len));
    r.f = v3_norm(v3_sub(r.W, r.E));
    // Шарнир: поперёк плоскости плечо — локоть — запястье; у прямой руки — по полюсу
    r.n = v3_cross(r.u, r.f);
    if (v3_dot(r.n, r.n) < 1e-6f) r.n = v3_cross(r.u, pole);
    r.n = v3_norm(r.n);
    return r;
}

// Кисть не ломается сильнее WRIST_MAX: ось кисти ставится на конус вокруг предплечья f
static bool wrist_clamp(Xf *hr, V3 fa, V3 f)
{
    V3 hd = xf_dir(hr, fa);
    if (v3_dot(hd, f) >= WRIST_COS) return false;
    V3 side = v3_sub(hd, v3_mul(f, v3_dot(hd, f)));
    f32 sl = v3_len(side);
    V3 to = sl > 1e-5f ? v3_add(v3_mul(f, WRIST_COS), v3_mul(side, WRIST_SIN / sl)) : f;
    Xf back = xf_between(hd, to);
    *hr = xf_mul(&back, hr);
    return true;
}

// Рука без локтя и предплечья внутри корпуса и головы: подсказка локтя выводится из цилиндра груди;
// если локоть или предплечье всё же ближе FORE_R к оси груди (или к голове) — локоть отводится наружу
// и рука ставится заново
// Насколько локоть и предплечье залезли в корпус (ось cc) или голову hc; push — куда их выводить
static f32 arm_inside(const ArmFit *k, const Xf *ch, V3 cc, V3 hc, V3 *push)
{
    f32 worst = 0.0f;
    for (u32 j = 0; j <= 3; j++) {
        V3 q = v3_lerp(k->E, k->W, 0.25f * (f32)j);
        V3 r = v3_sub(q, cc);
        f32 h = v3_dot(r, ch->y);
        if (h > -0.30f && h < 0.36f) {
            V3 rq = v3_sub(r, v3_mul(ch->y, h));
            f32 l2 = v3_dot(rq, rq);
            if (l2 < FORE_R * FORE_R && l2 > 1e-8f) {
                f32 l = sqrtf(l2);
                if (FORE_R - l > worst) {
                    worst = FORE_R - l;
                    *push = v3_mul(rq, 1.0f / l);
                }
            }
        }
        if (h < 0.30f) continue;  // голова выше
        V3 rh2 = v3_sub(q, hc);
        f32 lh2 = v3_dot(rh2, rh2);
        if (lh2 < HEAD_R * HEAD_R && lh2 > 1e-8f) {
            f32 lh = sqrtf(lh2);
            if (HEAD_R - lh > worst) {
                worst = HEAD_R - lh;
                *push = v3_mul(rh2, 1.0f / lh);
            }
        }
    }
    return worst;
}

static ArmFit arm_fit_clear(V3 S, V3 target, V3 Ei, u32 side, f32 l1, f32 l2, const Xf *ch, V3 hc)
{
    // Ось корпуса чуть впереди сустава груди: грудь выпуклая спереди
    V3 cc = v3_add(ch->t, v3_mul(ch->z, CHEST_FWD));
    V3 rc = v3_sub(Ei, cc);
    V3 rh = v3_sub(rc, v3_mul(ch->y, v3_dot(rc, ch->y)));
    f32 rl = v3_len(rh);
    if (rl < ELBOW_R) {
        V3 out = rl > 1e-4f ? v3_mul(rh, 1.0f / rl) : v3_mul(ch->x, side == L ? 1.0f : -1.0f);
        Ei = v3_add(Ei, v3_mul(out, ELBOW_R - rl));
    }
    ArmFit k = arm_fit(S, target, Ei, side, l1, l2);
    V3 push = v3(0, 0, 0);
    f32 worst = arm_inside(&k, ch, cc, hc, &push);
    if (worst <= 0.0f) return k;
    k = arm_fit(S, target, v3_add(k.E, v3_mul(push, 2.0f * worst + 0.02f)), side, l1, l2);
    worst = arm_inside(&k, ch, cc, hc, &push);
    if (worst <= 0.01f) return k;
    // Наружу не выходит (рука сложена у плеча, локоть ушёл за корпус): локоть вперёд-вниз и в сторону руки
    V3 fwd = v3_add(v3_add(v3_mul(ch->z, 0.30f), v3_mul(ch->y, -0.15f)), v3_mul(ch->x, side == L ? 0.10f : -0.10f));
    ArmFit k2 = arm_fit(S, target, v3_add(S, fwd), side, l1, l2);
    return arm_inside(&k2, ch, cc, hc, &push) < worst ? k2 : k;
}

// Рука к запястью target с поворотом кисти hand_rot. Локоть ставится так, чтобы предплечье продолжало
// кисть, насколько дотягивается; плечо и предплечье строятся по шарниру локтя, поэтому локоть гнётся,
// как у живого. Если кисть пришлось довернуть к предплечью, а grip задан (точка хвата от запястья в осях
// кисти) — рука переставляется так, чтобы хват остался на месте. Закрутка кисти — поровну локтю и запястью
static void arm_ik(Xf *wld, u32 side, V3 target, const Xf *hand_rot, const V3 *grip)
{
    u8 up = side == L ? BONE_UPPER_ARM_L : BONE_UPPER_ARM_R;
    u8 fo = up + 1, ha = up + 2;
    V3 ua = v3_sub(joint(fo), joint(up)), fa = v3_sub(joint(ha), joint(fo));
    f32 l1 = v3_len(ua), l2 = v3_len(fa);
    ua = v3_mul(ua, 1.0f / l1);
    fa = v3_mul(fa, 1.0f / l2);
    // Шарнир локтя в позе привязки: сгиб ведёт предплечье вперёд
    const V3 hinge_b = v3(-1.0f, 0.0f, 0.0f);
    Xf hr = hand_rot ? *hand_rot : wld[ha];
    V3 S = wld[up].t;
    if (v3_dot(v3_sub(target, S), v3_sub(target, S)) < 1e-8f) return;
    // Локоть без излома запястья: от запястья назад по оси кисти
    const Xf *ch = &wld[BONE_CHEST];
    V3 hc = xf_point(&wld[BONE_HEAD], v3_sub(v3(0.0f, 1.69f, 0.03f), joint(BONE_HEAD)));
    ArmFit k = arm_fit_clear(S, target, v3_sub(target, v3_mul(xf_dir(&hr, fa), l2)), side, l1, l2, ch, hc);
    if (wrist_clamp(&hr, fa, k.f) && grip) {
        V3 at = v3_add(target, xf_dir(hand_rot ? hand_rot : &wld[ha], *grip));
        V3 t2 = v3_sub(at, xf_dir(&hr, *grip));
        hr = hand_rot ? *hand_rot : wld[ha];
        k = arm_fit_clear(S, t2, v3_sub(t2, v3_mul(xf_dir(&hr, fa), l2)), side, l1, l2, ch, hc);
        wrist_clamp(&hr, fa, k.f);
    }
    Xf ub = basis(ua, hinge_b), uw = basis(k.u, k.n);
    Xf fb = basis(fa, hinge_b), fw = basis(k.f, k.n);
    Xf ru = basis_map(&ub, &uw), rf = basis_map(&fb, &fw);
    // Закрутка кисти вокруг предплечья: половину берёт предплечье (поворот к середине между ними)
    V3 hn = xf_dir(&hr, hinge_b), fn = xf_dir(&rf, hinge_b);
    hn = v3_sub(hn, v3_mul(k.f, v3_dot(hn, k.f)));
    fn = v3_norm(v3_sub(fn, v3_mul(k.f, v3_dot(fn, k.f))));
    V3 mid = v3_add(v3_mul(fn, v3_len(hn)), hn);
    if (v3_dot(mid, mid) < 1e-8f) mid = v3_cross(k.f, fn);
    Xf twr = xf_between(fn, v3_norm(mid));
    rf = xf_mul(&twr, &rf);
    ru.t = S;
    rf.t = k.E;
    hr.t = k.W;
    wld[up] = ru;
    wld[fo] = rf;
    wld[ha] = hr;
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

    // Походка
    ArmK pre = get_arm(&o, R);
    Loco lo = { 0 };
    locomotion(p, &o, &lo);
    // Оружие в руке вне стойки, одной рукой: клинок опущен, остриё вперёд-вниз; древко стоймя, как посох;
    // тяжёлое на правом плече. Рука с оружием машет на ходу меньше, с тяжёлым почти не машет
    if (wield > 0.0f) {
        ArmK carry = (ArmK){ -0.20f, 0.05f, 0.10f, -0.45f, 0.95f, 0.0f };
        f32 sway = 0.5f;
        if (g_weapons[wpn].wclass == WC_POLEARM) {
            carry = (ArmK){ -0.10f, 0.00f, 0.12f, -1.45f, 0.00f, 0.0f };
            sway = 0.25f;
        } else if (weapon_two_hand(wpn)) {
            carry = (ArmK){ -0.35f, 0.10f, 0.12f, -2.30f, 0.00f, 0.0f };
            sway = 0.12f;
        }
        ArmK cur = get_arm(&o, R);
        carry.ux += sway * (cur.ux - pre.ux);
        set_arm(&o, R, arm_lerp(cur, carry, smooth01((wield - 0.45f) / 0.55f)));
    }
    // С пояса: левая висит чуть в стороне от ножен, при доставании придерживает их и уходит назад-наружу,
    // чтобы клинок вышел мимо предплечья
    if (armed && wield < 1.0f && weapon_carry(wpn) == CARRY_BELT) {
        ArmK l = get_arm(&o, L);
        l.out += 0.10f * (1.0f - wield);
        set_arm(&o, L, arm_lerp(l, (ArmK){ 0.30f, -0.10f, 0.38f, -0.55f, 0.20f, 0.0f }, fsin(wield * PI_F)));
    }

    // Факел в левой руке: предплечье вперёд, древко стоит, пламя сбоку от лица; рука почти не машет.
    // В стойке руки отдаются бою
    f32 tw = smooth01(clampf(p->torch, 0.0f, 1.0f) * (1.0f - guard));
    if (tw > 0.0f) {
        ArmK k = get_arm(&o, L);
        ArmK hold = { -0.30f + 0.15f * k.ux, 0.05f, 0.30f, -1.30f, 0.10f, 0.0f };
        set_arm(&o, L, arm_lerp(k, hold, tw));
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

    // Всем телом: падение, лёжа, подъём, на колени, ползком. Из прежнего клипа в новый — плавно за fade,
    // без рывка, если смерть или обморок застали посреди падения или подъёма
    f32 clip_w = 0.0f, settle = 0.0f;
    if (a->clip != CLIP_NONE) {
        Pose c;
        clip_w = clip_pose(&c, a, a->clip, a->clip_t, a->prone, a->side, p->seed, &settle);
        f32 fd = smooth01(a->fade);
        if (a->prev_clip != CLIP_NONE && fd < 1.0f) {
            Pose pc;
            f32 ps;
            clip_pose(&pc, a, a->prev_clip, a->prev_t, a->prev_prone, a->prev_side, p->seed, &ps);
            pose_lerp(&pc, &c, fd);
            c = pc;
            clip_w = lerpf(1.0f, clip_w, fd);
            settle = lerpf(ps, settle, fd);
        }
        pose_lerp(&o, &c, clip_w);
        settle *= clip_w;
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
    if (settle > 0.0f) settle_limbs(wld, settle);
    // На склоне тело у земли ложится по нему: поворот вокруг точки опоры к нормали склона, тем больше,
    // чем ниже таз. Стоящий остаётся вертикальным
    if (clip_w > 0.0f && (p->slope_x != 0.0f || p->slope_z != 0.0f)) {
        f32 low = clip_w * (1.0f - smooth01((wld[BONE_PELVIS].t.y - 0.25f) / 0.45f));
        if (low > 0.0f) {
            f32 hs, hc;
            fsincos(p->heading, &hs, &hc);
            f32 gx = p->slope_x * hc - p->slope_z * hs, gz = p->slope_x * hs + p->slope_z * hc;
            V3 n = v3_norm(v3(-gx * low, 1.0f, -gz * low));
            Xf tilt = xf_between(v3(0.0f, 1.0f, 0.0f), n);
            for (u32 i = 1; i < BONE_COUNT; i++) wld[i] = xf_mul(&tilt, &wld[i]);
        }
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
        g.t = v3_sub(g_body_sockets[SOCKET_HAND_R].pos, g_body_bones[BONE_HAND_R].joint);
        // Поворот кисти, при котором рукоять ложится в ножны
        Xf ginv = { v3(g.x.x, g.y.x, g.z.x), v3(g.x.y, g.y.y, g.z.y), v3(g.x.z, g.y.z, g.z.z), v3(0, 0, 0) };
        const f32 GRAB = 0.45f;
        if (wield > 0.0f && wield < 1.0f && a->clip == CLIP_NONE) {
            // Рука идёт к ножнам вокруг корпуса; оружие выходит через промежуточное положение: с пояса —
            // вдоль ножен вверх-вперёд, со спины — через верх над плечом (тяжёлое) или вниз к правому боку
            // (древко), а не напрямую сквозь тело. Убирают тем же путём назад
            Xf hand_fk = wld[BONE_HAND_R];
            Xf carry = xf_mul(&hand_fk, &g);
            Xf mid = draw_mid(wpn, &sheath, &wld[BONE_CHEST]);
            const Xf *ch = &wld[BONE_CHEST];
            Xf wf_;
            if (wield < GRAB) wf_ = frame_around(&carry, &sheath, smooth01(wield / GRAB), ch);
            else {
                f32 s = (wield - GRAB) / (1.0f - GRAB);
                if (s < DRAW_MID) wf_ = frame_around(&sheath, &mid, smooth01(s / DRAW_MID), ch);
                else wf_ = frame_around(&mid, &carry, smooth01((s - DRAW_MID) / (1.0f - DRAW_MID)), ch);
            }
            Xf hrot = xf_mul(&wf_, &ginv);
            arm_ik(wld, R, v3_sub(wf_.t, xf_dir(&hrot, g.t)), &hrot, &g.t);
        }
        // В бою оружие ведёт рамка стойки, блока или удара, правая рука идёт за ним
        f32 wf = guard * smooth01((wield - 0.6f) / 0.4f) * (1.0f - 0.7f * clampf(a->stagger, 0.0f, 1.0f));
        if (wf > 0.0f && a->clip == CLIP_NONE) {
            Xf ff = fight_frame(a, wpn, wld);
            Xf hrot = xf_mul(&ff, &ginv);
            V3 wrist = v3_sub(ff.t, xf_dir(&hrot, g.t));
            Xf hb = xf_slerp_rot(&wld[BONE_HAND_R], &hrot, wf, v3(0, 0, 0));
            arm_ik(wld, R, v3_lerp(wld[BONE_HAND_R].t, wrist, wf), &hb, &g.t);
        }
        bool in_hand = wield >= GRAB && a->clip == CLIP_NONE;
        // Выбывшие роняют оружие сами (вызывающий рисует его на земле); в клипе — в руке
        if (a->clip != CLIP_NONE && wield >= GRAB) in_hand = true;
        weapon = in_hand ? xf_mul(&wld[BONE_HAND_R], &g) : sheath;
        // Двуручное: левая рука на древке
        if (in_hand && weapon_two_hand(wpn)) {
            // Вне стойки несут одной рукой: левая свободна
            f32 w2 = smooth01((wield - 0.75f) / 0.25f) * (1.0f - clip_w) * (1.0f - 0.6f * clampf(a->stagger, 0.0f, 1.0f)) *
                     smooth01(guard / 0.5f);
            if (w2 > 0.0f) {
                // Левая скользит по древку в пределах хвата к месту, куда достаёт; если не достаёт никуда —
                // оружие сдвигается к ней, пока обе руки не будут на нём
                f32 glo = wpn_grip_lo(wpn), ghi = wpn_grip_hi(wpn), gs = weapon_left_grip(wpn);
                V3 SL = wld[BONE_UPPER_ARM_L].t, SR = wld[BONE_UPPER_ARM_R].t;
                f32 rl = 0.97f * (v3_len(v3_sub(joint(BONE_FOREARM_L), joint(BONE_UPPER_ARM_L))) +
                                  v3_len(v3_sub(joint(BONE_HAND_L), joint(BONE_FOREARM_L)))) + 0.06f;
                f32 rr = 0.97f * (v3_len(v3_sub(joint(BONE_FOREARM_R), joint(BONE_UPPER_ARM_R))) +
                                  v3_len(v3_sub(joint(BONE_HAND_R), joint(BONE_FOREARM_R)))) + 0.06f;
                V3 os = v3_sub(weapon.t, SL);
                f32 bb = v3_dot(weapon.y, os), disc = bb * bb - (v3_dot(os, os) - rl * rl);
                f32 ra = disc >= 0.0f ? -bb - sqrtf(disc) : 1e9f, rb = disc >= 0.0f ? -bb + sqrtf(disc) : -1e9f;
                if (fmaxf(ra, glo) <= fminf(rb, ghi)) gs = clampf(gs, fmaxf(ra, glo), fminf(rb, ghi));
                else gs = clampf(-bb, glo, ghi);
                V3 shift = v3(0, 0, 0);
                const Xf *ch = &wld[BONE_CHEST];
                f32 b0 = g_weapons[wpn].wclass == WC_POLEARM ? -0.22f : -0.18f, b1 = g_weapons[wpn].length;
                for (u32 it = 0; it < 3; it++) {
                    V3 dl = v3_sub(v3_add(v3_add(weapon.t, shift), v3_mul(weapon.y, gs)), SL);
                    V3 dr = v3_sub(v3_add(weapon.t, shift), SR);
                    f32 ll = v3_len(dl), lr = v3_len(dr);
                    if (ll > rl) shift = v3_sub(shift, v3_mul(dl, 1.0f - rl / ll));
                    if (lr > rr) shift = v3_sub(shift, v3_mul(dr, 1.0f - rr / lr));
                }
                // Далеко к левой оружие не тянут: в крайних точках размаха левая отпускает древко.
                // Укол не укорачивают совсем: на полном выпаде древко остаётся в правой
                f32 sl = v3_len(shift), smax = a->attack == ATK_THRUST && a->swing > 0.0f ? THRUST_SHIFT : SHIFT_MAX;
                if (sl > smax) {
                    w2 *= 1.0f - smooth01((sl - smax) / 0.10f);
                    shift = v3_mul(shift, smax / sl);
                }
                shift = v3_mul(shift, w2);
                // Древко не сквозь корпус: ближайшая к оси груди точка отводится наружу
                for (u32 it = 0; it < 2; it++) {
                    f32 best = SHAFT_R * SHAFT_R;
                    V3 away = v3(0, 0, 0);
                    for (u32 k = 0; k <= 6; k++) {
                        V3 q = v3_sub(v3_add(v3_add(weapon.t, shift), v3_mul(weapon.y, lerpf(b0, b1, (f32)k / 6.0f))), ch->t);
                        f32 h = v3_dot(q, ch->y);
                        if (h < -0.30f || h > 0.45f) continue;
                        V3 rq = v3_sub(q, v3_mul(ch->y, h));
                        f32 l2 = v3_dot(rq, rq);
                        if (l2 < best) {
                            best = l2;
                            away = rq;
                        }
                    }
                    if (best < SHAFT_R * SHAFT_R) {
                        f32 l = sqrtf(best);
                        away = l > 1e-4f ? v3_mul(away, 1.0f / l) : ch->z;
                        shift = v3_add(shift, v3_mul(away, SHAFT_R - l));
                    }
                }
                if (v3_dot(shift, shift) > 1e-6f) {
                    weapon.t = v3_add(weapon.t, shift);
                    Xf hrot = xf_mul(&weapon, &ginv);
                    arm_ik(wld, R, v3_sub(weapon.t, xf_dir(&hrot, g.t)), &hrot, &g.t);
                    weapon = xf_mul(&wld[BONE_HAND_R], &g);
                }
                V3 at = v3_add(weapon.t, v3_mul(weapon.y, gs));
                Xf flip = { v3(-1, 0, 0), v3(0, 1, 0), v3(0, 0, -1), v3(0, 0, 0) };
                Xf wr = xf_mul(&weapon, &flip);
                wr.t = v3(0, 0, 0);
                Xf hl = xf_mul(&wr, &ginv);
                Xf gl = g;
                gl.t.x = -gl.t.x;
                V3 wrist_t = v3_sub(at, xf_dir(&hl, gl.t));
                // Отпущенная левая уходит вниз к левому бедру вокруг корпуса, не сквозь него
                Xf fr = wld[BONE_HAND_L], on = hl;
                fr.t = v3_add(SL, v3_add(v3_add(v3_mul(ch->x, 0.10f), v3_mul(ch->y, -0.50f)), v3_mul(ch->z, 0.06f)));
                on.t = wrist_t;
                Xf hrot = w2 < 1.0f ? frame_around(&fr, &on, w2, ch) : on;
                arm_ik(wld, L, hrot.t, &hrot, &gl.t);
            }
        }
    }

    // В мир: точка, поворот, рост и ширина. Рост масштабирует всё тело, ширина — поверх него:
    // иначе низкий выходил приплюснутым, а высокий тощим
    f32 sh = p->height > 0.0f ? p->height : 1.0f;
    f32 sw = (p->width > 0.0f ? p->width : 1.0f) * sh;
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
    // Факел: древко в левом кулаке так же, как рукоять оружия в правом
    Xf tg = grip_frame();
    tg.t = v3_sub(g_body_sockets[SOCKET_HAND_L].pos, g_body_bones[BONE_HAND_L].joint);
    Xf torch = xf_mul(&wld[BONE_HAND_L], &tg);
    if (a->severed & (1u << PART_ARM_L)) torch = (Xf){ v3(0, 0, 0), v3(0, 0, 0), v3(0, 0, 0), torch.t };
    Xf tw_world = xf_mul(&root, &torch);
    out[CHAR_BONE_TORCH] = xf_m4(&tw_world);
    // Огонь тянется вверх: у пламени только поворот тела, без наклона руки
    Xf flame = { v3(hc, 0, -hs), v3(0, 1, 0), v3(hs, 0, hc), xf_point(&tw_world, v3(0, TORCH_FLAME_BASE, 0)) };
    if (a->severed & (1u << PART_ARM_L)) flame = (Xf){ v3(0, 0, 0), v3(0, 0, 0), v3(0, 0, 0), flame.t };
    out[CHAR_BONE_FLAME] = xf_m4(&flame);

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
