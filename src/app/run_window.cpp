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
#include "../view/people.h"
#include "../view/view.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static constexpr u32 MAX_STEPS_PER_FRAME = 8;  // больше — мир отстаёт, а не кадр замирает
static const f32 SPEEDS[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
static constexpr u32 SPEED_NORMAL = 3;

// Части кадра (docs/frame_time.md): процессорное время от начала кадра до начала следующего. Части рендера —
// из render_cpu_times(); «прочее» — остаток (камера, намерения, сбор переполнений и т.п.)
enum FramePartId : u32 {
    FP_INPUT, FP_UI, FP_STEPS, FP_VIEW, FP_FENCE, FP_PREPARE, FP_ACQUIRE, FP_RECORD, FP_SUBMIT, FP_PRESENT, FP_LOG,
    FP_REST, FP_COUNT
};
static const char *const FP_NAMES[FP_COUNT] = {"ввод", "интерфейс", "шаги мира", "вид", "ожидание GPU", "подготовка",
                                               "картинка", "запись команд", "отправка", "показ", "журнал", "прочее"};
static_assert(FP_COUNT <= PROF_MAX_PARTS, "частей кадра больше, чем мест в FrameProf");
static_assert(FP_COUNT <= DEBUG_MAX_FRAME_PARTS, "частей кадра больше, чем строк в панели");

// Дописать в строку с длиной len (обрезка — по месту)
__attribute__((format(printf, 4, 5))) static void put_text(char *buf, usize cap, usize &len, const char *fmt, ...) {
    if (len >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    i32 k = vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
    if (k > 0) len += (usize)k;
}

// Строка журнала раз в секунду: части кадра (среднее/наибольшее) и из чего сложился худший кадр
static void log_frame_parts(const FrameProf &p) {
    char line[768];
    usize len = 0;
#define put(...) put_text(line, sizeof line, len, __VA_ARGS__)
    put("кадр по частям, мс (среднее/наибольшее):");
    for (u32 i = 0; i < p.nparts; ++i) put(" %s %.2f/%.2f", p.names[i], p.avg_last[i], p.max_last[i]);
    // Худший кадр: три самые большие части
    put(" · худший %.2f:", p.worst_total_last);
    bool used[PROF_MAX_PARTS] = {};
    for (u32 k = 0; k < 3; ++k) {
        u32 best = PROF_MAX_PARTS;
        for (u32 i = 0; i < p.nparts; ++i)
            if (!used[i] && (best == PROF_MAX_PARTS || p.worst_last[i] > p.worst_last[best])) best = i;
        if (best == PROF_MAX_PARTS || p.worst_last[best] < 0.05) break;
        used[best] = true;
        put("%s %s %.2f", k ? "," : "", p.names[best], p.worst_last[best]);
    }
#undef put
    log_info("%s", line);
}

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
// Отладочные режимы, с которыми к/с и мс нельзя сравнивать с бюджетом (docs/budget.md): без ячеек кеш земли не
// работает и вся земля считается на пиксель; vsync упирает к/с в частоту экрана; слои проверки и отладочная
// сборка медленнее в разы. Пусто — замер честный
static void unfair_modes(char *out, usize cap, bool pixel, bool nogcache, bool lights, bool heat, bool validation,
                         bool vsync, bool sm_pages, bool sdf_view) {
    out[0] = 0;
    usize n = 0;
    auto add = [&](bool on, const char *what) {
        if (!on || n + 1 >= cap) return;
        int k = snprintf(out + n, cap - n, "%s%s", n ? ", " : "", what);
        if (k > 0) n = (usize)k + n < cap ? n + (usize)k : cap - 1;
    };
    add(!pixel, "без ячеек (P)");
    add(nogcache, "без кеша земли");
    add(lights, "тестовые огни");
    add(heat, "огни по клеткам");
    add(sm_pages, "страницы теней");
    add(sdf_view, "поле расстояний");
    add(validation, "слои проверки");
#ifdef ANA_DEBUG
    add(true, "отладочная сборка");
#endif
    add(vsync, "vsync");
}

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
    rc.validation = a.validation && !a.gpu_validation;
    rc.gpu_validation = a.gpu_validation;
    rc.gpu_validation_skip = a.gpu_validation_skip;
    char shader_dir[1024];
    snprintf(shader_dir, sizeof shader_dir, "%sshaders/", platform_exe_dir());
    rc.shader_dir = shader_dir;
    char pipe_cache[1024];
    snprintf(pipe_cache, sizeof pipe_cache, "%spipelines.cache", platform_exe_dir());
    rc.pipeline_cache_path = pipe_cache;
    if (!render_init(rc)) {
        log_error("отрисовка не запустилась (причина — строкой выше; подробнее: --validation)");
        platform_window_close();
        return 1;
    }

    World w;
    u64 desc = 0;
    MaterialTable mats;
    ViewAssets *assets = arena_push<ViewAssets>(m.world, 1);
    ScatterCache *scatter = arena_push<ScatterCache>(m.world, 1);
    scatter_init(*scatter, m.world);
    PeopleView *pv = arena_push<PeopleView>(m.world, 1);  // ~1.3 МБ: не на стек
    people_view_init(*pv);
    // Приказы по кадрам (--go): «кадр место x z y аллюр», через «;»
    struct GoOrder {
        u64 frame;
        u32 slot, gait;
        f32 x, z, y;
    };
    GoOrder go[32];
    u32 ngo = 0;
    // Удары по кадрам (--attack): «кадр место цель», через «;»
    struct AttackOrder {
        u64 frame;
        u32 slot, foe;
    };
    AttackOrder atk[32];
    u32 natk = 0;
    for (const char *q = a.attack; q && *q && natk < 32;) {
        unsigned long long f = 0;
        unsigned sl = 0, fo = 0;
        if (sscanf(q, " %llu %u %u", &f, &sl, &fo) == 3) atk[natk++] = {(u64)f, sl, fo};
        else log_warn("--attack: непонятный приказ %s", q);
        const char *e = strchr(q, ';');
        q = e ? e + 1 : nullptr;
    }
    for (const char *q = a.go; q && *q && ngo < 32;) {
        unsigned long long f = 0;
        unsigned sl = 0, g = 0;
        float x = 0, z = 0, y = 0;
        if (sscanf(q, " %llu %u %f %f %f %u", &f, &sl, &x, &z, &y, &g) == 6) go[ngo++] = {(u64)f, sl, g, x, z, y};
        else log_warn("--go: непонятный приказ %s", q);
        const char *e = strchr(q, ';');
        q = e ? e + 1 : nullptr;
    }
    if (!app_world_create(w, m, a.seed, &desc) || !app_assets_load(m, mats, *assets) || !app_terrain_upload(w)) {
        log_error("мир или ассеты не загрузились (причина — строкой выше)");
        render_shutdown();
        platform_window_close();
        return 1;
    }
    DuelDesc duel{};
    GroupDesc group{};
    if (!app_fight_load(duel, group, m)) {
        render_shutdown();
        platform_window_close();
        return 1;
    }
    Rewind *rewind = arena_push<Rewind>(m.world, 1);
    if (!a.record) rewind_init(*rewind, w);
    const i64 slow_from = a.slow_from;
    // Интерфейс: без шрифта игра идёт дальше, только без HUD
    char data_dir[1024];
    snprintf(data_dir, sizeof data_dir, "%sdata/", platform_exe_dir());
    const bool hud_on = !a.nohud && !a.timelapse && ui_init(m.world, m.load, data_dir);
    HudMenu menu;
    HudStats hs{};
    char unfair[160] = "";  // включённые отладочные режимы (unfair_modes); пусто — замер честный
    hs.unfair = unfair;
    // Проходы GPU для панели отладки: сумма за секунду → среднее (как остальные числа кадра)
    static_assert(RENDER_MAX_PASSES <= DEBUG_MAX_PASSES, "строк проходов в панели меньше, чем у отрисовки");
    const char *pass_names[RENDER_MAX_PASSES] = {};
    f64 pass_sum[RENDER_MAX_PASSES] = {}, pass_avg[RENDER_MAX_PASSES] = {};
    u32 npass = 0, npass_avg = 0, pass_frames = 0;
    // Переполнения за секунду: по каждому — наибольшее «сколько просили»; прошлая секунда — для панели
    RenderOverflow over_sec[DEBUG_MAX_OVERFLOWS], over_last[DEBUG_MAX_OVERFLOWS];
    u32 nover_sec = 0, nover_last = 0;
    // Работа освещения последнего прочитанного кадра мира (docs/tiles.md)
    RenderWork work{};
    bool work_have = false;
    RenderLightInfo eye_info{};  // глаз последнего прочитанного кадра (docs/light.md): F3 и журнал
    bool eye_have = false;
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
        recorder_init(rec, m.record, w, a.seed, 280000, 1u << 18);  // 93 столбца × 8 байт × 280 000 ≈ 208 МБ + намерения 10 МБ
        rec.desc_hash = desc;
    }

    FixedArray<Intent> pending;  // намерения игрока до ближайшего шага
    pending.init(m.world, 64);
    FixedArray<Intent> step_its;  // намерения шага: игрока (в первом шаге кадра) и ИИ бойцов
    step_its.init(m.world, 1024);
    Camera cam;
    if (a.cam_set) {
        const f32 deg = PI_F / 180.0f;
        cam.yaw = a.cam[0] * deg;
        cam.pitch = a.cam[1] * deg;
        cam.dist = a.cam[2];
        cam.pivot.x = a.cam[3];
        cam.pivot.z = a.cam[4];
        wpos_rebase(cam.pivot);
    } else if (w.start.set) {
        // Точка старта из сцены (строка start, docs/text.md): сейчас — у входа в Остов в пустыне
        const f32 deg = PI_F / 180.0f;
        cam.yaw = w.start.yaw * deg;
        cam.pitch = w.start.pitch * deg;
        cam.dist = w.start.dist;
        cam.pivot.x = w.start.x;
        cam.pivot.z = w.start.z;
        wpos_rebase(cam.pivot);
    } else {
        cam.pivot.z = 10.0f;  // сцена этапа 1 — к северу от начала карты
    }
    cam.pivot = view_pivot_at(w.terrain, cam.pivot);
    if (a.pivot_above >= 0) {
        cam.pivot_above = a.pivot_above;
        cam.pivot.y = ground_at(w.terrain, cam.pivot).height + a.pivot_above;
    }
    TimeCtl tc;
    DebugState ds;
    if (a.tonemap >= 0) ds.tonemap = (u32)a.tonemap;
    u32 clock_speed = 0;  // номер в CLOCK_SPEEDS
    // Час дня → доля суток в оборотах (намерение часов)
    auto hour_turn = [](f64 h) { return (u64)(u32)(u64)(h / 24.0 * 4294967296.0); };
    if (a.hour_set) pending.push_back({0, INTENT_CLOCK_SET, WPos{}, hour_turn(a.hour)});
    if (a.timelapse) pending.push_back({0, INTENT_CLOCK_RATE, WPos{}, 0});
    // Люди окна у точки старта: намерение первого шага, поэтому запись окна их повторяет, а --sim не трогает
    const bool brawl = a.brawl[0] && a.brawl[1];
    if (w.start.set && w.start.people && !brawl) {
        WPos at;
        at.x = w.start.x;
        at.z = w.start.z;
        wpos_rebase(at);
        pending.push_back({0, INTENT_PEOPLE_SPAWN, at, (u64)w.start.people | ((u64)w.start.radius << 16)});
    }
    if (brawl) brawl_intents(w, a.brawl, pending, 0.0f, -17.0f);  // стенка на стенку на площадке Остова (видео группы)
    ds.lines = set.lines;
    debug_stand_args(ds, a.stand);
    if (a.people_lines) ds.people_lines = true;
    if (a.sides || (a.brawl[0] && a.brawl[1])) ds.people_sides = true;
    if (a.open && (!strcmp(a.open, "menu") || !strcmp(a.open, "keys"))) {
        menu.open = true;
        menu.tab = !strcmp(a.open, "keys") ? HUD_TAB_CONTROLS : HUD_TAB_SCREEN;
    } else if (a.open) {
        set.debug = set.debug_panel = true;
        ds.tab = !strcmp(a.open, "world") ? DEBUG_TAB_WORLD : !strcmp(a.open, "frame") ? DEBUG_TAB_FRAME : DEBUG_TAB_MAIN;
    }
    Input in{};
    TimeStat st_frame, st_step, st_cpu, st_gpu;
    FrameProf *prof = arena_push<FrameProf>(m.world, 1);
    *prof = FrameProf{};
    prof->nparts = FP_COUNT;
    prof->rest = FP_REST;
    for (u32 i = 0; i < FP_COUNT; ++i) prof->names[i] = FP_NAMES[i];
    u64 frames = 0, frames_window = 0;
    u64 last = platform_time_ns(), window_t0 = last;
    int rc_exit = 0;
    debug_print_help();

    for (;;) {
        u64 now = platform_time_ns();
        if (frames) prof_end(*prof, (f64)(now - last) / 1e6);  // прошлый кадр — до этой отметки
        prof_begin(*prof, now);
        f64 dt = (f64)(now - last) / 1e9;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (a.video) dt = 1.0 / 30.0;  // видео: мир и вид идут ровно 1/30 с на кадр, как бы долго ни рисовался кадр
        stat_add(st_frame, dt * 1000.0);

        platform_poll(in);
        if (in.quit || menu.quit) break;
        if (in.resized) render_resized();
        i32 ww = 0, wh = 0;
        platform_window_size(&ww, &wh);
        prof_part(*prof, FP_INPUT, platform_time_ns());

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
        prof_mark(*prof, platform_time_ns());
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
                di.gpu_max_ms = st_gpu.max_last;
                di.prof = prof;
                di.step_ms = hs.step_ms;
                di.gpu_name = render_device_name();
                di.unfair = unfair;
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
                di.work = work_have;
                for (u32 i = 0; i < RENDER_TILE_KINDS; ++i) di.work_tiles[i] = work.tiles[i];
                di.work_px[0] = work.px_sky;
                di.work_px[1] = work.px_ground;
                di.work_px[2] = work.px_mesh;
                di.work_surf[0] = work.surf_ground;
                di.work_surf[1] = work.surf_mesh;
                di.work_shadow = work.shadow_taps;
                di.work_gc[0] = work.gc_used;
                di.work_gc[1] = work.gc_baked;
                di.work_gc[2] = work.gc_over;
                di.work_gc[3] = work.gc_direct;
                di.work_skin = work.skin_verts;
                di.work_sm[0] = work.sm_used;
                di.work_sm[1] = work.sm_new;
                di.work_sm[2] = work.sm_over;
                di.work_sm[3] = work.sm_miss;
                di.work_sm[4] = work.sm_draws;
                di.work_sm[5] = work.sm_taps;
                di.work_sm[6] = work.sm_wait;
                di.work_sm[7] = work.sm_blend;
                di.work_sdf[0] = work.sdf_taps;
                di.work_sdf[1] = work.sdf_tris;
                di.work_sdf[2] = work.sdf_bricks;
                di.gc_off = work.gc_off;
                di.light = eye_have;
                di.lux_key = eye_info.lux_key;
                di.lux_sky = eye_info.lux_sky;
                di.lux_eye = eye_info.lux_eye;
                di.ev100 = eye_info.ev100;
                di.ev100_goal = eye_info.ev100_goal;
                di.overflows = nover_last;
                for (u32 i = 0; i < nover_last; ++i) {
                    di.over_what[i] = over_last[i].what;
                    di.over_want[i] = over_last[i].want;
                    di.over_cap[i] = over_last[i].cap;
                }
                debug_panel(di, w, ds, *inspect);
            }
            hud_menu(menu, set);
            ui_end();
        }
        prof_part(*prof, FP_UI, platform_time_ns());
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
        unfair_modes(unfair, sizeof unfair, pixel, a.nogcache || ds.gcache_off, a.test_lights || ds.test_lights,
                     a.froxel_heat || ds.froxel_heat, a.validation || a.gpu_validation, vsync_applied,
                     a.shadow_pages || ds.shadow_pages, a.sdf_view || ds.sdf_view);
        dbg.enabled = ds.lines;

        Input win = world_input(in, mouse_owner, hud_on && ui_wants_mouse(), menu.open);
        if (ds.flight_toggle) win.key_pressed[KEY_F] = true;  // кнопка панели — как клавиша F
        if (ds.pause_toggle) tc.paused = !tc.paused;
        if (ds.speed_set >= 0 && (u32)ds.speed_set < ANA_COUNTOF(SPEEDS)) tc.speed = (u32)ds.speed_set;
        view_camera_control(cam, win, (f32)dt, w.terrain);

        // Мышь → выбор и намерения игрока (docs/ui.md, «Управление»): левая — выбрать человека; мимо — снять выбор
        // и притянуть точки (с Ctrl — вспышка). Правая — приказ выбранному «идти» (Shift — бег, Ctrl — трусца),
        // Alt + правая — вращать камеру. C — камера за выбранным. R — отпустить точки.
        // На вкладке «Мир» панели отладки левая кнопка выбирает строку точки, а не приказывает
        bool inspecting = hud_on && set.debug && set.debug_panel && ds.tab == DEBUG_TAB_WORLD;
        const Extract &seen = ring_cur(ring);
        WPos at;
        if (win.mouse_pressed[MOUSE_LEFT]) {
            i32 hit = inspecting ? -1 : people_view_pick(*pv, seen, cam, ww, wh, in.mouse_x, in.mouse_y);
            if (hit >= 0) {
                pv->sel = hit;
                pv->sel_gen = seen.person_gen[hit];
            } else if (view_screen_to_ground(cam, ww, wh, in.mouse_x, in.mouse_y, w.terrain, at)) {
                if (inspecting) debug_inspect_pick(w, *inspect, at);
                else {
                    pv->sel = -1;
                    cam.follow = false;
                    pending.push_back({0, win.key_down[KEY_CTRL] ? (u32)INTENT_BURST : (u32)INTENT_ATTRACT, at, 0});
                }
            }
        }
        auto order_go = [&](u32 slot, u16 gen, const WPos &to, u32 gait) {
            pending.push_back({0, INTENT_GO, to, (u64)slot | ((u64)gen << 16) | ((u64)gait << 32)});
            if ((i32)slot == pv->sel) {
                pv->mark_on = true;
                pv->mark = to;
                pv->mark_fade = 1.0f;
                pv->mark_step = w.step;
            }
        };
        // Правая по другому человеку — «атаковать» (этап 10: подойти и ударить раз)
        i32 foe = -1;
        if (win.mouse_pressed[MOUSE_RIGHT] && !win.key_down[KEY_ALT] && pv->sel >= 0)
            foe = people_view_pick(*pv, seen, cam, ww, wh, in.mouse_x, in.mouse_y);
        if (foe >= 0 && foe != pv->sel) {
            pending.push_back({0, INTENT_ATTACK, WPos{}, ((u64)pv->sel | ((u64)pv->sel_gen << 16)) |
                                                             ((u64)((u32)foe | ((u32)seen.person_gen[foe] << 16)) << 32)});
        } else if (win.mouse_pressed[MOUSE_RIGHT] && !win.key_down[KEY_ALT] && pv->sel >= 0 &&
                   view_screen_to_support(cam, ww, wh, in.mouse_x, in.mouse_y, w.terrain, &w.coll, at))
            order_go((u32)pv->sel, pv->sel_gen, at, win.key_down[KEY_SHIFT] ? GAIT_RUN : win.key_down[KEY_CTRL] ? GAIT_JOG : GAIT_WALK);
        if (win.key_pressed[KEY_C] && pv->sel >= 0) cam.follow = !cam.follow;
        // Выбор и приказы из командной строки (видео, проверка повтора)
        if (a.select >= 0 && pv->sel < 0 && seen.person_gen[a.select] && frames < 4) {
            pv->sel = a.select;
            pv->sel_gen = seen.person_gen[a.select];
            cam.follow = a.follow;
        }
        for (u32 i = 0; i < ngo; ++i) {
            if (go[i].frame != frames || go[i].slot >= PEOPLE_CAPACITY || !seen.person_gen[go[i].slot]) continue;
            WPos to;
            to.x = go[i].x;
            to.z = go[i].z;
            wpos_rebase(to);
            to.y = ground_at(w.terrain, to).height + go[i].y;  // y — над землёй (плита, пол руины)
            order_go(go[i].slot, seen.person_gen[go[i].slot], to, go[i].gait);
        }
        for (u32 i = 0; i < natk; ++i) {
            const AttackOrder &o = atk[i];
            if (o.frame != frames || o.slot >= PEOPLE_CAPACITY || o.foe >= PEOPLE_CAPACITY || !seen.person_gen[o.slot] ||
                !seen.person_gen[o.foe])
                continue;
            pending.push_back({0, INTENT_ATTACK, WPos{}, ((u64)o.slot | ((u64)seen.person_gen[o.slot] << 16)) |
                                                             ((u64)(o.foe | ((u32)seen.person_gen[o.foe] << 16)) << 32)});
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

        // Перемотка и перечитывание описаний (отладка). С записью повтора — нет: запись не знает ни того, ни другого
        if (ds.step_back && tc.paused && !a.record && w.step > 0) {
            if (rewind_to(*rewind, w, w.step - 1, m.step)) {
                sim_extract(w, ring_next(ring));
                ring_commit(ring);
                sim_extract(w, ring_next(ring));
                ring_commit(ring);
            } else {
                log_warn("перемотка: дальше снимков нет");
            }
        }
        if (ds.reload_actions && !a.record) {
            if (app_actions_load(w, m)) pv->act_bound = false;
            else log_warn("описания действий не перечитаны (ошибка выше): старые остаются");
            if (!app_fight_load(duel, group, m)) log_warn("data/duel.txt и group.txt не перечитаны (ошибка выше): старые остаются");
        }
        if (slow_from >= 0 && frames == (u64)slow_from) tc.speed = 1;  // --slow: замедление ×0.25 с кадра

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

        prof_mark(*prof, platform_time_ns());
        for (u32 k = 0; k < nsteps; ++k) {
            step_its.clear();
            if (k == 0)
                for (usize i = 0; i < pending.count; ++i) {
                    pending.data[i].step = w.step;
                    step_its.push_back(pending.data[i]);
                }
            ai_fight(w, duel, group, step_its);
            Span<const Intent> its = step_its.span();
            StepCtx ctx{its, &m.step, &dbg};
            u64 ts = platform_time_ns();
            sim_step(w, ctx);
            stat_add(st_step, (f64)(platform_time_ns() - ts) / 1e6);
            if (a.record) recorder_step(rec, w, its);
            else rewind_after_step(*rewind, w, its);
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

        prof_part(*prof, FP_STEPS, platform_time_ns());

        arena_reset(m.frame);
        ViewInput vi{&ring_prev(ring), &ring_cur(ring), tc.paused || a.timelapse ? 1.0f : (f32)(tc.acc / SIM_DT), &dbg, ww, wh, pixel,
                     a.test_lights || ds.test_lights, a.froxel_heat || ds.froxel_heat,
                     a.nogcache || ds.gcache_off, scatter, pv};
        // Глаз привыкает по времени кадра; в видео — ровно 1/30 с. Сразу — первые два кадра (час из --hour
        // мир ставит после первого), снимок и --timelapse (кадр — свой час)
        vi.dt = a.frames ? 1.0f / 30.0f : (f32)dt;
        vi.eye_snap = frames < 2 || a.timelapse || a.shot;
        vi.tonemap = ds.tonemap;
        vi.shadow_pages = a.shadow_pages ? a.shadow_pages : ds.shadow_pages;
        vi.sdf_view = a.sdf_view ? a.sdf_view : ds.sdf_view;
        vi.light_quality = a.light_quality >= 0 ? (u32)a.light_quality : set.light_quality;
        if (!ds.lines) vi.dbg = nullptr;
        vi.people_debug = vi.dbg && ds.people_lines;
        RenderFrame rf;
        u32 dbg_keep = dbg.count;  // метка осмотра — только на этот кадр, шаг мира её не видит
        if (hud_on && set.debug && set.debug_panel && ds.tab == DEBUG_TAB_WORLD) debug_inspect_mark(w, *inspect, dbg);
        // Стенд: в видео и снимках время клипа идёт ровно 1/30 с на кадр
        if (set.debug) debug_stand_update(ds, *assets, w.terrain, a.frames ? 1.0 / 30.0 : dt, vi.dbg ? &dbg : nullptr);
        if (set.debug && vi.dbg && ds.people_lines) debug_people(w, dbg, pv->sel);
        if (set.debug && vi.dbg && ds.people_sides) debug_sides(w, dbg);
        // Камера за выбранным: опора — его голова на этот кадр
        if (cam.follow) {
            WPos head;
            if (people_view_head(*pv, *vi.prev, *vi.cur, vi.alpha, head)) view_camera_follow(cam, head, (f32)dt, w.terrain, &w.coll);
            else if (pv->sel < 0) cam.follow = false;  // выбран, но тела ещё не было — подождать кадр
        }
        view_build(cam, *assets, vi, m.frame, rf);
        if (set.debug) debug_stand(ds, *assets, m.frame, rf);
        dbg.count = dbg_keep;
        prof_part(*prof, FP_VIEW, platform_time_ns());
        bool last_frame = a.frames && frames + 1 >= a.frames;
        if ((last_frame && a.shot) || a.timelapse || a.video) render_capture_next();
        if (!render_frame(rf)) { rc_exit = 1; break; }
        {
            RenderCpuTimes rt = render_cpu_times();
            prof_add(*prof, FP_FENCE, rt.fence);
            prof_add(*prof, FP_PREPARE, rt.prepare);
            prof_add(*prof, FP_ACQUIRE, rt.acquire);
            prof_add(*prof, FP_RECORD, rt.record);
            prof_add(*prof, FP_SUBMIT, rt.submit);
            prof_add(*prof, FP_PRESENT, rt.present);
            prof_mark(*prof, platform_time_ns());
            // CPU кадра: вид и работа рендера без ожиданий (fence, картинка, показ) — как раньше
            stat_add(st_cpu, prof->cur[FP_VIEW] + rt.prepare + rt.record + rt.submit);
            f64 g = render_gpu_ms();
            if (g > 0) stat_add(st_gpu, g);
        }
        if (a.timelapse || a.video) {
            char path[1024];
            snprintf(path, sizeof path, "%s_%04u.bmp", a.timelapse ? a.timelapse : a.video, (u32)frames);
            if (!debug_write_shot(path, m.load)) { rc_exit = 1; break; }
        }
        {
            const char *nm[RENDER_MAX_PASSES];
            f64 ms[RENDER_MAX_PASSES];
            u32 n = render_gpu_passes(nm, ms, RENDER_MAX_PASSES);
            bool same = n == npass;
            for (u32 i = 0; i < n && same; ++i) same = nm[i] == pass_names[i];
            if (!same) {  // другой набор проходов (сборка карты горизонта, пересчёт таблиц неба) — заново
                for (u32 i = 0; i < RENDER_MAX_PASSES; ++i) pass_sum[i] = 0;
                pass_frames = 0;
            }
            npass = n;
            for (u32 i = 0; i < n; ++i) {
                pass_names[i] = nm[i];
                pass_sum[i] += ms[i];
            }
            pass_frames++;
            work_have = render_work(work);
            eye_have = render_light(eye_info);
            RenderOverflow ov[DEBUG_MAX_OVERFLOWS];
            u32 nov = render_overflows(ov, DEBUG_MAX_OVERFLOWS);
            for (u32 i = 0; i < nov; ++i) {
                u32 k = 0;
                while (k < nover_sec && over_sec[k].what != ov[i].what) k++;
                if (k == nover_sec && nover_sec < DEBUG_MAX_OVERFLOWS) over_sec[nover_sec++] = ov[i];
                else if (k < nover_sec && ov[i].want > over_sec[k].want) over_sec[k].want = ov[i].want;
            }
        }

        frames++;
        frames_window++;
        if (now - window_t0 >= 1000000000ull) {
            prof_mark(*prof, platform_time_ns());
            stat_roll(st_frame);
            stat_roll(st_step);
            stat_roll(st_cpu);
            stat_roll(st_gpu);
            prof_roll(*prof);
            f64 secs = (f64)(now - window_t0) / 1e9;
            i32 hour_hm = (i32)(sky_hour(w.clock, (f64)w.clock.tick) * 60.0f) % 1440;
            char title[320];
            snprintf(title, sizeof title,
                     "Anathemyre — %.0f к/с · кадр %.2f мс (макс %.2f) · CPU кадра %.2f мс · GPU %.2f мс (макс %.2f) · "
                     "шаг %.3f мс · ×%g%s · шаг %" PRIu64 " · %02d:%02d%s%s",
                     (f64)frames_window / secs, st_frame.avg_last, st_frame.max_last, st_cpu.avg_last, st_gpu.avg_last,
                     st_gpu.max_last,
                     st_step.avg_last, (f64)SPEEDS[tc.speed], tc.paused ? " · пауза" : "", w.step, hour_hm / 60,
                     hour_hm % 60, unfair[0] ? " · НЕ ДЛЯ ЗАМЕРА: " : "", unfair);
            platform_set_title(title);
            npass_avg = npass;
            for (u32 i = 0; i < npass; ++i) pass_avg[i] = pass_frames ? pass_sum[i] / pass_frames : 0.0;
            for (u32 i = 0; i < RENDER_MAX_PASSES; ++i) pass_sum[i] = 0;
            pass_frames = 0;
            // Переполнения за секунду — в журнал предупреждением и в панель
            for (u32 i = 0; i < nover_sec; ++i) {
                const RenderOverflow &o = over_sec[i];
                if (o.cap) log_warn("переполнение: %s — %llu из %llu", o.what, (unsigned long long)o.want,
                                    (unsigned long long)o.cap);
                else log_warn("переполнение: %s — %llu", o.what, (unsigned long long)o.want);
                over_last[i] = o;
            }
            nover_last = nover_sec;
            nover_sec = 0;
            hs = HudStats{(f64)frames_window / secs, st_frame.avg_last, st_cpu.avg_last, st_gpu.avg_last, st_step.avg_last, unfair};
            log_info("%s", title);
            // Проходы GPU за секунду — те же числа, что в F3, для журнала и сравнения на другой машине
            if (npass_avg) {
                char line[640];
                i32 len = snprintf(line, sizeof line, "проходы GPU, мс:");
                for (u32 i = 0; i < npass_avg && len > 0 && len < (i32)sizeof line; ++i)
                    len += snprintf(line + len, sizeof line - (size_t)len, " %s %.3f", pass_names[i] ? pass_names[i] : "?",
                                    pass_avg[i]);
                log_info("%s", line);
            }
            log_frame_parts(*prof);
            if (work_have && work.gc_off) log_info("кеш земли выключен: %s — вся земля считается на пиксель", work.gc_off);
            if (work_have)
                log_info("работа освещения: плитки небо %u земля %u сетки %u смесь %u земля на пиксель %u · пиксели небо %u земля %u "
                         "сетки %u · поверхности земли %u сеток %u · выборки тени %u · кеш земли: страниц %u запечено %u "
                         "без места %u, мимо кеша %u · скинено вершин %u · тени: страниц %u нарисовано %u без места %u "
                         "выборок %u без страницы %u, вызовов %u",
                         work.tiles[0], work.tiles[1], work.tiles[2], work.tiles[3], work.tiles[4], work.px_sky,
                         work.px_ground, work.px_mesh, work.surf_ground, work.surf_mesh, work.shadow_taps, work.gc_used,
                         work.gc_baked, work.gc_over, work.gc_direct, work.skin_verts, work.sm_used, work.sm_new,
                         work.sm_over, work.sm_taps, work.sm_miss, work.sm_draws);
            if (eye_have)
                log_info("глаз: светило %.4g лк, небо %.4g лк, у глаза %.4g лк, EV100 %.2f (цель %.2f), экспозиция %.3g",
                         (double)eye_info.lux_key, (double)eye_info.lux_sky, (double)eye_info.lux_eye, (double)eye_info.ev100,
                         (double)eye_info.ev100_goal, (double)eye_info.exposure);
            frames_window = 0;
            window_t0 = now;
            prof_part(*prof, FP_LOG, platform_time_ns());
        }
        if (last_frame) break;
    }

    if (save_settings) settings_save(set, settings_path());
    if (a.frames && eye_have)
        log_info("глаз: светило %.4g лк, небо %.4g лк, у глаза %.4g лк, EV100 %.2f (цель %.2f), экспозиция %.3g",
                 (double)eye_info.lux_key, (double)eye_info.lux_sky, (double)eye_info.lux_eye, (double)eye_info.ev100,
                 (double)eye_info.ev100_goal, (double)eye_info.exposure);
    if (a.shot && !debug_write_shot(a.shot, m.load)) rc_exit = 1;
    if (a.work) {
        // Счётчики работы для сверки CI (tools/work_check.sh): «имя значение» по строке
        char t[768];
        i32 n = snprintf(t, sizeof t,
                         "tiles_sky %u\ntiles_ground %u\ntiles_mesh %u\ntiles_mix %u\ntiles_ground_px %u\npx_sky %u\n"
                         "px_ground %u\n"
                         "px_mesh %u\nsurf_ground %u\nsurf_mesh %u\nshadow_taps %u\ngc_used %u\ngc_baked %u\n"
                         "gc_over %u\ngc_direct %u\nskin_verts %u\nsm_used %u\nsm_new %u\nsm_over %u\nsm_miss %u\n"
                         "sm_draws %u\nsm_taps %u\nsm_wait %u\nsdf_taps %u\nsdf_bricks %u\n",
                         work.tiles[0], work.tiles[1], work.tiles[2], work.tiles[3], work.tiles[4], work.px_sky,
                         work.px_ground, work.px_mesh, work.surf_ground, work.surf_mesh, work.shadow_taps, work.gc_used,
                         work.gc_baked, work.gc_over, work.gc_direct, work.skin_verts, work.sm_used, work.sm_new,
                         work.sm_over, work.sm_miss, work.sm_draws, work.sm_taps, work.sm_wait,
                         work.sdf_taps, work.sdf_bricks);
        if (!work_have) log_error("--work: кадр мира не прочитан (мало кадров)");
        if (!work_have || n <= 0 || !platform_write_file(a.work, t, (usize)n)) rc_exit = 1;
    }
    if (a.record && !save_record(rec, a.record, m.load)) rc_exit = 1;
    u32 verr = render_validation_errors();
    if (verr) {
        log_error("слой проверки Vulkan: %u ошибок", verr);
        rc_exit = 1;
    }
    rewind_free(*rewind);
    render_shutdown();
    platform_window_close();
    return rc_exit;
}
