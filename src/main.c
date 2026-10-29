// Anathemyre — этап 0: тестовая комната
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/arena.h"
#include "core/clock.h"
#include "core/log.h"
#include "core/math3d.h"
#include "core/rng.h"
#include "game/character.h"
#include "game/testroom.h"
#include "platform/platform.h"
#include "render/render.h"

#define CHAR_VARIANTS 4
#define CROWD_SIZE 9

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
    if (in->mouse_down[MOUSE_RIGHT] || in->mouse_down[MOUSE_MIDDLE]) {
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

static void usage(void)
{
    puts("anathemyre [--size W H] [--res H] [--seed N] [--novsync] [--cam X Z YAW PITCH DIST] [--shot file.ppm] [--frames N]");
}

static bool parse_args(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--shot") && i + 1 < argc) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) o->shot_frames = atoi(argv[++i]);
        else if (!strcmp(a, "--novsync")) o->no_vsync = true;
        else if (!strcmp(a, "--res") && i + 1 < argc) o->internal_h = atoi(argv[++i]);
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
    time_init();
    Options opt = {
        .shot_frames = 90, .width = 1280, .height = 720, .internal_h = 360, .seed = 1337,
        .cam = { .target = { 2, 0, 6 }, .yaw = -0.5f, .pitch = 0.5f, .dist = 18.0f },
    };
    if (!parse_args(argc, argv, &opt)) return 1;
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
    gpu_mesh_upload(&near_mesh, &mb);
    u32 tris = mb.icount / 3;
    perm.used = mark;
    mb_init(&mb, &perm, 400000, 1200000);
    testroom_build_far(&mb, &rng);
    gpu_mesh_upload(&far_mesh, &mb);
    log_info("МИР: зал %u треугольников, рельеф и мегаструктуры %u", tris, mb.icount / 3);
    perm.used = mark;

    GpuMesh char_mesh[CHAR_VARIANTS];
    for (u32 v = 0; v < CHAR_VARIANTS; v++) {
        mb_init(&mb, &perm, 40000, 120000);
        character_build(&mb, v);
        gpu_mesh_upload(&char_mesh[v], &mb);
        if (v == 0) log_info("ФИГУРА: %u вершин, %u треугольников", mb.vcount, mb.icount / 3);
        perm.used = mark;
    }

    Crowd crowd;
    crowd_init(&crowd, &rng, CROWD_SIZE);
    M4 *bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)TR_MAX_CHARS * RENDER_MAX_BONES);
    DrawItem *items = ARENA_PUSH_ARRAY(&perm, DrawItem, TR_MAX_CHARS + 2);
    u8 *shot_buf = NULL;
    if (opt.shot_path) shot_buf = arena_push(&perm, (size_t)opt.width * (size_t)opt.height * 3, 1);
    for (u32 i = 0; i < TR_MAX_CHARS * RENDER_MAX_BONES; i++) bones[i] = m4_identity();

    RtsCamera cam = opt.cam;
    RenderSettings rs = { .internal_h = opt.internal_h, .post = true };
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
    u32 frame = 0;

    log_info("УПРАВЛЕНИЕ: WASD — сдвиг, Q/E и ПКМ — поворот, колесо — дистанция, F1 — стиль, F2 — разрешение, F3 — vsync, F5 — шейдеры");

    while (!in.quit) {
        platform_poll(&in);
        if (in.key_pressed[KEY_ESCAPE]) break;
        if (in.key_pressed[KEY_F1]) rs.post = !rs.post;
        if (in.key_pressed[KEY_F2]) {
            res_index = (res_index + 1) % ARRAY_COUNT(res_steps);
            rs.internal_h = res_steps[res_index];
        }
        if (in.key_pressed[KEY_F5]) render_reload_shaders();
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

        u32 n = 0;
        items[n++] = (DrawItem){ .mesh = &near_mesh };
        items[n++] = (DrawItem){ .mesh = &far_mesh, .no_shadow = true };
        for (u32 i = 0; i < crowd.count; i++) {
            CharPose p = {
                .pos = v3(crowd.x[i], 0.1f, crowd.z[i]), .heading = crowd.heading[i],
                .phase = crowd.phase[i], .walk = crowd.walk[i], .time = sim_time, .seed = crowd.seed[i],
            };
            M4 *b = &bones[i * RENDER_MAX_BONES];
            character_pose(&p, b);
            items[n++] = (DrawItem){ .mesh = &char_mesh[crowd.variant[i]], .bones = b };
        }

        f64 t_submit = time_now();
        cpu_logic += t_submit - t_logic;
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
        // Низкое солнце сбоку и сзади: длинные тени через зал
        fv.sun_dir = v3_norm(v3(0.70f, 0.30f, -0.62f));
        fv.sun_color = v3(2.2f, 1.75f, 1.1f);
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
