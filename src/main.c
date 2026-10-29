// Anathemyre — этап 0: тестовая комната
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/arena.h"
#include "core/clock.h"
#include "core/log.h"
#include "core/math3d.h"
#include "core/rng.h"
#include "game/arena.h"
#include "game/arena_view.h"
#include "game/battle.h"
#include "game/character.h"
#include "game/player.h"
#include "game/testroom.h"
#include "platform/platform.h"
#include "render/render.h"
#include "world/sky.h"

#define CHAR_VARIANTS 4
#define CROWD_SIZE 9
// Арена у зала, перед его открытым краем
#define ARENA_CENTER v3(14.0f, 0.0f, 34.0f)
#define ARENA_SHOW_AFTER_BOUT_S 12.0f
static const char *const FILTER_NAMES[RENDER_FILTERS] = { "без фильтра", "первый", "естественный", "охра", "закат", "плёнка" };

#define DAY_LENGTH_S 120.0f  // полные сутки, с; пока быстро, чтобы смену было видно

typedef struct RtsCamera {
    V3  target;
    f32 yaw, pitch, dist;
} RtsCamera;

typedef struct Options {
    const char *shot_path;   // снять кадр в PPM и выйти
    i32         shot_frames;
    i32         width, height;
    i32         internal_h;
    bool        cam_set;
    RtsCamera   cam;
    u64         seed;
    bool        no_vsync;
    bool        follow;      // камера следует за своим персонажем
    f32         tod;         // время суток на старте: 0 полночь, 0.5 полдень
    u32         filter;      // фильтр кадра, см. FILTER_NAMES
    u32         arena_bench; // прогнать N боёв арены без окна
    u32         arena_log;   // сколько первых боёв печатать подробно
    u32         battle[2];   // полевой бой: бойцов в каждом отряде
    bool        battle_verify;
    u32         threads;     // 0 — по числу ядер
} Options;

static void camera_update(RtsCamera *c, const Input *in, f32 dt)
{
    f32 fast = in->key_down[KEY_SHIFT] ? 3.0f : 1.0f;
    f32 pan = (8.0f + c->dist * 0.9f) * dt * fast;
    V3 fwd = v3(-sinf(c->yaw), 0, -cosf(c->yaw));
    V3 right = v3(cosf(c->yaw), 0, -sinf(c->yaw));
    if (in->key_down[KEY_W] || in->key_down[KEY_UP]) c->target = v3_add(c->target, v3_mul(fwd, pan));
    if (in->key_down[KEY_S] || in->key_down[KEY_DOWN]) c->target = v3_sub(c->target, v3_mul(fwd, pan));
    if (in->key_down[KEY_D] || in->key_down[KEY_RIGHT]) c->target = v3_add(c->target, v3_mul(right, pan));
    if (in->key_down[KEY_A] || in->key_down[KEY_LEFT]) c->target = v3_sub(c->target, v3_mul(right, pan));
    if (in->key_down[KEY_Q]) c->yaw += 1.5f * dt;
    if (in->key_down[KEY_E]) c->yaw -= 1.5f * dt;
    // ПКМ отдан приказам, камеру крутит средняя кнопка (как в Kenshi)
    if (in->mouse_down[MOUSE_MIDDLE]) {
        c->yaw -= (f32)in->mouse_dx * 0.005f;
        c->pitch += (f32)in->mouse_dy * 0.004f;
    }
    c->dist *= powf(0.88f, (f32)in->wheel);
    c->dist = clampf(c->dist, 4.0f, 120.0f);
    c->pitch = clampf(c->pitch, 0.08f, 1.45f);
}

static V3 camera_eye(const RtsCamera *c)
{
    V3 off = v3(sinf(c->yaw) * cosf(c->pitch), sinf(c->pitch), cosf(c->yaw) * cosf(c->pitch));
    return v3_add(c->target, v3_mul(off, c->dist));
}

static bool write_ppm(const char *path, const u8 *rgb, i32 w, i32 h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    // GL отдаёт снизу вверх
    for (i32 y = h - 1; y >= 0; y--) fwrite(rgb + (size_t)y * (size_t)w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    return true;
}

// Высота под ногами: пол зала, рельеф, плита арены
static f32 ground_height(f32 x, f32 z)
{
    f32 h = testroom_ground(x, z);
    f32 dx = x - ARENA_CENTER.x, dz = z - ARENA_CENTER.z;
    if (dx * dx + dz * dz < 8.5f * 8.5f) h = fmaxf(h, 0.125f);
    return h;
}

// Луч из камеры через точку экрана до земли. nx, ny — координаты в -1..1
static bool pick_ground(M4 view_proj, V3 eye, f32 nx, f32 ny, V3 *out)
{
    M4 inv = m4_inverse(view_proj);
    const f32 *m = inv.m;
    f32 w = m[3] * nx + m[7] * ny + m[11] + m[15];
    if (fabsf(w) < 1e-9f) return false;
    V3 far_p = v3_mul(m4_point(inv, v3(nx, ny, 1.0f)), 1.0f / w);
    V3 dir = v3_norm(v3_sub(far_p, eye));
    // Шаг по лучу до пересечения с рельефом, затем уточнение делением пополам
    const f32 step = 0.5f, max_t = 1500.0f;
    f32 t0 = 0.0f;
    for (f32 t = step; t < max_t; t += step) {
        V3 p = v3_add(eye, v3_mul(dir, t));
        if (p.y > ground_height(p.x, p.z)) {
            t0 = t;
            continue;
        }
        f32 a = t0, b = t;
        for (int i = 0; i < 20; i++) {
            f32 c = 0.5f * (a + b);
            V3 q = v3_add(eye, v3_mul(dir, c));
            if (q.y > ground_height(q.x, q.z)) a = c;
            else b = c;
        }
        V3 hit = v3_add(eye, v3_mul(dir, b));
        *out = v3(hit.x, ground_height(hit.x, hit.z), hit.z);
        return true;
    }
    return false;
}

static void usage(void)
{
    puts("anathemyre [--size W H] [--res H] [--seed N] [--novsync] [--follow] [--tod 0..1] [--filter N] [--cam X Z YAW PITCH DIST] [--shot file.ppm] [--frames N]");
    puts("anathemyre --arena-bench N [--arena-log K] [--seed N]   бои арены без окна");
    puts("anathemyre --battle-bench A B [--threads N] [--battle-verify] [--seed N]   полевой бой без окна");
}

static bool parse_args(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--shot") && i + 1 < argc) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) o->shot_frames = atoi(argv[++i]);
        else if (!strcmp(a, "--novsync")) o->no_vsync = true;
        else if (!strcmp(a, "--follow")) o->follow = true;
        else if (!strcmp(a, "--tod") && i + 1 < argc) o->tod = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--filter") && i + 1 < argc) o->filter = (u32)atoi(argv[++i]) % RENDER_FILTERS;
        else if (!strcmp(a, "--res") && i + 1 < argc) o->internal_h = atoi(argv[++i]);
        else if (!strcmp(a, "--arena-bench") && i + 1 < argc) o->arena_bench = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--arena-log") && i + 1 < argc) o->arena_log = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--battle-bench") && i + 2 < argc) {
            o->battle[0] = (u32)atoi(argv[++i]);
            o->battle[1] = (u32)atoi(argv[++i]);
        } else if (!strcmp(a, "--battle-verify")) o->battle_verify = true;
        else if (!strcmp(a, "--threads") && i + 1 < argc) o->threads = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) o->seed = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--size") && i + 2 < argc) {
            o->width = atoi(argv[++i]);
            o->height = atoi(argv[++i]);
        } else if (!strcmp(a, "--cam") && i + 5 < argc) {
            o->cam.target = v3(strtof(argv[i + 1], NULL), 0, strtof(argv[i + 2], NULL));
            o->cam.yaw = strtof(argv[i + 3], NULL);
            o->cam.pitch = strtof(argv[i + 4], NULL);
            o->cam.dist = strtof(argv[i + 5], NULL);
            o->cam_set = true;
            i += 5;
        } else {
            usage();
            return false;
        }
    }
    return true;
}

static bool find_data_dir(char *out, size_t size)
{
    char exe[512];
    const char *candidates[3] = { NULL, "data", "../data" };
    char from_exe[600];
    if (platform_exe_dir(exe, sizeof exe)) {
        snprintf(from_exe, sizeof from_exe, "%s/../data", exe);
        candidates[0] = from_exe;
    }
    for (int i = 0; i < 3; i++) {
        if (!candidates[i]) continue;
        char probe[700];
        snprintf(probe, sizeof probe, "%s/shaders/common.glsl", candidates[i]);
        FILE *f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(out, size, "%s", candidates[i]);
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    platform_preinit();
    time_init();
    Options opt = {
        .tod = 0.31f, .filter = 1, .shot_frames = 90, .width = 1280, .height = 720, .internal_h = 360, .seed = 1337,
        .cam = { .target = { 14, 0, 32 }, .yaw = -0.5f, .pitch = 0.42f, .dist = 17.0f },
    };
    if (!parse_args(argc, argv, &opt)) return 1;
    if (opt.arena_bench) return arena_bench(opt.arena_bench, opt.seed, opt.arena_log);
    if (opt.battle[0] || opt.battle[1])
        return battle_bench(opt.battle[0], opt.battle[1], opt.seed, opt.threads, opt.battle_verify);
    log_info("АНАТЕМАЙР. ЭТАП 0. ЗЕРНО МИРА %llu", (unsigned long long)opt.seed);

    char data_dir[512];
    if (!find_data_dir(data_dir, sizeof data_dir)) {
        log_error("ДАННЫЕ: папка data/shaders не найдена");
        return 1;
    }

    // Вся память выделяется здесь, один раз
    Arena perm, scratch;
    if (!arena_init(&perm, MB(64)) || !arena_init(&scratch, MB(4))) {
        log_error("ПАМЯТЬ: не выделена");
        return 1;
    }

    if (!platform_init("ANATHEMYRE", opt.width, opt.height,
#ifdef NDEBUG
                       false
#else
                       true
#endif
                       )) {
        return 1;
    }
    if (!render_init(&scratch, data_dir)) {
        platform_shutdown();
        return 1;
    }
    platform_set_vsync(!opt.no_vsync && opt.shot_path == NULL);

    Rng rng;
    rng_seed(&rng, opt.seed, 1);

    // Геометрия мира
    MeshBuilder mb;
    size_t mark = perm.used;
    GpuMesh near_mesh, far_mesh;
    mb_init(&mb, &perm, 400000, 1200000);
    testroom_build_near(&mb, &rng);
    arena_view_build(&mb, ARENA_CENTER);
    gpu_mesh_upload(&near_mesh, &mb);
    u32 tris = mb.icount / 3;
    perm.used = mark;
    mb_init(&mb, &perm, 400000, 1200000);
    testroom_build_far(&mb, &rng);
    gpu_mesh_upload(&far_mesh, &mb);
    log_info("МИР: зал %u треугольников, рельеф и мегаструктуры %u", tris, mb.icount / 3);
    perm.used = mark;

    // Фигуры зала с оружием тестовой комнаты; бойцы и зрители арены без него
    GpuMesh char_mesh[CHAR_VARIANTS], body_mesh[CHAR_VARIANTS], weapon_mesh[W_COUNT];
    for (u32 v = 0; v < CHAR_VARIANTS; v++) {
        mb_init(&mb, &perm, 40000, 120000);
        character_build(&mb, v, true);
        gpu_mesh_upload(&char_mesh[v], &mb);
        if (v == 0) log_info("ФИГУРА: %u вершин, %u треугольников", mb.vcount, mb.icount / 3);
        perm.used = mark;
        mb_init(&mb, &perm, 40000, 120000);
        character_build(&mb, v, false);
        gpu_mesh_upload(&body_mesh[v], &mb);
        perm.used = mark;
    }
    for (u32 k = 0; k < W_COUNT; k++) {
        mb_init(&mb, &perm, 4000, 12000);
        weapon_build(&mb, (u8)k);
        gpu_mesh_upload(&weapon_mesh[k], &mb);
        perm.used = mark;
    }

    // Метка точки, куда идёт персонаж: бледно-зелёный крест на земле
    GpuMesh marker_mesh;
    mb_init(&mb, &perm, 200, 600);
    mb_set(&mb, v3(0.35f, 0.9f, 0.45f), MAT_EMISSIVE, 0);
    mb_box(&mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    mb_box(&mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(-PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    gpu_mesh_upload(&marker_mesh, &mb);
    perm.used = mark;

    // Арена: те же бои, что в --arena-bench с этим seed, но в реальном времени
    ArenaSession *arena = ARENA_PUSH_ARRAY(&perm, ArenaSession, 1);
    arena_session_init(arena, &perm, opt.seed);
    arena->log_bouts = 0xFFFFFFFFu;
    arena->after_bout_s = ARENA_SHOW_AFTER_BOUT_S;
    ArenaView *aview = ARENA_PUSH_ARRAY(&perm, ArenaView, 1);
    arena_view_init(aview, ARENA_CENTER, 0.125f);
    M4 *arena_bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)AV_BONE_SETS * RENDER_MAX_BONES);

    Crowd crowd;
    crowd_init(&crowd, &rng, CROWD_SIZE);
    // Свой персонаж появляется у края зала, лицом к арене
    Player player;
    player_init(&player, v3(9.0f, ground_height(9.0f, 22.0f), 22.0f), atan2f(ARENA_CENTER.x - 9.0f, ARENA_CENTER.z - 22.0f));
    M4 *player_bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)2 * RENDER_MAX_BONES);
    M4 *bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)TR_MAX_CHARS * RENDER_MAX_BONES);
    DrawItem *items = ARENA_PUSH_ARRAY(&perm, DrawItem, TR_MAX_CHARS + 2 + AV_DRAW_ITEMS + 3);
    u8 *shot_buf = NULL;
    if (opt.shot_path) shot_buf = arena_push(&perm, (size_t)opt.width * (size_t)opt.height * 3, 1);
    for (u32 i = 0; i < TR_MAX_CHARS * RENDER_MAX_BONES; i++) bones[i] = m4_identity();

    RtsCamera cam = opt.cam;
    RenderSettings rs = { .internal_h = opt.internal_h, .filter = opt.filter };
    static const i32 res_steps[] = { 270, 360, 540, 720 };
    u32 res_index = 1;

    Input in;
    memset(&in, 0, sizeof in);
    f64 last = time_now();
    f64 stat_t = last;
    u32 stat_frames = 0;
    f64 cpu_logic = 0, cpu_submit = 0, cpu_swap = 0;
    bool vsync = !opt.no_vsync && opt.shot_path == NULL;
    f32 sim_time = 0;
    f32 tod = opt.tod;
    bool day_run = true;
    u32 frame = 0;

    log_info("УПРАВЛЕНИЕ: WASD — сдвиг, колесо — дистанция, ПКМ — идти, средняя кнопка и Q/E — поворот, F — следовать за персонажем, F1 — фильтр, F2 — разрешение, F3 — vsync, F4 — остановить сутки, F5 — шейдеры");

    while (!in.quit) {
        platform_poll(&in);
        if (in.key_pressed[KEY_ESCAPE]) break;
        if (in.key_pressed[KEY_F1]) {
            rs.filter = (rs.filter + 1) % RENDER_FILTERS;
            log_info("ФИЛЬТР: %s", FILTER_NAMES[rs.filter]);
        }
        if (in.key_pressed[KEY_F2]) {
            res_index = (res_index + 1) % ARRAY_COUNT(res_steps);
            rs.internal_h = res_steps[res_index];
        }
        if (in.key_pressed[KEY_F5]) render_reload_shaders();
        if (in.key_pressed[KEY_F4]) {
            day_run = !day_run;
            log_info("СУТКИ: %s", day_run ? "идут" : "остановлены");
        }
        if (in.key_pressed[KEY_F]) {
            opt.follow = !opt.follow;
            if (opt.follow) cam.dist = MIN(cam.dist, 8.0f);
        }
        if (in.key_pressed[KEY_F3]) {
            vsync = !vsync;
            platform_set_vsync(vsync);
            log_info("VSYNC: %s", vsync ? "вкл" : "выкл");
        }

        f64 now = time_now();
        f32 dt = opt.shot_path ? 1.0f / 60.0f : (f32)fmin(now - last, 0.05);
        last = now;
        sim_time += dt;

        f64 t_logic = time_now();
        camera_update(&cam, &in, dt);
        crowd_update(&crowd, &rng, dt);
        arena_view_update(aview, arena, dt);
        if (opt.follow) cam.target = v3(player.pos.x, 0, player.pos.z);

        i32 ww, wh;
        platform_window_size(&ww, &wh);
        if (opt.shot_path) {
            ww = opt.width;
            wh = opt.height;
        }
        FrameView fv;
        fv.cam_pos = camera_eye(&cam);
        fv.view = m4_look_at(fv.cam_pos, v3_add(cam.target, v3(0, 1.0f, 0)), v3(0, 1, 0));
        fv.proj = m4_perspective(0.95f, (f32)ww / (f32)(wh > 0 ? wh : 1), 0.3f, 6000.0f);

        // ПКМ по земле: идти туда
        if (in.mouse_pressed[MOUSE_RIGHT] && ww > 0 && wh > 0) {
            f32 nx = 2.0f * ((f32)in.mouse_x + 0.5f) / (f32)ww - 1.0f;
            f32 ny = 1.0f - 2.0f * ((f32)in.mouse_y + 0.5f) / (f32)wh;
            V3 hit;
            if (pick_ground(m4_mul(fv.proj, fv.view), fv.cam_pos, nx, ny, &hit)) player_order_move(&player, hit);
        }
        player_update(&player, dt, ground_height);

        u32 n = 0;
        items[n++] = (DrawItem){ .mesh = &near_mesh };
        items[n++] = (DrawItem){ .mesh = &far_mesh, .no_shadow = true };
        for (u32 i = 0; i < crowd.count; i++) {
            CharPose p = {
                .pos = v3(crowd.x[i], 0.1f, crowd.z[i]), .heading = crowd.heading[i],
                .phase = crowd.phase[i], .walk = crowd.walk[i], .time = sim_time, .seed = crowd.seed[i],
                // Разброс сложения внутри класса H: рост ±6%, ширина ±8%
                .height = 1.0f + 0.06f * sinf(crowd.seed[i] * 7.31f),
                .width = 1.0f + 0.08f * sinf(crowd.seed[i] * 3.17f + 1.0f),
            };
            M4 *b = &bones[i * RENDER_MAX_BONES];
            character_pose(&p, NULL, b);
            items[n++] = (DrawItem){ .mesh = &char_mesh[crowd.variant[i]], .bones = b };
        }
        n += arena_view_draw(aview, arena, sim_time, body_mesh, CHAR_VARIANTS, weapon_mesh, arena_bones, items + n);
        {
            CharPose p = {
                .pos = player.pos, .heading = player.heading, .phase = player.phase, .walk = player.walk,
                .time = sim_time, .seed = 7.0f,
            };
            character_pose(&p, NULL, player_bones);
            items[n++] = (DrawItem){ .mesh = &body_mesh[0], .bones = player_bones };
            items[n++] = (DrawItem){ .mesh = &weapon_mesh[W_RESSORA], .bones = player_bones };
            if (player.marker > 0.0f && player.moving) {
                M4 *mk = player_bones + RENDER_MAX_BONES;
                mk[0] = m4_translate(player.target);
                items[n++] = (DrawItem){ .mesh = &marker_mesh, .bones = mk, .no_shadow = true };
            }
        }

        f64 t_submit = time_now();
        cpu_logic += t_submit - t_logic;
        if (day_run) tod = fmodf(tod + dt / DAY_LENGTH_S, 1.0f);
        Sky sky;
        sky_at(tod, &sky);
        fv.sun_dir = sky.light_dir;
        fv.sun_color = sky.light_color;
        fv.sky_amb = sky.sky_amb;
        fv.ground_amb = sky.ground_amb;
        fv.haze = sky.haze;
        fv.zenith = sky.zenith;
        fv.shadow_center = cam.target;
        fv.time = sim_time;
        fv.frame = frame;
        render_frame(&fv, items, n, &rs, ww, wh);
        f64 t_swap = time_now();
        cpu_submit += t_swap - t_submit;

        frame++;
        if (opt.shot_path && (i32)frame >= opt.shot_frames) {
            render_read_pixels(shot_buf, ww, wh);
            if (write_ppm(opt.shot_path, shot_buf, ww, wh)) log_info("КАДР: записан %s", opt.shot_path);
            else log_error("КАДР: не записан %s", opt.shot_path);
            break;
        }
        platform_swap();
        cpu_swap += time_now() - t_swap;

        stat_frames++;
        if (now - stat_t >= 2.0) {
            RenderStats st;
            render_take_stats(&st);
            f64 k = 1000.0 / stat_frames;
            char gpu[192];
            // Драйвер может не отдавать метки времени (например, D3D12 в WSL)
            if (st.samples && st.gpu_total_ms > 0.0)
                snprintf(gpu, sizeof gpu, "GPU %.3f: тени %.3f, сцена %.3f, атмосфера %.3f, стиль %.3f, апскейл %.3f",
                         st.gpu_total_ms, st.gpu_ms[PASS_SHADOW], st.gpu_ms[PASS_SCENE], st.gpu_ms[PASS_ATMOS],
                         st.gpu_ms[PASS_STYLE], st.gpu_ms[PASS_BLIT]);
            else
                snprintf(gpu, sizeof gpu, "GPU: драйвер не отдаёт метки времени");
            log_info("КАДР: %.2f мс, %.0f к/с | CPU: логика %.3f, отправка %.3f, swap %.3f | %s",
                     (now - stat_t) * k, stat_frames / (now - stat_t),
                     cpu_logic * k, cpu_submit * k, cpu_swap * k, gpu);
            stat_t = now;
            stat_frames = 0;
            cpu_logic = cpu_submit = cpu_swap = 0;
        }
    }

    render_shutdown();
    platform_shutdown();
    arena_free(&scratch);
    arena_free(&perm);
    log_info("СЕАНС ЗАВЕРШЁН");
    return 0;
}
