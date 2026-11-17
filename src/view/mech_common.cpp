// Общее для машин (docs/vezha.md, docs/tat.md): описания видов, решение ноги, опора и путь
#include "mech_in.h"

static constexpr f32 DEG = PI_F / 180.0f;

// Вежа — четыре паучьи ноги (колено вверх, стойка голеностопа отвесна; лёжа рама на брюхе, лапы на земле, ноги
// повёрнуты к носу и корме, колени торчат вверх); Тать — четыре одинаковые ноги с двумя суставами (колено выше
// крыши, плюсна наклонна — tools/parts/tat_leg.py), тело висит под ними, лёжа лапы остаются на земле.
// Порядок ног — fr, fl, rl, rr (по кругу). Числа Вежи — как в tools/parts/vezha_leg.py и vezha_pose.py
const MechDesc MECH_DESC[MECH_KINDS] = {
    {"vezha", 0.21f, 0.82f, 1.0f, 0.58f, 0.4f, 3.0f, 16.0f, 0.3f, 0.45f,
     {{MECH_LEG_VERT, 1.0f, 0.065f, -40.0f * DEG, 95.0f * DEG, 0.92f, 0.92f, {-20.0f * DEG, 0.95f, -0.145f}, 1.3f, 0.1f},
      {MECH_LEG_VERT, 1.0f, 0.065f, -40.0f * DEG, 95.0f * DEG, 0.92f, 0.92f, {20.0f * DEG, 0.95f, -0.145f}, 1.3f, 0.1f},
      {MECH_LEG_VERT, 1.0f, 0.065f, -40.0f * DEG, 95.0f * DEG, 0.92f, 0.92f, {-20.0f * DEG, 0.95f, -0.145f}, 1.3f, 0.1f},
      {MECH_LEG_VERT, 1.0f, 0.065f, -40.0f * DEG, 95.0f * DEG, 0.92f, 0.92f, {20.0f * DEG, 0.95f, -0.145f}, 1.3f, 0.1f}}},
    {"tat", 0.37f, 0.62f, 0.74f, 0.50f, 0.5f, 2.4f, 24.0f, 0.37f, 0.25f,
     {{MECH_LEG_VERT, 1.0f, 0.07f, -40.0f * DEG, 100.0f * DEG, 0.32f, 0.38f, {0.0f, 0.30f, -0.30f}, 1.0f, 0.0f},
      {MECH_LEG_VERT, 1.0f, 0.07f, -40.0f * DEG, 100.0f * DEG, 0.32f, 0.38f, {0.0f, 0.30f, -0.30f}, 1.0f, 0.0f},
      {MECH_LEG_VERT, 1.0f, 0.07f, -40.0f * DEG, 100.0f * DEG, 0.32f, 0.38f, {0.0f, 0.30f, -0.30f}, 1.0f, 0.0f},
      {MECH_LEG_VERT, 1.0f, 0.07f, -40.0f * DEG, 100.0f * DEG, 0.32f, 0.38f, {0.0f, 0.30f, -0.30f}, 1.0f, 0.0f}}},
};

const char *mech_kind_name(u8 kind) { return kind < MECH_KINDS ? MECH_DESC[kind].name : "?"; }

void mech_leg_solve(const MechLegRig &L, const MechLegDesc &D, f32 x, f32 y, f32 *thigh, f32 *shin, f32 *tarsus) {
    f32 la, lb, ox, oy;
    mech_leg_links(L, D, &la, &lb, &ox, &oy);
    f32 tx = x + ox, ty = y + oy;
    f32 d = f_clamp(f_sqrt(tx * tx + ty * ty), mech_leg_min(L, D), la + lb - 1e-4f);
    f32 a = atan2f(ty, tx) + D.knee * acosf(f_clamp((la * la + d * d - lb * lb) / (2.0f * la * d), -1.0f, 1.0f));
    a = f_clamp(a, D.th_min, D.th_max);
    *thigh = a;
    *shin = atan2f(ty - la * sinf(a), tx - la * cosf(a));
    *tarsus = D.mode == MECH_LEG_VERT ? L.a3 : a;
}

void mech_leg_hlimit(const MechView &mv, const Mech &m, u32 k, f32 &hmin, f32 &hmax) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    const MechLegDesc &D = mech_desc(m).leg[k];
    f32 la, lb, ox, oy;
    mech_leg_links(L, D, &la, &lb, &ox, &oy);
    MechReach r = mech_leg_reach(L, mech_world_chassis(m, m.leg[k].at, D.foot_h));
    f32 lo = mech_leg_min(L, D), hi = mech_leg_max(L, D);
    f32 tx = r.x + ox;
    f32 drop = m.h + r.y + oy;   // на столько цель решения выше, чем была бы при высоте 0
    if (tx < lo) hmin = f_max(hmin, f_sqrt(lo * lo - tx * tx) + drop);
    if (tx < hi) hmax = f_min(hmax, f_sqrt(hi * hi - tx * tx) + drop);
}

Vec3 mech_polar(const MechView &mv, const Mech &m, u32 k, Vec3 pol) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    f32 phi = atan2f(-L.dir.z, L.dir.x) + pol.x;
    return L.corner + Vec3{cosf(phi) * pol.y, pol.z, -sinf(phi) * pol.y};
}

WPos mech_chassis_world(const Mech &m, Vec3 p) {
    Vec3 b = quat_rotate(mech_tilt(m), p) + mech_trans(m);
    Vec3 w = mech_to_world(m.yaw, b);
    WPos q = mech_moved(m.at, w.x, w.z);
    q.y = m.at.y + w.y;
    return q;
}

Vec3 mech_world_chassis(const Mech &m, const WPos &p, f32 lift) {
    Vec2 d = wpos_delta_xz(m.at, p);
    Vec3 b = mech_to_body(m.yaw, Vec3{d.x, p.y + lift - m.at.y, d.y}) - mech_trans(m);
    return quat_rotate(quat_conj(mech_tilt(m)), b);
}

WPos mech_leg_ground(const MechView &mv, const Mech &m, const Extract &c, u32 k, Vec3 pol) {
    WPos q = mech_chassis_world(m, mech_polar(mv, m, k, pol));
    q.y = mech_support(c, q, m.at.y + 0.3f, nullptr);
    return q;
}

// Стена или уступ на месте p: коробка выше шага и ниже крыши машины
static bool mech_blocked(const Extract &c, const WPos &p, f32 ground) {
    static const f32 OFF[5][2] = {{0, 0}, {0.6f, 0}, {-0.6f, 0}, {0, 0.6f}, {0, -0.6f}};
    for (u32 i = 0; i < 5; ++i) {
        WPos q = mech_moved(p, OFF[i][0], OFF[i][1]);
        f32 hgt = mech_support(c, q, ground + 0.2f, nullptr);
        if (f_abs(hgt - ground) > 0.45f) return true;
        Span<const u16> near = colliders_near(*c.coll, q.cx, q.cz, q.x, q.z);
        for (usize j = 0; j < near.count; ++j) {
            u32 k = near.data[j];
            if (c.coll->y1[k] > ground + 0.4f && c.coll->y0[k] < ground + 1.9f && coll_touches(*c.coll, k, q.cx, q.cz, q.x, q.z, 0.45f))
                return true;
        }
    }
    return false;
}

bool mech_path_clear(const Extract &c, const WPos &a, const WPos &b) {
    Vec2 d = wpos_delta_xz(a, b);
    f32 len = length(d);
    u32 n = (u32)(len / 0.5f) + 1;
    f32 ground = mech_support(c, a, a.y + 0.3f, nullptr);
    for (u32 i = 1; i <= n; ++i) {
        f32 t = (f32)i / (f32)n;
        WPos q = mech_moved(a, d.x * t, d.y * t);
        f32 hgt = mech_support(c, q, ground + 0.3f, nullptr);
        if (f_abs(hgt - ground) > 0.3f || mech_blocked(c, q, hgt)) return false;
        ground = hgt;
    }
    return true;
}

void mech_stand_now(const MechView &mv, Mech &m, const Extract &c) {
    const MechDesc &d = mech_desc(m);
    m.h = m.h_to = d.h_stand;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        l.fold = 0.0f;
        l.t = -1.0f;
        l.at = mech_leg_ground(mv, m, c, k, Vec3{0.0f, mech_reach(mv, m, k, m.h), 0.0f});
        mech_support(c, l.at, l.at.y + 0.2f, &l.n);
    }
    m.state = MECH_STAND;
    m.wait = 1.0f;
    m.points_left = 3;
    m.stow = m.stow_to = 0.0f;
}
