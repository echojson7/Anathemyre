// Общее для машин (docs/vezha.md, docs/tat.md): описания видов, решение ноги, опора и путь
#include "mech_in.h"

static constexpr f32 DEG = PI_F / 180.0f;

// Вежа — прежние константы (ноги одинаковые, лапа поднимается в пачку у рамы); Тать — четыре разные ноги, лёжа
// лапы остаются на земле. Порядок ног — fr, fl, rl, rr (по кругу)
const MechDesc MECH_DESC[MECH_KINDS] = {
    {"vezha", 0.20f, 0.86f, 1.16f, 0.50f, 1.5f, 3.3f, 16.0f, 0.26f, 0.5f,
     {{MECH_LEG_PAR, 1.0f, 0.08f, -1.536f, 0.558f, 0.48f, 0.40f, {-2.3561945f, 0.0f, 0.0f}, 1.0f},
      {MECH_LEG_PAR, 1.0f, 0.08f, -1.536f, 0.558f, 0.48f, 0.40f, {-2.3561945f, 0.0f, 0.0f}, 1.0f},
      {MECH_LEG_PAR, 1.0f, 0.08f, -1.536f, 0.558f, 0.48f, 0.40f, {-2.3561945f, 0.0f, 0.0f}, 1.0f},
      {MECH_LEG_PAR, 1.0f, 0.08f, -1.536f, 0.558f, 0.48f, 0.40f, {-2.3561945f, 0.0f, 0.0f}, 1.0f}}},
    {"tat", 0.15f, 0.62f, 0.80f, 0.44f, 0.5f, 2.4f, 24.0f, 0.15f, 0.45f,
     {{MECH_LEG_PAR, 1.0f, 0.06f, -70.0f * DEG, 30.0f * DEG, 0.30f, 0.42f, {-58.0f * DEG, 0.22f, -0.09f}, 0.8f},      // упор
      {MECH_LEG_VERT, 1.0f, 0.045f, -40.0f * DEG, 78.0f * DEG, 0.36f, 0.50f, {62.0f * DEG, 0.30f, -0.105f}, 1.5f},   // ходуля
      {MECH_LEG_VERT, 1.0f, 0.04f, -62.0f * DEG, 55.0f * DEG, 0.32f, 0.40f, {-55.0f * DEG, 0.30f, -0.11f}, 0.6f},    // лыжа
      {MECH_LEG_PAR, 1.0f, 0.05f, -70.0f * DEG, 30.0f * DEG, 0.30f, 0.40f, {52.0f * DEG, 0.24f, -0.10f}, 1.1f}}},    // жало
};

const char *mech_kind_name(u8 kind) { return kind < MECH_KINDS ? MECH_DESC[kind].name : "?"; }

void mech_leg_solve(const MechLegRig &L, const MechLegDesc &D, f32 x, f32 y, f32 *thigh, f32 *shin, f32 *tarsus) {
    f32 la, lb, up;
    mech_leg_links(L, D, &la, &lb, &up);
    f32 ty = y + up;
    f32 d = f_clamp(f_sqrt(x * x + ty * ty), mech_leg_min(L, D), la + lb - 1e-4f);
    f32 a = atan2f(ty, x) + D.knee * acosf(f_clamp((la * la + d * d - lb * lb) / (2.0f * la * d), -1.0f, 1.0f));
    a = f_clamp(a, D.th_min, D.th_max);
    *thigh = a;
    *shin = atan2f(ty - la * sinf(a), x - la * cosf(a));
    *tarsus = D.mode == MECH_LEG_VERT ? -0.5f * PI_F : a;
}

void mech_leg_hlimit(const MechView &mv, const Mech &m, u32 k, f32 &hmin, f32 &hmax) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    const MechLegDesc &D = mech_desc(m).leg[k];
    f32 la, lb, up;
    mech_leg_links(L, D, &la, &lb, &up);
    MechReach r = mech_leg_reach(L, mech_world_chassis(m, m.leg[k].at, D.foot_h));
    f32 lo = mech_leg_min(L, D), hi = mech_leg_max(L, D);
    f32 drop = m.h + r.y + up;   // на столько цель решения выше, чем была бы при высоте 0
    if (r.x < lo) hmin = f_max(hmin, f_sqrt(lo * lo - r.x * r.x) + drop);
    if (r.x < hi) hmax = f_min(hmax, f_sqrt(hi * hi - r.x * r.x) + drop);
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
    m.mast = m.mast_to = 0.0f;
}
