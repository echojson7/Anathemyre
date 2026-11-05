// Окно: цикл кадра с фиксированным шагом мира 60 Гц и интерполяцией между двумя выписками.
// Поток кадра (docs/architecture.md): ввод → намерения → 0..N шагов → выписка → вид → рендер
#include "app.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/record.h"
#include "../sim/step.h"
#include "../view/view.h"
#include <inttypes.h>
#include <stdio.h>

static constexpr u32 MAX_STEPS_PER_FRAME = 8;  // больше — мир отстаёт, а не кадр замирает
static const f32 SPEEDS[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
static constexpr u32 SPEED_NORMAL = 3;

// Время: пауза и замедление меняют число шагов за кадр, а не длину шага
struct TimeCtl {
    bool paused = false;
    u32 speed = SPEED_NORMAL;
    f64 acc = 0;  // накопленное время мира, с
};

int run_window(const Args &a, AppMemory &m) {
    if (!platform_window_open("Anathemyre", 1600, 900)) return 1;
    RenderConfig rc;
    rc.vsync = a.vsync;
    rc.validation = a.validation;
    char shader_dir[1024];
    snprintf(shader_dir, sizeof shader_dir, "%sshaders/", platform_exe_dir());
    rc.shader_dir = shader_dir;
    if (!render_init(rc)) {
        platform_window_close();
        return 1;
    }

    World w;
    world_init(w, m.world, a.seed);
    ExtractRing ring;
    ring_init(ring, m.world);
    sim_extract(w, ring_next(ring));
    ring_commit(ring);
    DebugDraw dbg;
    dbg_init(dbg, m.world, 4096);
    Recorder rec;
    if (a.record) recorder_init(rec, m.record, w, a.seed, 600000, 1u << 16);

    FixedArray<Intent> pending;  // намерения игрока до ближайшего шага
    pending.init(m.world, 64);
    Camera cam;
    TimeCtl tc;
    DebugState ds;
    Input in{};
    TimeStat st_frame, st_step;
    u64 frames = 0, frames_window = 0;
    u64 last = platform_time_ns(), window_t0 = last;
    int rc_exit = 0;
    debug_print_help();

    for (;;) {
        u64 now = platform_time_ns();
        f64 dt = (f64)(now - last) / 1e9;
        last = now;
        if (dt > 0.25) dt = 0.25;
        stat_add(st_frame, dt * 1000.0);

        platform_poll(in);
        if (in.quit || in.key_pressed[KEY_ESCAPE]) break;
        if (in.resized) render_resized();
        if (in.key_pressed[KEY_F11]) platform_toggle_fullscreen();
        if (in.key_pressed[KEY_SPACE]) tc.paused = !tc.paused;
        if (in.key_pressed[KEY_MINUS] && tc.speed > 0) tc.speed--;
        if (in.key_pressed[KEY_EQUALS] && tc.speed + 1 < ANA_COUNTOF(SPEEDS)) tc.speed++;
        debug_keys(in, ds);
        dbg.enabled = ds.lines;
        if (in.wheel != 0) view_zoom(cam, in.wheel);

        i32 ww = 0, wh = 0;
        platform_window_size(&ww, &wh);
        // Мышь → намерения игрока
        if (ww > 0 && wh > 0) {
            WPos at = view_screen_to_world(cam, ww, wh, in.mouse_x, in.mouse_y);
            if (in.mouse_pressed[MOUSE_LEFT]) pending.push_back({0, INTENT_ATTRACT, at});
            if (in.mouse_pressed[MOUSE_RIGHT]) pending.push_back({0, INTENT_BURST, at});
            if (in.mouse_pressed[MOUSE_MIDDLE]) pending.push_back({0, INTENT_RELEASE, at});
        }

        u32 nsteps = 0;
        if (tc.paused) {
            if (ds.step_once) nsteps = 1;
            tc.acc = 0;
        } else {
            tc.acc += dt * SPEEDS[tc.speed];
            while (tc.acc >= SIM_DT && nsteps < MAX_STEPS_PER_FRAME) {
                tc.acc -= SIM_DT;
                nsteps++;
            }
            if (nsteps == MAX_STEPS_PER_FRAME) tc.acc = 0;
        }
        ds.step_once = false;

        for (u32 k = 0; k < nsteps; ++k) {
            for (usize i = 0; i < pending.count; ++i) pending.data[i].step = w.step;
            Span<const Intent> its = k == 0 ? pending.span() : Span<const Intent>{};
            StepCtx ctx{its, &m.step, &dbg};
            u64 ts = platform_time_ns();
            sim_step(w, ctx);
            stat_add(st_step, (f64)(platform_time_ns() - ts) / 1e6);
            if (a.record) recorder_step(rec, w, its);
            sim_extract(w, ring_next(ring));
            ring_commit(ring);
            if (k == 0) pending.clear();
#ifdef ANA_DEBUG
            u64 h = world_hash(w);  // в отладке хеш каждый шаг, в журнал — раз в секунду
#endif
            if (w.step % SIM_HZ == 0) {
#ifndef ANA_DEBUG
                u64 h = world_hash(w);
#endif
                char hex[17];
                log_info("шаг %" PRIu64 " хеш %s", w.step, hex_u64(h, hex));
            }
        }

        arena_reset(m.frame);
        ViewInput vi{&ring_prev(ring), &ring_cur(ring), tc.paused ? 1.0f : (f32)(tc.acc / SIM_DT), &dbg, ww, wh};
        if (!ds.lines) vi.dbg = nullptr;
        RenderFrame rf;
        view_build(cam, vi, m.frame, rf);
        if (!render_frame(rf)) { rc_exit = 1; break; }

        frames++;
        frames_window++;
        if (now - window_t0 >= 1000000000ull) {
            stat_roll(st_frame);
            stat_roll(st_step);
            f64 secs = (f64)(now - window_t0) / 1e9;
            char title[256];
            snprintf(title, sizeof title,
                     "Anathemyre — %.0f к/с · кадр %.2f мс (макс %.2f) · шаг %.3f мс · ×%g%s · шаг %" PRIu64,
                     (f64)frames_window / secs, st_frame.avg_last, st_frame.max_last, st_step.avg_last,
                     (f64)SPEEDS[tc.speed], tc.paused ? " · пауза" : "", w.step);
            platform_set_title(title);
            log_info("%s", title);
            frames_window = 0;
            window_t0 = now;
        }
        if (a.frames && frames >= a.frames) break;
    }

    if (a.record && !save_record(rec, a.record, m.load)) rc_exit = 1;
    u32 verr = render_validation_errors();
    if (verr) {
        log_error("слой проверки Vulkan: %u ошибок", verr);
        rc_exit = 1;
    }
    render_shutdown();
    platform_window_close();
    return rc_exit;
}
