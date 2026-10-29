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
#include "game/field.h"
#include "game/hud.h"
#include "game/squad.h"
#include "game/testroom.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/ui.h"
#include "world/sky.h"

// Тела людей (tools/humanconv): мужчина, женщина, худой мужчина; у каждого LOD0..2
#define BODY_KINDS 3
static const char *const g_body_file[BODY_KINDS] = { "mh_male", "mh_female", "mh_thin" };
#define CROWD_SIZE 9
// Арена у зала, перед его открытым краем
#define ARENA_CENTER v3(14.0f, 0.0f, 34.0f)
// Пороги детализации фигуры: диаметр её сферы на экране, пикселей. Больше первого — полный уровень
static const f32 g_char_lod_px[CHAR_LODS - 1] = { 200.0f, 70.0f };
#define ARENA_SHOW_AFTER_BOUT_S 12.0f
#define CAM_FOV 0.95f  // по вертикали, рад
#define DAY_LENGTH_S 120.0f  // полные сутки, с; пока быстро, чтобы смену было видно

typedef struct RtsCamera {
    V3  target;
    f32 yaw, pitch, dist;
} RtsCamera;

typedef struct Options {
    const char *shot_path;   // снять кадр в PPM и выйти
    i32         shot_frames;
    i32         width, height;
    bool        cam_set;
    RtsCamera   cam;
    u64         seed;
    bool        no_vsync;
    bool        follow;      // камера следует за своим персонажем
    f32         tod;         // время суток на старте: 0 полночь, 0.5 полдень
    bool        inspect;     // выбрать отряд и открыть осмотр (для снимков)
    u32         arena_bench; // прогнать N боёв арены без окна
    u32         field_bench; // сценка атаки и нагрузка поля на N−1 тел без окна
    i32         attack;      // для снимков: весь отряд сразу атакует бродящего номер attack−1
    u32         arena_log;   // сколько первых боёв печатать подробно
    u32         battle[2];   // полевой бой: бойцов в каждом отряде
    bool        battle_verify;
    u32         threads;     // 0 — по числу ядер
    bool        bench;       // замер кадра сразу после старта, затем выход
    bool        move_bench;  // проверка разворотов без окна
    const char *move_trace;
    bool        no_pixel;    // кадр без пиксельности (сравнение моделей)
    bool        no_vrs;      // без шейдинга блоками (сравнение скорости и кадра)
    bool        day_stop;    // сутки стоят на --tod
    bool        humans;      // ряд людей для сравнения сразу при старте (иначе из меню НАСТРОЙКИ)
} Options;

// Что сейчас нарисовано в ячейке атласа портретов: портрет неподвижен, перерисовка только при смене
typedef struct PortraitKey {
    const GpuMesh *body, *weapon;
    f32 seed, height, width;
    i32 size;
} PortraitKey;
static PortraitKey g_portrait_key[RENDER_PORTRAIT_SLOTS];

// Портрет: неподвижная фигура в покое, камера по кости головы спереди, чуть сверху
static void portrait(const FrameView *base, u32 slot, i32 size, const GpuMesh *body, const GpuMesh *weapon,
                     f32 seed, f32 height, f32 width, M4 *bones)
{
    PortraitKey key = { body, weapon, seed, height, width, size };
    if (slot >= RENDER_PORTRAIT_SLOTS || !memcmp(&g_portrait_key[slot], &key, sizeof key)) return;
    g_portrait_key[slot] = key;
    CharPose p = { .seed = seed, .height = height, .width = width };
    character_pose(&p, NULL, bones);
    DrawItem items[2] = { { .mesh = body, .bones = bones }, { .mesh = weapon, .bones = bones } };
    M4 hb = bones[BONE_HEAD];
    V3 head = m4_point(hb, v3(0, 1.70f, 0.03f));
    V3 fwd = v3_norm(m4_dir(hb, v3(0, 0, 1)));
    V3 up = v3_norm(m4_dir(hb, v3(0, 1, 0)));
    FrameView fv = *base;
    fv.cam_pos = v3_add(v3_add(head, v3_mul(fwd, 1.05f)), v3_mul(up, 0.1f));
    fv.view = m4_look_at(fv.cam_pos, v3_sub(head, v3_mul(up, 0.06f)), up);
    fv.proj = m4_perspective(0.55f, 1.0f, 0.05f, 20.0f);
    render_portrait(slot, size, &fv, m4_mul(fv.proj, fv.view), items, weapon ? 2 : 1);
}

// Тело в бою поля: поза из состояния боя. act == NULL — вне боя, только походка
static const CharAction *field_action(const Field *f, u32 slot, CharPose *p, CharAction *act)
{
    if (!field_awake(f, slot)) return NULL;
    const CombatWorld *w = &f->w;
    u8 st = w->state[slot];
    f32 prog = w->dur[slot] > 0.0f ? clampf(1.0f - (w->timer[slot] - f->acc) / w->dur[slot], 0.0f, 1.0f) : 1.0f;
    bool lying = st == FS_KNOCKED || st == FS_DOWN || st == FS_UNCONSCIOUS || st == FS_CRAWL || st == FS_DEAD;
    u8 wc = g_weapons[w->weapon[slot]].wclass;
    *act = (CharAction){
        .guard = w->disarmed[slot] || fs_out(st) ? 0.0f : 1.0f, .attack = w->attack[slot],
        .swing = st == FS_WINDUP ? prog : (st == FS_STRIKE ? 1.0f + prog : (st == FS_RECOVER ? 2.0f + prog : 0.0f)),
        .block = st == FS_BLOCK, .dodge = st == FS_DODGE, .stagger = st == FS_STAGGER,
        .down = st == FS_GETUP ? 1.0f - prog : (lying ? 1.0f : 0.0f), .down_dir = st == FS_CRAWL ? -1.0f : 1.0f,
        .kneel = st == FS_SURRENDER, .crawl = st == FS_CRAWL, .limp = st == FS_UNCONSCIOUS || st == FS_DEAD,
        .severed = w->severed[slot], .two_hand = wc == WC_POLEARM || wc == WC_HEAVY,
    };
    // Шаг в бою по ходу симуляции: тело уже стоит там, где его поставило поле
    f32 dx = w->x[slot] - f->px[slot], dz = w->z[slot] - f->pz[slot];
    f32 v = sqrtf(dx * dx + dz * dz) / COMBAT_DT;
    p->walk = lying ? 0.0f : clampf(v / 1.4f, 0.0f, 1.0f);
    p->run = 0.0f;
    p->phase = p->time * v / CHAR_STRIDE;
    p->phase -= floorf(p->phase);
    if (v > 0.15f) p->move_dir = atan2f(dx, dz) - w->heading[slot];
    p->lean_fwd = p->lean_side = p->look = 0.0f;
    return act;
}

// Ближе 4 м камера подходит только к ряду людей (меню НАСТРОЙКИ, крупные планы)
static f32 g_cam_min_dist = 4.0f;

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
    c->dist = clampf(c->dist, g_cam_min_dist, 120.0f);
    c->pitch = clampf(c->pitch, 0.08f, 1.45f);
}

// Замер кадра (меню НАСТРОЙКИ или --bench): камера у арены вплотную, утро, сутки стоят, vsync выкл.
// BENCH_WARMUP_S на прогрев, затем BENCH_S секунд замера и итог в лог
#define BENCH_WARMUP_S 2.0
#define BENCH_S 10.0
#define BENCH_MAX_FRAMES 200000
#define BENCH_CAM ((RtsCamera){ .target = ARENA_CENTER, .yaw = -0.5f, .pitch = 0.42f, .dist = 4.0f })
#define BENCH_TOD 0.31f

typedef struct Bench {
    bool on;
    f64  start, measure_from;     // начало прогрева и замера
    f64  last_now;                // начало предыдущего кадра замера, 0 — замер ещё не начат
    u32  frames, cpu_frames;
    f32 *frame_ms;                // BENCH_MAX_FRAMES из постоянной арены
    f64  cpu_logic, cpu_submit, cpu_swap;
    // Что вернуть после замера
    RtsCamera cam;
    f32  tod;
    bool day_run, vsync, follow;
} Bench;

static int cmp_f32(const void *a, const void *b)
{
    f32 x = *(const f32 *)a, y = *(const f32 *)b;
    return (x > y) - (x < y);
}

// Время проходов GPU одной строкой; драйвер может не отдавать метки времени (например, D3D12 в WSL)
static void gpu_line(char *out, size_t size, const RenderStats *st)
{
    if (st->samples && st->gpu_total_ms > 0.0)
        snprintf(out, size, "GPU %.3f: тени %.3f, глубина %.3f, сцена %.3f, атмосфера %.3f, стиль %.3f, апскейл %.3f, интерфейс %.3f",
                 st->gpu_total_ms, st->gpu_ms[PASS_SHADOW], st->gpu_ms[PASS_DEPTH], st->gpu_ms[PASS_SCENE],
                 st->gpu_ms[PASS_ATMOS], st->gpu_ms[PASS_STYLE], st->gpu_ms[PASS_BLIT], st->gpu_ms[PASS_UI]);
    else
        snprintf(out, size, "GPU: драйвер не отдаёт метки времени");
}

static void bench_report(Bench *b, f64 seconds, i32 ww, i32 wh)
{
    RenderStats st;
    render_take_stats(&st);
    u32 n = b->frames;
    if (!n || !b->cpu_frames) return;
    f64 sum = 0;
    for (u32 i = 0; i < n; i++) sum += b->frame_ms[i];
    qsort(b->frame_ms, n, sizeof *b->frame_ms, cmp_f32);
    f64 k = 1000.0 / b->cpu_frames;
    log_stat("ЗАМЕР: окно %dx%d, %u кадров за %.1f с | кадр: среднее %.3f мс (%.0f к/с), медиана %.3f, худший 1%% %.3f",
             ww, wh, n, seconds, sum / n, n / seconds, b->frame_ms[n / 2], b->frame_ms[MIN(n - 1, n * 99 / 100)]);
    char gpu[256];
    gpu_line(gpu, sizeof gpu, &st);
    log_stat("ЗАМЕР: CPU логика %.3f, отправка %.3f, swap %.3f | %s", b->cpu_logic * k, b->cpu_submit * k, b->cpu_swap * k, gpu);
    log_stat("ЗАМЕР: сцена: видно %.0f, вызовов %.0f, треугольников %.0fk | тени: видно %.0f, вызовов %.0f, треугольников %.0fk | шейдинг блоками: %s",
             st.visible[1], st.draws[1], st.tris[1] / 1000.0, st.visible[0], st.draws[0], st.tris[0] / 1000.0,
             st.vrs ? "да" : "нет");
}

static V3 camera_eye(const RtsCamera *c)
{
    V3 off = v3(sinf(c->yaw) * cosf(c->pitch), sinf(c->pitch), cosf(c->yaw) * cosf(c->pitch));
    V3 eye = v3_add(c->target, v3_mul(off, c->dist));
    // У ряда людей камера кружит вокруг точки взгляда (на 1 м выше цели), иначе вблизи смотрит снизу
    if (g_cam_min_dist < 4.0f) eye.y += 1.0f;
    return eye;
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
    puts("anathemyre [--size W H] [--seed N] [--novsync] [--follow] [--inspect] [--tod 0..1] [--cam X Z YAW PITCH DIST] [--shot file.ppm] [--frames N] [--bench] [--attack K]");
    puts("anathemyre --move-bench [trace.csv]   развороты шагом, трусцой и бегом без окна");
    puts("anathemyre --arena-bench N [--arena-log K] [--seed N]   бои арены без окна");
    puts("anathemyre --battle-bench A B [--threads N] [--battle-verify] [--seed N]   полевой бой без окна");
    puts("anathemyre --field-bench N [--seed N]   приказ атаки без окна, затем нагрузка поля на N тел");
}

static bool parse_args(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--shot") && i + 1 < argc) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) o->shot_frames = atoi(argv[++i]);
        else if (!strcmp(a, "--novsync")) o->no_vsync = true;
        else if (!strcmp(a, "--bench")) o->bench = true;
        else if (!strcmp(a, "--follow")) o->follow = true;
        else if (!strcmp(a, "--tod") && i + 1 < argc) o->tod = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--inspect")) o->inspect = true;
        else if (!strcmp(a, "--move-bench")) {
            o->move_bench = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') o->move_trace = argv[++i];
        } else if (!strcmp(a, "--humans")) o->humans = true;
        else if (!strcmp(a, "--no-pixel")) o->no_pixel = true;
        else if (!strcmp(a, "--no-vrs")) o->no_vrs = true;
        else if (!strcmp(a, "--day-stop")) o->day_stop = true;
        else if (!strcmp(a, "--cam-y") && i + 1 < argc) o->cam.target.y = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--attack") && i + 1 < argc) o->attack = atoi(argv[++i]) + 1;
        else if (!strcmp(a, "--field-bench") && i + 1 < argc) o->field_bench = (u32)atoi(argv[++i]) + 1;
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
        .tod = 0.31f, .shot_frames = 90, .width = 1280, .height = 720, .seed = 1337,
        .cam = { .target = { 14, 0, 32 }, .yaw = -0.5f, .pitch = 0.42f, .dist = 17.0f },
    };
    if (!parse_args(argc, argv, &opt)) return 1;
    if (opt.move_bench) return player_bench(opt.move_trace);
    if (opt.field_bench) return field_bench(opt.field_bench - 1, opt.seed);
    if (opt.arena_bench) return arena_bench(opt.arena_bench, opt.seed, opt.arena_log);
    if (opt.battle[0] || opt.battle[1])
        return battle_bench(opt.battle[0], opt.battle[1], opt.seed, opt.threads, opt.battle_verify);
    // Дальше в консоль — только к/с, замеры и сбои; остальное в журнал на экране
    log_set_echo(false);
    log_info("АНАТЕМАЙР. ЭТАП 0. ЗЕРНО МИРА %llu", (unsigned long long)opt.seed);

    char data_dir[512];
    if (!find_data_dir(data_dir, sizeof data_dir)) {
        log_error("ДАННЫЕ: папка data/shaders не найдена");
        return 1;
    }

    // Вся память выделяется здесь, один раз
    Arena perm, scratch;
    if (!arena_init(&perm, MB(96)) || !arena_init(&scratch, MB(4))) {
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
    if (!render_init(&scratch, data_dir) || !ui_init()) {
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

    // Тела людей; грубые уровни лежат отдельно: остальной код видит только полный (body_mesh)
    GpuMesh body_mesh[BODY_KINDS], body_lod[BODY_KINDS][CHAR_LODS - 1], weapon_mesh[W_COUNT];
    for (u32 k = 0; k < BODY_KINDS; k++)
        for (u32 l = 0; l < CHAR_LODS; l++) {
            char path[700], suf[16] = "";
            if (l) snprintf(suf, sizeof suf, "_lod%u", l);
            snprintf(path, sizeof path, "%s/meshes/%s%s.mesh", data_dir, g_body_file[k], suf);
            if (!mb_load(&mb, &perm, path)) {
                render_shutdown();
                platform_shutdown();
                return 1;
            }
            GpuMesh *gm = l ? &body_lod[k][l - 1] : &body_mesh[k];
            gpu_mesh_upload(gm, &mb);
            if (l) gpu_mesh_set_lod(l == 1 ? &body_mesh[k] : &body_lod[k][l - 2], gm, g_char_lod_px[l - 1]);
            perm.used = mark;
        }
    for (u32 k = 0; k < W_COUNT; k++) {
        mb_init(&mb, &perm, 4000, 12000);
        weapon_build(&mb, (u8)k);
        gpu_mesh_upload(&weapon_mesh[k], &mb);
        perm.used = mark;
    }

    // Люди из MakeHuman (tools/humanconv): у каждого три LOD рядом, для сравнения
    enum { HUMAN_N = BODY_KINDS * CHAR_LODS };
    GpuMesh human_mesh[HUMAN_N];
    u32 human_count = 0;
    M4 *human_bones = ARENA_PUSH_ARRAY(&perm, M4, HUMAN_N * RENDER_MAX_BONES);
    mark = perm.used;
    bool humans_on = false, humans_flip = opt.humans;

    // Метка точки, куда идёт персонаж: бледно-зелёный крест на земле
    GpuMesh marker_mesh;
    mb_init(&mb, &perm, 200, 600);
    mb_set(&mb, v3(0.35f, 0.9f, 0.45f), MAT_EMISSIVE, 0);
    mb_box(&mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    mb_box(&mb, m4_mul(m4_translate(v3(0, 0.02f, 0)), m4_mul(m4_rot_y(-PI_F * 0.25f), m4_scale(v3(0.5f, 0.02f, 0.06f)))));
    gpu_mesh_upload(&marker_mesh, &mb);
    perm.used = mark;

    // Кольца под ногами: свой выбранный — бледно-зелёное, осматриваемый чужой — охра, цель атаки — красное
    GpuMesh ring_mesh[3];
    static const V3 ring_color[3] = { { 0.1f, 0.42f, 0.16f }, { 0.5f, 0.3f, 0.08f }, { 0.62f, 0.06f, 0.04f } };
    for (u32 k = 0; k < 3; k++) {
        mb_init(&mb, &perm, 600, 1800);
        mb_set(&mb, ring_color[k], MAT_EMISSIVE, 0);
        for (u32 j = 0; j < 20; j++) {
            f32 a = (f32)j * (2.0f * PI_F / 20.0f);
            mb_box(&mb, m4_mul(m4_translate(v3(sinf(a) * 0.48f, 0.03f, cosf(a) * 0.48f)),
                               m4_mul(m4_rot_y(a), m4_scale(v3(0.09f, 0.015f, 0.025f)))));
        }
        gpu_mesh_upload(&ring_mesh[k], &mb);
        perm.used = mark;
    }

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
    // Свой отряд появляется у края зала, лицом к арене
    Squad *squad = ARENA_PUSH_ARRAY(&perm, Squad, 1);
    squad_init(squad, &rng, v3(9.0f, 0.0f, 22.0f), atan2f(ARENA_CENTER.x - 9.0f, ARENA_CENTER.z - 22.0f), ground_height);
    // Поле боя: свой отряд и бродящие. Бродящие каждый сам по себе: за чужого не вступаются
    Field *field = ARENA_PUSH_ARRAY(&perm, Field, 1);
    field_init(field, &perm, opt.seed);
    for (u32 i = 0; i < squad->count; i++) field_add(field, &squad->body[i], &squad->sheet[i], 0, FF_PLAYER | FF_ASSIST);
    for (u32 i = 0; i < crowd.count; i++) field_add(field, &crowd.body[i], &crowd.sheet[i], (u8)(2 + i), 0);
    // На каждого: тело, кольцо, метка цели; плюс кольцо осматриваемого чужого и кольца целей атаки
    M4 *squad_bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)(SQUAD_MAX * 4 + 1) * RENDER_MAX_BONES);
    M4 *portrait_bones = ARENA_PUSH_ARRAY(&perm, M4, 2 * RENDER_MAX_BONES);
    M4 *bones = ARENA_PUSH_ARRAY(&perm, M4, (size_t)TR_MAX_CHARS * RENDER_MAX_BONES);
    DrawItem *items = ARENA_PUSH_ARRAY(&perm, DrawItem, TR_MAX_CHARS * 2 + 2 + AV_DRAW_ITEMS + SQUAD_MAX * 5 + 1);
    Hud hud;
    hud_init(&hud);
    if (opt.inspect) {
        for (u32 i = 0; i < squad->count; i++) squad->selected[i] = true;
        hud_look(&hud, LOOK_SQUAD, 0);
        hud.win_inv = hud.win_char = true;
    }
    if (opt.attack > 0 && (u32)opt.attack <= crowd.count)
        for (u32 i = 0; i < squad->count; i++) {
            squad->selected[i] = true;
            field_attack(field, (u32)squad->body[i].slot, (u32)crowd.body[opt.attack - 1].slot);
        }
    u8 *shot_buf = NULL;
    if (opt.shot_path) shot_buf = arena_push(&perm, (size_t)opt.width * (size_t)opt.height * 3, 1);
    for (u32 i = 0; i < TR_MAX_CHARS * RENDER_MAX_BONES; i++) bones[i] = m4_identity();

    RtsCamera cam = opt.cam;
    RenderSettings rs = { .post = true, .pixel = !opt.no_pixel, .vrs = !opt.no_vrs };

    Input in;
    memset(&in, 0, sizeof in);
    f64 last = time_now();
    f64 stat_t = last;
    u32 stat_frames = 0;
    f32 shown_ms = 0, shown_fps = 0;  // для отладочного меню
    f32 ui_user = 1.0f, ui_user_text = 1.0f;  // ползунки размера интерфейса
    f64 fps_t = last;                 // счётчик кадров обновляется чаще, чем строка КАДР
    u32 fps_frames = 0;
    f64 cpu_logic = 0, cpu_submit = 0, cpu_swap = 0;
    bool vsync = !opt.no_vsync && opt.shot_path == NULL;
    f32 sim_time = 0, real_time = 0;
    f32 tod = opt.tod;
    u32 day = 1;
    bool day_run = !opt.day_stop;
    u32 frame = 0;

    Bench bench = { .frame_ms = ARENA_PUSH_ARRAY(&perm, f32, BENCH_MAX_FRAMES) };
    bool bench_request = opt.bench && !opt.shot_path;

    log_info("УПРАВЛЕНИЕ: WASD — сдвиг, колесо — дистанция, ЛКМ — выбор и рамка, ПКМ — идти, I — инвентарь, P — персонаж, пробел — пауза, средняя кнопка и Q/E — поворот, F — следовать; остальное — в меню НАСТРОЙКИ слева вверху");

    while (!in.quit) {
        platform_poll(&in);
        // Esc сначала закрывает окна
        if (in.key_pressed[KEY_ESCAPE] && !hud_window_open(&hud)) break;
        if (in.key_pressed[KEY_F]) {
            opt.follow = !opt.follow;
            if (opt.follow) cam.dist = MIN(cam.dist, 8.0f);
        }
        // Ряд людей для сравнения (MakeHuman, LOD0..2); сетки грузятся при первом показе
        if (humans_flip) {
            humans_flip = false;
            humans_on = !humans_on;
            g_cam_min_dist = humans_on ? 0.8f : 4.0f;
            mark = perm.used;
            bool build = humans_on && human_count == 0;
            for (u32 k = 0; build && k < HUMAN_N; k++) {
                char path[700], suf[16] = "";
                u32 lod = k % CHAR_LODS;
                if (lod) snprintf(suf, sizeof suf, "_lod%u", lod);
                snprintf(path, sizeof path, "%s/meshes/%s%s.mesh", data_dir, g_body_file[k / CHAR_LODS], suf);
                if (!mb_load(&mb, &perm, path)) continue;
                gpu_mesh_upload(&human_mesh[human_count], &mb);
                log_info("ЧЕЛОВЕК %u: %u вершин, %u треугольников", k, mb.vcount, mb.icount / 3);
                perm.used = mark;
                f32 x = 0.3f + 2.4f * (f32)(k / CHAR_LODS) + 0.7f * (f32)lod;
                for (u32 b = 0; b < RENDER_MAX_BONES; b++)
                    human_bones[human_count * RENDER_MAX_BONES + b] = m4_translate(v3(x, ground_height(x, 22.0f), 22.0f));
                human_count++;
            }
        }
        if (bench_request && !bench.on && !opt.shot_path) {
            bench_request = false;
            bench.on = true;
            bench.cam = cam;
            bench.tod = tod;
            bench.day_run = day_run;
            bench.vsync = vsync;
            bench.follow = opt.follow;
            cam = BENCH_CAM;
            tod = BENCH_TOD;
            day_run = opt.follow = vsync = false;
            platform_set_vsync(false);
            bench.start = time_now();
            bench.measure_from = bench.start + BENCH_WARMUP_S;
            bench.last_now = 0;
            bench.frames = bench.cpu_frames = 0;
            bench.cpu_logic = bench.cpu_submit = bench.cpu_swap = 0;
            log_stat("ЗАМЕР: начат, %.0f с прогрева и %.0f с замера; камера у арены, управление камерой отключено", BENCH_WARMUP_S, BENCH_S);
        }

        f64 now = time_now();
        f32 dt = opt.shot_path ? 1.0f / 60.0f : (f32)fmin(now - last, 0.05);
        last = now;
        // Скорость времени из интерфейса; камера живёт в реальном времени
        f32 real_dt = dt;
        dt *= (f32)hud.speed;
        sim_time += dt;
        real_time += real_dt;

        f64 t_logic = time_now();
        if (!bench.on) camera_update(&cam, &in, real_dt);
        crowd_update(&crowd, &rng, dt);
        arena_view_update(aview, arena, dt);
        i32 lead = MAX(0, squad_primary(squad));
        if (opt.follow) cam.target = v3(squad->body[lead].pos.x, 0, squad->body[lead].pos.z);

        i32 ww, wh;
        platform_window_size(&ww, &wh);
        if (opt.shot_path) {
            ww = opt.width;
            wh = opt.height;
        }
        FrameView fv;
        fv.cam_pos = camera_eye(&cam);
        fv.view = m4_look_at(fv.cam_pos, v3_add(cam.target, v3(0, 1.0f, 0)), v3(0, 1, 0));
        fv.proj = m4_perspective(CAM_FOV, (f32)ww / (f32)(wh > 0 ? wh : 1), 0.3f, 6000.0f);

        ui_set_user_scale(ui_user, ui_user_text);
        ui_begin(ww, wh);
        // Отладочное меню слева вверху: все настройки, бывшие на F1–F10
        Input hin = in;
        if (hud_debug_begin(&hud, &in)) {
            char val[32];
            hud_debug_check(&hud, "ФИЛЬТР КАДРА", &rs.post);
            hud_debug_check(&hud, "ПИКСЕЛЬНОСТЬ", &rs.pixel);
            hud_debug_check(&hud, "ШЕЙДИНГ БЛОКАМИ", &rs.vrs);
            if (hud_debug_check(&hud, "VSYNC", &vsync)) {
                platform_set_vsync(vsync);
                log_info("VSYNC: %s", vsync ? "вкл" : "выкл");
            }
            if (hud_debug_check(&hud, "СУТКИ ИДУТ", &day_run)) log_info("СУТКИ: %s", day_run ? "идут" : "остановлены");
            if (hud_debug_check(&hud, "КАМЕРА СЛЕДУЕТ", &opt.follow) && opt.follow) cam.dist = MIN(cam.dist, 8.0f);
            bool humans_want = humans_on;
            if (hud_debug_check(&hud, "ЛЮДИ ДЛЯ СРАВНЕНИЯ", &humans_want)) {
                humans_flip = true;
                if (humans_want) cam = (RtsCamera){ .target = { 3.4f, 0, 22 }, .yaw = 0.0f, .pitch = 0.15f, .dist = 6.5f };
            }
            u32 mins = (u32)(tod * 24.0f * 60.0f);
            snprintf(val, sizeof val, "%02u:%02u", mins / 60 % 24, mins % 60);
            hud_debug_slider(&hud, "ВРЕМЯ СУТОК", &tod, 0.0f, 0.999f, val);
            static const char *speed_name[] = { "ПАУЗА", "x1", "x2", "x3" };
            f32 sp = (f32)hud.speed;
            if (hud_debug_slider(&hud, "СКОРОСТЬ", &sp, 0.0f, 3.0f, speed_name[hud.speed])) {
                hud.speed = (u8)(sp + 0.5f);
                if (hud.speed) hud.speed_before = hud.speed;
            }
            snprintf(val, sizeof val, "X%.2f", (f64)ui_scale());
            hud_debug_slider(&hud, "МАСШТАБ ИНТЕРФЕЙСА", &ui_user, 0.5f, 1.5f, val);
            snprintf(val, sizeof val, "X%.2f", (f64)ui_text_scale());
            hud_debug_slider(&hud, "РАЗМЕР ТЕКСТА", &ui_user_text, 0.5f, 1.5f, val);
            snprintf(val, sizeof val, "%.2f МС  %.0f К/С", (f64)shown_ms, (f64)shown_fps);
            hud_debug_text(&hud, "КАДР", val);
            hud_debug_check(&hud, "СЧЁТЧИК КАДРОВ", &hud.dbg.fps_on);
            if (hud_debug_button(&hud, "ПЕРЕЗАГРУЗИТЬ ШЕЙДЕРЫ")) {
                render_reload_shaders();
                memset(g_portrait_key, 0, sizeof g_portrait_key);
            }
            if (hud_debug_button(&hud, "ЗАМЕР КАДРА")) bench_request = true;
            hud_debug_check(&hud, "ЖУРНАЛ", &hud.dbg.journal);
        }
        if (hud_debug_end(&hud)) {
            memset(hin.mouse_pressed, 0, sizeof hin.mouse_pressed);
            memset(hin.mouse_down, 0, sizeof hin.mouse_down);
        }
        HudWorld hw = { .squad = squad, .arena = arena, .aview = aview, .field = field, .view_proj = m4_mul(fv.proj, fv.view),
                        .time = real_time, .tod = tod, .day = day };
        bool ui_busy = hud_input(&hud, &hin, &hw);
        if (hud.focus >= 0) cam.target = v3(squad->body[hud.focus].pos.x, 0, squad->body[hud.focus].pos.z);

        // ПКМ по чужому: выбранные атакуют его. ПКМ по земле: идут туда, бросив бой и погоню
        if (hin.mouse_pressed[MOUSE_RIGHT] && !ui_busy && ww > 0 && wh > 0) {
            u8 pk;
            u32 pi;
            bool on_body = hud_pick(&hud, &hin, &hw, &pk, &pi);
            if (on_body && pk == LOOK_FIELD) {
                for (u32 i = 0; i < squad->count; i++)
                    if (squad->selected[i]) field_attack(field, (u32)squad->body[i].slot, pi);
            } else if (!on_body || pk != LOOK_SQUAD) {
                f32 nx = 2.0f * ((f32)in.mouse_x + 0.5f) / (f32)ww - 1.0f;
                f32 ny = 1.0f - 2.0f * ((f32)in.mouse_y + 0.5f) / (f32)wh;
                V3 hit;
                if (pick_ground(hw.view_proj, fv.cam_pos, nx, ny, &hit)) {
                    for (u32 i = 0; i < squad->count; i++)
                        if (squad->selected[i]) field_cancel(field, (u32)squad->body[i].slot, true);
                    squad_order_move(squad, hit);
                }
            }
        }
        // Z шаг, X трусца, C бег — выбранным; G — держать строй; H — стоять: бросить приказ, но отбиваться
        if (!hud_window_open(&hud)) {
            if (in.key_pressed[KEY_H])
                for (u32 i = 0; i < squad->count; i++) {
                    Player *p = &squad->body[i];
                    if (!squad->selected[i]) continue;
                    field_cancel(field, (u32)p->slot, false);
                    if (p->ctrl == CTRL_FREE) {
                        p->target = p->pos;
                        p->moving = false;
                    }
                }
            if (in.key_pressed[KEY_Z]) squad_set_gait(squad, GAIT_WALK);
            if (in.key_pressed[KEY_X]) squad_set_gait(squad, GAIT_JOG);
            if (in.key_pressed[KEY_C]) squad_set_gait(squad, GAIT_RUN);
            if (in.key_pressed[KEY_G]) squad->formation = !squad->formation;
        }
        squad_update(squad, dt, ground_height);
        field_update(field, dt, ground_height);

        u32 n = 0;
        items[n++] = (DrawItem){ .mesh = &near_mesh };
        items[n++] = (DrawItem){ .mesh = &far_mesh, .no_shadow = true };
        for (u32 i = 0; i < crowd.count; i++) {
            const Player *cp = &crowd.body[i];
            const CharSheet *cs = &crowd.sheet[i];
            CharPose p = {
                .pos = cp->pos, .heading = cp->heading, .phase = cp->phase, .walk = cp->walk, .run = cp->run,
                .time = sim_time, .seed = crowd.seed[i],
                .lean_fwd = cp->lean_fwd, .lean_side = cp->lean_side, .look = cp->look,
                // Разброс сложения внутри класса H: рост ±6%, ширина ±8%
                .height = 1.0f + 0.06f * sinf(crowd.seed[i] * 7.31f),
                .width = 1.0f + 0.08f * sinf(crowd.seed[i] * 3.17f + 1.0f),
            };
            M4 *b = &bones[i * RENDER_MAX_BONES];
            CharAction act;
            character_pose(&p, field_action(field, (u32)cp->slot, &p, &act), b);
            items[n++] = (DrawItem){ .mesh = &body_mesh[crowd.variant[i] % BODY_KINDS], .bones = b };
            if (cs->equip[SLOT_WEAPON].kind == IK_WEAPON)
                items[n++] = (DrawItem){ .mesh = &weapon_mesh[cs->equip[SLOT_WEAPON].id], .bones = b };
        }
        n += arena_view_draw(aview, arena, sim_time, body_mesh, BODY_KINDS, weapon_mesh, arena_bones, items + n);
        for (u32 i = 0; i < squad->count; i++) {
            const Player *pl = &squad->body[i];
            const CharSheet *cs = &squad->sheet[i];
            M4 *b = squad_bones + (size_t)i * 4 * RENDER_MAX_BONES;
            CharPose p = {
                .pos = pl->pos, .heading = pl->heading, .phase = pl->phase, .walk = pl->walk, .run = pl->run,
                .time = sim_time, .seed = 7.0f + (f32)i * 5.3f, .height = cs->d.height / 1.8f,
                .lean_fwd = pl->lean_fwd, .lean_side = pl->lean_side, .look = pl->look,
            };
            CharAction act;
            character_pose(&p, field_action(field, (u32)pl->slot, &p, &act), b);
            items[n++] = (DrawItem){ .mesh = &body_mesh[squad->variant[i] % BODY_KINDS], .bones = b };
            if (cs->equip[SLOT_WEAPON].kind == IK_WEAPON)
                items[n++] = (DrawItem){ .mesh = &weapon_mesh[cs->equip[SLOT_WEAPON].id], .bones = b };
            if (squad->selected[i]) {
                M4 *rb = b + RENDER_MAX_BONES;
                rb[0] = m4_translate(pl->pos);
                items[n++] = (DrawItem){ .mesh = &ring_mesh[0], .bones = rb, .no_shadow = true };
                // Красное кольцо под целью выбранного; у нескольких одна цель — кольцо одно
                i32 t = field_target(field, (u32)pl->slot);
                bool first = t >= 0;
                for (u32 j = 0; j < i && first; j++)
                    if (squad->selected[j] && field_target(field, (u32)squad->body[j].slot) == t) first = false;
                if (first) {
                    M4 *tb = b + 3 * RENDER_MAX_BONES;
                    tb[0] = m4_translate(field->body[t]->pos);
                    items[n++] = (DrawItem){ .mesh = &ring_mesh[2], .bones = tb, .no_shadow = true };
                }
            }
        }
        if (squad->order_marker > 0.0f) {
            M4 *mk = squad_bones + 2 * RENDER_MAX_BONES;
            mk[0] = m4_translate(squad->order_pos);
            items[n++] = (DrawItem){ .mesh = &marker_mesh, .bones = mk, .no_shadow = true };
        }
        for (u32 k = 0; humans_on && k < human_count; k++)
            items[n++] = (DrawItem){ .mesh = &human_mesh[k], .bones = human_bones + k * RENDER_MAX_BONES };
        V3 look_pos;
        bool look_field = hud.look_kind == LOOK_FIELD && hud.look < field->w.count;
        if (look_field) look_pos = field->body[hud.look]->pos;
        if (look_field || (hud.look_kind == LOOK_ARENA && hud_arena_pos(aview, arena, hud.look, &look_pos))) {
            M4 *rb = squad_bones + (size_t)SQUAD_MAX * 4 * RENDER_MAX_BONES;
            rb[0] = m4_translate(look_pos);
            items[n++] = (DrawItem){ .mesh = &ring_mesh[1], .bones = rb, .no_shadow = true };
        }

        f64 t_submit = time_now();
        cpu_logic += t_submit - t_logic;
        if (day_run) {
            tod += dt / DAY_LENGTH_S;
            if (tod >= 1.0f) {
                tod -= 1.0f;
                day++;
            }
        }
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
        // Полное разрешение окна; размер пикселя задаёт глубина по одному правилу (style.frag)
        rs.internal_h = wh;
        render_frame(&fv, items, n, &rs, ww, wh);
        // Портреты: миниатюры отряда и крупно выбранный
        for (u32 i = 0; i <= squad->count; i++) {
            bool focus = i == squad->count;
            u32 k = focus ? hud.look : i;
            M4 *pb = portrait_bones + (size_t)(focus ? 1 : 0) * RENDER_MAX_BONES;
            if (focus && hud.look_kind == LOOK_ARENA) {
                const FighterDesc *d = &arena->roster[k].d;
                f32 seed = (f32)k * 13.7f + 3.0f;
                portrait(&fv, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX, &body_mesh[k % BODY_KINDS], &weapon_mesh[d->weapon],
                         seed, d->height / 1.8f, 1.0f + 0.08f * sinf(seed * 3.17f + 1.0f), pb);
                continue;
            }
            if (focus && hud.look_kind == LOOK_FIELD) {
                // Чужой на поле: из бродящих тот, чьё тело в этом слоте
                for (u32 c = 0; c < crowd.count; c++) {
                    if (crowd.body[c].slot != (i32)k) continue;
                    const CharSheet *cs = &crowd.sheet[c];
                    const GpuMesh *wm = cs->equip[SLOT_WEAPON].kind == IK_WEAPON ? &weapon_mesh[cs->equip[SLOT_WEAPON].id] : NULL;
                    portrait(&fv, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX, &body_mesh[crowd.variant[c] % BODY_KINDS], wm,
                             crowd.seed[c], 1.0f + 0.06f * sinf(crowd.seed[c] * 7.31f), 1.0f + 0.08f * sinf(crowd.seed[c] * 3.17f + 1.0f), pb);
                }
                continue;
            }
            if (focus && (hud.look_kind != LOOK_SQUAD || k >= squad->count)) continue;
            const CharSheet *cs = &squad->sheet[k];
            const GpuMesh *wm = cs->equip[SLOT_WEAPON].kind == IK_WEAPON ? &weapon_mesh[cs->equip[SLOT_WEAPON].id] : NULL;
            portrait(&fv, focus ? HUD_PORTRAIT_FOCUS : i, focus ? HUD_PORTRAIT_FOCUS_PX : HUD_PORTRAIT_CARD_PX,
                     &body_mesh[squad->variant[k] % BODY_KINDS], wm, 7.0f + (f32)k * 5.3f, cs->d.height / 1.8f, 0.0f, pb);
        }
        ui_set_image(render_portrait_texture());
        ui_set_time(real_time);
        hud_draw(&hud, &hin, &hw);
        ui_end();
        render_frame_end();
        f64 t_swap = time_now();
        cpu_submit += t_swap - t_submit;

        frame++;
        if (opt.shot_path && (i32)frame >= opt.shot_frames) {
            render_read_pixels(shot_buf, ww, wh);
            if (write_ppm(opt.shot_path, shot_buf, ww, wh)) log_stat("КАДР: записан %s", opt.shot_path);
            else log_error("КАДР: не записан %s", opt.shot_path);
            break;
        }
        platform_swap();
        f64 t_end = time_now();
        cpu_swap += t_end - t_swap;

        if (bench.on && now >= bench.measure_from) {
            // Время кадра — от начала прошлого кадра до начала этого; счётчики рендера — с начала замера
            if (bench.last_now == 0) {
                render_take_stats(&(RenderStats){ 0 });
            } else {
                if (bench.frames < BENCH_MAX_FRAMES) bench.frame_ms[bench.frames++] = (f32)((now - bench.last_now) * 1000.0);
                bench.cpu_frames++;
                bench.cpu_logic += t_submit - t_logic;
                bench.cpu_submit += t_swap - t_submit;
                bench.cpu_swap += t_end - t_swap;
            }
            bench.last_now = now;
            if (now - bench.measure_from >= BENCH_S) {
                bench_report(&bench, now - bench.measure_from, ww, wh);
                bench.on = false;
                if (opt.bench) break;
                cam = bench.cam;
                tod = bench.tod;
                day_run = bench.day_run;
                opt.follow = bench.follow;
                vsync = bench.vsync;
                platform_set_vsync(vsync);
                stat_t = time_now();
                stat_frames = 0;
                cpu_logic = cpu_submit = cpu_swap = 0;
            }
            continue;
        }

        stat_frames++;
        fps_frames++;
        if (now - fps_t >= 0.5) {
            hud.dbg.fps = (f32)(fps_frames / (now - fps_t));
            hud.dbg.fps_ms = (f32)((now - fps_t) * 1000.0 / fps_frames);
            fps_t = now;
            fps_frames = 0;
        }
        if (now - stat_t >= 2.0) {
            RenderStats st;
            render_take_stats(&st);
            f64 k = 1000.0 / stat_frames;
            char gpu[256];
            gpu_line(gpu, sizeof gpu, &st);
            log_stat("КАДР: %.2f мс, %.0f к/с | CPU: логика %.3f, отправка %.3f, swap %.3f | %s",
                     (now - stat_t) * k, stat_frames / (now - stat_t),
                     cpu_logic * k, cpu_submit * k, cpu_swap * k, gpu);
            log_stat("ОТРИСОВКА: элементов %u | сцена: видно %.0f, вызовов %.0f, треугольников %.0fk | тени: видно %.0f, вызовов %.0f, треугольников %.0fk",
                     st.items, st.visible[1], st.draws[1], st.tris[1] / 1000.0, st.visible[0], st.draws[0], st.tris[0] / 1000.0);
            shown_ms = (f32)((now - stat_t) * k);
            shown_fps = (f32)(stat_frames / (now - stat_t));
            stat_t = now;
            stat_frames = 0;
            cpu_logic = cpu_submit = cpu_swap = 0;
        }
    }

    ui_shutdown();
    render_shutdown();
    platform_shutdown();
    arena_free(&scratch);
    arena_free(&perm);
    log_info("СЕАНС ЗАВЕРШЁН");
    return 0;
}
