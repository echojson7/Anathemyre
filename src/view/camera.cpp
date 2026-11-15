#include "camera.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include <math.h>

// Пределы
static constexpr f32 CAM_PITCH_MIN = -1.40f, CAM_PITCH_MAX = 1.50f;  // вверх до 80°, вниз почти отвесно (86°)
static constexpr f32 CAM_DIST_MAX = 12000.0f;
static constexpr f32 CAM_DIST_SNAP = 0.5f;      // ближе — сразу вид от глаз
static constexpr f32 CAM_ZOOM_STEP = 0.85f;     // щелчок колеса
static constexpr f32 CAM_RAD_PER_PX = 0.005f;
// Высоты
static constexpr f32 CAM_PIVOT_M = 1.7f;        // опора орбиты — на высоте глаз человека
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

// Доля пути к цели за dt при времени сглаживания tau (не зависит от частоты кадров)
static f32 cam_alpha(f32 dt, f32 tau) { return tau <= 1e-4f ? 1.0f : 1.0f - expf(-dt / tau); }

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

f32 view_camera_fov(const Camera &c) { return f_clamp(c.settings.fov_deg, 20.0f, 120.0f) * (PI_F / 180.0f); }

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
    f32 target = cam.feet + CAM_PIVOT_M;
    if (!under && !cam.exact) {
        f32 r = f_clamp(cam.dist_now * 0.12f, 1.0f, 40.0f);
        target = f_max(cam_ground_mean(ground, cam.pivot, r) + CAM_PIVOT_M, g + CAM_PIVOT_MIN_M);
    }
    cam.pivot.y += (target - cam.pivot.y) * cam_alpha(dt, cam.exact ? 0.0f : CAM_HEIGHT_S);
    if (!under) cam.pivot.y = f_max(cam.pivot.y, g + CAM_PIVOT_MIN_M);
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
        f32 want = cam.dist_now;
        bool under = coll && cam.pivot.y < ground_at(ground, cam.pivot).height + CAM_EYE_M;  // опора под землёй
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
    cam.pitch = f_clamp(cam.pitch, CAM_PITCH_MIN, CAM_PITCH_MAX);
    cam.dist = f_clamp(cam.dist, 0.0f, CAM_DIST_MAX);
    cam.yaw_now = cam.yaw;
    cam.pitch_now = cam.pitch;
    cam.dist_now = cam.dist;
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
        cam.pivot.y = g + CAM_PIVOT_M;
        cam_pivot_height(cam, now, ground, coll);
    }
    cam_solve(cam, now, ground, coll, cam.follow);
    cam.placed = true;
}

void view_camera_control(Camera &cam, const Input &in, f32 dt, const Terrain &ground, const Colliders *coll) {
    if (!cam.placed) view_camera_place(cam, ground, coll);
    const f32 ks = cam.exact ? 0.0f : f_clamp(cam.settings.smooth, 0.0f, 3.0f);  // плавность ввода

    if (in.key_pressed[KEY_F]) {
        if (!cam.free) {
            // В полёт: опора — глаз, как он стоит в кадре
            cam.pivot = cam.eye;
            cam.ease = Vec3{};
            cam.vel = Vec3{};
            cam.dolly = 0.0f;
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
                cam.pivot = eye;
                cam.feet = eye.y - CAM_PIVOT_M;
            } else {
                if (ok && len <= CAM_BACK_MAX_M) {
                    cam.dist = len;
                    cam.pivot = hit;
                } else {
                    cam.dist = f_clamp(cam.dist, 5.0f, 60.0f);
                    cam.pivot = cam_offset(eye, normalize(Vec3{b.forward.x, 0.0f, b.forward.z}) * cam.dist);
                }
                cam.feet = ground_at(ground, cam.pivot).height;
                cam.pivot.y = cam.feet + CAM_PIVOT_M;
            }
            cam.dist_now = cam.dist;
            cam.arm = -1.0f;
            cam.lift = 0.0f;
            cam.vel = Vec3{};
            cam.blend = true;
            cam.free = false;
        }
    }

    // Взгляд: средняя кнопка или Alt + правая (правая — приказ), одинаково на орбите и в полёте: мышь вправо —
    // взгляд вправо, мышь вверх — взгляд вверх. Shift + средняя — тащить опору
    bool rotate = (in.mouse_down[MOUSE_MIDDLE] && !in.key_down[KEY_SHIFT]) || (in.mouse_down[MOUSE_RIGHT] && in.key_down[KEY_ALT]);
    bool pan = !cam.free && in.mouse_down[MOUSE_MIDDLE] && in.key_down[KEY_SHIFT];
    cam.grab = rotate || pan;
    if (rotate) {
        f32 k = CAM_RAD_PER_PX * f_clamp(cam.settings.mouse_sens, 0.05f, 20.0f);
        cam.yaw = cam_wrap(cam.yaw - (cam.settings.invert_x ? -1.0f : 1.0f) * in.mouse_dx * k);
        cam.pitch += (cam.settings.invert_y ? -1.0f : 1.0f) * in.mouse_dy * k;
    }
    cam.pitch = f_clamp(cam.pitch, CAM_PITCH_MIN, CAM_PITCH_MAX);
    f32 ar = cam_alpha(dt, CAM_ROT_S * ks);
    cam.yaw_now = cam_wrap(cam.yaw_now + cam_wrap(cam.yaw - cam.yaw_now) * ar);
    cam.pitch_now += (cam.pitch - cam.pitch_now) * ar;

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
    }
    f32 ml = length(move);
    if (ml > 1.0f) move = move * (1.0f / ml);  // по диагонали не быстрее
    f32 am = cam_alpha(dt, CAM_MOVE_S * ks);

    if (cam.free) {
        // Высоко над землёй летать медленно скучно: скорость растёт с высотой. Колесо — проехать вперёд, плавно
        f32 above = cam.pivot.y - ground_at(ground, cam.pivot).height;
        f32 scale = fast * f_max(1.0f, above / 20.0f);
        cam.vel += (move * (12.0f * scale) - cam.vel) * am;
        cam.dolly += in.wheel * 2.0f * scale;
        f32 go = cam.dolly * cam_alpha(dt, CAM_ZOOM_S * ks);
        cam.dolly -= go;
        cam.pivot = cam_offset(cam.pivot, cam.vel * dt + b.forward * go);
        // Под землю — только в проём и в подземную постройку (стены полёт проходит насквозь)
        f32 floor_y = ground_at(ground, cam.pivot).height + CAM_EYE_M;
        if (cam.pivot.y < floor_y && !cam_open_below(ground, coll, cam.pivot)) {
            cam.pivot.y = floor_y;
            if (cam.vel.y < 0.0f) cam.vel.y = 0.0f;
        }
        cam.followed = false;
        cam_solve(cam, dt, ground, coll, false);
        return;
    }

    // Колесо — длина штанги (до вида от глаз). Если штангу держит стена, счёт идёт от длины в кадре: то, что
    // накрутили «в стену», не копится и потом не отбрасывает камеру на максимум
    if (in.wheel > 0.5f || in.wheel < -0.5f) {
        f32 d = cam.dist;
        if (cam.arm >= 0.0f && cam.arm < cam.arm_want - 0.05f) d = f_min(d, cam.arm);
        for (f32 w = in.wheel; w > 0.5f; w -= 1.0f) d = d * CAM_ZOOM_STEP < CAM_DIST_SNAP ? 0.0f : d * CAM_ZOOM_STEP;
        for (f32 w = in.wheel; w < -0.5f; w += 1.0f) d = d < CAM_DIST_SNAP ? CAM_DIST_SNAP : d / CAM_ZOOM_STEP;
        cam.dist = f_clamp(d, 0.0f, CAM_DIST_MAX);
    }
    cam.dist_now += (cam.dist - cam.dist_now) * cam_alpha(dt, CAM_ZOOM_S * ks);
    if (f_abs(cam.dist - cam.dist_now) < 1e-3f) cam.dist_now = cam.dist;

    // WASD и Shift + средняя кнопка двигают опору над землёй (и снимают слежение за человеком): скорость — по длине
    // штанги, с разгоном и остановкой. За человеком опору ставит view_camera_follow
    f32 reach = f_max(cam.dist_now, 5.0f);
    if (cam.follow && (ml > 0.0f || pan)) cam.follow = false;
    if (cam.follow) {
        cam.vel = Vec3{};
        return;
    }
    if (cam.followed) {
        // Слежение снято: опора остаётся где была, пол под ней ищем заново
        cam.feet = cam.pivot.y - CAM_PIVOT_M;
        cam.followed = false;
    }
    cam.vel += (move * (reach * 0.9f * fast) - cam.vel) * am;
    cam.vel.y = 0.0f;
    Vec3 step = cam.vel * dt;
    if (pan) {
        f32 k = reach * 0.0018f;
        step += flat_right * (-in.mouse_dx * k) + flat_fwd * (in.mouse_dy * k);
    }
    f32 y = cam.pivot.y;
    cam.pivot = cam_offset(cam.pivot, step);
    cam.pivot.y = y;
    cam_pivot_height(cam, dt, ground, coll);
    cam_solve(cam, dt, ground, coll, false);
}

void view_camera_follow(Camera &cam, const WPos &head, f32 dt, const Terrain &ground, const Colliders *coll) {
    if (!cam.placed) view_camera_place(cam, ground, coll);
    if (!cam.followed || cam.free) cam.blend = true;  // опора перескочила на голову: глаз доедет
    cam.free = false;
    cam.follow = true;
    cam.followed = true;
    cam.pivot = head;
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
