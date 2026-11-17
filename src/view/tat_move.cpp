// Жизнь «Тати» (docs/tat.md): поведение, ход, перестановка лап, лёжка с выстрелом, морок. Поза по этим данным —
// mech.cpp, опора и путь — mech_common.cpp. Походки свои, не как у Вежи: крадётся по одной ноге (боковая
// последовательность, тело переносит вес на три опоры) и перебегает парами «перед — зад» с качкой
#include "mech_in.h"

constexpr f32 TAT_H_PRONE = 0.19f;       // оси бёдер над опорой в лёжке: брюхо почти на земле
constexpr f32 TAT_STEP_STALK = 0.26f;    // шаг крадучись, с
constexpr f32 TAT_STEP_DASH = 0.12f;     // шаг перебежки, с
constexpr f32 TAT_VEIL_IN = 1.2f;        // морок наводится, с
constexpr f32 TAT_VEIL_OUT = 0.25f;      // морок спадает, с
constexpr f32 TAT_BIPOD_LEG = 0.44f;     // нога сошек от шарнира (tools/parts/tat.py)
constexpr f32 TAT_BIPOD_UP = 0.226f;     // шарнир сошек над осями бёдер при ровном стволе
constexpr f32 TAT_BIPOD_AHEAD = 0.72f;   // шарнир сошек впереди оси машины
constexpr f32 TAT_BIPOD_DIP = 0.0785f;   // нога сошек в бинде опущена от ствола, рад
constexpr f32 TAT_TURRET_X = 0.12f;      // башенка правее оси машины
constexpr f32 TAT_TURRET_MAX = 1.75f;    // башенка доворачивает не дальше, рад: дальше мешает мачта

// Крадучись ноги идут по кругу «зад-лево → перед-лево → зад-право → перед-право» (fr = 0, fl = 1, rl = 2, rr = 3)
static const u8 TAT_SEQ[MECH_LEGS] = {2, 1, 3, 0};
constexpr f32 TAT_FEET_GAP = 0.32f;   // лапы не ближе друг к другу, м
// Лёжка: поворот бедра от бинда и вынос оси лапы от оси качания — передние ноги раскинуты вперёд, задние назад
static const f32 TAT_PRONE[MECH_LEGS][2] = {{-0.49f, 0.77f}, {0.52f, 0.66f}, {0.21f, 0.52f}, {-0.31f, 0.91f}};

// Где лапе k стоять сейчас: под своим углом рамы (в лёжке — раскинута), со сдвигом вперёд по ходу и повороту
static WPos tat_ideal(const MechView &mv, const Mech &m, u32 k, f32 ahead) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    // Лёжка раскидывает лапы по мере того, как тело опускается ниже обычного присеста (и собирает, когда встаёт):
    // лапа, поставленная далеко при высоком теле, не дотянулась бы
    Vec3 home = L.corner + L.dir * mech_reach(mv, m, k, m.h);
    if (m.state == MECH_PRONE || m.state == MECH_STAND || m.state == MECH_WALK) {
        f32 lo = mech_desc(m).h_low;
        f32 t = mech_smooth((lo - m.h) / (lo - TAT_H_PRONE));
        if (m.h < lo) {
            Vec3 d = mech_to_world(TAT_PRONE[k][0] * t, L.dir);   // поворот вокруг вертикали в осях рамы
            home = L.corner + d * (L.u + mech_mix(mech_reach(mv, m, k, lo) - L.u, TAT_PRONE[k][1], t));
        }
    }
    Vec3 w = mech_to_world(m.yaw + m.yaw_v * ahead, Vec3{home.x, 0, home.z});
    WPos to = mech_moved(m.at, w.x + m.vel.x * ahead, w.z + m.vel.z * ahead);
    // Не ставить лапу вплотную к стоящей соседней (поворот на месте сводит правые или левые лапы)
    for (u32 j = 0; j < MECH_LEGS; ++j) {
        const MechLeg &o = m.leg[j];
        if (j == k || o.fold > 0.0f || o.t >= 0.0f) continue;
        Vec2 d = wpos_delta_xz(o.at, to);
        f32 len = length(d);
        if (len < TAT_FEET_GAP && len > 1e-4f) to = mech_moved(to, d.x * (TAT_FEET_GAP / len - 1.0f), d.y * (TAT_FEET_GAP / len - 1.0f));
    }
    return to;
}

// Следующая точка обхода: вокруг дома, по свободной прямой. false — некуда
static bool tat_pick_point(Mech &m, const Extract &c) {
    const MechDesc &md = mech_desc(m);
    for (u32 tries = 0; tries < 8; ++tries) {
        f32 a = mech_rnd(m.rng) * TAU_F, r = 3.0f + 5.0f * mech_rnd(m.rng);
        WPos p = mech_moved(m.home, sinf(a) * r, cosf(a) * r);
        if (length(wpos_delta_xz(m.at, p)) < 2.5f || !mech_path_clear(c, m.at, p)) continue;
        m.goal = p;
        m.goal_home = false;
        bool dash = mech_rnd(m.rng) < 0.3f;
        m.speed_max = dash ? md.v_dash : md.v_walk;
        m.gait = dash ? 1 : 0;
        m.veil_to = !dash && mech_rnd(m.rng) < 0.5f ? 1.0f : 0.0f;
        return true;
    }
    return false;
}

static void tat_go(Mech &m, const Extract &c, const WPos &to, bool home, bool dash) {
    const MechDesc &md = mech_desc(m);
    m.goal = to;
    m.goal_home = home;
    m.gait = dash ? 1 : 0;
    m.speed_max = dash ? md.v_dash : md.v_walk;
    if (!home && !mech_path_clear(c, m.at, m.goal) && !tat_pick_point(m, c)) {
        m.wait = 1.0f;
        return;
    }
    mech_enter(m, MECH_WALK);
}

// Лёжка: прицелиться в точку и выстрелить shots раз
static void tat_prone(Mech &m, const WPos &aim) {
    m.aim = aim;
    m.aim_on = true;
    m.aim_t = 1.6f;
    m.shots = 0;
    mech_enter(m, MECH_PRONE);
}

// Показ (--mech show): по порядку всё, что машина умеет. Зовётся стоя, когда пора что-то делать
static void tat_show(Mech &m, const Extract &c) {
    static const f32 PTS[3][2] = {{0.8f, 3.2f}, {-2.4f, 4.6f}, {3.4f, 10.4f}};   // точки показа от дома, в его осях
    auto at = [&](u32 i) {
        Vec3 w = mech_to_world(m.home_yaw, Vec3{PTS[i][0], 0, PTS[i][1]});
        return mech_moved(m.home, w.x, w.z);
    };
    u32 step = m.show++;
    switch (step) {
    case 0:   // привстала, осмотрелась
        m.h_to = mech_desc(m).h_high;
        m.mood_t = 1.6f;
        m.wait = 2.0f;
        break;
    case 1: tat_go(m, c, at(0), false, false); break;   // крадётся к первой точке
    case 2: {   // лёжка: цель в 40 м впереди по курсу дома
        Vec3 w = mech_to_world(m.home_yaw + 0.25f, Vec3{0, 0, 40.0f});
        WPos p = mech_moved(m.at, w.x, w.z);
        p.y = mech_support(c, p, m.at.y + 30.0f, nullptr) + 1.0f;
        tat_prone(m, p);
        break;
    }
    case 3:   // навела морок, крадётся ко второй точке
        m.veil_to = 1.0f;
        tat_go(m, c, at(1), false, false);
        break;
    case 4:   // сняла морок, перебежка к дальней точке
        m.veil_to = 0.0f;
        tat_go(m, c, at(2), false, true);
        break;
    case 5: tat_go(m, c, m.home, true, true); break;   // перебежкой домой
    default:
        m.show = 0;
        m.wait = 1.0f;
        mech_enter(m, MECH_FOLD);
        break;
    }
}

// Поведение: что делать дальше. Только меняет цель, высоту, мачту, сошки, морок и состояние; тело и лапы едут сами
static void tat_brain(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    const MechDesc &md = mech_desc(m);
    m.state_t += dt;
    m.clock += dt;
    bool planted = true, folded = true;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        if (m.leg[k].fold > 0.0f || m.leg[k].t >= 0.0f) planted = false;
        if (m.leg[k].fold < 1.0f) folded = false;
    }
    static const f32 ORDER[MECH_LEGS] = {0.36f, 0.12f, 0.0f, 0.24f};   // по одной, с задней левой
    switch (m.state) {
    case MECH_PACKED:
        m.h_to = md.belly;
        m.mast_to = 1.0f;
        m.wait -= dt;
        if (mv.demo == MECH_DEMO_PACK) break;
        if (m.wait <= 0.0f || (tg.on && tg.dist < 6.0f && m.state_t > 1.5f) || (mv.demo == MECH_DEMO_SHOW && m.state_t > 2.0f)) {
            for (u32 k = 0; k < MECH_LEGS; ++k) {
                Vec3 pol{0.0f, mech_reach(mv, m, k, md.belly), md.leg[k].foot_h - md.belly};
                WPos g = mech_leg_ground(mv, m, c, k, pol);
                pol.z += g.y - m.at.y;   // неровная земля: лапа встанет на свою высоту
                m.leg[k].pol = pol;
            }
            mech_enter(m, MECH_UNFOLD);
        }
        break;
    case MECH_UNFOLD:
        for (u32 k = 0; k < MECH_LEGS; ++k) {
            MechLeg &l = m.leg[k];
            if (l.fold <= 0.0f || m.state_t < 0.25f + ORDER[k] * 1.6f) continue;
            l.fold -= dt / 0.5f;
            if (l.fold <= 0.0f) {
                l.fold = 0.0f;
                l.at = mech_leg_ground(mv, m, c, k, l.pol);
                l.n = Vec3{0, 1, 0};
                mech_support(c, l.at, l.at.y + 0.2f, &l.n);
                l.t = -1.0f;
            }
        }
        if (planted) {
            m.h_to = md.h_stand;
            if (m.h > md.h_stand * 0.6f) m.mast_to = 0.0f;   // мачта поднимается, когда тело уже встаёт
            if (m.h > md.h_stand - 0.06f) {
                m.wait = 0.8f + mech_rnd(m.rng);
                m.show = 0;
                m.points_left = 2 + (u32)(mech_rnd(m.rng) * 2.99f);
                mech_enter(m, MECH_STAND);
            }
        }
        break;
    case MECH_STAND: {
        m.mood_t -= dt;
        if (m.mood_t <= 0.0f) m.h_to = md.h_stand;
        m.mast_to = 0.0f;
        m.bipod_to = 0.0f;
        m.wait -= dt;
        if (m.wait > 0.0f) break;
        if (mv.demo == MECH_DEMO_SHOW) {
            tat_show(m, c);
            break;
        }
        if (mv.prone_demo) {   // --mech prone: лёжка с целью в 40 м по курсу дома
            Vec3 w = mech_to_world(m.home_yaw, Vec3{0, 0, 40.0f});
            WPos p = mech_moved(m.at, w.x, w.z);
            p.y = mech_support(c, p, m.at.y + 30.0f, nullptr) + 1.0f;
            tat_prone(m, p);
            break;
        }
        // Человек в поле зрения, но не вплотную — залечь и прицелиться
        if (mv.demo == MECH_DEMO_AUTO && tg.on && tg.dist > 6.0f) {
            WPos p = tg.at;
            p.y += 1.25f;
            tat_prone(m, p);
            break;
        }
        f32 r = mech_rnd(m.rng);
        if (mv.demo == MECH_DEMO_STAND || r < 0.25f) {
            m.h_to = mech_rnd(m.rng) < 0.5f ? md.h_high : md.h_low;
            m.mood_t = 1.4f + 1.4f * mech_rnd(m.rng);
            m.wait = m.mood_t + 0.8f + mech_rnd(m.rng);
            break;
        }
        if (m.points_left > 0 || mv.demo == MECH_DEMO_WALK) {
            if (tat_pick_point(m, c)) {
                if (m.points_left) m.points_left--;
                mech_enter(m, MECH_WALK);
            } else {
                m.wait = 1.0f;
            }
            break;
        }
        m.veil_to = 0.0f;
        Vec2 d = wpos_delta_xz(m.at, m.home);
        if (length(d) > 0.35f || f_abs(mech_wrap(m.home_yaw - m.yaw)) > 0.12f) {
            tat_go(m, c, m.home, true, false);
        } else {
            m.wait = 1.0f;
            mech_enter(m, MECH_FOLD);
        }
        break;
    }
    case MECH_WALK: {
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        f32 speed = length(m.vel);
        // Крадётся низко, перебегает чуть выше и с наклоном
        m.h_to = m.gait ? md.h_stand - 0.08f : md.h_low + 0.04f;
        bool turned = !m.goal_home || f_abs(mech_wrap(m.home_yaw - m.yaw)) < 0.08f;
        if (length(d) < 0.12f && speed < 0.12f && turned) {
            if (mv.demo == MECH_DEMO_SHOW && m.show == 4) m.veil_to = 0.0f;   // дошла под мороком — сняла
            m.wait = mv.demo == MECH_DEMO_SHOW ? 0.6f : 1.0f + 2.0f * mech_rnd(m.rng);
            mech_enter(m, MECH_STAND);
        }
        break;
    }
    case MECH_PRONE: {
        m.h_to = TAT_H_PRONE;
        m.mast_to = 0.0f;
        // Цель — человек, если виден; иначе прежняя точка
        if (tg.on && mv.demo == MECH_DEMO_AUTO) {
            m.aim = tg.at;
            m.aim.y += 1.25f;
        }
        bool low = m.h < TAT_H_PRONE + 0.04f && planted;
        m.bipod_to = low ? 1.0f : 0.0f;
        if (low && m.bipod > 0.9f) m.aim_t -= dt;
        if (m.aim_t <= 0.0f && m.shots < 2) {
            // Выстрел: откат тела, ствол подбрасывает, антенны хлещут, морок спадает
            m.kick_v += 1.5f;
            m.gun_v += 2.2f;
            m.pitch_v += 0.5f;
            m.h_v += 0.08f;
            for (u32 a = 0; a < 2; ++a) m.ant_v[a][0] += 1.4f;
            m.veil_to = 0.0f;
            m.shots++;
            m.aim_t = m.shots < 2 ? 1.3f + 0.6f * mech_rnd(m.rng) : 1.2f;
        }
        if (mv.prone_demo && m.shots >= 2 && m.aim_t <= 0.0f) {   // --mech prone: лежит и стреляет дальше
            m.shots = 0;
            m.aim_t = 2.5f;
        }
        if (m.shots >= 2 && m.aim_t <= 0.0f) {
            m.bipod_to = 0.0f;
            m.aim_on = false;
            m.wait = mv.demo == MECH_DEMO_SHOW ? 0.9f : 1.0f + mech_rnd(m.rng);
            mech_enter(m, MECH_STAND);
        }
        if (!tg.on && mv.demo == MECH_DEMO_AUTO && m.state_t > 6.0f) {   // цель ушла
            m.aim_on = false;
            mech_enter(m, MECH_STAND);
        }
        break;
    }
    case MECH_FOLD:
        m.mast_to = 1.0f;
        m.veil_to = 0.0f;
        if (m.state_t < 0.05f) m.h_to = md.belly;
        if (m.h < md.belly + 0.012f && (planted || m.wait < 0.0f)) {
            // Лежит: ноги поджимаются по очереди, лапы остаются на земле
            if (m.wait >= 0.0f) {
                m.wait = -1.0f;
                m.state_t = 0.0f;
                for (u32 k = 0; k < MECH_LEGS; ++k) {
                    const MechLegRig &L = mech_rig(mv, m).leg[k];
                    MechReach r = mech_leg_reach(L, mech_world_chassis(m, m.leg[k].at, md.leg[k].foot_h));
                    m.leg[k].pol = Vec3{r.yaw, r.x + L.u, r.y};
                }
            }
            for (u32 k = 0; k < MECH_LEGS; ++k)
                if (m.state_t > ORDER[MECH_LEGS - 1 - k] * 1.6f) m.leg[k].fold = f_min(1.0f, m.leg[k].fold + dt / 0.55f);
            if (folded) {
                m.wait = 10.0f + 8.0f * mech_rnd(m.rng);
                mech_enter(m, MECH_PACKED);
            }
        }
        break;
    }
    // --mech veil: стоит, морок наводится и спадает по кругу (3.5 с под мороком, 2.5 с видна)
    if (mv.veil_demo && m.state == MECH_STAND) m.veil_to = fmodf(m.clock, 6.0f) < 3.5f ? 1.0f : 0.0f;
}

// Ход тела: к цели с разгоном и торможением, курс — по ходу; в лёжке — на цель
static void tat_body(Mech &m, f32 dt) {
    Vec3 want{0, 0, 0};
    f32 yaw_to = m.yaw;
    f32 accel = m.gait ? 3.4f : 1.6f, wmax = m.gait ? 1.6f : 0.8f;
    if (m.state == MECH_WALK) {
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        f32 dist = length(d);
        f32 turn = 0.0f;
        if (dist > 1.2f) {
            yaw_to = atan2f(d.x, d.y);
            turn = f_abs(mech_wrap(yaw_to - m.yaw));
        } else if (m.goal_home) {
            yaw_to = m.home_yaw;
        }
        if (dist > 0.02f) {
            f32 v = f_min(m.speed_max, f_sqrt(2.0f * accel * 0.6f * dist));
            v *= f_clamp(1.3f - turn / 1.0f, 0.15f, 1.0f) * f_clamp(2.2f - m.lag, 0.2f, 1.0f);
            want = Vec3{d.x / dist * v, 0, d.y / dist * v};
        }
    } else if (m.state == MECH_PRONE && m.aim_on) {
        // Рама доворачивает к цели, пока башенке далеко; тонко правит башенка
        Vec2 d = wpos_delta_xz(m.at, m.aim);
        f32 to = atan2f(d.x, d.y);
        if (f_abs(mech_wrap(to - m.yaw)) > 0.35f || m.h > TAT_H_PRONE + 0.1f) yaw_to = to;
        wmax = 0.6f;
    }
    Vec3 dv = want - m.vel;
    f32 l = length(dv), amax = accel * dt;
    Vec3 old = m.vel;
    if (l > amax) dv = dv * (amax / l);
    m.vel += dv;
    Vec3 a = (m.vel - old) * (1.0f / dt);
    m.acc = m.acc + (a - m.acc) * f_min(1.0f, 8.0f * dt);
    m.at = mech_moved(m.at, m.vel.x * dt, m.vel.z * dt);
    f32 err = mech_wrap(yaw_to - m.yaw);
    m.yaw_v += (25.0f * err - 10.0f * m.yaw_v) * dt;
    wmax *= f_clamp(2.2f - m.lag, 0.25f, 1.0f);
    m.yaw_v = f_clamp(m.yaw_v, -wmax, wmax);
    m.yaw = mech_wrap(m.yaw + m.yaw_v * dt);
}

static void tat_lift(const MechView &mv, Mech &m, const Extract &c, u32 k, f32 dur, f32 ahead, f32 speed) {
    MechLeg &l = m.leg[k];
    l.from = l.at;
    l.dur = dur * (1.0f + 0.1f * (mech_rnd(m.rng) - 0.5f));
    WPos to = tat_ideal(mv, m, k, l.dur + ahead);
    to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
    l.to = to;
    f32 lift = (m.gait ? 0.06f + 0.02f * speed : 0.09f) * mech_desc(m).leg[k].lift;
    l.lift = lift + 0.5f * f_abs(to.y - l.from.y);
    l.t = 0.0f;
}

// Лапы. Крадучись: по одной в своей очереди, следующая — когда предыдущая почти встала. Перебежкой: парами
// «перед — зад», задняя пара толкает, передняя ловит; на скорости пары в воздухе перекрываются (полёт)
static void tat_legs(const MechView &mv, Mech &m, const Extract &c, f32 dt) {
    f32 speed = length(m.vel) + f_abs(m.yaw_v) * 0.8f;
    f32 dur = m.gait ? TAT_STEP_DASH : TAT_STEP_STALK;
    f32 cycle = m.gait ? 2.0f * dur : 4.0f * dur;
    f32 thr = 0.035f + 0.5f * speed * cycle;
    f32 err[MECH_LEGS];
    u32 air = 0;
    f32 late = 1.0f;   // самая ранняя фаза среди лап в шаге
    m.lag = 0.0f;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        err[k] = 0.0f;
        if (l.fold > 0.0f) continue;
        if (l.t >= 0.0f) {
            l.t += dt / l.dur;
            if (l.t < 0.7f) {
                WPos to = tat_ideal(mv, m, k, (1.0f - l.t) * l.dur + 0.5f * cycle);
                to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
                l.to = to;
            }
            if (l.t >= 1.0f) {
                l.t = -1.0f;
                l.at = l.to;
                m.h_v -= 0.03f + 0.02f * speed;
                if (m.gait && speed > 1.0f) m.pitch_v += k < 2 ? -0.25f : 0.3f;   // передние ловят — нос вниз, задние — вверх
                m.veil -= 0.14f * m.veil;   // касание выдаёт: морок проседает
                u32 a = k & 1;
                m.ant_v[a][0] += 0.4f * (mech_rnd(m.rng) - 0.5f);
                m.ant_v[a][1] += 0.4f * (mech_rnd(m.rng) - 0.5f);
            } else {
                air++;
                late = f_min(late, l.t);
            }
            continue;
        }
        err[k] = length(wpos_delta_xz(l.at, tat_ideal(mv, m, k, 0.0f)));
        m.lag = f_max(m.lag, err[k] / thr);
    }
    if (m.gait == 0) {
        // Крадётся: одна лапа в воздухе; следующая отрывается, когда эта прошла три четверти шага
        if (air > 1 || (air == 1 && late < 0.75f)) return;
        u32 k = TAT_SEQ[m.next_leg % MECH_LEGS];
        bool moving = speed > 0.05f || m.state == MECH_PRONE;
        if (m.leg[k].fold <= 0.0f && m.leg[k].t < 0.0f && (err[k] > (moving ? 0.25f : 1.0f) * thr || m.lag > 1.0f)) {
            tat_lift(mv, m, c, k, dur, 0.5f * cycle, speed);
            m.next_leg = (u8)((m.next_leg + 1) % MECH_LEGS);
            return;
        }
        // Чья очередь, тому не надо, а другая сильно отстала (поворот на месте) — шагает она
        i32 best = -1;
        for (u32 j = 0; j < MECH_LEGS; ++j)
            if (m.leg[j].fold <= 0.0f && m.leg[j].t < 0.0f && err[j] > thr && (best < 0 || err[j] > err[best])) best = (i32)j;
        if (best >= 0) tat_lift(mv, m, c, (u32)best, dur, 0.5f * cycle, speed);
        else if (moving && err[k] > 0.01f) m.next_leg = (u8)((m.next_leg + 1) % MECH_LEGS);
        return;
    }
    // Перебежка: пара с большим отставанием; вторая пара — когда первая прошла больше половины шага, а если
    // стоящая лапа перетянута — сразу (короткий полёт лучше рывка тела вниз)
    if (air > 2 || (air > 0 && late < 0.55f && m.lag < 1.0f)) return;
    f32 front = f_max(err[0], err[1]), rear = f_max(err[2], err[3]);
    u32 pair = rear >= front ? 2 : 0;
    if (f_max(err[pair], err[pair + 1]) < thr) return;
    for (u32 i = 0; i < 2; ++i) {
        u32 k = pair + i;
        if (m.leg[k].fold > 0.0f || m.leg[k].t >= 0.0f || err[k] < thr * 0.2f) continue;
        tat_lift(mv, m, c, k, dur * (1.0f + 0.06f * (f32)i), 0.5f * cycle, speed);
    }
    if (pair == 2 && speed > 1.5f) m.h_v += 0.18f;   // толчок задней парой
}

// Опора высоты, наклон, перенос веса, пружины башенки, ствола, мачты, сошек, отдачи, антенн, морок
static void tat_springs(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    f32 sum = 0.0f, sz = 0.0f, sx = 0.0f, zz = 0.0f, xx = 0.0f;
    f32 hmin = 0.0f, hmax = 10.0f;
    u32 n = 0, air = 0;
    f32 ys[MECH_LEGS];
    Vec3 ps[MECH_LEGS];
    Vec3 support{0, 0, 0}, all{0, 0, 0};
    u32 ns = 0;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        const MechLeg &l = m.leg[k];
        if (l.fold > 0.0f) continue;
        WPos p = l.t >= 0.0f ? l.to : l.at;
        Vec2 d = wpos_delta_xz(m.at, p);
        ps[n] = mech_to_body(m.yaw, Vec3{d.x, 0, d.y});
        ys[n] = p.y;
        sum += p.y;
        all += ps[n];
        if (l.t >= 0.0f) air++;
        else {
            mech_leg_hlimit(mv, m, k, hmin, hmax);
            support += ps[n];
            ns++;
        }
        n++;
    }
    f32 pitch_to = 0.0f, roll_to = 0.0f;
    if (n) {
        f32 mean = sum / (f32)n;
        m.at.y += (mean - m.at.y) * f_min(1.0f, 9.0f * dt);
        for (u32 i = 0; i < n; ++i) {
            sz += ps[i].z * (ys[i] - mean);
            zz += ps[i].z * ps[i].z;
            sx += ps[i].x * (ys[i] - mean);
            xx += ps[i].x * ps[i].x;
        }
        if (n == MECH_LEGS) {
            pitch_to = -0.65f * atanf(sz / f_max(zz, 0.01f));
            roll_to = 0.65f * atanf(sx / f_max(xx, 0.01f));
        }
    } else {
        f32 g = mech_support(c, m.at, m.at.y + 0.3f, nullptr);
        m.at.y += (g - m.at.y) * f_min(1.0f, 9.0f * dt);
    }
    // Перенос веса крадучись: тело уходит к середине опор, пока одна нога в воздухе, и заранее — от той, что
    // пойдёт следующей
    Vec3 sway_to{0, 0, 0};
    if (m.gait == 0 && n == MECH_LEGS && (m.state == MECH_WALK || m.state == MECH_PRONE || m.state == MECH_STAND)) {
        Vec3 c3 = support * (1.0f / (f32)(ns ? ns : 1));
        u32 k = TAT_SEQ[m.next_leg % MECH_LEGS];
        if (ns == MECH_LEGS) {
            Vec2 d = wpos_delta_xz(m.at, m.leg[k].at);
            c3 = (all - mech_to_body(m.yaw, Vec3{d.x, 0, d.y})) * (1.0f / 3.0f);
        }
        sway_to = (c3 - all * 0.25f) * 0.45f;
        sway_to.y = 0.0f;
        f32 len = length(sway_to);
        if (len > 0.06f) sway_to = sway_to * (0.06f / len);
        if (m.state == MECH_STAND) sway_to = sway_to * 0.3f;
    }
    for (u32 i = 0; i < 3; ++i) {
        f32 *x = i == 0 ? &m.sway.x : (i == 1 ? &m.sway.y : &m.sway.z);
        f32 *v = i == 0 ? &m.sway_v.x : (i == 1 ? &m.sway_v.y : &m.sway_v.z);
        f32 to = i == 0 ? sway_to.x : (i == 1 ? sway_to.y : sway_to.z);
        mech_spring(*x, *v, to, 7.0f, 0.9f, dt);
    }
    // Наклон в сторону разгона
    Vec3 ab = mech_to_body(m.yaw, m.acc);
    pitch_to += f_clamp(0.03f * ab.z, -0.14f, 0.14f);
    roll_to -= f_clamp(0.02f * ab.x, -0.1f, 0.1f);
    mech_spring(m.pitch, m.pitch_v, pitch_to, m.gait ? 7.0f : 9.0f, m.gait ? 0.35f : 0.6f, dt);
    mech_spring(m.roll, m.roll_v, roll_to, 9.0f, 0.6f, dt);
    // Высота
    f32 to = m.h_to - 0.01f * (f32)air;
    if (m.state == MECH_STAND || m.state == MECH_PACKED || m.state == MECH_PRONE) to += 0.003f * sinf(m.clock * 2.0f);
    bool lying = m.state == MECH_PACKED || (m.state == MECH_FOLD && n < MECH_LEGS) || (m.state == MECH_UNFOLD && n < MECH_LEGS);
    bool slow = m.state == MECH_FOLD || m.state == MECH_UNFOLD || m.state == MECH_PRONE;
    mech_spring(m.h, m.h_v, to, lying ? 16.0f : (slow ? 4.0f : 8.0f), lying ? 1.0f : (slow ? 0.7f : 0.5f), dt);
    if (m.gait == 0 && m.h_v > 0.8f) { m.h -= (m.h_v - 0.8f) * dt; m.h_v = 0.8f; }   // встаёт с лёжки не рывком
    // Ложится не быстрее, чем лапы успевают разойтись (по одной, около 0.2 с на лапу)
    if (m.state == MECH_PRONE && m.h_v < -0.3f) { m.h -= (m.h_v + 0.3f) * dt; m.h_v = -0.3f; }
    if (n == MECH_LEGS && hmin < hmax) {
        if (m.h < hmin) { m.h = hmin; m.h_v = f_max(m.h_v, 0.0f); }
        if (m.h > hmax) { m.h = hmax; m.h_v = f_min(m.h_v, 0.0f); }
    }
    if (m.h < mech_desc(m).belly) { m.h = mech_desc(m).belly; m.h_v = f_max(m.h_v, 0.0f); }
    // Отдача: тело откатывается назад и возвращается
    mech_spring(m.kick, m.kick_v, 0.0f, 14.0f, 0.55f, dt);

    // Куда смотреть: в лёжке — на цель; иначе на человека; иначе по ходу; иначе оглядывается
    f32 aim = m.look, elev = 0.0f;
    bool track = false;
    WPos target{};
    if (m.state == MECH_PRONE && m.aim_on) {
        track = true;
        target = m.aim;
    } else if (tg.on) {
        track = true;
        target = tg.at;
        target.y += 1.25f;
    }
    f32 gun_y = m.at.y + m.h + 0.25f;
    if (track) {
        Vec2 d = wpos_delta_xz(m.at, target);
        aim = atan2f(d.x, d.y);
        elev = atan2f(target.y - gun_y, f_max(length(d), 0.5f));
    } else {
        m.look_wait -= dt;
        if (m.state == MECH_WALK && length(m.vel) > 0.3f && m.look_wait > 1.0f) m.look_wait = 1.0f;
        if (m.look_wait <= 0.0f) {
            f32 base = m.state == MECH_WALK ? atan2f(m.vel.x, m.vel.z) : m.yaw;
            f32 spread = m.state == MECH_WALK ? 0.4f : (m.state == MECH_PACKED ? 1.2f : 1.0f);
            m.look = base + (mech_rnd(m.rng) * 2.0f - 1.0f) * spread;
            m.look_wait = 1.4f + 2.6f * mech_rnd(m.rng);
        }
        aim = m.look;
    }
    // Прибор на мачте — быстрый, скачками; башенка с винтовкой — плавно, в своих пределах
    f32 want = mech_wrap(aim - m.yaw);
    m.eye_wait -= dt;
    if (m.eye_wait <= 0.0f) {
        f32 jitter = track ? 0.0f : (mech_rnd(m.rng) - 0.5f) * 0.6f;
        m.eye_to = f_clamp(want + jitter, -2.6f, 2.6f);
        m.eye_wait = track ? 0.1f : 0.4f + 1.1f * mech_rnd(m.rng);
    }
    m.eye += (m.eye_to - m.eye) * f_min(1.0f, (m.mast > 0.5f ? 4.0f : 12.0f) * dt);
    // Башенка смещена от оси: поправка на выстрел из её точки (на дальней цели — доли градуса)
    f32 hull_to = f_clamp(track ? want : (m.state == MECH_WALK ? want * 0.5f : want), -TAT_TURRET_MAX, TAT_TURRET_MAX);
    if (m.mast > 0.5f) hull_to = 0.0f;   // мачта сложена — башенка по курсу
    if (track) {
        Vec2 d = wpos_delta_xz(m.at, target);
        f32 dist = f_max(length(d), 1.0f);
        hull_to = f_clamp(hull_to - TAT_TURRET_X * cosf(want) / dist, -TAT_TURRET_MAX, TAT_TURRET_MAX);
    }
    f32 w = m.state == MECH_PRONE ? 5.0f : 3.4f;
    m.hull_v += (w * w * (hull_to - m.hull) - 2.0f * 0.95f * w * m.hull_v) * dt;
    m.hull_v = f_clamp(m.hull_v, -1.6f, 1.6f);
    m.hull += m.hull_v * dt;
    // Ствол держит возвышение (наклон рамы гасится), сошки на земле — ствол почти не качается
    f32 ch = cosf(m.hull), sh = sinf(m.hull);
    f32 gun_to = f_clamp(elev + m.pitch * ch - m.roll * sh, -0.12f, 0.5f);
    if (m.mast > 0.5f && m.state == MECH_PACKED) gun_to = -0.04f;
    mech_spring(m.gun, m.gun_v, gun_to, m.bipod > 0.5f ? 14.0f : 9.0f, 0.8f, dt);
    // Мачта и сошки
    f32 mv_ = 1.0f / 0.7f;
    m.mast += f_clamp(m.mast_to - m.mast, -mv_ * dt, mv_ * dt);
    f32 b_to = 0.0f;
    if (m.bipod_to > 0.0f) {
        // Угол сошек — по высоте шарнира над землёй под ним: ноги упираются в грунт
        f32 hy = m.h + TAT_BIPOD_UP + TAT_BIPOD_AHEAD * sinf(m.gun - m.pitch);
        Vec3 w2 = mech_to_world(m.yaw + m.hull, Vec3{0, 0, TAT_BIPOD_AHEAD});
        WPos g = mech_moved(m.at, w2.x + mech_to_world(m.yaw, Vec3{TAT_TURRET_X, 0, 0}).x, w2.z + mech_to_world(m.yaw, Vec3{TAT_TURRET_X, 0, 0}).z);
        hy += m.at.y - mech_support(c, g, m.at.y + 0.5f, nullptr);
        b_to = asinf(f_clamp(hy / TAT_BIPOD_LEG, 0.0f, 1.0f)) - TAT_BIPOD_DIP + m.gun;
    }
    f32 bmax = 2.0f * dt;
    m.bipod += f_clamp(b_to * m.bipod_to - m.bipod, -bmax, bmax);
    // Антенны
    Vec3 ah = mech_to_body(m.yaw, m.acc);
    for (u32 a = 0; a < 2; ++a) {
        f32 ww = a ? 7.5f : 12.0f;
        m.ant_v[a][0] += (-ww * ww * m.ant[a][0] - 2.0f * 0.07f * ww * m.ant_v[a][0] - 2.4f * ah.z) * dt;
        m.ant_v[a][1] += (-ww * ww * m.ant[a][1] - 2.0f * 0.07f * ww * m.ant_v[a][1] + 2.4f * ah.x) * dt;
        for (u32 i = 0; i < 2; ++i) m.ant[a][i] = f_clamp(m.ant[a][i] + m.ant_v[a][i] * dt, -0.35f, 0.35f);
    }
    // Морок: наводится долго, спадает быстро; на бегу не держится
    f32 cap = 1.0f - mech_smooth((length(m.vel) - 0.8f) / 1.4f);
    f32 vt = f_min(m.veil_to, cap);
    m.veil += vt > m.veil ? f_min(vt - m.veil, dt / TAT_VEIL_IN) : f_max(vt - m.veil, -dt / TAT_VEIL_OUT);
    m.veil = f_clamp(m.veil, 0.0f, 1.0f);
}

void tat_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    tat_brain(mv, m, c, tg, dt);
    tat_body(m, dt);
    tat_legs(mv, m, c, dt);
    tat_springs(mv, m, c, tg, dt);
}
