// Жизнь «Вежи» (docs/vezha.md): поведение, ход тела, перестановка лап (рысь) и пружины. Поза по этим данным —
// mech.cpp, опора и путь — mech_common.cpp
#include "mech_in.h"

// Где лапе k стоять сейчас: дом под своим углом рамы, со сдвигом вперёд по ходу и повороту
static WPos mech_ideal(const MechView &mv, const Mech &m, u32 k, f32 ahead) {
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    // Ложится — лапы сразу переходят на места «лёжа»: у стоек с локтем назад локоть иначе уходит в землю
    f32 h = m.state == MECH_FOLD ? mech_desc(m).belly : m.h;
    Vec3 home = L.corner + L.dir * mech_reach(mv, m, k, h);
    f32 yaw = m.yaw + m.yaw_v * ahead;
    Vec3 w = mech_to_world(yaw, Vec3{home.x, 0, home.z}) + m.vel * ahead;
    // Упреждение на повороте и боком не переводит лапу за ось машины: пары лап не перекрещиваются (у Вежи колея
    // передних стоек узкая)
    Vec3 b = mech_to_body(yaw, w);
    f32 side = home.x > 0.0f ? 1.0f : -1.0f, keep = 0.62f * f_abs(home.x);
    if (b.x * side < keep) w = mech_to_world(yaw, Vec3{side * keep, 0, b.z});
    return mech_moved(m.at, w.x, w.z);
}

static f32 mech_step_time(f32 speed) { return f_clamp(0.28f - 0.03f * speed, 0.17f, 0.28f); }

// Следующая точка обхода: вокруг дома, по свободной прямой. false — некуда
static bool mech_pick_point(Mech &m, const Extract &c) {
    const MechDesc &md = mech_desc(m);
    for (u32 tries = 0; tries < 8; ++tries) {
        f32 a = mech_rnd(m.rng) * TAU_F, r = 3.5f + 4.5f * mech_rnd(m.rng);
        WPos p = mech_moved(m.home, sinf(a) * r, cosf(a) * r);
        Vec2 d = wpos_delta_xz(m.at, p);
        if (length(d) < 2.5f) continue;
        if (!mech_path_clear(c, m.at, p)) continue;
        m.goal = p;
        m.goal_home = false;
        m.speed_max = mech_rnd(m.rng) < 0.35f ? md.v_dash : md.v_walk;
        m.leap = m.speed_max > md.v_walk && mech_rnd(m.rng) < 0.7f;
        return true;
    }
    return false;
}

// Показ (--mech show): по порядку всё, что машина умеет. Зовётся стоя, когда пора что-то делать
static void mech_show(Mech &m, const Extract &c) {
    const MechDesc &md = mech_desc(m);
    static const f32 PTS[2][2] = {{2.5f, 4.5f}, {-5.0f, 2.0f}};   // точки показа от дома, в его осях
    u32 step = m.show++;
    if (step == 0 || step == 2) {
        m.h_to = step == 0 ? md.h_high : md.h_low;   // привстала посмотреть; присела
        m.mood_t = 1.4f;
        m.wait = 2.0f;
    } else if (step == 1 || step == 3) {
        Vec3 w = mech_to_world(m.home_yaw, Vec3{PTS[step / 2][0], 0, PTS[step / 2][1]});
        m.goal = mech_moved(m.home, w.x, w.z);
        m.goal_home = false;
        m.speed_max = step == 3 ? md.v_dash : md.v_walk;   // шагом; рывком с прыжком
        m.leap = step == 3;
        if (!mech_path_clear(c, m.at, m.goal) && !mech_pick_point(m, c)) {
            m.wait = 1.0f;
            return;
        }
        mech_enter(m, MECH_WALK);
    } else if (step == 4) {
        m.goal = m.home;
        m.goal_home = true;
        m.speed_max = md.v_walk;
        mech_enter(m, MECH_WALK);
    } else {
        m.show = 0;
        m.wait = 1.0f;
        mech_enter(m, MECH_FOLD);
    }
}

// Поведение: что делать дальше. Только меняет цель, высоту и состояние; тело и лапы едут сами
static void mech_brain(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    const MechDesc &md = mech_desc(m);
    m.state_t += dt;
    m.clock += dt;
    bool planted = true, folded = true;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        if (m.leg[k].fold > 0.0f || m.leg[k].t >= 0.0f) planted = false;
        if (m.leg[k].fold < 1.0f) folded = false;
    }
    static const f32 ORDER[MECH_LEGS] = {0.0f, 0.30f, 0.12f, 0.42f};   // по диагоналям, со сдвигом
    switch (m.state) {
    case MECH_PACKED:
        m.h_to = md.belly;
        m.wait -= dt;
        if (mv.demo == MECH_DEMO_PACK) break;
        if (m.wait <= 0.0f || (tg.on && tg.dist < 5.0f && m.state_t > 1.5f) || (mv.demo == MECH_DEMO_SHOW && m.state_t > 2.0f)) {
            for (u32 k = 0; k < MECH_LEGS; ++k) {
                Vec3 pol{0.0f, mech_reach(mv, m, k, md.belly), md.leg[k].foot_h - md.belly};
                WPos g = mech_leg_ground(mv, m, c, k, pol);
                pol.z += g.y - m.at.y;   // неровная земля: лапа ляжет на свою высоту
                m.leg[k].pol = pol;
            }
            mech_enter(m, MECH_UNFOLD);
        }
        break;
    case MECH_UNFOLD:
        for (u32 k = 0; k < MECH_LEGS; ++k) {
            MechLeg &l = m.leg[k];
            if (l.fold <= 0.0f || m.state_t < 0.35f + ORDER[k]) continue;
            l.fold -= dt / 0.55f;
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
            if (m.h > md.h_stand - 0.08f) {
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
        // Человек подошёл вплотную — присесть и не уходить
        if (tg.on && tg.dist < 3.0f) {
            m.h_to = md.h_low + 0.12f;
            m.mood_t = 0.6f;
            m.wait = f_max(m.wait, 0.8f);
        }
        m.wait -= dt;
        if (m.wait > 0.0f) break;
        if (mv.demo == MECH_DEMO_SHOW) {
            mech_show(m, c);
            break;
        }
        f32 r = mech_rnd(m.rng);
        if (mv.demo == MECH_DEMO_STAND || r < 0.3f) {
            // Осмотреться: привстать или присесть
            m.h_to = mech_rnd(m.rng) < 0.6f ? md.h_high : md.h_low;
            m.mood_t = 1.4f + 1.4f * mech_rnd(m.rng);
            m.wait = m.mood_t + 0.8f + mech_rnd(m.rng);
            break;
        }
        if (m.points_left > 0 || mv.demo == MECH_DEMO_WALK) {
            if (mech_pick_point(m, c)) {
                if (m.points_left) m.points_left--;
                mech_enter(m, MECH_WALK);
            } else {
                m.wait = 1.0f;
            }
            break;
        }
        Vec2 d = wpos_delta_xz(m.at, m.home);
        if (length(d) > 0.35f || f_abs(mech_wrap(m.home_yaw - m.yaw)) > 0.12f) {
            m.goal = m.home;
            m.goal_home = true;
            m.speed_max = md.v_walk;
            mech_enter(m, MECH_WALK);
        } else {
            m.wait = 1.0f;   // ≥ 0 — ещё опускается; < 0 — лапы уже уходят в пачки
            mech_enter(m, MECH_FOLD);
        }
        break;
    }
    case MECH_WALK: {
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        f32 speed = length(m.vel);
        m.h_to = md.h_stand - 0.04f - 0.035f * speed;
        // Прыжок на рывке: присесть на ходу, толкнуться всеми лапами, пролететь пару метров
        if (m.jump == 0 && m.leap && speed > 3.0f && length(d) > 5.0f && f_abs(m.yaw_v) < 0.4f) {
            m.jump = 1;
            m.jump_t = 0.22f;
            m.leap = false;
        }
        if (m.jump == 1) {
            m.h_to = md.h_low;
            m.jump_t -= dt;
            if (m.jump_t <= 0.0f) {
                m.jump = 2;
                m.h_v = VEZHA_JUMP_V;
            }
        }
        bool turned = !m.goal_home || f_abs(mech_wrap(m.home_yaw - m.yaw)) < 0.08f;
        if (length(d) < 0.12f && speed < 0.15f && turned) {
            m.wait = mv.demo == MECH_DEMO_SHOW ? 0.7f : 1.2f + 2.5f * mech_rnd(m.rng);
            mech_enter(m, MECH_STAND);
        }
        break;
    }
    case MECH_FOLD:
        if (m.state_t < 0.05f) m.h_to = md.belly;
        if (m.h < md.belly + 0.012f && (planted || m.wait < 0.0f)) {
            // Лежит: лапы уходят в пачки по очереди
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
                if (m.state_t > ORDER[MECH_LEGS - 1 - k]) m.leg[k].fold = f_min(1.0f, m.leg[k].fold + dt / 0.6f);
            if (folded) {
                m.wait = 9.0f + 7.0f * mech_rnd(m.rng);
                mech_enter(m, MECH_PACKED);
            }
        }
        break;
    }
}

// Ход тела: к цели с разгоном и торможением, курс — по ходу (дома — как стояла)
static void mech_body(Mech &m, f32 dt) {
    Vec3 want{0, 0, 0};
    f32 yaw_to = m.yaw;
    if (m.state == MECH_WALK) {
        Vec2 d = wpos_delta_xz(m.at, m.goal);
        f32 dist = length(d);
        // Курс — на цель; у самой цели не доворачивает (дома — встаёт, как стояла)
        f32 turn = 0.0f;
        if (dist > 1.5f) {
            yaw_to = atan2f(d.x, d.y);
            turn = f_abs(mech_wrap(yaw_to - m.yaw));
        } else if (m.goal_home) {
            yaw_to = m.home_yaw;
        }
        if (dist > 0.02f) {
            // Боком и задом — не спеша: сначала довернуть, потом разгон. Лапы отстали — тело их ждёт
            f32 v = f_min(m.speed_max, f_sqrt(2.0f * 3.0f * dist));
            v *= f_clamp(1.35f - turn / 1.2f, 0.3f, 1.0f) * f_clamp(2.4f - m.lag, 0.3f, 1.0f);
            want = Vec3{d.x / dist * v, 0, d.y / dist * v};
        }
    }
    if (m.jump == 3) {   // в полёте ни разогнаться, ни повернуть
        want = m.vel;
        yaw_to = m.yaw;
    }
    Vec3 dv = want - m.vel;
    f32 l = length(dv), amax = 5.0f * dt;
    Vec3 old = m.vel;
    if (l > amax) dv = dv * (amax / l);
    m.vel += dv;
    Vec3 a = (m.vel - old) * (1.0f / dt);
    f32 k = f_min(1.0f, 8.0f * dt);
    m.acc = m.acc + (a - m.acc) * k;
    m.at = mech_moved(m.at, m.vel.x * dt, m.vel.z * dt);
    // Курс: пружина без перелёта, не быстрее 1.9 рад/с (лапы успевают переступать)
    f32 err = mech_wrap(yaw_to - m.yaw);
    m.yaw_v += (36.0f * err - 12.0f * m.yaw_v) * dt;
    f32 wmax = 1.5f * f_clamp(2.4f - m.lag, 0.3f, 1.0f);   // длинная рама: передние лапы далеко от оси
    m.yaw_v = f_clamp(m.yaw_v, -wmax, wmax);
    m.yaw = mech_wrap(m.yaw + m.yaw_v * dt);
}

// Стоящей лапе пора шагать сразу: стойка (MECH_LEG_SLIDE) уехала почти под ось поворота (дальше локоть назад
// вывернется) или лапа на повороте уехала к оси машины (пара лап перекрестится)
static bool mech_leg_urgent(const MechView &mv, const Mech &m, u32 k) {
    const MechLeg &l = m.leg[k];
    const MechLegDesc &D = mech_desc(m).leg[k];
    if (l.fold > 0.0f || l.t >= 0.0f) return false;
    const MechLegRig &L = mech_rig(mv, m).leg[k];
    Vec3 p = mech_world_chassis(m, l.at, D.foot_h);
    if (p.x * L.corner.x < 0.5f * L.corner.x * L.corner.x) return true;
    if (D.mode != MECH_LEG_SLIDE) return false;
    MechReach r = mech_leg_reach(L, p);
    return (r.x + L.u) * cosf(r.yaw) < 0.14f;
}

// Лапы: отставшая от своего места переставляется шагом; соседние по кругу в воздухе вместе не бывают — выходит рысь
static void mech_legs(const MechView &mv, Mech &m, const Extract &c, f32 dt) {
    f32 speed = length(m.vel) + f_abs(m.yaw_v) * 1.0f;   // лапа на повороте едет по кругу радиусом около метра
    f32 dur = mech_step_time(speed);
    f32 err[MECH_LEGS];
    f32 thr = 0.05f + 0.5f * speed * dur;
    i32 best = -1;
    m.lag = 0.0f;
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        err[k] = 0.0f;
        if (l.fold > 0.0f) continue;
        if (l.t >= 0.0f) {
            // В шаге: цель едет за телом, пока лапа высоко
            l.t += dt / l.dur;
            if (l.t < 0.75f) {
                f32 rest = (1.0f - l.t) * l.dur;
                WPos to = mech_ideal(mv, m, k, rest + 0.5f * dur);
                to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
                l.to = to;
            }
            if (l.t >= 1.0f) {
                l.t = -1.0f;
                l.at = l.to;
                m.h_v -= 0.05f + 0.02f * speed;   // тело проседает на опоре
                l.comp_v += 0.35f + 0.12f * speed;  // телескоп садится на касании
                u32 a = k & 1;
                m.ant_v[a][0] += 0.5f * (mech_rnd(m.rng) - 0.5f);
                m.ant_v[a][1] += 0.5f * (mech_rnd(m.rng) - 0.5f);
            }
            continue;
        }
        Vec2 d = wpos_delta_xz(l.at, mech_ideal(mv, m, k, 0.0f));
        err[k] = length(d);
        m.lag = f_max(m.lag, err[k] / thr);
        const MechLeg &p = m.leg[(k + 1) % MECH_LEGS], &q = m.leg[(k + 3) % MECH_LEGS];
        bool free_ = p.t < 0.0f && q.t < 0.0f && p.fold <= 0.0f && q.fold <= 0.0f;
        if (free_ && err[k] > thr && (best < 0 || err[k] > err[best])) best = (i32)k;
    }
    // Лапа, которой пора шагать сразу, шагает, даже если соседняя в воздухе
    for (u32 k = 0; k < MECH_LEGS; ++k)
        if (mech_leg_urgent(mv, m, k)) best = (i32)k;
    if (m.jump == 3) {
        // Все лапы снова на земле — прыжок кончился; антенны хлещут
        bool down = true;
        for (u32 k = 0; k < MECH_LEGS; ++k) down = down && m.leg[k].t < 0.0f;
        if (down) {
            m.jump = 0;
            for (u32 a = 0; a < 2; ++a) m.ant_v[a][0] += 1.6f;
        }
    }
    if (best < 0 || m.jump >= 2) return;
    // Шагает самая отставшая и с ней лапа по диагонали, если и та отстала хоть немного: пары не расходятся
    u32 pair[2] = {(u32)best, ((u32)best + 2) % MECH_LEGS};
    for (u32 i = 0; i < 2; ++i) {
        MechLeg &l = m.leg[pair[i]];
        if (i == 1 && (l.t >= 0.0f || l.fold > 0.0f || err[pair[i]] < thr * 0.2f)) continue;
        l.from = l.at;
        l.dur = dur * (1.0f + 0.12f * (mech_rnd(m.rng) - 0.5f));
        WPos to = mech_ideal(mv, m, pair[i], l.dur + 0.5f * dur);
        to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
        l.to = to;
        l.lift = (0.07f + 0.03f * speed) * mech_desc(m).leg[pair[i]].lift + 0.5f * f_abs(to.y - l.from.y);
        l.t = 0.0f;
    }
}

// Отрыв в прыжке: все лапы уходят в один длинный шаг до места касания, поджимаясь под тело на вершине
static void mech_leap(const MechView &mv, Mech &m, const Extract &c) {
    f32 up = f_max(m.h_v, 0.0f);
    f32 fly = (m.h_v + f_sqrt(f_max(m.h_v * m.h_v + 2.0f * MECH_G * (m.h - VEZHA_H_LAND), 0.0f))) / MECH_G;   // до касания, с
    fly = f_clamp(fly, 0.25f, 0.9f);
    f32 step = mech_step_time(length(m.vel));
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        l.from = mech_leg_now(l);
        l.dur = fly * (1.0f + 0.05f * (mech_rnd(m.rng) - 0.5f));
        WPos to = mech_ideal(mv, m, k, l.dur + 0.5f * step);
        to.y = mech_support(c, to, m.at.y + 0.4f, &l.n);
        l.to = to;
        l.lift = up * up / (2.0f * MECH_G) + 0.22f;
        l.t = 0.0f;
    }
    m.jump = 3;
}

// Высота и наклон тела по опоре лап, пружины корпуса, ствола, головы и антенн
// Телескопы ног (MECH_LEG_SLIDE): шток сидит под весом, садится глубже на касании и в приседе перед прыжком, в
// воздухе выходит до упора; пружина жёсткая и чуть недогашена — после касания шток раз-другой качнётся
static void mech_struts(Mech &m, f32 dt) {
    const MechDesc &md = mech_desc(m);
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        MechLeg &l = m.leg[k];
        if (md.leg[k].mode != MECH_LEG_SLIDE) continue;
        bool planted = l.fold <= 0.0f && l.t < 0.0f;
        f32 to = planted ? (m.jump == 1 ? 0.055f : 0.024f) : 0.0f;
        mech_spring(l.comp, l.comp_v, to, 34.0f, 0.32f, dt);
        if (l.comp < 0.0f) { l.comp = 0.0f; l.comp_v = f_max(l.comp_v, 0.0f); }
        if (l.comp > VEZHA_COMP_MAX) { l.comp = VEZHA_COMP_MAX; l.comp_v = f_min(l.comp_v, 0.0f); }
    }
}

static void mech_springs(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    mech_struts(m, dt);
    // Опора: средняя высота лап и наклон плоскости через них (в осях машины)
    f32 sum = 0.0f, sz = 0.0f, sx = 0.0f, zz = 0.0f, xx = 0.0f;
    f32 hmin = 0.0f, hmax = 10.0f;
    u32 n = 0, air = 0;
    f32 ys[MECH_LEGS];
    Vec3 ps[MECH_LEGS];
    for (u32 k = 0; k < MECH_LEGS; ++k) {
        const MechLeg &l = m.leg[k];
        if (l.fold > 0.0f) continue;
        WPos p = l.t >= 0.0f ? l.to : l.at;
        Vec2 d = wpos_delta_xz(m.at, p);
        ps[n] = mech_to_body(m.yaw, Vec3{d.x, 0, d.y});
        ys[n] = p.y;
        sum += p.y;
        if (l.t >= 0.0f) air++;
        else mech_leg_hlimit(mv, m, k, hmin, hmax);   // стоящая лапа ограничивает высоту
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
    // Наклон в сторону разгона: вперёд — носом вниз, вбок — в сторону хода
    Vec3 ab = mech_to_body(m.yaw, m.acc);
    pitch_to += f_clamp(0.022f * ab.z, -0.12f, 0.12f);
    roll_to -= f_clamp(0.022f * ab.x, -0.12f, 0.12f);
    if (m.jump >= 2) pitch_to -= f_clamp(0.05f * m.h_v, -0.16f, 0.16f);   // в прыжке: вверх — носом вверх, вниз — носом вниз
    mech_spring(m.pitch, m.pitch_v, pitch_to, 9.0f, 0.5f, dt);
    mech_spring(m.roll, m.roll_v, roll_to, 9.0f, 0.5f, dt);
    // Высота: пружина с перелётом, на ходу тело проседает, пока лапы в воздухе; стоя — «дышит»
    f32 to = m.h_to - 0.012f * (f32)air;
    if (m.state == MECH_STAND || m.state == MECH_PACKED) to += 0.004f * sinf(m.clock * 2.4f);
    bool lying = m.state == MECH_PACKED || (m.state == MECH_FOLD && n < MECH_LEGS) || (m.state == MECH_UNFOLD && n < MECH_LEGS);
    bool slow = m.state == MECH_FOLD || m.state == MECH_UNFOLD;   // встаёт и ложится не рывком
    if (m.jump >= 2) {
        // Толчок и полёт: тело летит по баллистике; лапы отрываются, когда ноги почти выпрямились
        m.h_v -= MECH_G * dt;
        m.h += m.h_v * dt;
        bool behind = false;   // стойка уехала под ось — толчок кончился раньше
        for (u32 k = 0; k < MECH_LEGS; ++k) behind = behind || mech_leg_urgent(mv, m, k);
        if (m.jump == 2 && (m.h > 0.76f || m.h > hmax - 0.03f || m.h_v < 1.0f || behind)) mech_leap(mv, m, c);
    } else {
        mech_spring(m.h, m.h_v, to, lying ? 16.0f : (slow ? 4.6f : 8.5f), lying ? 1.0f : (slow ? 0.62f : 0.55f), dt);
    }
    if (n == MECH_LEGS && hmin < hmax) {
        if (m.h < hmin) { m.h = hmin; m.h_v = f_max(m.h_v, 0.0f); }
        if (m.h > hmax) { m.h = hmax; m.h_v = f_min(m.h_v, 0.0f); }
    }
    if (m.h < mech_desc(m).belly) { m.h = mech_desc(m).belly; m.h_v = f_max(m.h_v, 0.0f); }

    // Куда смотреть: на человека рядом; иначе по ходу; иначе — оглядывается сама
    f32 aim = m.look;
    f32 elev = 0.0f;
    bool track = tg.on;
    if (track) {
        Vec2 d = wpos_delta_xz(m.at, tg.at);
        aim = atan2f(d.x, d.y);
        f32 gun_y = m.at.y + m.h + 0.38f;
        elev = atan2f(tg.at.y + 1.25f - gun_y, f_max(length(d), 0.5f));
    } else {
        m.look_wait -= dt;
        if (m.state == MECH_WALK && length(m.vel) > 0.5f && m.look_wait > 1.2f) m.look_wait = 1.2f;
        if (m.look_wait <= 0.0f) {
            f32 base = m.state == MECH_WALK ? atan2f(m.vel.x, m.vel.z) : (m.state == MECH_PACKED ? m.home_yaw : m.yaw + m.hull);
            f32 spread = m.state == MECH_WALK ? 0.5f : (m.state == MECH_PACKED ? 1.5f : 1.1f);
            m.look = base + (mech_rnd(m.rng) * 2.0f - 1.0f) * spread;
            m.look_wait = 1.6f + 3.0f * mech_rnd(m.rng);
        }
        aim = m.look;
    }
    // Голова быстрая: скачком смотрит на цель; корпус едет следом медленно и доворачивает, когда голове далеко
    f32 want = mech_wrap(aim - m.yaw - m.hull);
    m.eye_wait -= dt;
    if (m.eye_wait <= 0.0f) {
        f32 jitter = track ? 0.0f : (mech_rnd(m.rng) - 0.5f) * 0.5f;
        m.eye_to = f_clamp(want + jitter, -1.4f, 1.4f);
        m.eye_wait = track ? 0.12f : 0.5f + 1.3f * mech_rnd(m.rng);
    }
    m.eye += (m.eye_to - m.eye) * f_min(1.0f, 14.0f * dt);
    f32 hull_err = f_abs(want) > (track ? 0.06f : 0.5f) || f_abs(m.hull_v) > 0.2f ? want : 0.0f;
    f32 w = lying ? 2.6f : 4.2f;
    m.hull_v += (w * w * hull_err - 2.0f * 0.85f * w * m.hull_v) * dt;
    m.hull_v = f_clamp(m.hull_v, -2.2f, 2.2f);
    m.hull += m.hull_v * dt;
    m.eye_to -= m.hull_v * dt;   // голова держит взгляд, пока корпус под ней едет
    m.eye -= m.hull_v * dt;
    m.eye = f_clamp(m.eye, -1.5f, 1.5f);
    // Ствол держит возвышение: наклон рамы гасится
    f32 ch = cosf(m.hull), sh = sinf(m.hull);
    f32 gun_to = f_clamp(elev + m.pitch * ch - m.roll * sh, -0.17f, 0.6f);
    mech_spring(m.gun, m.gun_v, gun_to, 10.0f, 0.75f, dt);
    // Антенны — хлысты: отстают от разгона корпуса и долго качаются
    Vec3 ah = mech_to_body(m.yaw + m.hull, m.acc);
    for (u32 a = 0; a < 2; ++a) {
        f32 ww = a ? 11.5f : 8.5f;
        m.ant_v[a][0] += (-ww * ww * m.ant[a][0] - 2.0f * 0.07f * ww * m.ant_v[a][0] - 2.6f * ah.z) * dt;
        m.ant_v[a][1] += (-ww * ww * m.ant[a][1] - 2.0f * 0.07f * ww * m.ant_v[a][1] + 2.6f * ah.x) * dt;
        for (u32 i = 0; i < 2; ++i) m.ant[a][i] = f_clamp(m.ant[a][i] + m.ant_v[a][i] * dt, -0.35f, 0.35f);
    }
}

void mech_step(const MechView &mv, Mech &m, const Extract &c, const MechTarget &tg, f32 dt) {
    mech_brain(mv, m, c, tg, dt);
    mech_body(m, dt);
    mech_legs(mv, m, c, dt);
    mech_springs(mv, m, c, tg, dt);
}
