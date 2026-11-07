// Окно: цикл кадра с фиксированным шагом мира 60 Гц и интерполяцией между двумя выписками.
// Поток кадра (docs/architecture.md): ввод → намерения → 0..N шагов → выписка → вид → рендер
#include "app.h"
#include "hud.h"
#include "settings.h"
#include "../asset/material.h"
#include "../core/log.h"
#include "../core/prof.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/ground.h"
#include "../sim/record.h"
#include "../sim/step.h"
#include "../ui/ui.h"
#include "../view/view.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static constexpr u32 MAX_STEPS_PER_FRAME = 8;  // больше — мир отстаёт, а не кадр замирает
static const f32 SPEEDS[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
static constexpr u32 SPEED_NORMAL = 3;

// Время: пауза и замедление меняют число шагов за кадр, а не длину шага
struct TimeCtl {
    bool paused = false;
    u32 speed = SPEED_NORMAL;
    f64 acc = 0;  // накопленное время мира, с
};

// Мышь миру: кнопка, нажатая над интерфейсом, миру не достаётся до отпускания; колесо над интерфейсом — тоже
struct MouseOwner {
    bool world[MOUSE_COUNT] = {};
};
static Input world_input(const Input &in, MouseOwner &mo, bool ui_mouse, bool ui_keys) {
    Input w = in;
    for (u32 b = 0; b < MOUSE_COUNT; ++b) {
        if (in.mouse_pressed[b]) mo.world[b] = !ui_mouse;
        if (!in.mouse_down[b] && !in.mouse_pressed[b]) mo.world[b] = false;
        w.mouse_down[b] = in.mouse_down[b] && mo.world[b];
        w.mouse_pressed[b] = in.mouse_pressed[b] && mo.world[b];
    }
    if (ui_mouse) w.wheel = 0;
    if (ui_keys)
        for (u32 k = 0; k < KEY_COUNT; ++k) w.key_down[k] = w.key_pressed[k] = false;
    return w;
}

int run_window(const Args &a, AppMemory &m) {
    // Настройки игрока (docs/ui.md): запуски для CI и видео (--frames) их не пишут
    Settings set;
    settings_load(set, settings_path());
    const bool save_settings = a.frames == 0;
    if (!platform_window_open("Anathemyre", (i32)a.win_w, (i32)a.win_h)) return 1;
    if (set.fullscreen && !a.frames) platform_set_fullscreen(true);
    RenderConfig rc;
    rc.vsync = a.vsync && set.vsync;  // --novsync сильнее настройки и в файл не пишется
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
    // Интерфейс: без шрифта игра идёт дальше, только без HUD
    char data_dir[1024];
    snprintf(data_dir, sizeof data_dir, "%sdata/", platform_exe_dir());
    const bool hud_on = !a.nohud && !a.timelapse && ui_init(m.world, m.load, data_dir);
    HudMenu menu;
    HudStats hs{};
    // Проходы GPU для панели отладки: сумма за секунду → среднее (как остальные числа кадра)
    const char *pass_names[8] = {};
    f64 pass_sum[8] = {}, pass_avg[8] = {};
    u32 npass = 0, npass_avg = 0, pass_frames = 0;
    WorldInspect *inspect = arena_push<WorldInspect>(m.world, 1);
    *inspect = WorldInspect{};
    MouseOwner mouse_owner;
    u64 last_hash = 0;
    bool vsync_applied = rc.vsync;
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
        cam.pivot.x = a.cam[3];
        cam.pivot.z = a.cam[4];
        wpos_rebase(cam.pivot);
    } else {
        cam.pivot.z = 10.0f;  // сцена этапа 1 — к северу от начала карты
    }
    cam.pivot = view_pivot_at(w.terrain, cam.pivot);
    TimeCtl tc;
    DebugState ds;
    u32 clock_speed = 0;  // номер в CLOCK_SPEEDS
    // Час дня → доля суток в оборотах (намерение часов)
    auto hour_turn = [](f64 h) { return (u64)(u32)(u64)(h / 24.0 * 4294967296.0); };
    if (a.hour_set) pending.push_back({0, INTENT_CLOCK_SET, WPos{}, hour_turn(a.hour)});
    if (a.timelapse) pending.push_back({0, INTENT_CLOCK_RATE, WPos{}, 0});
    ds.lines = set.lines;
    if (a.open && (!strcmp(a.open, "menu") || !strcmp(a.open, "keys"))) {
        menu.open = true;
        menu.tab = !strcmp(a.open, "keys") ? HUD_TAB_CONTROLS : HUD_TAB_SCREEN;
    } else if (a.open) {
        set.debug = set.debug_panel = true;
        ds.tab = !strcmp(a.open, "world") ? DEBUG_TAB_WORLD : DEBUG_TAB_MAIN;
    }
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
        if (in.quit || menu.quit) break;
        if (in.resized) render_resized();
        i32 ww = 0, wh = 0;
        platform_window_size(&ww, &wh);

        // Общие клавиши: Esc — меню настроек, F11 — весь экран; отладочные — только при включённой отладке
        bool menu_was = menu.open;
        if (in.key_pressed[KEY_ESCAPE] && hud_on) menu.open = !menu.open;
        if (in.key_pressed[KEY_ESCAPE] && !hud_on) break;  // без интерфейса Esc по-прежнему закрывает окно
        if (in.key_pressed[KEY_F11]) set.fullscreen = !set.fullscreen;
        ds.lines = set.lines;
        ds.pixel = set.pixel;
        if (set.debug && !menu.open) {
            if (in.key_pressed[KEY_SPACE]) ds.pause_toggle = true;
            if (in.key_pressed[KEY_MINUS] && tc.speed > 0) ds.speed_set = (i32)tc.speed - 1;
            if (in.key_pressed[KEY_EQUALS] && tc.speed + 1 < ANA_COUNTOF(SPEEDS)) ds.speed_set = (i32)tc.speed + 1;
            if (in.key_pressed[KEY_F3]) set.debug_panel = !set.debug_panel;
            debug_keys(in, ds);
        }

        // Интерфейс до мира: он решает, чья мышь в этом кадре
        if (hud_on) {
            UiConfig uc{set.ui_scale, set.text_scale};
            ui_begin(ww, wh, uc, in);
            hud_fps(set, hs);
            if (set.debug && set.debug_panel && !menu.open) {
                WPos eye = view_eye(cam);
                DebugInfo di{};
                di.fps = hs.fps;
                di.frame_ms = hs.frame_ms;
                di.frame_max_ms = st_frame.max_last;
                di.cpu_ms = hs.cpu_ms;
                di.gpu_ms = hs.gpu_ms;
                di.step_ms = hs.step_ms;
                di.gpu_name = render_device_name();
                di.step = w.step;
                di.hash = last_hash;
                di.hour = sky_hour(w.clock, (f64)w.clock.tick);
                di.clock_speed = clock_speed;
                di.paused = tc.paused;
                di.speed = tc.speed;
                di.speeds = SPEEDS;
                di.nspeeds = ANA_COUNTOF(SPEEDS);
                di.cam_free = cam.free;
                di.cam[0] = (f64)eye.cx * CHUNK_M + eye.x;
                di.cam[1] = eye.y;
                di.cam[2] = (f64)eye.cz * CHUNK_M + eye.z;
                di.cam_above = eye.y - ground_at(w.terrain, eye).height;
                di.passes = npass_avg;
                for (u32 i = 0; i < npass_avg; ++i) {
                    di.pass_names[i] = pass_names[i];
                    di.pass_ms[i] = pass_avg[i];
                }
                di.vis_clusters = render_vis_clusters();
                debug_panel(di, w, ds, *inspect);
            }
            hud_menu(menu, set);
            ui_end();
        }
        if (set.debug) {
            set.lines = ds.lines;
            set.pixel = ds.pixel;
        }
        if (menu_was && !menu.open && menu.changed && save_settings) {
            settings_save(set, settings_path());
            menu.changed = false;
        }
        // Настройки окна и рендера — сразу
        if (set.fullscreen != platform_is_fullscreen() && !a.frames) platform_set_fullscreen(set.fullscreen);
        bool vs = a.vsync && set.vsync;
        if (vs != vsync_applied) {
            render_set_vsync(vs);
            vsync_applied = vs;
        }
        cam.settings = CameraSettings{set.mouse_sens, set.invert_y, set.fov_deg};
        bool pixel = set.pixel && !a.nopixel;
        dbg.enabled = ds.lines;

        Input win = world_input(in, mouse_owner, hud_on && ui_wants_mouse(), menu.open);
        if (ds.flight_toggle) win.key_pressed[KEY_F] = true;  // кнопка панели — как клавиша F
        if (ds.pause_toggle) tc.paused = !tc.paused;
        if (ds.speed_set >= 0 && (u32)ds.speed_set < ANA_COUNTOF(SPEEDS)) tc.speed = (u32)ds.speed_set;
        view_camera_control(cam, win, (f32)dt, w.terrain);

        // Мышь → намерения игрока: левая — притянуть точки, с Ctrl — вспышка; R — отпустить.
        // На вкладке «Мир» панели отладки левая кнопка выбирает строку точки, а не приказывает
        bool inspecting = hud_on && set.debug && set.debug_panel && ds.tab == DEBUG_TAB_WORLD;
        WPos at;
        if (win.mouse_pressed[MOUSE_LEFT] && view_screen_to_ground(cam, ww, wh, in.mouse_x, in.mouse_y, w.terrain, at)) {
            if (inspecting) debug_inspect_pick(w, *inspect, at);
            else pending.push_back({0, win.key_down[KEY_CTRL] ? (u32)INTENT_BURST : (u32)INTENT_ATTRACT, at, 0});
        }
        if (win.key_pressed[KEY_R]) pending.push_back({0, INTENT_RELEASE, WPos{}, 0});
        // Часы (отладка): [ ] и ползунок — час, T и выбор — скорость суток; через намерения, запись повтора их помнит
        if (ds.hour_set) pending.push_back({0, INTENT_CLOCK_SET, WPos{}, hour_turn(ds.hour)});
        if (ds.clock_speed_set >= 0 && (u32)ds.clock_speed_set < ANA_COUNTOF(CLOCK_SPEEDS)) {
            clock_speed = (u32)ds.clock_speed_set;
            pending.push_back({0, INTENT_CLOCK_RATE, WPos{}, CLOCK_SPEEDS[clock_speed]});
        }
        if (ds.clock_hours) {
            f64 h = sky_hour(w.clock, (f64)w.clock.tick) + ds.clock_hours;
            h -= 24.0 * (f64)(i64)(h / 24.0);
            if (h < 0) h += 24.0;
            pending.push_back({0, INTENT_CLOCK_SET, WPos{}, hour_turn(h)});
        }
        if (ds.clock_speed) {
            clock_speed = (clock_speed + 1) % ANA_COUNTOF(CLOCK_SPEEDS);
            pending.push_back({0, INTENT_CLOCK_RATE, WPos{}, CLOCK_SPEEDS[clock_speed]});
        }
        if (a.timelapse && a.frames > 1) {
            f64 k = (f64)frames / (f64)(a.frames - 1);
            f64 h = a.timelapse_hours[0] + (a.timelapse_hours[1] - a.timelapse_hours[0]) * k;
            h -= 24.0 * (f64)(i64)(h / 24.0);
            pending.push_back({0, INTENT_CLOCK_SET, WPos{}, hour_turn(h)});
        }

        u32 nsteps = 0;
        if (a.timelapse) {
            nsteps = 1;  // кадр — один шаг: каждый кадр видит свой час
        } else if (tc.paused) {
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
        debug_clear_frame(ds);

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
                last_hash = h;
                char hex[17];
                log_info("шаг %" PRIu64 " хеш %s", w.step, hex_u64(h, hex));
            }
        }

        // Замер CPU кадра: вид + запись команд, без шага мира и без ожидания GPU и показа (vsync)
        u64 tcpu = platform_time_ns();
        arena_reset(m.frame);
        ViewInput vi{&ring_prev(ring), &ring_cur(ring), tc.paused || a.timelapse ? 1.0f : (f32)(tc.acc / SIM_DT), &dbg, ww, wh, pixel,
                     a.forward || ds.forward, a.test_lights || ds.test_lights, a.froxel_heat || ds.froxel_heat, scatter};
        if (!ds.lines) vi.dbg = nullptr;
        RenderFrame rf;
        u32 dbg_keep = dbg.count;  // метка осмотра — только на этот кадр, шаг мира её не видит
        if (hud_on && set.debug && set.debug_panel && ds.tab == DEBUG_TAB_WORLD) debug_inspect_mark(w, *inspect, dbg);
        view_build(cam, *assets, vi, m.frame, rf);
        dbg.count = dbg_keep;
        bool last_frame = a.frames && frames + 1 >= a.frames;
        if ((last_frame && a.shot) || a.timelapse) render_capture_next();
        if (!render_frame(rf)) { rc_exit = 1; break; }
        if (a.timelapse) {
            char path[1024];
            snprintf(path, sizeof path, "%s_%04u.bmp", a.timelapse, (u32)frames);
            if (!debug_write_shot(path, m.load)) { rc_exit = 1; break; }
        }
        stat_add(st_cpu, (f64)(platform_time_ns() - tcpu) / 1e6 - render_wait_ms());
        {
            const char *nm[8];
            f64 ms[8];
            u32 n = render_gpu_passes(nm, ms, 8);
            bool same = n == npass;
            for (u32 i = 0; i < n && same; ++i) same = nm[i] == pass_names[i];
            if (!same) {  // другой набор проходов (переключили forward) — заново
                for (u32 i = 0; i < 8; ++i) pass_sum[i] = 0;
                pass_frames = 0;
            }
            npass = n;
            for (u32 i = 0; i < n; ++i) {
                pass_names[i] = nm[i];
                pass_sum[i] += ms[i];
            }
            pass_frames++;
        }

        frames++;
        frames_window++;
        if (now - window_t0 >= 1000000000ull) {
            stat_roll(st_frame);
            stat_roll(st_step);
            stat_roll(st_cpu);
            f64 secs = (f64)(now - window_t0) / 1e9;
            i32 hour_hm = (i32)(sky_hour(w.clock, (f64)w.clock.tick) * 60.0f) % 1440;
            char title[320];
            snprintf(title, sizeof title,
                     "Anathemyre — %.0f к/с · кадр %.2f мс (макс %.2f) · CPU кадра %.2f мс · GPU %.2f мс · шаг %.3f мс · "
                     "×%g%s%s · шаг %" PRIu64 " · %02d:%02d",
                     (f64)frames_window / secs, st_frame.avg_last, st_frame.max_last, st_cpu.avg_last, render_gpu_ms(),
                     st_step.avg_last, (f64)SPEEDS[tc.speed], tc.paused ? " · пауза" : "", pixel ? "" : " · без ячеек",
                     w.step, hour_hm / 60, hour_hm % 60);
            platform_set_title(title);
            npass_avg = npass;
            for (u32 i = 0; i < npass; ++i) pass_avg[i] = pass_frames ? pass_sum[i] / pass_frames : 0.0;
            for (u32 i = 0; i < 8; ++i) pass_sum[i] = 0;
            pass_frames = 0;
            hs = HudStats{(f64)frames_window / secs, st_frame.avg_last, st_cpu.avg_last, render_gpu_ms(), st_step.avg_last};
            log_info("%s", title);
            // Проходы GPU за секунду — те же числа, что в F3, для журнала и сравнения на другой машине
            if (npass_avg) {
                char line[320];
                i32 len = snprintf(line, sizeof line, "проходы GPU, мс:");
                for (u32 i = 0; i < npass_avg && len > 0 && len < (i32)sizeof line; ++i)
                    len += snprintf(line + len, sizeof line - (size_t)len, " %s %.3f", pass_names[i] ? pass_names[i] : "?",
                                    pass_avg[i]);
                log_info("%s", line);
            }
            frames_window = 0;
            window_t0 = now;
        }
        if (last_frame) break;
    }

    if (save_settings) settings_save(set, settings_path());
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
