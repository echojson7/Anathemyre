// Жизнь «Гриди» (docs/gryd.md): двуногий мех-стрелок. Поведение, ход тела, перестановка ступней по очереди,
// выпад и стрельба очередями, винтовка в руках. Поза по этим данным — mech.cpp (ноги в своих плоскостях, руки
// решаются за винтовкой), опора и путь — mech_common.cpp
#include "mech_in.h"

constexpr f32 GRYD_STEP = 0.40f;          // шаг ходьбы, с (на бегу короче)
constexpr f32 GRYD_FEET_GAP = 0.26f;      // ступни не ближе друг к другу, м
constexpr f32 GRYD_HULL_MAX = 0.9f;       // корпус на поясе доворачивает не дальше, рад: дальше поворачивается таз
constexpr f32 GRYD_HEAD_MAX = 1.0f;       // голова на шее относительно корпуса, рад
constexpr f32 GRYD_GUN_MIN = -0.5f, GRYD_GUN_MAX = 0.6f;   // возвышение винтовки у плеча
constexpr f32 GRYD_STOW_YAW = -0.45f;     // по-походному: винтовка внутрь (tools/parts/gryd_pose.py, STOW)
constexpr f32 GRYD_STOW_PITCH = -0.8f;    //               и стволом вниз
constexpr f32 GRYD_AIM_TWIST = 0.2f;      // в выпаде таз развёрнут от цели, корпус — к цели
// Выпад: вынос ступни от оси качания вдоль своей плоскости (передняя, задняя) и разворот задней ступни наружу
constexpr f32 GRYD_LUNGE_FRONT = 0.30f, GRYD_LUNGE_BACK = -0.46f, GRYD_LUNGE_TURN = 0.35f;
constexpr f32 GRYD_SHOT_GAP = 0.09f;      // между выстрелами очереди, с
constexpr f32 GRYD_BURST_GAP = 0.75f;     // между очередями, с

// Выпад: эта ступня задняя
static bool gryd_rear(const Mech &m, u32 k) { return k != m.lead; }

// Где ступне k стоять сейчас (подошва в мире) и её курс: под своим бедром (в выпаде — впереди или позади), со сдвигом
// вперёд по ходу и повороту
static WPos gryd_ideal(const MechView &mv, const Mech &m, u32 k, f32 ahead, f32 *yaw) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    f32 x = mech_reach(mv, m, k, m.h), turn = 0.0f;
    if (m.state == MECH_AIM) {
        bool rear = gryd_rear(m, k);
        x = L.u + (rear ? GRYD_LUNGE_BACK : GRYD_LUNGE_FRONT);
        turn = rear ? (k == 0 ? GRYD_LUNGE_TURN : -GRYD_LUNGE_TURN) : 0.0f;   // задняя — носком наружу
    }
    Vec3 home = L.corner + mech_to_world(turn, L.dir) * x;
    f32 yaw_at = m.yaw + m.yaw_v * ahead;
    Vec3 w = mech_to_world(yaw_at, Vec3{home.x, 0, home.z});
    WPos to = mech_moved(m.at, w.x + m.vel.x * ahead, w.z + m.vel.z * ahead);
    if (yaw) *yaw = mech_wrap(yaw_at + turn);
    // Не ставить ступню вплотную к другой (поворот на месте сводит ноги)
    const MechLeg &o = m.leg[k ^ 1u];
    Vec2 d = wpos_delta_xz(o.t >= 0.0f ? o.to : o.at, to);
    f32 len = length(d);
    if (len < GRYD_FEET_GAP && len > 1e-4f) to = mech_moved(to, d.x * (GRYD_FEET_GAP / len - 1.0f), d.y * (GRYD_FEET_GAP / len - 1.0f));
    return to;
}

// Следующая точка обхода: вокруг дома, по свободной прямой. false — некуда
static bool gryd_pick_point(Mech &m, const Extract &c) {
    for (u32 tries = 0; tries < 8; ++tries) {
        f32 a = mech_rnd(m.rng) * TAU_F, r = 3.0f + 6.0f * mech_rnd(m.rng);
        WPos p = mech_moved(m.home, sinf(a) * r, cosf(a) * r);
        if (length(wpos_delta_xz(m.at, p)) < 2.5f || !mech_path_clear(c, m.at, p)) continue;
        m.goal = p;
        m.goal_home = false;
        m.speed_max = mech_desc(m).v_walk;
        return true;
    }
    return false;
}

static void gryd_go(Mech &m, const Extract &c, const WPos &to, bool home, bool run) {
    m.goal = to;
    m.goal_home = home;
    m.speed_max = run ? mech_desc(m).v_dash : mech_desc(m).v_walk;
    m.gait = run ? 1 : 0;
    if (!home && !mech_path_clear(c, m.at, m.goal) && !gryd_pick_point(m, c)) {
        m.wait = 1.0f;
        return;
    }
    mech_enter(m, MECH_WALK);
}

// Выпад и стрельба по точке: bursts очередей (0 — только целиться, пока цель есть)
static void gryd_aim(Mech &m, const WPos &aim, u32 bursts) {
    m.aim = aim;
    m.aim_on = true;
    m.aim_t = 1.4f;   // выпад и вскинуть винтовку
    m.bursts = bursts;
    m.shots = 0;
    m.burst_left = 0;
    m.lead = 1;       // правша: вперёд левая нога
    mech_enter(m, MECH_AIM);
}

// Точка в осях дома (x вправо, z вперёд); y — земля под ней + dy
static WPos gryd_home_at(const Mech &m, const Extract &c, f32 x, f32 z, f32 dy) {
    Vec3 w = mech_to_world(m.home_yaw, Vec3{x, 0, z});
    WPos p = mech_moved(m.home, w.x, w.z);
    p.y = mech_support(c, p, m.at.y + 30.0f, nullptr) + dy;
    return p;
}

// Показ (--mech show): по порядку всё, что машина умеет. Зовётся стоя, когда пора что-то делать
static void gryd_show(Mech &m, const Extract &c) {
    u32 step = m.show++;
    switch (step) {
    case 0:   // привстала, осмотрелась
        m.h_to = mech_desc(m).h_high;
        m.mood_t = 1.4f;
        m.wait = 1.8f;
        break;
    case 1: gryd_go(m, c, gryd_home_at(m, c, 0.6f, 5.0f, 0.0f), false, false); break;      // идёт вперёд
    case 2: gryd_aim(m, gryd_home_at(m, c, 9.0f, 34.0f, 1.2f), 3); break;                   // выпад, три очереди
    case 3: gryd_go(m, c, gryd_home_at(m, c, -3.4f, 7.5f, 0.0f), false, true); break;      // перебегает влево
    case 4: gryd_aim(m, gryd_home_at(m, c, -22.0f, 26.0f, 0.8f), 2); break;                 // по второй цели
    case 5: gryd_go(m, c, m.home, true, false); break;                                       // домой шагом
    default:
        m.show = 0;
        m.wait = 2.0f;
        mech_enter(m, MECH_FOLD);
        break;
    }
}

// Поведение: что делать дальше. Меняет цель, высоту, положение винтовки и состояние; тело и ступни едут сами
static void gryd_brain(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    const MechDesc &md = mech_desc(m);
    m.clock += dt;
    m.state_t += dt;
    m.wait -= dt;
    switch (m.state) {
    case MECH_PACKED:   // присела, винтовка опущена
        m.h_to = md.belly;
        m.stow_to = 1.0f;
        if (mv.demo == MECH_DEMO_PACK) break;
        if (m.wait <= 0.0f || (tg.on && tg.dist < 10.0f)) {
            mech_enter(m, MECH_UNFOLD);
            m.wait = 1.2f;
        }
        break;
    case MECH_UNFOLD:   // встаёт
        m.h_to = md.h_stand;
        if (m.h > md.h_stand - 0.03f && m.state_t > 0.8f) {
            mech_enter(m, MECH_STAND);
            m.wait = 1.0f + mech_rnd(m.rng);
            m.points_left = 2 + (u32)(mech_rnd(m.rng) * 3.0f);
        }
        break;
    case MECH_FOLD:   // приседает отдохнуть
        m.h_to = md.belly;
        m.stow_to = 1.0f;
        m.aim_on = false;
        if (m.h < md.belly + 0.03f && m.state_t > 1.0f) {
            mech_enter(m, MECH_PACKED);
            m.wait = mv.demo == MECH_DEMO_SHOW ? 2.5f : 4.0f + 6.0f * mech_rnd(m.rng);
        }
        break;
    case MECH_STAND: {
        m.aim_on = false;
        m.stow_to = tg.on ? 0.0f : 1.0f;   // человек рядом — винтовку к плечу
        if (m.mood_t > 0.0f) {
            m.mood_t -= dt;
            if (m.mood_t <= 0.0f) m.h_to = md.h_stand;
        } else {
            m.h_to = md.h_stand;
        }
        if (mv.demo == MECH_DEMO_STAND) {
            if (mv.aim_demo && m.state_t > 1.0f) gryd_aim(m, gryd_home_at(m, c, 6.0f, 30.0f, 1.2f), 1000000);   // --mech aim
            else if (tg.on) m.h_to = md.h_stand - 0.04f;
            break;
        }
        if (m.wait > 0.0f) break;
        if (mv.demo == MECH_DEMO_SHOW) {
            gryd_show(m, c);
            break;
        }
        if (tg.on && tg.dist < md.see_m) {   // человек — выпад, целится (без выстрелов), пока он рядом
            WPos p = tg.at;
            p.y += 1.2f;
            gryd_aim(m, p, 0);
            break;
        }
        if (m.points_left == 0 && mv.demo != MECH_DEMO_WALK) {
            if (length(wpos_delta_xz(m.at, m.home)) > 0.4f) gryd_go(m, c, m.home, true, false);
            else {
                mech_enter(m, MECH_FOLD);
                m.wait = 1.0f;
            }
            break;
        }
        if (gryd_pick_point(m, c)) {
            if (m.points_left) m.points_left--;
            mech_enter(m, MECH_WALK);
        } else {
            m.wait = 1.0f;
        }
        break;
    }
    case MECH_WALK: {
        m.stow_to = 1.0f;
        m.h_to = md.h_stand - (m.gait ? 0.06f : 0.03f);
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        if (length(d) < 0.15f && length(m.vel) < 0.15f) {
            mech_enter(m, MECH_STAND);
            m.wait = 0.8f + 1.4f * mech_rnd(m.rng);
            m.gait = 0;
            if (mv.demo == MECH_DEMO_SHOW) m.wait = 0.4f;
        }
        break;
    }
    case MECH_AIM: {
        m.h_to = md.h_low;
        m.stow_to = 0.0f;
        if (m.bursts == 0) {   // целится в человека, пока он рядом
            if (!tg.on || tg.dist > md.see_m) {
                mech_enter(m, MECH_STAND);
                m.wait = 1.0f;
                break;
            }
            m.aim = tg.at;
            m.aim.y += 1.2f;
            break;
        }
        m.aim_t -= dt;
        if (m.aim_t > 0.0f) break;
        m.burst_t -= dt;
        if (m.burst_t > 0.0f) break;
        if (m.burst_left == 0) {
            if (m.shots >= m.bursts * 4u) {   // отстрелялась: встаёт из выпада
                mech_enter(m, MECH_STAND);
                m.wait = 0.6f;
                m.aim_on = false;
                break;
            }
            m.burst_left = 4;
        }
        // Выстрел: корпус откатывается, ствол подбрасывает, голова вздрагивает
        m.burst_left--;
        m.shots++;
        m.kick_v += 0.55f;
        m.gun_v += 1.1f + 0.4f * mech_rnd(m.rng);
        m.hull_v += 0.25f * (mech_rnd(m.rng) - 0.5f);
        m.burst_t = m.burst_left ? GRYD_SHOT_GAP : GRYD_BURST_GAP;
        m.flash = 1.0f;
        m.flash_roll = mech_rnd(m.rng) * TAU_F;
        for (u32 a = 0; a < 2; ++a) m.ant_v[a][0] += 0.5f;
        break;
    }
    default: break;
    }
}

// Ход тела: к цели с разгоном и торможением, курс — по ходу; в выпаде — таз чуть от цели, корпус к цели
static void gryd_body(Mech &m, f32 dt) {
    Vec3 want{0, 0, 0};
    f32 yaw_to = m.yaw;
    f32 accel = m.gait ? 2.6f : 1.6f, wmax = m.gait ? 1.4f : 1.0f;
    if (m.state == MECH_WALK) {
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        f32 dist = length(d);
        f32 turn = 0.0f;
        if (dist > 1.0f) {
            yaw_to = atan2f(d.x, d.y);
            turn = f_abs(mech_wrap(yaw_to - m.yaw));
        } else if (m.goal_home) {
            yaw_to = m.home_yaw;
        }
        if (dist > 0.02f) {
            f32 v = f_min(m.speed_max, f_sqrt(2.0f * accel * 0.6f * dist));
            v *= f_clamp(1.4f - turn / 0.9f, 0.1f, 1.0f) * f_clamp(2.2f - m.lag, 0.2f, 1.0f);
            want = Vec3{d.x / dist * v, 0, d.y / dist * v};
        }
    } else if (m.state == MECH_AIM && m.aim_on) {
        Vec2 d = wpos_delta_xz(m.at, m.aim);
        yaw_to = mech_wrap(atan2f(d.x, d.y) - GRYD_AIM_TWIST);
        wmax = 0.8f;
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
    m.yaw_v += (20.0f * err - 9.0f * m.yaw_v) * dt;
    wmax *= f_clamp(2.2f - m.lag, 0.25f, 1.0f);
    m.yaw_v = f_clamp(m.yaw_v, -wmax, wmax);
    m.yaw = mech_wrap(m.yaw + m.yaw_v * dt);
}

static void gryd_lift(const MechView &mv, Mech &m, const Extract &c, u32 k, f32 dur, f32 ahead, f32 speed) {
    MechLeg &l = m.leg[k];
    l.from = l.at;
    l.dur = dur * (1.0f + 0.08f * (mech_rnd(m.rng) - 0.5f));
    WPos to = gryd_ideal(mv, m, k, l.dur + ahead, &l.yaw_to);
    to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
    l.to = to;
    l.lift = 0.09f + 0.03f * f_min(speed, 2.0f) + 0.5f * f_abs(to.y - l.from.y);
    l.t = 0.0f;
}

// Ступни по очереди: правая, левая; следующая отрывается, когда эта встала (на бегу — чуть раньше: полёт)
static void gryd_legs(const MechView &mv, Mech &m, const Extract &c, f32 dt) {
    f32 speed = length(m.vel) + f_abs(m.yaw_v) * 0.4f;
    f32 dur = m.gait ? GRYD_STEP * 0.75f : GRYD_STEP;
    if (m.state == MECH_AIM) dur = 0.36f;   // выпад — широкий неторопливый шаг
    f32 cycle = 2.0f * dur;
    f32 thr = 0.04f + 0.5f * speed * cycle;
    f32 err[2] = {0, 0};
    u32 air = 0;
    f32 late = 1.0f;
    m.lag = 0.0f;
    for (u32 k = 0; k < 2; ++k) {
        MechLeg &l = m.leg[k];
        if (l.t >= 0.0f) {
            l.t += dt / l.dur;
            if (l.t < 0.7f) {
                WPos to = gryd_ideal(mv, m, k, (1.0f - l.t) * l.dur + 0.5f * cycle, &l.yaw_to);
                to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
                l.to = to;
            }
            if (l.t >= 1.0f) {   // касание: тело приседает, антенны качнулись
                l.t = -1.0f;
                l.at = l.to;
                l.yaw = l.yaw_to;
                m.h_v -= 0.06f + 0.05f * speed;
                m.ant_v[k][0] += 0.3f * (mech_rnd(m.rng) - 0.5f) - 0.2f;
                m.ant_v[k][1] += 0.3f * (mech_rnd(m.rng) - 0.5f);
            } else {
                air++;
                late = f_min(late, l.t);
            }
            continue;
        }
        f32 yaw_i;
        err[k] = length(wpos_delta_xz(l.at, gryd_ideal(mv, m, k, 0.0f, &yaw_i)));
        err[k] = f_max(err[k], 0.25f * f_abs(mech_wrap(yaw_i - l.yaw)));   // и разворот ступни
        m.lag = f_max(m.lag, err[k] / thr);
    }
    if (air > 1 || (air == 1 && late < (m.gait ? 0.8f : 0.97f))) return;
    // Очередь: не та нога, что шагнула последней; если ей не надо, а другой надо — шагает другая
    u32 k = (u32)(m.next_leg & 1u);
    bool moving = speed > 0.05f || m.state == MECH_AIM;
    if (m.leg[k].t < 0.0f && (err[k] > (moving ? 0.3f : 1.0f) * thr || m.lag > 1.0f)) {
        gryd_lift(mv, m, c, k, dur, 0.5f * cycle, speed);
        m.next_leg = (u8)(k ^ 1u);
        return;
    }
    u32 o = k ^ 1u;
    if (air == 0 && m.leg[o].t < 0.0f && err[o] > thr) {
        gryd_lift(mv, m, c, o, dur, 0.5f * cycle, speed);
        m.next_leg = (u8)k;
    }
}

// Опора высоты, наклон, перенос веса, пружины корпуса, винтовки, головы, отдачи, антенн
static void gryd_springs(const MechView &mv, Mech &m, const MechTarget &tg, f32 dt) {
    f32 hmin = 0.0f, hmax = 10.0f, sum = 0.0f;
    u32 air = 0, stance = 2;
    for (u32 k = 0; k < 2; ++k) {
        const MechLeg &l = m.leg[k];
        sum += l.t >= 0.0f ? f_max(l.to.y, l.from.y) : l.at.y;
        if (l.t >= 0.0f) air++;
        else {
            mech_leg_hlimit(mv, m, k, hmin, hmax);
            stance = k;
        }
    }
    f32 mean = sum * 0.5f;
    m.at.y += (mean - m.at.y) * f_min(1.0f, 9.0f * dt);
    // Перенос веса: на одной ноге таз уходит к ней и чуть вперёд
    Vec3 sway_to{0, 0, 0};
    if (air == 1 && stance < 2) {
        Vec2 d = wpos_delta_xz(m.at, m.leg[stance].at);
        Vec3 b = mech_to_body(m.yaw, Vec3{d.x, 0, d.y});
        sway_to.x = f_clamp(b.x * 0.22f, -0.045f, 0.045f);
    }
    for (u32 i = 0; i < 3; i += 2) {
        f32 *x = i == 0 ? &m.sway.x : &m.sway.z;
        f32 *v = i == 0 ? &m.sway_v.x : &m.sway_v.z;
        mech_spring(*x, *v, i == 0 ? sway_to.x : sway_to.z, 8.0f, 0.8f, dt);
    }
    // Наклон: в выпаде вперёд к винтовке, на ходу — по скорости и разгону; крен — к опорной ноге
    Vec3 ab = mech_to_body(m.yaw, m.acc);
    f32 pitch_to = m.state == MECH_AIM ? 0.10f : 0.035f * length(m.vel);
    pitch_to += f_clamp(0.03f * ab.z, -0.12f, 0.12f);
    f32 roll_to = -f_clamp(0.02f * ab.x, -0.08f, 0.08f) - 0.6f * m.sway.x;
    mech_spring(m.pitch, m.pitch_v, pitch_to, 6.0f, 0.7f, dt);
    mech_spring(m.roll, m.roll_v, roll_to, 8.0f, 0.7f, dt);
    // Высота: на одной ноге чуть выше (нога выпрямлена), в покое дышит
    f32 to = m.h_to + 0.012f * (f32)air;
    if (m.state == MECH_STAND || m.state == MECH_PACKED || m.state == MECH_AIM) to += 0.004f * sinf(m.clock * 1.7f);
    bool slow = m.state == MECH_FOLD || m.state == MECH_UNFOLD || m.state == MECH_AIM || (m.state == MECH_STAND && m.state_t < 1.2f);
    mech_spring(m.h, m.h_v, to, slow ? 4.5f : 9.0f, slow ? 0.8f : 0.55f, dt);
    if (hmin < hmax) {
        if (m.h < hmin) { m.h = hmin; m.h_v = f_max(m.h_v, 0.0f); }
        if (m.h > hmax) { m.h = hmax; m.h_v = f_min(m.h_v, 0.0f); }
    }
    mech_spring(m.kick, m.kick_v, 0.0f, 16.0f, 0.6f, dt);
    m.flash = f_max(0.0f, m.flash - dt / 0.05f);

    // Куда смотреть: в выпаде — на цель; иначе на человека; иначе по ходу; иначе оглядывается
    f32 aim = m.look, elev = 0.0f;
    bool track = false;
    WPos target{};
    if (m.state == MECH_AIM && m.aim_on) {
        track = true;
        target = m.aim;
    } else if (tg.on) {
        track = true;
        target = tg.at;
        target.y += 1.3f;
    }
    f32 gun_y = m.at.y + m.h + 0.46f;
    if (track) {
        Vec2 d = wpos_delta_xz(m.at, target);
        aim = atan2f(d.x, d.y);
        elev = atan2f(target.y - gun_y, f_max(length(d), 0.5f));
    } else {
        m.look_wait -= dt;
        if (m.state == MECH_WALK && length(m.vel) > 0.3f && m.look_wait > 1.0f) m.look_wait = 1.0f;
        if (m.look_wait <= 0.0f) {
            f32 base = m.state == MECH_WALK ? atan2f(m.vel.x, m.vel.z) : m.yaw;
            f32 spread = m.state == MECH_WALK ? 0.35f : 0.9f;
            m.look = base + (mech_rnd(m.rng) * 2.0f - 1.0f) * spread;
            m.look_wait = 1.2f + 2.4f * mech_rnd(m.rng);
        }
        aim = m.look;
    }
    f32 want = mech_wrap(aim - m.yaw);
    // Корпус на поясе: к цели (в своих пределах), на ходу — чуть навстречу шагу
    f32 hull_to = f_clamp(track ? want : want * 0.4f, -GRYD_HULL_MAX, GRYD_HULL_MAX);
    if (m.state == MECH_WALK) {
        f32 ph = m.leg[0].t >= 0.0f ? m.leg[0].t : (m.leg[1].t >= 0.0f ? -m.leg[1].t : 0.0f);
        hull_to += 0.06f * sinf(PI_F * ph);
    }
    f32 w = m.state == MECH_AIM ? 7.0f : 4.0f;
    m.hull_v += (w * w * (hull_to - m.hull) - 2.0f * 0.9f * w * m.hull_v) * dt;
    m.hull_v = f_clamp(m.hull_v, -2.0f, 2.0f);
    m.hull += m.hull_v * dt;
    // Голова — быстрая, скачками, относительно корпуса
    m.eye_wait -= dt;
    if (m.eye_wait <= 0.0f) {
        f32 jitter = track ? 0.0f : (mech_rnd(m.rng) - 0.5f) * 0.5f;
        m.eye_to = f_clamp(want - m.hull + jitter, -GRYD_HEAD_MAX, GRYD_HEAD_MAX);
        m.eye_wait = track ? 0.08f : 0.35f + 1.0f * mech_rnd(m.rng);
    }
    m.eye += (m.eye_to - m.eye) * f_min(1.0f, 11.0f * dt);
    // Винтовка: у плеча держит возвышение на цель (наклон таза гасится), по-походному — внутрь и стволом вниз
    f32 sv = 1.0f / 0.55f;
    m.stow += f_clamp(m.stow_to - m.stow, -sv * dt, sv * dt);
    f32 s = mech_smooth(m.stow);
    f32 ch = cosf(m.hull), sh = sinf(m.hull);
    f32 gun_to = f_clamp(elev + m.pitch * ch - m.roll * sh, GRYD_GUN_MIN, GRYD_GUN_MAX);
    if (!track) gun_to = 0.0f;
    gun_to = mech_mix(gun_to, GRYD_STOW_PITCH, s);
    mech_spring(m.gun, m.gun_v, gun_to, m.state == MECH_AIM ? 13.0f : 8.0f, 0.75f, dt);
    m.gun = f_clamp(m.gun, GRYD_STOW_PITCH - 0.05f, GRYD_GUN_MAX + 0.15f);
    m.gun_yaw = GRYD_STOW_YAW * s;
    // Антенны
    Vec3 ah = mech_to_body(m.yaw, m.acc);
    for (u32 a = 0; a < 2; ++a) {
        f32 ww = a ? 7.0f : 11.0f;
        m.ant_v[a][0] += (-ww * ww * m.ant[a][0] - 2.0f * 0.07f * ww * m.ant_v[a][0] - 2.4f * ah.z) * dt;
        m.ant_v[a][1] += (-ww * ww * m.ant[a][1] - 2.0f * 0.07f * ww * m.ant_v[a][1] + 2.4f * ah.x) * dt;
        for (u32 i = 0; i < 2; ++i) m.ant[a][i] = f_clamp(m.ant[a][i] + m.ant_v[a][i] * dt, -0.35f, 0.35f);
    }
}

void gryd_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    gryd_brain(mv, m, c, tg, dt);
    gryd_body(m, dt);
    gryd_legs(mv, m, c, dt);
    gryd_springs(mv, m, tg, dt);
}
