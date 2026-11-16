// «Вежа» в кадре (docs/vezha.md): привязка к скелету, поза по состоянию машины и экземпляр со скиннингом.
// Поведение и шаги — mech_move.cpp
#include <stdio.h>
#include "../core/hash.h"
#include "../core/log.h"
#include "mech_in.h"
#include "view.h"

static const char *const MECH_LEG_NAMES[MECH_LEGS] = {"fr", "fl", "rl", "rr"};
static const char *const MECH_LEG_BONES[7] = {"hip", "thigh", "shin", "tarsus", "foot", "pa", "pb"};
enum { ML_HIP, ML_THIGH, ML_SHIN, ML_TARSUS, ML_FOOT, ML_PA, ML_PB };

void mech_view_init(MechView &mv) { memset((void *)&mv, 0, sizeof mv); }

bool mech_view_demo(MechView &mv, const char *name) {
    static const char *const NAMES[] = {"pack", "stand", "walk", "show"};
    for (u32 i = 0; i < 4; ++i) {
        if (strcmp(name, NAMES[i])) continue;
        mv.demo = (u8)(MECH_DEMO_PACK + i);
        return true;
    }
    return false;
}

bool mech_view_is(u64 mesh_name) { return mesh_name == hash_str("vezha"); }

static Vec3 mech_head(const Skel &s, i32 b) {
    const SkelBone &k = s.bones.data[b];
    return {k.pos[0], k.pos[1], k.pos[2]};
}

// Кости и размеры ноги — из скелета. false — скелет не тот
static bool mech_bind_skel(MechView &mv, const Skel &s) {
    bool ok = true;
    auto find = [&](const char *name) {
        i32 b = skel_find(s, hash_str(name));
        if (b < 0) { log_warn("вежа: в скелете нет кости %s", name); ok = false; }
        return b;
    };
    mv.chassis = find("chassis");
    mv.hull = find("hull");
    mv.gun = find("gun");
    mv.eye = find("eye");
    mv.ant[0] = find("ant_l");
    mv.ant[1] = find("ant_r");
    for (u32 k = 0; k < MECH_LEGS; ++k)
        for (u32 j = 0; j < 7; ++j) {
            char name[32];
            snprintf(name, sizeof name, "%s_%s", MECH_LEG_BONES[j], MECH_LEG_NAMES[k]);
            mv.leg[k][j] = find(name);
        }
    if (!ok) return false;
    // Размеры ноги — из бинд-позы первой ноги; углы рамы и направления — у каждой свои
    Vec3 c0 = mech_head(s, mv.chassis);
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        Vec3 y = mech_head(s, mv.leg[k][ML_HIP]), p = mech_head(s, mv.leg[k][ML_THIGH]);
        mv.corner[k] = Vec3{y.x - c0.x, 0, y.z - c0.z};
        mv.dir[k] = normalize(Vec3{p.x - y.x, 0, p.z - y.z});
    }
    const i32 *L = mv.leg[0];
    Vec3 y = mech_head(s, L[ML_HIP]), p = mech_head(s, L[ML_THIGH]), kn = mech_head(s, L[ML_SHIN]);
    Vec3 an = mech_head(s, L[ML_TARSUS]), ft = mech_head(s, L[ML_FOOT]);
    auto along = [&](Vec3 v) { return dot(v, mv.dir[0]); };
    mv.u = along(p - y);
    mv.l1 = length(kn - p);
    mv.l2 = length(an - kn);
    mv.l3 = length(ft - an);
    mv.a1 = atan2f(kn.y - p.y, along(kn - p));
    mv.a2 = atan2f(an.y - kn.y, along(an - kn));
    return true;
}

static bool mech_bind(MechView &mv, const ViewAssets &a) {
    if (mv.bound) return !mv.failed;
    mv.bound = true;
    mv.failed = true;
    u32 body = 0;
    while (body < a.body_count && a.bodies[body].name != hash_str("vezha")) ++body;
    if (body == a.body_count || !a.bodies[body].skin_count) return false;
    if (!mech_bind_skel(mv, a.bodies[body].skel[0])) return false;
    mv.body = body;
    mv.failed = false;
    return true;
}

void mech_view_place(MechView &mv, u32 prop, const WPos &at, Turn yaw) {
    Mech *slot = nullptr;
    for (u32 i = 0; i < MECH_MAX; ++i) {
        if (mv.mech[i].live && mv.mech[i].prop == prop + 1) return;
        if (!mv.mech[i].live && !slot) slot = &mv.mech[i];
    }
    if (!slot) return;
    Mech &m = *slot;
    u32 copy = m.copy;   // копия сетки переживает машину
    m = Mech{};
    m.copy = copy;
    m.live = true;
    m.prop = prop + 1;
    m.rng = (u32)hash_mix(0x7E2A0000ull + prop) | 1u;
    m.home = at;
    m.home_yaw = turn_diff_rad(0, yaw);
    m.at = at;
    m.yaw = m.home_yaw;
    m.look = m.home_yaw;
    m.h = m.h_to = MECH_BELLY;
    m.state = MECH_PACKED;
    m.wait = 4.0f;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        m.leg[k].fold = 1.0f;
        m.leg[k].t = -1.0f;
        m.leg[k].n = Vec3{0, 1, 0};
    }
}

// Режим «стоит» и «обходит» — сразу на ногах: лапы на своих местах, тело на высоте
static void mech_stand_now(const MechView &mv, Mech &m, const Extract &c) {
    m.h = m.h_to = MECH_H_STAND;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        l.fold = 0.0f;
        l.t = -1.0f;
        l.at = mech_leg_ground(mv, m, c, k, Vec3{0.0f, mech_reach(mv, m.h), 0.0f});
        mech_support(c, l.at, l.at.y + 0.2f, &l.n);
    }
    m.state = MECH_STAND;
    m.wait = 1.0f;
    m.points_left = 3;
}

// Углы бедра и голени к горизонту для оси лапы в точке (x, y) от оси качания; плюсна параллельна бедру
static void mech_leg_angles(const MechView &mv, f32 x, f32 y, f32 *thigh, f32 *shin) {
    f32 lv = mv.l1 + mv.l3;
    f32 d = f_clamp(f_sqrt(x * x + y * y), mech_leg_min(mv), lv + mv.l2 - 1e-4f);
    f32 a = atan2f(y, x) + acosf(f_clamp((lv * lv + d * d - mv.l2 * mv.l2) / (2.0f * lv * d), -1.0f, 1.0f));
    a = f_clamp(a, MECH_THIGH_MIN, MECH_THIGH_MAX);
    *thigh = a;
    *shin = atan2f(y - lv * sinf(a), x - lv * cosf(a));
}

// Поза: состояние машины → палитра костей (оси сетки: начало — в m.at на опоре, курс — у экземпляра)
// wp — головы костей в осях сетки (отладочный след); palette может быть nullptr
static void mech_pose(const MechView &mv, const Mech &m, const Skel &s, PoseMatrix *palette, Vec3 *wp) {
    const u32 nb = (u32)s.bones.count;
    Quat rot[SKEL_MAX_BONES], wr[SKEL_MAX_BONES];
    for (u32 i = 0; i < nb; ++i) rot[i] = Quat{};
    const Vec3 Y{0, 1, 0};
    Quat tilt = mech_tilt(m);
    rot[mv.chassis] = tilt;
    rot[mv.hull] = quat_axis_angle(Y, m.hull);
    rot[mv.gun] = quat_axis_angle(Vec3{1, 0, 0}, -m.gun);
    rot[mv.eye] = quat_axis_angle(Y, m.eye);
    for (u32 a = 0; a < 2; ++a)
        rot[mv.ant[a]] = quat_mul(quat_axis_angle(Vec3{1, 0, 0}, m.ant[a][0]), quat_axis_angle(Vec3{0, 0, 1}, m.ant[a][1]));
    Vec3 trans{0, m.h, 0};
    f32 foot_w[MECH_LEGS];
    Vec3 foot_n[MECH_LEGS];
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        const MechLeg &l = m.leg[k];
        const i32 *B = mv.leg[k];
        // Цель оси лапы в осях рамы
        MechReach r;
        f32 foot_pitch = 0.0f;
        foot_w[k] = 0.0f;
        foot_n[k] = Y;
        if (l.fold > 0.0f) {
            // Путь складывания: сначала лапа поднимается к высоте пачки, потом нога уходит к раме
            f32 f = mech_smooth(l.fold);
            f32 up = mech_smooth(l.fold / 0.4f);
            r.yaw = mech_mix(l.pol.x, MECH_PACK_YAW, mech_smooth((l.fold - 0.15f) / 0.85f));
            r.x = mech_mix(l.pol.y - mv.u, mech_leg_min(mv), f);
            r.y = mech_mix(l.pol.z, 0.0f, up) + 0.03f * sinf(PI_F * f);
        } else {
            WPos p = mech_leg_now(l);
            if (l.t >= 0.0f) {
                // Лапа в шаге: носок вниз на отрыве, чуть вверх перед касанием
                foot_pitch = -0.55f * sinf(PI_F * l.t) * (1.0f - l.t) + 0.25f * sinf(PI_F * l.t) * l.t;
                foot_w[k] = f_clamp(f_abs(2.0f * l.t - 1.0f) * 3.0f - 2.0f, 0.0f, 1.0f);
            } else {
                foot_w[k] = 1.0f;
            }
            foot_n[k] = l.n;
            Vec2 d = wpos_delta_xz(m.at, p);
            Vec3 b = mech_to_body(m.yaw, Vec3{d.x, p.y + MECH_FOOT_H - m.at.y, d.y}) - trans;
            r = mech_leg_reach(mv, k, quat_rotate(quat_conj(tilt), b));
        }
        f32 a, sh;
        mech_leg_angles(mv, r.x, r.y, &a, &sh);
        // Ось сгиба ноги в бинде: поперёк ноги, x ноги × вверх
        Vec3 z = cross(mv.dir[k], Y);
        f32 da = a - mv.a1, ds = sh - mv.a2;
        rot[B[ML_HIP]] = quat_axis_angle(Y, r.yaw);
        rot[B[ML_THIGH]] = quat_axis_angle(z, da);
        rot[B[ML_SHIN]] = quat_axis_angle(z, ds - da);
        rot[B[ML_TARSUS]] = quat_axis_angle(z, da - ds);
        rot[B[ML_FOOT]] = quat_axis_angle(z, foot_pitch - da);
    }
    pose_world(s, rot, mv.chassis, &trans, wr, wp);
    // Стоящая лапа ложится подошвой по опоре (нормаль — в осях сетки)
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        if (foot_w[k] <= 0.0f) continue;
        u32 f = (u32)mv.leg[k][ML_FOOT];
        Vec3 up = quat_rotate(wr[f], Y), n = normalize(mech_to_body(m.yaw, foot_n[k]));
        f32 ang = acosf(f_clamp(dot(up, n), -1.0f, 1.0f));
        if (ang < 1e-3f) continue;
        Vec3 ax = normalize(cross(up, n));
        pose_turn_world(rot, wr, f, quat_axis_angle(ax, f_min(ang, 0.6f) * foot_w[k]));
    }
    // Поршень: цилиндр и шток смотрят друг на друга
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        u32 i = (u32)mv.leg[k][ML_PA], j = (u32)mv.leg[k][ML_PB];
        Vec3 d0 = normalize(mech_head(s, (i32)j) - mech_head(s, (i32)i));
        Vec3 d = normalize(wp[j] - wp[i]);
        rot[i] = quat_between(d0, quat_rotate(quat_conj(wr[i]), d));
        rot[j] = quat_between(-d0, quat_rotate(quat_conj(wr[j]), -d));
    }
    pose_world(s, rot, mv.chassis, &trans, wr, wp);
    if (palette) pose_palette_world(s, wr, wp, palette);
}

// Кадр жизни машины: первый раз — поставить на опору, найти ближнего человека, шаги по 1/120 с
static void mech_live(const MechView &mv, Mech &m, const Extract &c, f32 dt) {
    if (m.clock == 0.0f) {
        m.at.y = mech_support(c, m.at, m.at.y + 0.5f, nullptr);
        if (mv.demo == MECH_DEMO_STAND || mv.demo == MECH_DEMO_WALK) mech_stand_now(mv, m, c);
        m.clock = 1e-3f;
    }
    // Ближний человек: на него смотрит голова и ствол
    MechTarget tg{};
    if (c.person_gen) {
        f32 best = MECH_SEE_M * MECH_SEE_M;
        for (u32 p = 0; p < PEOPLE_CAPACITY; ++p) {
            if (!c.person_gen[p]) continue;
            Vec2 d = wpos_delta_xz(m.at.cx, m.at.cz, m.at.x, m.at.z, c.person_cx[p], c.person_cz[p], c.person_x[p], c.person_z[p]);
            f32 d2 = dot(d, d);
            if (d2 >= best) continue;
            best = d2;
            tg.on = true;
            tg.at = WPos{c.person_cx[p], c.person_cz[p], c.person_x[p], c.person_y[p], c.person_z[p]};
            tg.dist = f_sqrt(d2);
        }
    }
    // Жизнь идёт мелкими шагами: пружины не зависят от частоты кадров
    u32 steps = (u32)(dt * 120.0f) + (dt > 0.0f ? 1 : 0);
    for (u32 k = 0; k < steps; ++k) mech_step(mv, m, c, tg, dt / (f32)steps);
}

static u32 mech_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

void mech_view_build(MechView &mv, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderInstance *inst,
                     u32 &ni, RenderFrame &out) {
    const Extract &c = *in.cur;
    if (!c.terrain || !c.coll || !mech_bind(mv, assets)) return;
    const ViewBody &body = assets.bodies[mv.body];
    const Skel &s = body.skel[0];
    const u32 nb = (u32)s.bones.count;
    u32 count = 0;
    for (u32 i = 0; i < MECH_MAX; ++i) count += mv.mech[i].live ? 1 : 0;
    if (!count) return;
    usize nb0 = out.bones.count, ns0 = out.skinned.count;
    RenderBone *bones = arena_push<RenderBone>(frame, nb0 + (usize)count * nb);
    if (nb0) memcpy(bones, out.bones.data, nb0 * sizeof(RenderBone));
    RenderSkinned *sk = arena_push<RenderSkinned>(frame, ns0 + count);
    if (ns0) memcpy(sk, out.skinned.data, ns0 * sizeof(RenderSkinned));
    usize nbo = nb0, nso = ns0;
    const f32 dt = f_clamp(in.dt, 0.0f, 0.1f);
    for (u32 i = 0; i < MECH_MAX; ++i) {
        Mech &m = mv.mech[i];
        if (!m.live || ni >= RENDER_MAX_INSTANCES) continue;
        if (!m.copy) m.copy = render_skinned_create(body.skin[0]);
        if (!m.copy) continue;
        mech_live(mv, m, c, dt);
        Vec3 wp[SKEL_MAX_BONES];
        mech_pose(mv, m, s, (PoseMatrix *)(bones + nbo), wp);
        RenderInstance &it = inst[ni];
        it = RenderInstance{};
        it.cx = m.at.cx;
        it.cz = m.at.cz;
        it.x = m.at.x;
        it.y = m.at.y;
        it.z = m.at.z;
        it.rot_xy = mech_snorm2(0.0f, sinf(m.yaw * 0.5f));
        it.rot_zw = mech_snorm2(0.0f, cosf(m.yaw * 0.5f));
        it.scale = 1.0f;
        it.mesh = m.copy;
        it.material = RENDER_NO_MATERIAL;
        it.seed = m.prop;
        RenderSkinned &o = sk[nso++];
        o = RenderSkinned{};
        o.instance = ni;
        o.first_bone = (u32)nbo;
        ni++;
        nbo += nb;
    }
    out.bones = {bones, nbo};
    out.skinned = {sk, nso};
}

void mech_view_follow(const MechView &mv, WPos &pivot, f32 dt) {
    if (!mv.mech[0].live) return;
    Vec2 d = wpos_delta_xz(pivot, mv.mech[0].at);   // место машины — с прошлого кадра
    f32 k = f_min(1.0f, 3.0f * dt);
    pivot.x += d.x * k;
    pivot.z += d.y * k;
    wpos_rebase(pivot);
}

bool mech_view_trace(MechView &mv, const Skel &s, const Extract &c, f32 dt, char *line, usize cap) {
    if (!mv.bound) {
        mv.bound = true;
        mv.failed = !mech_bind_skel(mv, s);
    }
    Mech &m = mv.mech[0];
    if (mv.failed || !m.live || !c.terrain || !c.coll) return false;
    mech_live(mv, m, c, dt);
    Vec3 wp[SKEL_MAX_BONES];
    mech_pose(mv, m, s, nullptr, wp);
    Vec2 d = wpos_delta_xz(m.home, m.at);
    int n = snprintf(line, cap, "%u %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f", (u32)m.state, d.x, m.at.y - m.home.y, d.y,
                     m.yaw, m.h, m.pitch, m.roll, m.hull, m.gun, m.eye);
    // Точки костей — в метрах от дома (мир): рама, корпус, ствол, голова, по ноге — пять суставов и флаг «стоит»
    auto put = [&](i32 b) {
        Vec3 w = mech_to_world(m.yaw, wp[b]);
        if (n > 0 && (usize)n < cap) n += snprintf(line + n, cap - (usize)n, " %.3f %.3f %.3f", d.x + w.x, m.at.y - m.home.y + w.y, d.y + w.z);
    };
    put(mv.chassis);
    put(mv.hull);
    put(mv.gun);
    put(mv.eye);
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        for (u32 j = 0; j < 5; ++j) put(mv.leg[k][j]);
        if (n > 0 && (usize)n < cap) n += snprintf(line + n, cap - (usize)n, " %d", m.leg[k].fold > 0.0f ? 2 : (m.leg[k].t >= 0.0f ? 0 : 1));
    }
    return n > 0 && (usize)n < cap;
}
