// Общее для машин (mech.cpp — поза и кадр, mech_ground.cpp — опора и путь, mech_move.cpp — жизнь Вежи,
// tat_move.cpp — жизнь Тати): описания видов, размеры ног и мелкие функции
#pragma once
#include <math.h>
#include "../anim/pose.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include "mech.h"

constexpr f32 MECH_G = 9.8f;

// Нога вида: как решается, ход бедра, вынос лапы, складывание
struct MechLegDesc {
    u8 mode;          // MechLegMode
    f32 knee;         // +1 — колено над прямой от оси качания к лапе (вверх-наружу), −1 — под ней
    f32 foot_h;       // ось лапы над подошвой
    f32 th_min, th_max;            // ход бедра к горизонту, рад
    f32 reach_lie, reach_stand;    // вынос оси лапы от оси качания: лёжа и стоя (между ними — по высоте тела)
    Vec3 pack;        // сложена: поворот бедра от бинда, вынос оси лапы от оси качания (0 — самая короткая нога),
                      // высота оси лапы от осей бёдер
    f32 lift;         // высота дуги шага, доля от обычной
    f32 fold_arc;     // лапа на пути складывания поднимается ещё на столько, м (лёжа лапы на земле — переступают)
    f32 meta_lie, meta_stand;      // MECH_LEG_VERT: плюсна к горизонту лёжа и стоя, рад (−π/2 — отвесна; между —
                                   // по высоте тела, как вынос лап)
};

// Вид машины: имя сетки и скелета, высоты, скорости, ноги. Числа Вежи — прежние константы MECH_*
struct MechDesc {
    const char *name;
    f32 belly;        // оси бёдер над землёй, когда рама лежит
    f32 h_stand, h_high, h_low;    // стоит, привстала, присела
    f32 v_walk, v_dash;            // скорость шагом (крадучись) и рывком, м/с
    f32 see_m;        // дальше людей не замечает
    f32 reach_h0, reach_span;      // вынос лап переходит от «лежит» к «стоит» на высотах h0 … h0 + span
    MechLegDesc leg[MECH_LEGS];
};
extern const MechDesc MECH_DESC[MECH_KINDS];

// Вежа: прыжок на рывке (mech_move.cpp)
constexpr f32 VEZHA_JUMP_V = 3.6f;       // скорость тела вверх в толчке прыжка, м/с
constexpr f32 VEZHA_H_LAND = 0.74f;      // оси бёдер над опорой в миг касания после прыжка

// Тать: мачта прибора ложится вперёд на палубу на этот угол, рад (TAT_MAST_FOLD в tools/parts/tat_pose.py)
constexpr f32 TAT_MAST_FOLD = 1.30f;

inline const MechDesc &mech_desc(const Mech &m) { return MECH_DESC[m.kind]; }
inline const MechRig &mech_rig(const MechView &mv, const Mech &m) { return mv.rig[m.kind]; }

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
// Сдвиг рамы в осях сетки: высота, перенос веса и откат (у Вежи — только высота)
inline Vec3 mech_trans(const Mech &m) { return Vec3{m.sway.x, m.h + m.sway.y, m.sway.z - m.kick}; }
// Вынос лапы k от оси поворота бедра по высоте тела: лёжа — шире, стоя — под собой
inline f32 mech_reach(const MechView &mv, const Mech &m, u32 k, f32 h) {
    const MechDesc &d = mech_desc(m);
    return mech_rig(mv, m).leg[k].u + mech_mix(d.leg[k].reach_lie, d.leg[k].reach_stand, mech_smooth((h - d.reach_h0) / d.reach_span));
}
// Опора под точкой: высота и нормаль
inline f32 mech_support(const Extract &c, const WPos &p, f32 from_y, Vec3 *n) {
    Support s = support_at(*c.terrain, *c.coll, p.cx, p.cz, p.x, p.z, from_y);
    if (n) *n = s.kind ? Vec3{0, 1, 0} : ground_at(*c.terrain, p).normal;
    return s.height;
}
// Точка в осях рамы (начало — на оси машины на высоте осей бёдер) → вынос от оси качания и высота в плоскости ноги
// и поворот бедра от бинда
struct MechReach {
    f32 yaw, x, y;
};
inline MechReach mech_leg_reach(const MechLegRig &L, Vec3 p) {
    Vec3 v = p - L.corner;
    f32 phi0 = atan2f(-L.dir.z, L.dir.x);
    MechReach r;
    r.yaw = mech_wrap(atan2f(-v.z, v.x) - phi0);
    r.x = f_sqrt(v.x * v.x + v.z * v.z) - L.u;
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
// Две длины, которыми решается нога, и высота цели решения над осью лапы (у отвесной плюсны — голеностоп)
inline void mech_leg_links(const MechLegRig &L, const MechLegDesc &D, f32 *la, f32 *lb) {
    bool vert = D.mode == MECH_LEG_VERT;
    *la = vert ? L.l1 : L.l1 + L.l3;
    *lb = L.l2;
}
// Угол плюсны к горизонту при высоте осей бёдер h (tools/parts/vezha_leg.py, meta_at)
inline f32 mech_leg_meta(const MechDesc &d, u32 k, f32 h) {
    return mech_mix(d.leg[k].meta_lie, d.leg[k].meta_stand, mech_smooth((h - d.reach_h0) / d.reach_span));
}
// Сдвиг цели решения от оси лапы к скакательному суставу (MECH_LEG_VERT: плюсна под углом meta; иначе — нет)
inline Vec2 mech_leg_off(const MechLegRig &L, const MechLegDesc &D, f32 meta) {
    if (D.mode != MECH_LEG_VERT) return {0.0f, 0.0f};
    return {-L.l3 * cosf(meta), -L.l3 * sinf(meta)};
}
// Самая короткая и самая длинная нога (от оси качания до цели решения)
inline f32 mech_leg_min(const MechLegRig &L, const MechLegDesc &D) {
    f32 la, lb;
    mech_leg_links(L, D, &la, &lb);
    return f_abs(la - lb) + 0.0005f;
}
inline f32 mech_leg_max(const MechLegRig &L, const MechLegDesc &D) {
    f32 la, lb;
    mech_leg_links(L, D, &la, &lb);
    return la + lb - 0.02f;
}
// Углы бедра, голени и плюсны к горизонту для оси лапы в точке (x, y) от оси качания; meta — угол плюсны
// (MECH_LEG_VERT, mech_leg_meta)
void mech_leg_solve(const MechLegRig &L, const MechLegDesc &D, f32 meta, f32 x, f32 y, f32 *thigh, f32 *shin, f32 *tarsus);
// Стоящая лапа k ограничивает высоту тела: нога не короче сложенной и не длиннее вытянутой (hmin, hmax сужает)
void mech_leg_hlimit(const MechView &mv, const Mech &m, u32 k, f32 &hmin, f32 &hmax);

// Человек, на которого машина смотрит
struct MechTarget {
    bool on;
    WPos at;
    f32 dist;
};

// Опора и путь (mech_ground.cpp)
Vec3 mech_polar(const MechView &mv, const Mech &m, u32 k, Vec3 pol);     // точка рамы по пути складывания
WPos mech_chassis_world(const Mech &m, Vec3 p);                          // оси рамы → мир
Vec3 mech_world_chassis(const Mech &m, const WPos &p, f32 lift);         // мир → оси рамы
WPos mech_leg_ground(const MechView &mv, const Mech &m, const Extract &c, u32 k, Vec3 pol);   // куда встаёт лапа k
bool mech_path_clear(const Extract &c, const WPos &a, const WPos &b);    // свободен ли прямой путь
inline void mech_enter(Mech &m, u8 state) {
    m.state = state;
    m.state_t = 0.0f;
}

// Шаг жизни машины на dt секунд (dt ≤ 1/100): поведение, тело, лапы, пружины. Вежа — mech_move.cpp, Тать —
// tat_move.cpp
void mech_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt);
void tat_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt);
// Первый кадр в режимах «стоит» и «обходит»: сразу на ногах
void mech_stand_now(const MechView &mv, Mech &m, const Extract &c);
