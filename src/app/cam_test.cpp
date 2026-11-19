// Проверка камеры без окна (--cam-test файл.txt, docs/camera.md, «Проверка»): записанный ввод (колесо, мышь, Q/E,
// WASD, захват земли, взгляд снизу) на земле мира при 30, 60 и 150 к/с. Числа: насколько кадр зависит от частоты
// кадров, рывки (ускорение глаза растёт с частотой кадров — скорость скачет), уход точки из-под курсора, запас над
// землёй. Мир не трогает: камера — часть вида
#include "app.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include "../sim/ground.h"
#include "../sim/world.h"
#include "../view/camera.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr i32 CT_W = 1600, CT_H = 900;

enum CtKind { CT_WHEEL, CT_MOUSE, CT_KEYS, CT_WALK, CT_WALK_NEAR, CT_GRAB, CT_LOOKUP, CT_COUNT };
const char *const CT_NAMES[CT_COUNT] = {"колесо", "мышь", "Q/E", "WASD", "WASD у земли", "тащить землю", "снизу вверх"};
constexpr f32 CT_SECONDS[CT_COUNT] = {6.0f, 4.5f, 3.0f, 5.0f, 4.0f, 2.0f, 3.0f};

// Ввод сценария в момент t (секунды) за кадр dt: мышь, колесо, клавиши
void ct_input(CtKind k, f32 t, f32 dt, f32 t_prev, Input &in) {
    memset(&in, 0, sizeof in);
    in.mouse_x = CT_W * 0.5f;
    in.mouse_y = CT_H * 0.5f;
    in.mouse_in = true;
    auto mouse = [&](f32 vx, f32 vy) {
        in.mouse_dx = vx * dt;
        in.mouse_dy = vy * dt;
    };
    switch (k) {
    case CT_WHEEL: {
        // 12 щелчков вперёд (каждые 0.15 с с 0.2 с), курсор правее и ниже центра; 20 назад с 3 с (каждые 0.1 с)
        in.mouse_x = CT_W * 0.7f;
        in.mouse_y = CT_H * 0.62f;
        auto notches = [](f32 a, f32 b, f32 t0, f32 every, i32 n) {
            i32 na = a < t0 ? 0 : (i32)((a - t0) / every) + 1, nb = b < t0 ? 0 : (i32)((b - t0) / every) + 1;
            if (na > n) na = n;
            if (nb > n) nb = n;
            return nb - na;
        };
        in.wheel = (f32)notches(t_prev, t, 0.2f, 4.0f / 30.0f, 12) - (f32)notches(t_prev, t, 3.0f, 0.1f, 20);
    } break;
    case CT_MOUSE:
        in.mouse_down[MOUSE_MIDDLE] = t < 3.5f;
        if (t < 1.5f) mouse(600.0f, 0.0f);
        else if (t < 2.5f) mouse(0.0f, -400.0f);
        else if (t < 3.5f) mouse(-300.0f, 800.0f);
        break;
    case CT_KEYS:
        in.key_down[KEY_Q] = t < 1.0f;
        in.key_down[KEY_E] = t >= 1.6f && t < 2.1f;
        break;
    case CT_WALK:
    case CT_WALK_NEAR:
        in.key_down[KEY_W] = t < 3.0f;
        in.key_down[KEY_D] = t >= 1.0f && t < 2.0f;
        in.key_down[KEY_SHIFT] = k == CT_WALK && t >= 1.5f && t < 2.5f;
        break;
    case CT_GRAB:
        in.mouse_x = CT_W * 0.3f;
        in.mouse_y = CT_H * 0.6f;
        in.key_down[KEY_SHIFT] = true;
        in.mouse_down[MOUSE_MIDDLE] = t < 1.2f;
        if (t > 0.1f && t < 1.1f) mouse(500.0f, -150.0f);
        break;
    case CT_LOOKUP:
        in.mouse_down[MOUSE_MIDDLE] = t < 2.0f;
        if (t < 1.5f) mouse(100.0f, -500.0f);
        break;
    default: break;
    }
}

}  // namespace

// Показ для видео (--cam-play, docs/camera.md): колесо к курсору, поворот мышью с взглядом вверх, Q, отдаление,
// захват земли, WASD — 15.5 с. t — середина кадра, t_prev и t_next — его края (щелчки колеса по краям)
void cam_show_input(f32 t, f32 t_prev, f32 t_next, f32 dt, i32 w, i32 h, Input &in) {
    memset(&in, 0, sizeof in);
    in.mouse_x = (f32)w * 0.5f;
    in.mouse_y = (f32)h * 0.5f;
    auto notches = [&](f32 t0, f32 every, i32 n) {
        auto c = [&](f32 x) {
            i32 k = x < t0 ? 0 : (i32)((x - t0) / every) + 1;
            return k > n ? n : k;
        };
        return c(t_next) - c(t_prev);
    };
    auto mouse = [&](f32 vx, f32 vy) {
        in.mouse_dx = vx * dt;
        in.mouse_dy = vy * dt;
    };
    if (t < 2.8f) {
        in.mouse_x = (f32)w * 0.46f;
        in.mouse_y = (f32)h * 0.53f;
        in.wheel = (f32)notches(0.5f, 4.0f / 30.0f, 12);
    } else if (t < 6.0f) {
        in.mouse_down[MOUSE_MIDDLE] = true;
        if (t < 5.0f) mouse(200.0f, t < 4.0f ? -130.0f : 0.0f);
        else if (t < 5.8f) mouse(0.0f, 150.0f);
    } else if (t < 7.5f) {
        in.key_down[KEY_Q] = t < 7.0f;
    } else if (t < 10.5f) {
        in.wheel = -(f32)notches(7.5f, 0.1f, 24);
    } else if (t < 12.5f) {
        in.key_down[KEY_SHIFT] = true;
        in.mouse_down[MOUSE_MIDDLE] = t < 12.1f;
        if (t > 10.6f && t < 12.0f) mouse(-300.0f, 200.0f);
    } else {
        in.key_down[KEY_W] = t < 14.5f;
        in.key_down[KEY_D] = t >= 13.5f && t < 14.5f;
    }
}

namespace {

// Точка мира → пиксель окна (false — за спиной)
bool ct_project(const Camera &cam, const WPos &p, f32 &px, f32 &py) {
    CameraBasis b = view_camera_basis(cam);
    WPos e = view_eye(cam);
    Vec2 dxz = wpos_delta_xz(e, p);
    Vec3 v{dxz.x, p.y - e.y, dxz.y};
    f32 z = dot(v, b.forward);
    if (z <= 1e-3f) return false;
    f32 t = tanf(view_camera_fov(cam) * 0.5f), aspect = (f32)CT_W / (f32)CT_H;
    px = (dot(v, b.right) / (z * t * aspect) + 1.0f) * 0.5f * CT_W;
    py = (1.0f - dot(v, b.up) / (z * t)) * 0.5f * CT_H;
    return true;
}

struct CtRun {
    Vec3 eye[200];      // глаз каждые 0.1 с, м от глаза в начале
    f32 yaw[200], pitch[200], dist[200];
    u32 samples = 0;
    f32 dv_max = 0.0f;    // наибольший скачок скорости глаза за кадр, м/с (у гладкого движения падает с ростом к/с)
    f32 dv_t = 0.0f;      // когда
    f32 clear_min = 1e9f; // наименьший запас глаза над землёй, м
    f32 cursor_err = 0.0f;  // захват земли: наибольший уход взятой точки от курсора, пиксели
};

void ct_run(CtKind k, f32 dt, const World &w, Camera start, CtRun &r) {
    Camera cam = start;
    view_camera_place(cam, w.terrain, &w.coll);
    const WPos origin = view_eye(cam);
    const u32 frames = (u32)(CT_SECONDS[k] / dt + 0.5f), per = (u32)(0.1f / dt + 0.5f);
    WPos prev[2];
    bool grabbed = false;
    WPos grab_at;
    f32 gx = 0, gy = 0;
    for (u32 f = 0; f < frames; ++f) {
        f32 t = (f32)(f + 1) * dt;
        Input in;
        // Клавиши и мышь — по середине кадра: их смены на 1/30 с ложатся на края кадров всех трёх частот
        ct_input(k, k == CT_WHEEL ? t : t - dt * 0.5f, dt, (f32)f * dt, in);
        if (k == CT_WHEEL && !grabbed) {
            // Колесо к курсору: точка под курсором в начале должна остаться под ним, пока приближаем
            grabbed = view_screen_to_support(cam, CT_W, CT_H, in.mouse_x, in.mouse_y, w.terrain, &w.coll, grab_at);
            gx = in.mouse_x;
            gy = in.mouse_y;
        }
        if (k == CT_GRAB && in.mouse_down[MOUSE_MIDDLE] && !grabbed) {
            grabbed = view_screen_to_support(cam, CT_W, CT_H, in.mouse_x, in.mouse_y, w.terrain, &w.coll, grab_at);
            gx = in.mouse_x;
            gy = in.mouse_y;
        }
        view_camera_control(cam, in, dt, CT_W, CT_H, true, w.terrain, &w.coll);
        WPos e = view_eye(cam);
        if ((k == CT_GRAB && grabbed && in.mouse_down[MOUSE_MIDDLE]) || (k == CT_WHEEL && grabbed && t < 3.0f)) {
            gx += in.mouse_dx;
            gy += in.mouse_dy;
            f32 px, py;
            if (ct_project(cam, grab_at, px, py)) {
                f32 err = sqrtf((px - gx) * (px - gx) + (py - gy) * (py - gy));
                if (err > r.cursor_err) r.cursor_err = err;
            }
        }
        f32 clr = e.y - ground_at(w.terrain, e).height;
        if (clr < r.clear_min) r.clear_min = clr;
        if (f >= 2) {
            Vec2 a = wpos_delta_xz(prev[1], prev[0]), b = wpos_delta_xz(prev[0], e);
            Vec3 dv{(b.x - a.x) / dt, ((e.y - prev[0].y) - (prev[0].y - prev[1].y)) / dt, (b.y - a.y) / dt};
            f32 l = length(dv);
            if (l > r.dv_max) {
                r.dv_max = l;
                r.dv_t = t;
            }
        }
        prev[1] = prev[0];
        prev[0] = e;
        if ((f + 1) % per == 0 && r.samples < 200) {
            Vec2 d = wpos_delta_xz(origin, e);
            r.eye[r.samples] = Vec3{d.x, e.y - origin.y, d.y};
            r.yaw[r.samples] = cam.yaw_now;
            r.pitch[r.samples] = cam.pitch_now;
            r.dist[r.samples] = cam.dist_now;
            r.samples++;
        }
    }
}

char ct_text[16384];
usize ct_len = 0;
__attribute__((format(printf, 1, 2))) void out(const char *fmt, ...) {
    va_list v;
    va_start(v, fmt);
    int n = vsnprintf(ct_text + ct_len, sizeof ct_text - ct_len, fmt, v);
    va_end(v);
    if (n > 0) ct_len += (usize)n < sizeof ct_text - ct_len ? (usize)n : sizeof ct_text - ct_len - 1;
}

}  // namespace

int run_cam_test(const Args &a, AppMemory &m) {
    World w;
    if (!app_world_create(w, m, a.seed, nullptr)) return 1;
    Camera start;
    // Колесо и захват земли — к курсору (настройка): уход точки из-под курсора считается для них
    start.settings.zoom_cursor = true;
    start.settings.grab_cursor = true;
    const f32 deg = PI_F / 180.0f;
    if (w.start.set) {
        start.yaw = w.start.yaw * deg;
        start.dist = w.start.dist;
        start.pitch = view_camera_auto_pitch(start.dist);  // как окно с наклоном от расстояния
        start.pivot.x = w.start.x;
        start.pivot.z = w.start.z;
        wpos_rebase(start.pivot);
    }
    out("# сценарий: разница 30/150 к/с по ходу и в конце (глаз, м; взгляд, °); скачок скорости глаза за кадр при 60 и "
        "150 к/с (м/с: у гладкого движения падает с ростом к/с, не падает — рывок) и когда; запас над землёй (м); уход "
        "точки из-под курсора (px)\n");
    static const f32 DTS[3] = {1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 150.0f};
    static CtRun runs[3];
    for (u32 k = 0; k < CT_COUNT; ++k) {
        Camera s = start;
        if (k == CT_WALK_NEAR || k == CT_LOOKUP) s.dist = 4.0f;
        for (u32 i = 0; i < 3; ++i) {
            runs[i] = CtRun{};
            ct_run((CtKind)k, DTS[i], w, s, runs[i]);
        }
        f32 de = 0.0f, da = 0.0f, fe = 0.0f, fa = 0.0f;
        u32 n = runs[0].samples < runs[2].samples ? runs[0].samples : runs[2].samples;
        for (u32 j = 0; j < n; ++j) {
            f32 e = length(runs[0].eye[j] - runs[2].eye[j]);
            f32 dy = runs[0].yaw[j] - runs[2].yaw[j];
            while (dy > PI_F) dy -= 2.0f * PI_F;
            while (dy < -PI_F) dy += 2.0f * PI_F;
            f32 ang = fabsf(dy) + fabsf(runs[0].pitch[j] - runs[2].pitch[j]);
            if (e > de) de = e;
            if (ang > da) da = ang;
            fe = e;
            fa = ang;
        }
        f32 clr = runs[0].clear_min;
        for (const CtRun &r : runs) clr = r.clear_min < clr ? r.clear_min : clr;
        out("%s\n  к/с: глаз %.3f м (конец %.3f), взгляд %.2f° (конец %.2f°); скачок %.2f / %.2f м/с (%.2f с); запас %.2f м; "
            "курсор %.1f px; конец: штанга %.1f м, наклон %.1f°\n",
            CT_NAMES[k], (double)de, (double)fe, (double)(da / deg), (double)(fa / deg), (double)runs[1].dv_max,
            (double)runs[2].dv_max, (double)runs[2].dv_t, (double)clr,
            (double)runs[2].cursor_err, (double)runs[2].dist[runs[2].samples - 1],
            (double)(runs[2].pitch[runs[2].samples - 1] / deg));
    }
    if (!platform_write_file(a.cam_test, ct_text, ct_len)) {
        log_error("не записать %s", a.cam_test);
        return 1;
    }
    log_info("проверка камеры: %s\n%s", a.cam_test, ct_text);
    return 0;
}
