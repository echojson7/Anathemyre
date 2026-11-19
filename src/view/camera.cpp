#include "camera.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include <math.h>

// Пределы
static constexpr f32 CAM_PITCH_MIN = -1.40f, CAM_PITCH_MAX = 1.50f;  // вверх до 80°, вниз почти отвесно (86°)
static constexpr f32 CAM_PITCH_ORBIT = -0.52f;  // на орбите вверх до 30° вблизи (cam_pitch_min)
static constexpr f32 CAM_DIST_MAX = 12000.0f;
static constexpr f32 CAM_DIST_SNAP = 0.5f;      // ближе — сразу вид от глаз
static constexpr f32 CAM_ZOOM_STEP = 0.85f;     // щелчок колеса
static constexpr f32 CAM_RAD_PER_PX = 0.005f;
// Высоты
static constexpr f32 CAM_PIVOT_M = 1.7f;        // опора камеры снимка и --pivot (у орбиты — cam.rise по штанге)
static constexpr f32 CAM_PIVOT_MIN_M = 0.8f;    // сглаженная опора не ниже над землёй под собой
static constexpr f32 CAM_EYE_M = 0.6f;          // глаз не ниже над землёй
static constexpr f32 CAM_LIFT_SLACK_M = 0.35f;  // насколько подъём глаза может отстать от земли
static constexpr f32 CAM_WALL_M = 0.3f;         // глаз не ближе к коробкам построек
static constexpr f32 CAM_FOLLOW_ARM_MIN = 1.2f; // штанга за человеком не короче, м
static constexpr f32 CAM_STEP_M = 1.25f;        // уступ, на который опора под землёй встаёт сразу (край платформы)
static constexpr f32 CAM_BELOW_M = 1.0f;        // глубже — опора под землёй (склон за кадр столько не даёт)
static constexpr f32 CAM_BACK_MAX_M = 400.0f;   // из полёта на орбиту: опора не дальше
// Времена сглаживания, секунды (за это время остаток падает в e раз). Ввод — умножается на настройку «плавность»
static constexpr f32 CAM_ROT_S = 0.045f;        // взгляд
static constexpr f32 CAM_ZOOM_S = 0.09f;        // колесо
static constexpr f32 CAM_MOVE_S = 0.08f;        // разгон и остановка опоры
static constexpr f32 CAM_HEIGHT_S = 0.14f;      // высота опоры по рельефу
static constexpr f32 CAM_ARM_IN_S = 0.04f;      // штанга к стене
static constexpr f32 CAM_ARM_OUT_S = 0.30f;     // штанга обратно
static constexpr f32 CAM_ARM_HOLD_S = 0.20f;    // пауза перед ростом обратно (колонна мелькнула — штанга не качается)
static constexpr f32 CAM_LIFT_UP_S = 0.06f;     // глаз вверх от земли
static constexpr f32 CAM_LIFT_DOWN_S = 0.25f;   // и обратно
static constexpr f32 CAM_EASE_S = 0.12f;        // переход между режимами
static constexpr f32 CAM_FOLLOW_S = 0.16f;      // опора догоняет человека (пружина): без покачиваний, с весом
static constexpr f32 CAM_KEYROT_S = 0.10f;      // Q/E: разгон и остановка поворота
// Клавиши
static constexpr f32 CAM_KEYROT = 1.9f;         // Q/E, рад/с (≈110°/с)
static constexpr f32 CAM_EDGE_PX = 3.0f;        // край экрана, пиксели
static constexpr f32 CAM_DEG = PI_F / 180.0f;

// Доля пути к цели за dt при времени сглаживания tau (не зависит от частоты кадров)
static f32 cam_alpha(f32 dt, f32 tau) { return tau <= 1e-4f ? 1.0f : 1.0f - expf(-dt / tau); }

// Скорость v идёт к цели target по экспоненте (время tau); путь за dt — точный интеграл, поэтому разгон и
// остановка одинаковы при 30 и 150 к/с
static f32 cam_ramp(f32 &v, f32 target, f32 dt, f32 tau) {
    if (tau <= 1e-4f) {
        v = target;
        return target * dt;
    }
    f32 k = 1.0f - expf(-dt / tau), s = target * dt + (v - target) * (tau * k);
    v += (target - v) * k;
    return s;
}
static Vec3 cam_ramp(Vec3 &v, Vec3 target, f32 dt, f32 tau) {
    return Vec3{cam_ramp(v.x, target.x, dt, tau), cam_ramp(v.y, target.y, dt, tau), cam_ramp(v.z, target.z, dt, tau)};
}

// Направление от опоры к глазу по yaw и pitch
static Vec3 cam_back(f32 yaw, f32 pitch) {
    f32 cp = cos_f(pitch), sp = sin_f(pitch);
    return {sin_f(yaw) * cp, sp, -cos_f(yaw) * cp};
}

static WPos cam_offset(const WPos &p, Vec3 d) {
    WPos q = p;
    q.x += d.x;
    q.y += d.y;
    q.z += d.z;
    wpos_rebase(q);
    return q;
}

// Метры от a до b
static Vec3 cam_delta(const WPos &a, const WPos &b) {
    Vec2 d = wpos_delta_xz(a.cx, a.cz, a.x, a.z, b.cx, b.cz, b.x, b.z);
    return {d.x, b.y - a.y, d.y};
}

static f32 cam_wrap(f32 a) {
    while (a > PI_F) a -= TAU_F;
    while (a < -PI_F) a += TAU_F;
    return a;
}

f32 view_camera_fov(const Camera &c) { return f_clamp(c.settings.fov_deg, 20.0f, 120.0f) * CAM_DEG + c.fov_add; }

// Кривая наклона по длине штанги (Нески, «50 ошибок камеры»: наклон и расстояние идут вместе): вблизи ~4–6°
// (вдоль земли, видно людей и горизонт), 20 м — 33°, 150 м — 56°, дальше к 62° (карта сверху). Гладкая, без
// изломов: при прокрутке колеса наклон не дёргается
static f32 cam_tilt_curve(f32 d) { return (62.0f - 58.0f / (1.0f + powf(f_max(d, 0.0f) / 20.0f, 1.1f))) * CAM_DEG; }
f32 view_camera_auto_pitch(f32 dist) { return cam_tilt_curve(dist); }
static f32 cam_base(const Camera &c, f32 d) { return c.tilt_on ? cam_tilt_curve(d) : 0.0f; }

// Насколько вверх можно смотреть: вблизи (до 3 м) — на 30° над горизонтом, от 12 м — до горизонта. Издалека глаз
// выше опоры у самой земли только при взгляде вниз; смотреть в небо — приблизиться. Полёт и снимок — до 80°
static f32 cam_pitch_min(const Camera &c, f32 d) {
    if (c.free || c.exact) return CAM_PITCH_MIN;
    f32 t = f_clamp((2.4849f - logf(f_max(d, 1e-3f))) / (2.4849f - 1.0986f), 0.0f, 1.0f);  // ln 12 … ln 3
    return CAM_PITCH_ORBIT * t * t * (3.0f - 2.0f * t);
}

// Опора над полом по длине штанги: вблизи (до 3 м) — 1.6 м, на уровне груди и головы; издалека (от 60 м) — 0.4 м,
// у земли: камера вращается вокруг того места земли, что в центре экрана
static f32 cam_rise_of(f32 d) {
    f32 t = f_clamp((logf(f_max(d, 1e-3f)) - 1.0986f) / (4.0943f - 1.0986f), 0.0f, 1.0f);  // ln 3 … ln 60
    return 1.6f - 1.2f * t * t * (3.0f - 2.0f * t);
}

// Взгляд снизу вверх: штанга короче, чтобы глаз остался над полом опоры (Нески: снизу места мало — камера
// подходит ближе). Зависит только от наклона и высоты опоры, не от рельефа: ответ непрерывный; минимум мягкий
static f32 cam_worm(f32 d, f32 pitch, f32 rise) {
    if (pitch >= 0.0f || d <= 0.0f) return d;
    f32 room = f_max(0.5f, (rise - 0.45f) / sin_f(-pitch));
    f32 a = d * d, b = room * room;
    return d * room / sqrtf(sqrtf(a * a + b * b));
}

// Пружина с критическим затуханием (SmoothDamp, Game Programming Gems 4): остаток x идёт к нулю, v — скорость.
// В отличие от экспоненты, трогается без рывка; от частоты кадров почти не зависит
static Vec3 cam_damp(Vec3 x, Vec3 &v, f32 tau, f32 dt) {
    if (tau <= 1e-4f) {
        v = Vec3{};
        return Vec3{};
    }
    f32 w = 2.0f / tau, k = w * dt, e = 1.0f / (1.0f + k + 0.48f * k * k + 0.235f * k * k * k);
    Vec3 t = (v + x * w) * dt;
    v = (v - t * w) * e;
    return (x + t) * e;
}

CameraBasis view_camera_basis(const Camera &cam) {
    CameraBasis b;
    b.forward = -cam_back(cam.yaw_now, cam.pitch_now);
    b.right = normalize(cross(Vec3{0, 1, 0}, b.forward));
    b.up = cross(b.forward, b.right);
    return b;
}

WPos view_eye(const Camera &cam) { return cam.eye; }

// Земля вокруг точки: среднее по кругу радиуса r (высота опоры идёт по рельефу, а не по каждой кочке) и
// верх по кругу (глаз не цепляет гребень рядом)
static constexpr f32 CAM_RING[8][2] = {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f},
                                       {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f}};
static f32 cam_ground_mean(const Terrain &ground, const WPos &p, f32 r) {
    f32 sum = ground_at(ground, p).height * 2.0f;
    for (const f32 *k : CAM_RING) {
        sum += ground_at(ground, cam_offset(p, Vec3{k[0] * r, 0, k[1] * r})).height;
        sum += ground_at(ground, cam_offset(p, Vec3{k[0] * r * 0.5f, 0, k[1] * r * 0.5f})).height * 0.5f;
    }
    return sum / 14.0f;
}
static f32 cam_ground_top(const Terrain &ground, const WPos &p, f32 r) {
    f32 top = ground_at(ground, p).height;
    for (const f32 *k : CAM_RING) top = f_max(top, ground_at(ground, cam_offset(p, Vec3{k[0] * r, 0, k[1] * r})).height);
    return top;
}

// Точка ниже земли, но не в грунте: проём земли или подземная постройка (docs/camera.md, «Под землёй»)
static bool cam_open_below(const Terrain &ground, const Colliders *coll, const WPos &p) {
    if (!coll) return false;
    if (ground_hole(ground, p)) return true;
    return p.y < ground_at(ground, p).height && underground_open(ground, *coll, p.cx, p.cz, p.x, p.z, p.y);
}

// Пол под опорой орбиты (feet — прошлый). На поверхности — земля. Над проёмом или глубже CAM_BELOW_M под землёй —
// пол постройки: по ступеням как человек, не вышло — на уступ до CAM_STEP_M. Пол годится, если над ним не грунт.
// Пола нет (опора в стене) — высота прежняя; постройки нет — на землю. under — опора на полу постройки
static f32 cam_feet_settle(const Terrain &ground, const Colliders *coll, const WPos &p, f32 feet, bool *under) {
    f32 g = ground_at(ground, p).height;
    *under = false;
    bool below = feet < g - CAM_BELOW_M;
    if (coll && (below || ground_hole(ground, p))) {
        const f32 steps[2] = {STEP_UP_M, CAM_STEP_M};
        for (f32 step : steps) {
            Support s = support_at(ground, *coll, p.cx, p.cz, p.x, p.z, feet + step - STEP_UP_M);
            WPos q = p;
            q.y = s.height + 0.1f;
            if (s.kind && (s.height > g || cam_open_below(ground, coll, q))) {
                *under = true;
                return s.height;
            }
        }
        WPos q = p;
        q.y = feet + CAM_PIVOT_M;
        if (below && cam_open_below(ground, coll, q)) {
            *under = true;
            return feet;
        }
    }
    return g;
}

// Высота опоры орбиты: над полом (под землёй) или над сглаженной землёй, плавно по времени
static void cam_pivot_height(Camera &cam, f32 dt, const Terrain &ground, const Colliders *coll) {
    f32 g = ground_at(ground, cam.pivot).height;
    if (cam.pivot_above > CAM_PIVOT_UNSET) {
        cam.pivot.y = g + cam.pivot_above;
        cam.feet = cam.pivot.y - CAM_PIVOT_M;
        return;
    }
    bool under;
    cam.feet = cam_feet_settle(ground, coll, cam.pivot, cam.feet, &under);
    f32 target = cam.feet + cam.rise;
    const f32 low = f_min(CAM_PIVOT_MIN_M, cam.rise * 0.6f);  // не ниже над землёй под собой
    if (!under && !cam.exact) {
        f32 r = f_clamp(cam.dist_now * 0.12f, 1.0f, 40.0f);
        target = f_max(cam_ground_mean(ground, cam.pivot, r) + cam.rise, g + low);
    }
    cam.pivot.y += (target - cam.pivot.y) * cam_alpha(dt, cam.exact ? 0.0f : CAM_HEIGHT_S);
    if (!under) cam.pivot.y = f_max(cam.pivot.y, g + low);
}

// Глазу в точке p мешают коробки построек (не ближе CAM_WALL_M) или грунт. soil — опора под землёй: грунт вокруг
// постройки — стена. На поверхности грунт не стена: глаз над ним поднимется (cam_solve), коробки смотрим там
static bool cam_blocked(const Terrain &ground, const Colliders *coll, WPos p, bool soil) {
    f32 floor_y = ground_at(ground, p).height + CAM_EYE_M;
    if (p.y < floor_y && !cam_open_below(ground, coll, p)) {
        if (soil) return true;
        p.y = floor_y;
    }
    if (coll)
        for (u16 k : colliders_near(*coll, p.cx, p.cz, p.x, p.z))
            if (p.y > coll->y0[k] - CAM_WALL_M && p.y < coll->y1[k] + CAM_WALL_M &&
                coll_touches(*coll, k, p.cx, p.cz, p.x, p.z, CAM_WALL_M))
                return true;
    return false;
}

// Сколько штанги длиной want помещается от опоры вдоль d: шагами наружу, последний свободный шаг. Шаг мелкий
// (стена 0.2 м); дальше 20 м он растёт — длинная штанга не стоит тысяч проверок
static f32 cam_arm_room(const WPos &pivot, Vec3 d, f32 want, const Terrain &ground, const Colliders *coll, bool soil) {
    if (want <= 0.0f) return 0.0f;
    f32 ok = 0.0f;
    for (f32 t = 0.1f;; t += t > 20.0f ? t * 0.02f : 0.1f) {
        f32 tt = f_min(t, want);
        if (cam_blocked(ground, coll, cam_offset(pivot, d * tt), soil)) return ok;
        ok = tt;
        if (tt >= want) return ok;
    }
}

// Глаз по опоре, взгляду и штанге. Стены укорачивают штангу (к стене быстро, обратно плавно и с паузой), земля
// поднимает глаз; взгляд не меняет ни то, ни другое. walls — штангу держат постройки (за человеком; под землёй —
// всегда, вместе с грунтом)
static void cam_solve(Camera &cam, f32 dt, const Terrain &ground, const Colliders *coll, bool walls) {
    const f32 ke = cam.exact ? 0.0f : 1.0f;
    WPos prev = cam.eye, e;
    if (cam.free) {
        e = cam.pivot;
        cam.arm = 0.0f;
        cam.arm_want = 0.0f;
        cam.lift = 0.0f;
    } else {
        Vec3 d = cam_back(cam.yaw_now, cam.pitch_now);
        f32 want = cam.exact ? cam.dist_now : cam_worm(cam.dist_now, cam.pitch_now, cam.rise);
        // Опора под землёй: ниже, чем её держит над землёй cam_pivot_height (издалека опора у самой земли)
        f32 under_m = cam.exact ? CAM_EYE_M : f_min(CAM_EYE_M, cam.rise * 0.5f);
        bool under = coll && cam.pivot.y < ground_at(ground, cam.pivot).height + under_m;
        f32 room = want;
        if (walls || under) room = cam_arm_room(cam.pivot, d, want, ground, coll, under);
        // Не ближе CAM_FOLLOW_ARM_MIN к голове: спиной к стене глаз лучше уйдёт за стену, чем в голову
        if (cam.follow) room = f_max(room, f_min(want, CAM_FOLLOW_ARM_MIN));
        bool tracking = cam.arm >= cam.arm_want - 1e-3f;  // стены не мешали: штанга шла за колесом
        if (cam.arm < 0.0f || (tracking && room >= want - 1e-3f)) {
            cam.arm = room;
        } else if (room < cam.arm) {
            cam.arm += (room - cam.arm) * cam_alpha(dt, CAM_ARM_IN_S * ke);
            cam.arm_hold = CAM_ARM_HOLD_S;
        } else {
            cam.arm_hold -= dt;
            if (cam.arm_hold <= 0.0f || ke == 0.0f) cam.arm += (room - cam.arm) * cam_alpha(dt, CAM_ARM_OUT_S * ke);
            if (room - cam.arm < 0.01f) cam.arm = room;
        }
        cam.arm = f_min(cam.arm, want);
        cam.arm_want = want;

        e = cam_offset(cam.pivot, d * cam.arm);
        // Земля: чем длиннее штанга, тем выше запас и шире круг, по которому берём верх (на большой скорости
        // глаза над рельефом мелкие гребни не толкают). Камера снимка — ровно старое правило 0.6 м
        f32 need = 0.0f;
        if (!under && !cam_open_below(ground, coll, e)) {
            f32 clr = CAM_EYE_M, top = ground_at(ground, e).height;
            if (!cam.exact) {
                clr += f_min(cam.arm * 0.02f, 2.4f);
                top = cam_ground_top(ground, e, f_clamp(cam.arm * 0.03f, 0.4f, 12.0f));
            }
            need = f_max(0.0f, top + clr - e.y);
        }
        // Вверх — почти жёстко (отставание не больше CAM_LIFT_SLACK_M: глаз не ближе 0.25 м к земле), вниз — мягко
        cam.lift += (need - cam.lift) * cam_alpha(dt, (need > cam.lift ? CAM_LIFT_UP_S : CAM_LIFT_DOWN_S) * ke);
        cam.lift = f_max(cam.lift, need - CAM_LIFT_SLACK_M);
        if (cam.lift < 1e-4f) cam.lift = 0.0f;
        e.y += cam.lift;
    }
    // Переход: опора прыгнула (полёт ↔ орбита, за человеком) — глаз доезжает от прежнего места
    cam.ease = cam.ease * (1.0f - cam_alpha(dt, CAM_EASE_S * ke));
    if (cam.blend) {
        cam.blend = false;
        Vec3 jump = cam_delta(e, prev);
        cam.ease = (ke > 0.0f && cam.placed && length(jump) < 2000.0f) ? jump : Vec3{};
    }
    if (dot(cam.ease, cam.ease) < 1e-8f) cam.ease = Vec3{};
    cam.eye = cam_offset(e, cam.ease);
}

void view_camera_place(Camera &cam, const Terrain &ground, const Colliders *coll) {
    cam.yaw = cam_wrap(cam.yaw);
    cam.pitch = f_clamp(cam.pitch, cam_pitch_min(cam, cam.dist), CAM_PITCH_MAX);
    cam.dist = f_clamp(cam.dist, 0.0f, CAM_DIST_MAX);
    cam.yaw_now = cam.yaw;
    cam.pitch_now = cam.pitch;
    cam.dist_now = cam.dist;
    // Заданный наклон сохраняется: свой наклон — разница с кривой
    cam.tilt_on = cam.settings.tilt_zoom && !cam.exact;
    cam.tilt = cam.tilt_now = cam.pitch - cam_base(cam, cam.dist);
    cam.rise = cam.exact ? CAM_PIVOT_M : cam_rise_of(cam.dist);
    cam.fov_add = 0.0f;
    cam.yaw_rate = 0.0f;
    cam.zoom_pan = cam.zoom_pan_v = Vec3{};
    cam.panning = false;
    cam.follow_vel = Vec3{};
    cam.arm = -1.0f;
    cam.arm_hold = 0.0f;
    cam.lift = 0.0f;
    cam.vel = Vec3{};
    cam.dolly = 0.0f;
    cam.ease = Vec3{};
    cam.blend = false;
    cam.placed = false;
    const f32 now = 1e6f;  // всё сглаживание доходит до цели сразу
    if (!cam.free) {
        f32 g = ground_at(ground, cam.pivot).height;
        cam.feet = g;
        cam.pivot.y = g + cam.rise;
        cam_pivot_height(cam, now, ground, coll);
    }
    cam_solve(cam, now, ground, coll, cam.follow);
    cam.placed = true;
}

// Кривая наклона включена или выключена (настройка, камера снимка без неё): переключили — наклон в кадре не прыгает
static void cam_tilt_mode(Camera &cam) {
    bool on = cam.settings.tilt_zoom && !cam.exact;
    if (on == cam.tilt_on) return;
    cam.tilt += cam_base(cam, cam.dist);
    cam.tilt_now += cam_base(cam, cam.dist_now);
    cam.tilt_on = on;
    cam.tilt -= cam_base(cam, cam.dist);
    cam.tilt_now -= cam_base(cam, cam.dist_now);
}

// Наклон в кадре: кривая по штанге в кадре + свой наклон игрока, в пределах
static void cam_pitch_frame(Camera &cam) {
    f32 p = cam_base(cam, cam.dist_now) + cam.tilt_now;
    cam.pitch_now = f_clamp(p, cam_pitch_min(cam, cam.dist_now), CAM_PITCH_MAX);
}

// Колесо к курсору: куда поставить опору, чтобы точка p осталась под пикселем (px, py) при штанге d — с наклоном,
// высотой опоры и штангой снизу этой длины (кривые). false — луч курсора при новом наклоне не смотрит вниз
static bool cam_zoom_target(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const WPos &p, f32 d,
                            const Terrain &ground, WPos &out) {
    f32 pitch = f_clamp(cam_base(cam, d) + cam.tilt, cam_pitch_min(cam, d), CAM_PITCH_MAX);
    f32 rise = cam_rise_of(d), arm = cam_worm(d, pitch, rise);
    Vec3 back = cam_back(cam.yaw, pitch), fwd = -back;
    Vec3 right = normalize(cross(Vec3{0, 1, 0}, fwd)), up = cross(fwd, right);
    f32 s, c;
    sincos_turn(turn_from_rad(view_camera_fov(cam) * 0.5f), &s, &c);
    f32 t = s / c, aspect = (f32)width / (f32)height;
    f32 nx = (px / (f32)width) * 2.0f - 1.0f, ny = 1.0f - (py / (f32)height) * 2.0f;
    Vec3 r = normalize(fwd + right * (nx * t * aspect) + up * (ny * t));
    if (r.y > -0.02f) return false;
    // Высота опоры: над точкой, потом над землёй там, где опора встанет (два приближения хватает)
    out = p;
    f32 floor_y = p.y;
    for (u32 i = 0; i < 2; ++i) {
        f32 eye_y = floor_y + rise + back.y * arm;
        f32 k = (p.y - eye_y) / r.y;  // от глаза до точки вдоль луча
        if (k <= 0.0f) return false;
        out = cam_offset(p, Vec3{-r.x * k - back.x * arm, 0.0f, -r.z * k - back.z * arm});
        floor_y = ground_at(ground, out).height;
        if (p.y > floor_y + 0.5f || p.y < floor_y - 3.0f) floor_y = p.y;  // точка на постройке: пол — её высота
    }
    return true;
}

// Точка земли, за которую взяли (Shift + средняя), под точкой экрана (px, py): насколько сдвинуть опору. Луч до
// плоскости на высоте взятой точки
static bool cam_grab_step(const Camera &cam, i32 width, i32 height, f32 px, f32 py, Vec3 &step,
                          bool *clipped = nullptr) {
    WPos eye;
    Vec3 dir;
    if (!view_screen_ray(cam, width, height, px, py, eye, dir)) return false;
    // Луч выше горизонта или пересекает плоскость слишком далеко — точка на луче на пределе: сдвиг остаётся
    // непрерывным, а не прыгает
    f32 dy = cam.pan_at.y - eye.y, lim = f_max(cam.dist_now, 5.0f) * 12.0f;
    f32 t = dir.y * dy > 1e-6f ? f_min(dy / dir.y, lim) : lim;
    if (clipped) *clipped = t >= lim;
    WPos hit = cam_offset(eye, dir * t);
    step = cam_delta(hit, cam.pan_at);
    step.y = 0.0f;
    return true;
}

void view_camera_control(Camera &cam, const Input &in, f32 dt, i32 width, i32 height, bool edge, const Terrain &ground,
                         const Colliders *coll) {
    if (!cam.placed) view_camera_place(cam, ground, coll);
    const f32 ks = cam.exact ? 0.0f : f_clamp(cam.settings.smooth, 0.0f, 3.0f);  // плавность ввода
    cam_tilt_mode(cam);

    if (in.key_pressed[KEY_F]) {
        if (!cam.free) {
            // В полёт: опора — глаз, как он стоит в кадре
            cam.pivot = cam.eye;
            cam.ease = Vec3{};
            cam.vel = Vec3{};
            cam.dolly = 0.0f;
            cam.zoom_pan = cam.zoom_pan_v = Vec3{};
            cam.free = true;
            cam.follow = false;
        } else {
            // Назад на орбиту: опора — над точкой земли в центре экрана, штанга — до глаза; глаз доезжает плавно.
            // Точка далеко или смотрим в небо — опора впереди на прежнем расстоянии
            WPos eye = cam.eye, hit;
            CameraBasis b = view_camera_basis(cam);
            f32 len = 0.0f;
            bool ok = view_screen_to_ground(cam, 2, 2, 1.0f, 1.0f, ground, hit);
            if (ok) len = length(cam_delta(eye, hit));
            if (cam_open_below(ground, coll, eye) && eye.y < ground_at(ground, eye).height) {
                // Под землёй (подземная постройка): опора — на месте глаза, на пол её ставит cam_feet_settle
                cam.dist = f_min(cam.dist, 3.0f);
                cam.rise = cam_rise_of(cam.dist);
                cam.pivot = eye;
                cam.feet = eye.y - cam.rise;
            } else {
                if (ok && len <= CAM_BACK_MAX_M) {
                    cam.dist = len;
                    cam.pivot = hit;
                } else {
                    cam.dist = f_clamp(cam.dist, 5.0f, 60.0f);
                    cam.pivot = cam_offset(eye, normalize(Vec3{b.forward.x, 0.0f, b.forward.z}) * cam.dist);
                }
                cam.rise = cam.exact ? CAM_PIVOT_M : cam_rise_of(cam.dist);
                cam.feet = ground_at(ground, cam.pivot).height;
                cam.pivot.y = cam.feet + cam.rise;
            }
            cam.dist_now = cam.dist;
            // Наклон в кадре тот же, что был в полёте; на орбите вверх — не круче cam_pitch_min
            cam.free = false;
            f32 p = f_clamp(cam.pitch_now, cam_pitch_min(cam, cam.dist), CAM_PITCH_MAX);
            cam.tilt = cam.tilt_now = p - cam_base(cam, cam.dist);
            cam.arm = -1.0f;
            cam.lift = 0.0f;
            cam.vel = Vec3{};
            cam.blend = true;
            cam.free = false;
        }
    }

    // Взгляд: средняя кнопка или Alt + правая (правая — приказ), одинаково на орбите и в полёте: мышь вправо —
    // взгляд вправо, мышь вверх — взгляд вверх. Мышь — без задержки (сглаживание короткое). Наклон мышью —
    // свой наклон игрока поверх кривой (tilt): колесо его не сбрасывает. Shift + средняя — тащить землю
    bool rotate = (in.mouse_down[MOUSE_MIDDLE] && !in.key_down[KEY_SHIFT]) ||
                  (in.mouse_down[MOUSE_RIGHT] && in.key_down[KEY_ALT]);
    bool pan = !cam.free && in.mouse_down[MOUSE_MIDDLE] && in.key_down[KEY_SHIFT];
    cam.grab = rotate || pan;
    const f32 pmin = cam_pitch_min(cam, cam.dist);
    if (rotate) {
        f32 k = CAM_RAD_PER_PX * f_clamp(cam.settings.mouse_sens, 0.05f, 20.0f);
        cam.yaw = cam_wrap(cam.yaw - (cam.settings.invert_x ? -1.0f : 1.0f) * in.mouse_dx * k);
        f32 base = cam_base(cam, cam.dist);
        f32 p = base + cam.tilt + (cam.settings.invert_y ? -1.0f : 1.0f) * in.mouse_dy * k;
        cam.tilt = f_clamp(p, pmin, CAM_PITCH_MAX) - base;
    }
    // Q/E — поворот клавишами (как в Kenshi и Baldur's Gate 3): Q — взгляд влево, E — вправо, с разгоном. В полёте
    // Q/E — вниз и вверх
    f32 key_rot = 0.0f;
    if (!cam.free) key_rot = ((in.key_down[KEY_Q] ? 1.0f : 0.0f) - (in.key_down[KEY_E] ? 1.0f : 0.0f)) * CAM_KEYROT;
    f32 turn = cam_ramp(cam.yaw_rate, key_rot, dt, CAM_KEYROT_S * f_max(ks, 0.3f));
    if (f_abs(cam.yaw_rate) < 1e-3f && key_rot == 0.0f) cam.yaw_rate = 0.0f;
    cam.yaw = cam_wrap(cam.yaw + turn);
    cam.pitch = f_clamp(cam_base(cam, cam.dist) + cam.tilt, pmin, CAM_PITCH_MAX);
    f32 ar = cam_alpha(dt, CAM_ROT_S * ks);
    cam.yaw_now = cam_wrap(cam.yaw_now + cam_wrap(cam.yaw - cam.yaw_now) * ar);
    cam.tilt_now += (cam.tilt - cam.tilt_now) * ar;
    if (cam.exact) cam.tilt_now = cam.tilt;

    CameraBasis b = view_camera_basis(cam);
    Vec3 flat_fwd = normalize(Vec3{b.forward.x, 0.0f, b.forward.z});
    Vec3 flat_right = b.right;
    f32 fast = in.key_down[KEY_SHIFT] ? 4.0f : 1.0f;
    Vec3 move{};
    if (in.key_down[KEY_W]) move += cam.free ? b.forward : flat_fwd;
    if (in.key_down[KEY_S]) move += -(cam.free ? b.forward : flat_fwd);
    if (in.key_down[KEY_D]) move += flat_right;
    if (in.key_down[KEY_A]) move += -flat_right;
    if (cam.free) {
        if (in.key_down[KEY_E]) move += Vec3{0, 1, 0};
        if (in.key_down[KEY_Q]) move += Vec3{0, -1, 0};
    } else if (edge && cam.settings.edge_pan && !cam.grab && width > 0 && height > 0) {
        // Курсор у края экрана (настройка) — как WASD
        if (in.mouse_x <= CAM_EDGE_PX) move += -flat_right;
        else if (in.mouse_x >= (f32)width - 1.0f - CAM_EDGE_PX) move += flat_right;
        if (in.mouse_y <= CAM_EDGE_PX) move += flat_fwd;
        else if (in.mouse_y >= (f32)height - 1.0f - CAM_EDGE_PX) move += -flat_fwd;
    }
    f32 ml = length(move);
    if (ml > 1.0f) move = move * (1.0f / ml);  // по диагонали не быстрее
    const f32 tm = CAM_MOVE_S * ks;
    const f32 pace = f_clamp(cam.settings.pan_speed, 0.1f, 5.0f);

    if (cam.free) {
        // Высоко над землёй летать медленно скучно: скорость растёт с высотой. Колесо — проехать вперёд, плавно
        f32 above = cam.pivot.y - ground_at(ground, cam.pivot).height;
        f32 scale = fast * f_max(1.0f, above / 20.0f);
        Vec3 fly = cam_ramp(cam.vel, move * (12.0f * scale * pace), dt, tm);
        cam.dolly += in.wheel * 2.0f * scale;
        f32 go = cam.dolly * cam_alpha(dt, CAM_ZOOM_S * ks);
        cam.dolly -= go;
        cam.pivot = cam_offset(cam.pivot, fly + b.forward * go);
        // Под землю — только в проём и в подземную постройку (стены полёт проходит насквозь)
        f32 floor_y = ground_at(ground, cam.pivot).height + CAM_EYE_M;
        if (cam.pivot.y < floor_y && !cam_open_below(ground, coll, cam.pivot)) {
            cam.pivot.y = floor_y;
            if (cam.vel.y < 0.0f) cam.vel.y = 0.0f;
        }
        cam_pitch_frame(cam);
        cam.fov_add = 0.0f;
        cam.followed = false;
        cam_solve(cam, dt, ground, coll, false);
        return;
    }

    // Колесо — длина штанги (до вида от глаз). Если штангу держит стена, счёт идёт от длины в кадре: то, что
    // накрутили «в стену», не копится и потом не отбрасывает камеру на максимум. Вперёд — к точке под курсором
    // (настройка, как в Total War): опора едет к ней так, что точка остаётся под курсором; назад — от центра
    if (in.wheel > 0.5f || in.wheel < -0.5f) {
        f32 d = cam.dist;
        if (cam.arm >= 0.0f && cam.arm < cam.arm_want - 0.05f) d = f_min(d, cam.arm);
        for (f32 w = in.wheel; w > 0.5f; w -= 1.0f) d = d * CAM_ZOOM_STEP < CAM_DIST_SNAP ? 0.0f : d * CAM_ZOOM_STEP;
        for (f32 w = in.wheel; w < -0.5f; w += 1.0f) d = d < CAM_DIST_SNAP ? CAM_DIST_SNAP : d / CAM_ZOOM_STEP;
        d = f_clamp(d, 0.0f, CAM_DIST_MAX);
        if (d < cam.dist && cam.settings.zoom_cursor && !cam.follow && !cam.exact && width > 0) {
            // Точка под курсором — та же, пока приближение идёт и курсор стоит (иначе взяли бы уже съехавшую)
            f32 mx = in.mouse_x - cam.zoom_px, my = in.mouse_y - cam.zoom_py;
            bool settled = cam.zoom_v == 0.0f && dot(cam.zoom_pan, cam.zoom_pan) == 0.0f;
            if (!cam.zoom_anchor || settled || mx * mx + my * my > 16.0f) {
                cam.zoom_anchor =
                    view_screen_to_support(cam, width, height, in.mouse_x, in.mouse_y, ground, coll, cam.zoom_at);
                cam.zoom_px = in.mouse_x;
                cam.zoom_py = in.mouse_y;
            }
            WPos to;
            if (cam.zoom_anchor &&
                cam_zoom_target(cam, width, height, cam.zoom_px, cam.zoom_py, cam.zoom_at, d, ground, to)) {
                Vec3 go = cam_delta(cam.pivot, to);
                go.y = 0.0f;
                if (length(go) < f_max(cam.dist, 5.0f) * 4.0f) cam.zoom_pan = go;
            }
        } else if (d > cam.dist) {
            cam.zoom_anchor = false;  // назад — от центра: опора стоит
        }
        cam.dist = d;
    }
    // Штанга — по логарифму (щелчок колеса выглядит одинаково и в 2 м, и в 200 м) пружиной: щелчок трогает камеру
    // без рывка. Сдвиг опоры к курсору — такой же пружиной: оба идут в ногу, точка остаётся под курсором
    {
        Vec3 v{cam.zoom_v, 0.0f, 0.0f};
        Vec3 o = cam_damp(Vec3{logf(cam.dist_now + 0.1f) - logf(cam.dist + 0.1f), 0.0f, 0.0f}, v, CAM_ZOOM_S * ks, dt);
        cam.zoom_v = v.x;
        cam.dist_now = f_max(0.0f, (cam.dist + 0.1f) * expf(o.x) - 0.1f);
        if (f_abs(cam.dist - cam.dist_now) < 1e-3f && f_abs(cam.zoom_v) < 1e-3f) {
            cam.dist_now = cam.dist;
            cam.zoom_v = 0.0f;
        }
    }
    // Наклон и высота опоры — от штанги в кадре: колесо ведёт их вместе (Нески), а не каждое со своим временем
    cam_pitch_frame(cam);
    cam.rise = cam.exact ? CAM_PIVOT_M : cam_rise_of(cam.dist_now);
    // Взгляд снизу вверх — поле зрения чуть шире (Нески: небу нужно место)
    f32 up = cam.exact ? 0.0f : f_clamp(cam.pitch_now / CAM_PITCH_ORBIT, 0.0f, 1.0f);
    cam.fov_add = 8.0f * CAM_DEG * up * up;

    // WASD, край экрана и Shift + средняя кнопка двигают опору над землёй (и снимают слежение за человеком):
    // скорость — по длине штанги, с разгоном и остановкой. За человеком опору ставит view_camera_follow
    f32 reach = f_max(cam.dist_now, 5.0f);
    if (cam.follow && (ml > 0.0f || pan)) cam.follow = false;
    if (cam.follow) {
        cam.vel = Vec3{};
        cam.zoom_pan = cam.zoom_pan_v = Vec3{};
        cam.panning = false;
        return;
    }
    if (cam.followed) {
        // Слежение снято: опора остаётся где была, пол под ней ищем заново
        cam.feet = cam.pivot.y - cam.rise;
        cam.followed = false;
    }
    Vec3 step = cam_ramp(cam.vel, move * (reach * 0.9f * fast * pace), dt, tm);
    cam.vel.y = step.y = 0.0f;
    Vec3 left = cam_damp(cam.zoom_pan, cam.zoom_pan_v, CAM_ZOOM_S * ks, dt);
    step += cam.zoom_pan - left;
    cam.zoom_pan = left;
    if (dot(cam.zoom_pan, cam.zoom_pan) < 1e-6f && dot(cam.zoom_pan_v, cam.zoom_pan_v) < 1e-6f)
        cam.zoom_pan = cam.zoom_pan_v = Vec3{};
    if (pan) {
        // Тащим землю: точка, за которую взяли, идёт за мышью (курсор спрятан — её место на экране считаем сами).
        // По умолчанию берём землю в центре экрана: скорость не зависит от того, где стоял курсор (у горизонта
        // точка под курсором летела бы, у ног ползла). Настройка — взять под курсором
        if (!cam.panning) {
            bool at_cursor = cam.settings.grab_cursor;
            cam.pan_px = at_cursor ? in.mouse_x - in.mouse_dx : (f32)width * 0.5f;
            cam.pan_py = at_cursor ? in.mouse_y - in.mouse_dy : (f32)height * 0.5f;
            cam.panning = width > 0 && view_screen_to_support(cam, width, height, cam.pan_px, cam.pan_py, ground, coll,
                                                              cam.pan_at);
            cam.zoom_pan = cam.zoom_pan_v = Vec3{};
            if (!cam.panning) cam.pan_px = -1e9f;  // земли под курсором нет — сдвиг по длине штанги
        }
        Vec3 g, g0{};
        bool ok = cam.pan_px > -1e8f;
        if (ok && !cam.settings.grab_cursor) {
            // От центра: каждый кадр земля в центре экрана сдвигается на сдвиг мыши (на высоте взятой точки), место
            // не копится — сколько бы ни тащили, скорость та же
            cam.pan_px = (f32)width * 0.5f;
            cam.pan_py = (f32)height * 0.5f;
            // Центр выше горизонта (взгляд вверх) — сдвиг по длине штанги
            bool far = true;
            ok = cam_grab_step(cam, width, height, cam.pan_px, cam.pan_py, g0, &far) && !far;
        }
        cam.pan_px += in.mouse_dx;
        cam.pan_py += in.mouse_dy;
        if (ok && cam_grab_step(cam, width, height, cam.pan_px, cam.pan_py, g)) {
            g = g - g0;
            f32 gl = length(g), lim = reach * 2.0f;  // за кадр не дальше двух длин штанги (у горизонта)
            step += gl > lim ? g * (lim / gl) : g;
        } else {
            f32 k = reach * 0.0018f;
            step += flat_right * (-in.mouse_dx * k) + flat_fwd * (in.mouse_dy * k);
        }
        cam.panning = true;
    } else {
        cam.panning = false;
    }
    f32 y = cam.pivot.y;
    cam.pivot = cam_offset(cam.pivot, step);
    cam.pivot.y = y;
    cam_pivot_height(cam, dt, ground, coll);
    cam_solve(cam, dt, ground, coll, false);
}

void view_camera_follow(Camera &cam, const WPos &head, f32 dt, const Terrain &ground, const Colliders *coll) {
    if (!cam.placed) view_camera_place(cam, ground, coll);
    bool jump = !cam.followed || cam.free;
    if (jump) cam.blend = true;  // опора перескочила на человека: глаз доедет
    cam.free = false;
    cam.follow = true;
    cam.followed = true;
    // Опора — на rise над ногами (голова — 1.62 м у человека среднего роста, HEAD_ABOVE_M в people.cpp); догоняет
    // пружиной: шаг, поворот корня и ступени не качают камеру, а остановка не обрывается. Скачок больше 30 м — сразу
    WPos to = head;
    if (!cam.exact) to.y += cam.rise - 1.62f;
    Vec3 off = cam_delta(to, cam.pivot);
    f32 ks = cam.exact ? 0.0f : f_clamp(cam.settings.smooth, 0.0f, 3.0f);
    if (jump || dot(off, off) > 900.0f) {
        cam.pivot = to;
        cam.follow_vel = Vec3{};
    } else {
        cam.pivot = cam_offset(to, cam_damp(off, cam.follow_vel, CAM_FOLLOW_S * ks, dt));
    }
    cam_solve(cam, dt, ground, coll, true);
}

bool view_screen_ray(const Camera &cam, i32 width, i32 height, f32 px, f32 py, WPos &eye, Vec3 &dir) {
    if (width <= 0 || height <= 0) return false;
    CameraBasis b = view_camera_basis(cam);
    f32 s, c;
    sincos_turn(turn_from_rad(view_camera_fov(cam) * 0.5f), &s, &c);
    f32 t = s / c, aspect = (f32)width / (f32)height;
    f32 nx = (px / (f32)width) * 2.0f - 1.0f;
    f32 ny = 1.0f - (py / (f32)height) * 2.0f;
    dir = normalize(b.forward + b.right * (nx * t * aspect) + b.up * (ny * t));
    eye = cam.eye;
    return true;
}

// Точка внутри коробки постройки или под землёй
static bool cam_solid_at(const Terrain &ground, const Colliders *coll, const WPos &p, f32 *top) {
    if (coll) {
        for (u16 k : colliders_near(*coll, p.cx, p.cz, p.x, p.z)) {
            if (p.y < coll->y0[k] || p.y > coll->y1[k] || !coll_covers(*coll, k, p.cx, p.cz, p.x, p.z)) continue;
            *top = coll->y1[k];
            return true;
        }
    }
    if (p.y <= ground_at(ground, p).height && !cam_open_below(ground, coll, p)) {
        *top = -1e9f;
        return true;
    }
    return false;
}

bool view_screen_to_support(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground,
                            const Colliders *coll, WPos &out) {
    WPos eye;
    Vec3 dir;
    if (!view_screen_ray(cam, width, height, px, py, eye, dir)) return false;
    f32 k0 = 0.0f, k = 0.1f, top = 0.0f;
    for (u32 i = 0; i < 4000 && k < 30000.0f; ++i) {
        if (cam_solid_at(ground, k < 2000.0f ? coll : nullptr, cam_offset(eye, dir * k), &top)) {
            for (u32 j = 0; j < 20; ++j) {
                f32 m = (k0 + k) * 0.5f;
                if (cam_solid_at(ground, coll, cam_offset(eye, dir * m), &top)) k = m;
                else k0 = m;
            }
            cam_solid_at(ground, coll, cam_offset(eye, dir * k), &top);
            out = cam_offset(eye, dir * k);
            // В коробку попал сверху (почти у верха) — точка на её верху; в бок — как есть, путь найдёт ближний пол
            if (top > -1e8f && top - out.y < 0.15f) out.y = top;
            else if (top <= -1e8f) out.y = ground_at(ground, out).height;
            return true;
        }
        k0 = k;
        k += k < 60.0f ? 0.1f : f_max(0.5f, k * 0.02f);
    }
    return false;
}

bool view_screen_to_ground(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground, WPos &out) {
    WPos eye;
    Vec3 dir;
    if (!view_screen_ray(cam, width, height, px, py, eye, dir)) return false;
    // Шаги растут с расстоянием, на пересечении — деление пополам
    f32 k0 = 0.0f, k = 0.5f;
    for (u32 i = 0; i < 400 && k < 30000.0f; ++i) {
        WPos p = cam_offset(eye, dir * k);
        if (p.y <= ground_at(ground, p).height) {
            for (u32 j = 0; j < 24; ++j) {
                f32 m = (k0 + k) * 0.5f;
                WPos q = cam_offset(eye, dir * m);
                if (q.y <= ground_at(ground, q).height) k = m;
                else k0 = m;
            }
            out = cam_offset(eye, dir * k);
            out.y = ground_at(ground, out).height;
            return true;
        }
        k0 = k;
        k += f_max(0.5f, k * 0.02f);
    }
    return false;
}
