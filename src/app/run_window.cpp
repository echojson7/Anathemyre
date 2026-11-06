// Окно: цикл кадра с фиксированным шагом мира 60 Гц и интерполяцией между двумя выписками.
// Поток кадра (docs/architecture.md): ввод → намерения → 0..N шагов → выписка → вид → рендер
#include "app.h"
#include "../asset/material.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/ground.h"
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
    if (!platform_window_open("Anathemyre", (i32)a.win_w, (i32)a.win_h)) return 1;
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
    u64 desc = 0;
    MaterialTable mats;
    ViewAssets *assets = arena_push<ViewAssets>(m.world, 1);
    ScatterCache *scatter = arena_push<ScatterCache>(m.world, 1);
    scatter_init(*scatter, m.world);
    if (!app_world_create(w, m, a.seed, &desc) || !app_assets_load(m, mats, *assets) || !app_terrain_upload(w)) {
        render_shutdown();
        platform_window_close();
        return 1;
    }
    ExtractRing ring;
    ring_init(ring, m.world);
    sim_extract(w, ring_next(ring));
    ring_commit(ring);
    DebugDraw dbg;
    dbg_init(dbg, m.world, 4096);
    Recorder rec;
    if (a.record) {
        recorder_init(rec, m.record, w, a.seed, 600000, 1u << 16);
        rec.desc_hash = desc;
    }

    FixedArray<Intent> pending;  // намерения игрока до ближайшего шага
    pending.init(m.world, 64);
    Camera cam;
    if (a.cam_set) {
        const f32 deg = PI_F / 180.0f;
        cam.yaw = a.cam[0] * deg;
        cam.pitch = a.cam[1] * deg;
        cam.dist = a.cam[2];
        cam.target.x = a.cam[3];
        cam.target.z = a.cam[4];
        wpos_rebase(cam.target);
        cam.target.y = ground_at(w.terrain, cam.target).height;
    } else {
        cam.target.z = 10.0f;  // сцена этапа 1 — к северу от начала карты
    }
    TimeCtl tc;
    DebugState ds;
    bool pixel = !a.nopixel;
    Input in{};
    TimeStat st_frame, st_step, st_cpu;
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
        if (in.key_pressed[KEY_P]) pixel = !pixel;
        debug_keys(in, ds);
        dbg.enabled = ds.lines;
        view_camera_control(cam, in, (f32)dt, w.terrain);

        i32 ww = 0, wh = 0;
        platform_window_size(&ww, &wh);
        // Мышь → намерения игрока: левая — притянуть точки, с Ctrl — вспышка; R — отпустить
        WPos at;
        if (in.mouse_pressed[MOUSE_LEFT] && view_screen_to_ground(cam, ww, wh, in.mouse_x, in.mouse_y, w.terrain, at))
            pending.push_back({0, in.key_down[KEY_CTRL] ? (u32)INTENT_BURST : (u32)INTENT_ATTRACT, at});
        if (in.key_pressed[KEY_R]) pending.push_back({0, INTENT_RELEASE, WPos{}});

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

        // Замер CPU кадра: вид + запись команд, без шага мира и без ожидания GPU и показа (vsync)
        u64 tcpu = platform_time_ns();
        arena_reset(m.frame);
        ViewInput vi{&ring_prev(ring), &ring_cur(ring), tc.paused ? 1.0f : (f32)(tc.acc / SIM_DT), &dbg, ww, wh, pixel, scatter};
        if (!ds.lines) vi.dbg = nullptr;
        RenderFrame rf;
        view_build(cam, *assets, vi, m.frame, rf);
        bool last_frame = a.frames && frames + 1 >= a.frames;
        if (last_frame && a.shot) render_capture_next();
        if (!render_frame(rf)) { rc_exit = 1; break; }
        stat_add(st_cpu, (f64)(platform_time_ns() - tcpu) / 1e6 - render_wait_ms());

        frames++;
        frames_window++;
        if (now - window_t0 >= 1000000000ull) {
            stat_roll(st_frame);
            stat_roll(st_step);
            stat_roll(st_cpu);
            f64 secs = (f64)(now - window_t0) / 1e9;
            char title[320];
            snprintf(title, sizeof title,
                     "Anathemyre — %.0f к/с · кадр %.2f мс (макс %.2f) · CPU кадра %.2f мс · GPU %.2f мс · шаг %.3f мс · "
                     "×%g%s%s · шаг %" PRIu64,
                     (f64)frames_window / secs, st_frame.avg_last, st_frame.max_last, st_cpu.avg_last, render_gpu_ms(),
                     st_step.avg_last, (f64)SPEEDS[tc.speed], tc.paused ? " · пауза" : "", pixel ? "" : " · без ячеек",
                     w.step);
            platform_set_title(title);
            log_info("%s", title);
            frames_window = 0;
            window_t0 = now;
        }
        if (last_frame) break;
    }

    if (a.shot && !debug_write_shot(a.shot, m.load)) rc_exit = 1;
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
