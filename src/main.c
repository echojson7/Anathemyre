// Anathemyre — этап 0: тестовая комната. Здесь вход: разбор аргументов, проверки без окна, окно и цикл кадра.
// Мир и его шаг — world.c, люди — people.c, позы и отрисовка людей — figures.c, камера — camera.c,
// меню и приказы — controls.c, замер кадра — bench.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/audio.h"
#include "core/arena.h"
#include "core/clock.h"
#include "core/jobs.h"
#include "core/log.h"
#include "core/math3d.h"
#include "core/prof.h"
#include "defs/defs.h"
#include "game/anim.h"
#include "game/arena.h"
#include "game/battle.h"
#include "game/bench.h"
#include "game/camera.h"
#include "game/controls.h"
#include "game/editor.h"
#include "game/field.h"
#include "game/figures.h"
#include "game/hud.h"
#include "game/humans_row.h"
#include "game/look.h"
#include "game/showhall.h"
#include "game/stand.h"
#include "game/sfx.h"
#include "game/testroom.h"
#include "game/world.h"
#include "platform/platform.h"
#include "render/rb.h"
#include "render/render.h"
#include "render/ui.h"
#include "world/map.h"
#include "world/sky.h"

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
    bool        editor;      // выбрать первого из отряда и открыть ВНЕШНОСТЬ (для снимков)
    i32         look_slot;   // для снимков: осмотреть чужого в слоте поля look_slot−1
    bool        bands_test;  // пробные отряды вдали сразу при старте
    u32         phys_drop;   // куча лома у точки камеры на первом шаге (проверка физики)
    u32         loot_drop;   // вещи (оружие, товар) у точки камеры на первом шаге
    f32         warp;        // для снимков: прогнать столько секунд мира до первого кадра
    const char *save_path;   // записать мир после прогона (--warp)
    const char *load_path;   // прочитать мир до прогона
    const char *defs_dir, *mods_dir;  // описания вещей и моды вместо data/defs и mods
    f32         sum_after;   // проверка сохранений: ещё столько секунд мира, сумма мира в консоль и выход
    bool        sum_set;
    u32         arena_bench; // прогнать N боёв арены без окна
    u32         field_bench; // сценка атаки и нагрузка поля на N−1 тел без окна
    i32         attack;      // для снимков: весь отряд сразу атакует бродящего номер attack−1
    u32         arena_log;   // сколько первых боёв печатать подробно
    u32         bands_bench; // отряды вдали без окна: столько отрядов
    f32         bands_hours; // часов дальнего мира
    u32         bands_log;   // сколько первых событий печатать
    u32         battle[2];   // полевой бой: бойцов в каждом отряде
    bool        battle_verify;
    u32         threads;     // потоков вместе с главным (позы людей, бой); 0 — по числу ядер
    bool        bench;       // замер кадра сразу после старта, затем выход
    bool        bench_stages; // замер по этапам сцены сразу после старта, затем выход
    u32         stage;       // этап сцены на старте (SceneStage), по умолчанию полная
    bool        move_bench;  // проверка разворотов без окна
    const char *move_trace;
    bool        no_pixel;    // кадр без пиксельности (сравнение моделей)
    bool        no_vrs;      // без шейдинга блоками (сравнение скорости и кадра)
    bool        ao_half;     // AO в половинном разрешении
    bool        noise_tex;   // шум материалов из 3D-текстуры (по умолчанию; --hash-noise — прежний счёт хешами)
    bool        no_mdi;      // люди по вызову на сетку (сравнение с одним вызовом на проход)
    bool        no_skin_cache; // позы людей в каждом проходе (сравнение с кешем поз)
    i32         light_preset;  // набор СВЕТ на старте (LightPreset)
    i32         light[9];      // части СВЕТ на старте: материалы, кривая, экспозиция, свечение, отражённый, каскады, мягкий край, контактные, углы; -1 — из набора
    bool        fullscreen;  // окно без рамки на весь монитор
    bool        day_stop;    // сутки стоят на --tod
    bool        humans;      // ряд людей для сравнения сразу при старте (иначе из меню НАСТРОЙКИ)
    i32         stand;       // стенд фигур сразу при старте: ряд + 1 (0 — нет)
    f32         hall_t;      // сдвиг часов Мерильни, с (--hall-t): кадр нужной фазы
    bool        valley;      // начать в долине (иначе пешком или переход из меню НАСТРОЙКИ)
    bool        mute;        // без звука
    const char *sound_demo;  // сценка звука в WAV без окна
    i32         api;         // рендер: -1 — из anathemyre.cfg, иначе RbApi (--gl, --vk)
} Options;

// Замер кадра (меню НАСТРОЙКИ или --bench): камера у арены вплотную, утро, сутки стоят, vsync выкл.
#define BENCH_CAM ((RtsCamera){ .target = ARENA_CENTER, .yaw = -0.5f, .pitch = 0.42f, .dist = 4.0f })
// В долине: отряд у реки, вверх по течению видны акведук, роща и трава
#define VALLEY_BENCH_CAM(sp) ((RtsCamera){ .target = (sp), .yaw = 0.35f, .pitch = 0.32f, .dist = 22.0f })
#define BENCH_TOD 0.31f
// Камера не дальше этого от главного в отряде, м (кроме снимков --shot)
#define CAM_LEASH 1000.0f

// Что вернуть после замера
typedef struct BenchRestore {
    RtsCamera cam;
    f32  tod;
    bool day_run, vsync, follow;
} BenchRestore;

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
    puts("anathemyre [--size W H] [--seed N] [--novsync] [--follow] [--inspect] [--editor] [--tod 0..1] [--cam X Z YAW PITCH DIST] [--shot file.ppm] [--frames N] [--bench] [--bench-stages] [--stage 0..12] [--attack K] [--look-slot N] [--valley] [--bands-test] [--phys-drop N] [--loot-drop N] [--warp S] [--threads N] [--hall-t S]");
    puts("anathemyre [--light 0..2] [--light-parts МАТЕРИАЛЫ КРИВАЯ ЭКСПОЗИЦИЯ СВЕЧЕНИЕ ОТРАЖЁННЫЙ КАСКАДЫ МЯГКИЙ КОНТАКТ УГЛЫ]   свет на старте: набор ПРЕЖНИЙ/БЫСТРЫЙ/РЕАЛИСТИЧНЫЙ или части (docs/lighting.md)");
    puts("anathemyre [--load file.sav] [--warp S] [--save file.sav] [--sum-after S]   сохранение без меню: прочитать, прогнать, записать; сумма мира через S секунд и выход");
    puts("anathemyre [--defs DIR] [--mods DIR]   описания вещей и моды из других папок (по умолчанию data/defs и mods, docs/content.md)");
    puts("anathemyre --move-bench [trace.csv]   развороты шагом, трусцой и бегом без окна");
    puts("anathemyre --arena-bench N [--arena-log K] [--seed N]   бои арены без окна");
    puts("anathemyre --battle-bench A B [--threads N] [--battle-verify] [--seed N]   полевой бой без окна");
    puts("anathemyre --field-bench N [--seed N]   приказ атаки без окна, затем нагрузка поля на N тел");
    puts("anathemyre --bands-bench N [--hours H] [--bands-log K] [--seed N]   отряды вдали без окна: N отрядов, H часов мира");
    puts("anathemyre --sound-demo out.wav   сценка звука (ветер, шаги, бой, даль) в файл и замер микшера; --mute — играть без звука");
}

static bool parse_args(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--shot") && i + 1 < argc) o->shot_path = argv[++i];
        else if (!strcmp(a, "--frames") && i + 1 < argc) o->shot_frames = atoi(argv[++i]);
        else if (!strcmp(a, "--novsync")) o->no_vsync = true;
        else if (!strcmp(a, "--bench")) o->bench = true;
        else if (!strcmp(a, "--bench-stages")) o->bench = o->bench_stages = true;
        else if (!strcmp(a, "--stage") && i + 1 < argc) o->stage = (u32)atoi(argv[++i]) % STAGE_COUNT;
        else if (!strcmp(a, "--follow")) o->follow = true;
        else if (!strcmp(a, "--tod") && i + 1 < argc) o->tod = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--inspect")) o->inspect = true;
        else if (!strcmp(a, "--editor")) o->editor = true;
        else if (!strcmp(a, "--look-slot") && i + 1 < argc) o->look_slot = atoi(argv[++i]) + 1;
        else if (!strcmp(a, "--bands-test")) o->bands_test = true;
        else if (!strcmp(a, "--phys-drop") && i + 1 < argc) o->phys_drop = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--loot-drop") && i + 1 < argc) o->loot_drop = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--warp") && i + 1 < argc) o->warp = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--save") && i + 1 < argc) o->save_path = argv[++i];
        else if (!strcmp(a, "--load") && i + 1 < argc) o->load_path = argv[++i];
        else if (!strcmp(a, "--defs") && i + 1 < argc) o->defs_dir = argv[++i];
        else if (!strcmp(a, "--mods") && i + 1 < argc) o->mods_dir = argv[++i];
        else if (!strcmp(a, "--sum-after") && i + 1 < argc) {
            o->sum_after = strtof(argv[++i], NULL);
            o->sum_set = true;
        }
        else if (!strcmp(a, "--move-bench")) {
            o->move_bench = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') o->move_trace = argv[++i];
        } else if (!strcmp(a, "--humans")) o->humans = true;
        else if (!strcmp(a, "--stand") && i + 1 < argc) o->stand = atoi(argv[++i]) + 1;
        else if (!strcmp(a, "--hall-t") && i + 1 < argc) o->hall_t = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--no-pixel")) o->no_pixel = true;
        else if (!strcmp(a, "--valley")) o->valley = true;
        else if (!strcmp(a, "--mute")) o->mute = true;
        else if (!strcmp(a, "--sound-demo") && i + 1 < argc) o->sound_demo = argv[++i];
        else if (!strcmp(a, "--no-vrs")) o->no_vrs = true;
        else if (!strcmp(a, "--gl")) o->api = RB_API_GL;
        else if (!strcmp(a, "--vk")) o->api = RB_API_VULKAN;
        else if (!strcmp(a, "--ao-half")) o->ao_half = true;
        else if (!strcmp(a, "--hash-noise")) o->noise_tex = false;
        else if (!strcmp(a, "--no-mdi")) o->no_mdi = true;
        else if (!strcmp(a, "--no-skin-cache")) o->no_skin_cache = true;
        else if (!strcmp(a, "--light") && i + 1 < argc) o->light_preset = (i32)((u32)atoi(argv[++i]) % LIGHT_PRESET_COUNT);
        else if (!strcmp(a, "--light-parts") && i + 9 < argc)
            for (u32 k = 0; k < 9; k++) o->light[k] = atoi(argv[++i]);
        else if (!strcmp(a, "--fullscreen")) o->fullscreen = true;
        else if (!strcmp(a, "--day-stop")) o->day_stop = true;
        else if (!strcmp(a, "--cam-y") && i + 1 < argc) o->cam.target.y = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--attack") && i + 1 < argc) o->attack = atoi(argv[++i]) + 1;
        else if (!strcmp(a, "--field-bench") && i + 1 < argc) o->field_bench = (u32)atoi(argv[++i]) + 1;
        else if (!strcmp(a, "--arena-bench") && i + 1 < argc) o->arena_bench = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--arena-log") && i + 1 < argc) o->arena_log = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--bands-bench") && i + 1 < argc) o->bands_bench = (u32)atoi(argv[++i]);
        else if (!strcmp(a, "--hours") && i + 1 < argc) o->bands_hours = strtof(argv[++i], NULL);
        else if (!strcmp(a, "--bands-log") && i + 1 < argc) o->bands_log = (u32)atoi(argv[++i]);
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

// Настройки между запусками: anathemyre.cfg рядом с папкой data, строки «ключ значение».
// Пока одна: render gl|vulkan (НАСТРОЙКИ → РЕНДЕР, после перезапуска)
static void cfg_path(char *out, size_t size, const char *data_dir) { snprintf(out, size, "%s/../anathemyre.cfg", data_dir); }

static RbApi cfg_api(const char *data_dir)
{
    char path[600], line[128];
    cfg_path(path, sizeof path, data_dir);
    FILE *f = fopen(path, "r");
    RbApi api = RB_API_GL;
    if (!f) return api;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "render ", 7)) api = !strncmp(line + 7, "vulkan", 6) ? RB_API_VULKAN : RB_API_GL;
    fclose(f);
    return api;
}

static void cfg_save_api(const char *data_dir, RbApi api)
{
    char path[600];
    cfg_path(path, sizeof path, data_dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        log_error("НАСТРОЙКИ: не записан %s", path);
        return;
    }
    fprintf(f, "render %s\n", api == RB_API_VULKAN ? "vulkan" : "gl");
    fclose(f);
    log_info("РЕНДЕР: со следующего запуска %s", api == RB_API_VULKAN ? "VULKAN" : "GL");
}

// Окно и рендер. Vulkan не поднялся (нет драйвера, устройства, шейдера) — всё заново на GL
static bool gfx_start(RbApi api, const Options *o, Arena *scratch, const char *data_dir)
{
#ifdef NDEBUG
    const bool gl_debug = false;
#else
    const bool gl_debug = true;
#endif
    for (;;) {
        platform_use_vulkan(api == RB_API_VULKAN);
        rb_select(api);
        bool win = platform_init("ANATHEMYRE", o->width, o->height, gl_debug);
        if (win && render_init(scratch, data_dir) && ui_init()) {
            log_info("РЕНДЕР: %s", api == RB_API_VULKAN ? "VULKAN" : "GL");
            return true;
        }
        if (win) {
            ui_shutdown();
            render_shutdown();
            platform_shutdown();
        }
        if (api == RB_API_GL) return false;
        log_error("РЕНДЕР: Vulkan не запустился, кадр рисует GL");
        api = RB_API_GL;
    }
}

// Портреты: миниатюры отряда и крупно выбранный (свой, чужой на поле или боец арены)
static void draw_portraits(Figures *fig, const FrameView *fv, const CharMeshes *cm, const World *w, const Hud *hud)
{
    const Squad *sq = &w->squad;
    for (u32 i = 0; i <= sq->count; i++) {
        bool focus = i == sq->count;
        u32 k = focus ? hud->look : i;
        if (focus && hud->look_kind == LOOK_ARENA) {
            figures_portrait_arena(fig, fv, cm, w->arena, k, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX);
            continue;
        }
        // Чужой на поле: слот поля — номер в таблице людей, внешность та же, что в мире
        if (focus && hud->look_kind == LOOK_FIELD && k < w->field->w.count) {
            figures_portrait(fig, fv, cm, w->people, k, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX, true);
            continue;
        }
        if (focus && (hud->look_kind != LOOK_SQUAD || k >= sq->count)) continue;
        figures_portrait(fig, fv, cm, w->people, sq->member[k], focus ? HUD_PORTRAIT_FOCUS : i,
                         focus ? HUD_PORTRAIT_FOCUS_PX : HUD_PORTRAIT_CARD_PX, focus);
    }
}

int main(int argc, char **argv)
{
    platform_preinit();
    time_init();
    Options opt = {
        .tod = 0.31f, .shot_frames = 90, .stage = STAGE_FULL, .bands_hours = 1.0f, .noise_tex = true, .width = 1280, .height = 720, .seed = 1337,
        .light = { -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        .cam = { .target = { 14, 0, 32 }, .yaw = -0.5f, .pitch = 0.42f, .dist = 17.0f }, .api = -1,
    };
    if (!parse_args(argc, argv, &opt)) return 1;

    char data_dir[512];
    if (!find_data_dir(data_dir, sizeof data_dir)) {
        log_error("ДАННЫЕ: папка data/shaders не найдена");
        return 1;
    }
    // Описания вещей — до всего, что берёт оружие и вещи, и до замеров без окна. Моды — рядом с data
    static Arena defs_mem;
    char defs_dir[600], mods_dir[600];
    snprintf(defs_dir, sizeof defs_dir, "%s/defs", data_dir);
    snprintf(mods_dir, sizeof mods_dir, "%s/../mods", data_dir);
    if (!arena_init(&defs_mem, DEFS_MEM) ||
        !defs_load(&defs_mem, opt.defs_dir ? opt.defs_dir : defs_dir, opt.mods_dir ? opt.mods_dir : mods_dir)) {
        log_error("ОПИСАНИЯ: не прочитаны, игра не запускается");
        return 1;
    }
    // Скелет и капсулы тела — после описаний (стойка берёт оружие) и тоже до замеров боя
    if (!look_load_skeletons(data_dir)) return 1;
    body_hit_build();
    anim_bake_hit_guard();
    if (opt.move_bench) return player_bench(opt.move_trace);
    if (opt.field_bench) return field_bench(opt.field_bench - 1, opt.seed);
    if (opt.sound_demo) return audio_demo(opt.sound_demo);
    if (opt.arena_bench) return arena_bench(opt.arena_bench, opt.seed, opt.arena_log);
    if (opt.bands_bench) return bands_bench(opt.bands_bench, opt.bands_hours, opt.seed, TICK_HZ, opt.bands_log);
    if (opt.battle[0] || opt.battle[1])
        return battle_bench(opt.battle[0], opt.battle[1], opt.seed, opt.threads, opt.battle_verify);
    // Дальше в консоль — только к/с, замеры и сбои; остальное в журнал на экране
    log_set_echo(false);
    log_info("АНАТЕМАЙР. ЭТАП 0. ЗЕРНО МИРА %llu", (unsigned long long)opt.seed);

    // Вся память выделяется здесь, один раз
    Arena perm, scratch;
    // Карта ~18 МБ, долина ~70 МБ при старте (экземпляры растений и временные массивы)
    if (!arena_init(&perm, MB(256)) || !arena_init(&scratch, MB(4))) {
        log_error("ПАМЯТЬ: не выделена");
        return 1;
    }

    RbApi api_want = opt.api >= 0 ? (RbApi)opt.api : cfg_api(data_dir);
    if (!gfx_start(api_want, &opt, &scratch, data_dir)) return 1;
    render_set_vsync(!opt.no_vsync && opt.shot_path == NULL);
    // Пул потоков: позы людей и бой на поле
    if (!jobs_init(opt.threads)) log_error("ПОТОКИ: пул не запущен, всё в главном потоке");
    log_info("ПОТОКИ: %u", jobs_threads());

    // Мир, затем сетки людей; ряд людей для сравнения строится при первом показе.
    // Рендер раскладывает вызовы по адресам сеток: одежда в памяти раньше статики мира, тела и оружие — после.
    // Так было и до таблицы людей; одежда после тел — проход сцены на программном GL на ~13% дольше
    MeshBuilder mb;
    GpuMesh *gear = ARENA_PUSH_ARRAY(&perm, GpuMesh, CHAR_GEAR_MESHES);
    GpuMesh *gear_lod = ARENA_PUSH_ARRAY(&perm, GpuMesh, CHAR_GEAR_MESHES * (CHAR_LODS - 1));
    World *world = ARENA_PUSH_ARRAY(&perm, World, 1);
    CharMeshes *cm = ARENA_PUSH_ARRAY(&perm, CharMeshes, 1);
    cm->gear = gear;
    cm->gear_lod = gear_lod;
    Figures *fig = ARENA_PUSH_ARRAY(&perm, Figures, 1);
    HumansRow *humans = ARENA_PUSH_ARRAY(&perm, HumansRow, 1);
    Stand *stand = ARENA_PUSH_ARRAY(&perm, Stand, 1);
    ShowHall *hall = ARENA_PUSH_ARRAY(&perm, ShowHall, 1);
    // Формы оружия — до мира: стойки зала собираются из них в world_init
    weapon_models_load(&perm, data_dir);
    if (!world_init(world, &perm, &mb, opt.seed, opt.tod, !opt.day_stop) || !figures_init(fig, cm, &mb, &perm, data_dir)) {
        jobs_shutdown();
        render_shutdown();
        platform_shutdown();
        return 1;
    }
    figures_view(fig, world->people, world->field, world->alpha, world_ground);
    humans_row_init(humans, &perm, world_ground);
    stand_init(stand, &perm);
    showhall_init(hall, &mb, &perm);
    hall->t_off = opt.hall_t;
    // Рельеф карты и растения долины — на весь сеанс
    render_set_terrain(map_fill, NULL);
    render_set_groups(world->valley->groups, world->valley->clusters, world->valley->cluster_count);
    bool humans_flip = opt.humans;
    Squad *squad = &world->squad;
    Field *field = world->field;
    ValleyView *valley = world->valley;

    PointLight *lights = ARENA_PUSH_ARRAY(&perm, PointLight, TORCH_LIGHTS_MAX);
    DrawItem *items = ARENA_PUSH_ARRAY(&perm, DrawItem, RENDER_MAX_ITEMS);
    Settings set = {
        .rs = { .post = true, .pixel = !opt.no_pixel, .vrs = !opt.no_vrs,
                .ao_half = opt.ao_half, .noise_tex = opt.noise_tex, .mdi = !opt.no_mdi, .skin_cache = !opt.no_skin_cache },
        .vsync = !opt.no_vsync && opt.shot_path == NULL, .fullscreen = opt.fullscreen && opt.shot_path == NULL,
        // Swap после логики: пока GPU рисует прошлый кадр, CPU уже считает следующий (только без vsync,
        // иначе кадр показывался бы на период позже)
        .swap_late = true, .follow = opt.follow, .ui_user = 1.0f, .ui_user_text = 1.0f,
        .sound_vol = 0.8f, .music_vol = 0.7f, .music_on = true, .travel = opt.valley,
        .stand = opt.stand > 0, .stand_moved = opt.stand > 0, .stand_row = opt.stand > 0 ? (u32)(opt.stand - 1) : 0,
        .stage = opt.stage, .bench_stages = opt.bench_stages, .render_vk = api_want == RB_API_VULKAN,
    };
    light_preset_apply(&set.rs, (LightPreset)opt.light_preset);
    if (opt.light[0] >= 0) set.rs.light_mat = (u8)(opt.light[0] % LIGHT_MAT_COUNT);
    if (opt.light[1] >= 0) set.rs.light_curve = (u8)(opt.light[1] % LIGHT_CURVE_COUNT);
    if (opt.light[2] >= 0) set.rs.light_expo = opt.light[2] != 0;
    if (opt.light[3] >= 0) set.rs.light_bloom = opt.light[3] != 0;
    if (opt.light[4] >= 0) set.rs.light_gi = (u8)(opt.light[4] % LIGHT_GI_COUNT);
    if (opt.light[5] >= 0) set.rs.light_casc = opt.light[5] != 0;
    if (opt.light[6] >= 0) set.rs.light_soft = opt.light[6] != 0;
    if (opt.light[7] >= 0) set.rs.light_contact = opt.light[7] != 0;
    if (opt.light[8] >= 0) set.rs.light_ao = (u8)(opt.light[8] % LIGHT_AO_COUNT);
    // Звук: слушатель у камеры, шаги, бой поля и арены. Без звука снимки кадров и --mute.
    // Банк собирается в своём потоке при старте; устройство открывается, только если звук включён
    WorldSound *wsound = ARENA_PUSH_ARRAY(&perm, WorldSound, 1);
    sfx_init(wsound, field, world->arena, ARENA_CENTER, world_ground);
    set.sound_ok = !opt.shot_path && audio_init(&perm);
    set.sound_on = set.sound_ok && !opt.mute;
    if (set.sound_on) audio_start();
    Hud hud;
    hud_init(&hud);
    if (opt.inspect) {
        for (u32 i = 0; i < squad->count; i++) squad->selected[i] = true;
        hud_look(&hud, LOOK_SQUAD, 0);
        hud.win_inv = hud.win_char = true;
    }
    Editor editor;
    editor_init(&editor);
    if (opt.editor) {
        squad->selected[0] = true;
        hud_look(&hud, LOOK_SQUAD, 0);
        hud.win_look = true;
    }
    if (opt.look_slot > 0 && (u32)opt.look_slot <= field->w.count) {
        hud_look(&hud, LOOK_FIELD, (u32)opt.look_slot - 1);
        hud.win_inv = hud.win_char = true;
    }
    i32 victim = opt.attack > 0 ? people_nth(world->people, ROLE_WANDER, (u32)opt.attack - 1) : -1;
    for (u32 i = 0; victim >= 0 && i < squad->count; i++) {
        squad->selected[i] = true;
        field_attack(field, (u32)squad_body(squad, i)->slot, (u32)victim);
    }
    if (opt.bands_test) world_bands_test(world);
    if (opt.phys_drop || opt.loot_drop) {
        world->focus = opt.cam.target;
        world->phys_drop = opt.phys_drop;
        world->loot_drop = opt.loot_drop;
    }
    // Сохранение в папке saves рядом с data
    char save_path[600];
    snprintf(save_path, sizeof save_path, "%s/../saves", data_dir);
    bool save_dir = platform_make_dir(save_path);
    snprintf(save_path + strlen(save_path), sizeof save_path - strlen(save_path), "/world.sav");
    // Прогон мира до первого кадра (снимки, проверка сохранений): камера стоит, журнал — в консоль
    bool quit_now = false;
    int status = 0;  // сохранение без меню не прошло — выход с ошибкой
    if (opt.load_path || opt.warp > 0.0f || opt.save_path || opt.sum_set) {
        log_set_echo(true);
        world->focus = opt.cam.target;
        if (opt.load_path && !world_load(world, opt.load_path)) quit_now = true;
        for (u32 k = 0; !quit_now && k < (u32)(opt.warp * (f32)TICK_HZ); k++) world_update(world, TICK_DT);
        if (!quit_now && opt.save_path && !world_save(world, opt.save_path)) quit_now = true;
        status = quit_now ? 1 : 0;
        if (!quit_now && opt.sum_set) {
            for (u32 k = 0; k < (u32)(opt.sum_after * (f32)TICK_HZ); k++) world_update(world, TICK_DT);
            log_stat("МИР: шаг %llu, сумма %016llx", (unsigned long long)world->tick,
                     (unsigned long long)world_checksum(world));
            quit_now = true;
        }
        log_set_echo(false);
    }
    u8 *shot_buf = NULL;
    if (opt.shot_path) shot_buf = arena_push(&perm, (size_t)opt.width * (size_t)opt.height * 3, 1);

    RtsCamera cam = opt.cam;
    // Снимок вблизи (--cam с дистанцией меньше 4 м): камера как у стенда, кружит над точкой взгляда
    if (opt.cam_set && opt.cam.dist < 4.0f) camera_close(true);
    // Точка взгляда идёт по земле (сглаженно); --cam-y — подъём над ней
    f32 cam_lift = opt.cam.target.y, cam_y = world_ground(cam.target.x, cam.target.z);
    Input in;
    memset(&in, 0, sizeof in);
    in.quit = quit_now;
    f64 last = time_now();
    FrameStats stats = { .t = last, .fps_t = last };
    f32 real_time = 0;
    bool swap_pending = false;  // кадр отрисован, но ещё не показан
    if (set.fullscreen) platform_set_fullscreen(true);
    u32 frame = 0;
    Bench bench;
    bench_init(&bench, &perm);
    BenchRestore restore = { 0 };
    bool bench_request = opt.bench && !opt.shot_path;
    // Замер по этапам: от ПУСТО до ПОЛНОЙ подряд, итог таблицей, затем прежний этап
    bool stage_sweep = false;
    u32 stage_before = set.stage;
    BenchResult stage_res[STAGE_COUNT];
    bool stage_sound = set.stage >= STAGE_FULL;

    log_info("УПРАВЛЕНИЕ: WASD — сдвиг, колесо — дистанция, ЛКМ — выбор и рамка, ПКМ — идти, I — инвентарь, P — персонаж, пробел — пауза, средняя кнопка и Q/E — поворот, F — следовать; остальное — в меню НАСТРОЙКИ слева вверху");

    while (!in.quit) {
        prof_zone(PZ_INPUT);
        platform_poll(&in);
        // Esc сначала закрывает окна
        if (in.key_pressed[KEY_ESCAPE] && !hud_window_open(&hud)) break;
        if (in.key_pressed[KEY_F]) {
            set.follow = !set.follow;
            if (set.follow) cam.dist = MIN(cam.dist, 8.0f);
        }
        if (humans_flip) {
            humans_flip = false;
            humans_row_toggle(humans, &mb, &perm, data_dir, world_ground);
        }
        if (set.stand_moved) {
            set.stand_moved = false;
            stand_show(stand, set.stand, set.stand_row, &mb, &perm, world_ground, &cam);
            // --stand N --cam ...: снимок стенда своей камерой (крупно голову и т. п.), --cam-y тут — высота над морем
            if (opt.cam_set && opt.stand > 0) {
                cam = opt.cam;
                opt.stand = 0;
            }
            // Высоту взгляда над плитами задаёт стенд: держать её над землёй, пока он открыт
            if (set.stand) {
                cam_y = world_ground(cam.target.x, cam.target.z);
                cam_lift = cam.target.y - cam_y;
            } else cam_lift = opt.cam.target.y;
        }
        // Перенос отряда между лагерем и долиной (отладка; пешком тоже можно): камера за ним
        bool jumped = false;   // фигуры ещё на старом месте: в этом кадре камера за ними не идёт
        if (set.travel) {
            set.travel = false;
            if ((jumped = world_travel(world))) {
                cam = !world_squad_in_valley(world) || opt.cam_set ? opt.cam : VALLEY_BENCH_CAM(valley->spawn);
                cam_y = world_ground(cam.target.x, cam.target.z);
                if (hud.look_kind != LOOK_SQUAD) hud_look(&hud, LOOK_SQUAD, 0);
            }
        }
        if (set.bench_stages && !bench.on && !opt.shot_path) {
            set.bench_stages = false;
            stage_sweep = bench_request = true;
            stage_before = set.stage;
            set.stage = STAGE_EMPTY;
            log_stat("ЗАМЕР ПО ЭТАПАМ: %u этапов по %.0f с; этап 0 %s", STAGE_COUNT, BENCH_STAGE_WARMUP_S + BENCH_STAGE_S,
                     g_stage_name[STAGE_EMPTY]);
        }
        if (bench_request && !bench.on) {
            bench_request = false;
            restore = (BenchRestore){ cam, world->tod, world->day_run, set.vsync, set.follow };
            cam = world_squad_in_valley(world) ? VALLEY_BENCH_CAM(valley->spawn) : BENCH_CAM;
            cam_y = world_ground(cam.target.x, cam.target.z);
            world->tod = BENCH_TOD;
            world->day_run = set.follow = set.vsync = false;
            render_set_vsync(false);
            if (stage_sweep) bench_begin(&bench, time_now(), BENCH_STAGE_WARMUP_S, BENCH_STAGE_S);
            else bench_begin(&bench, time_now(), BENCH_WARMUP_S, BENCH_S);
        }
        // Звук только на полной сцене: на других этапах устройство и микшер остановлены
        if (stage_sound != (set.stage >= STAGE_FULL) && set.sound_ok) {
            stage_sound = set.stage >= STAGE_FULL;
            if (!stage_sound) audio_stop();
            else if (set.sound_on) audio_start();
        }
        const bool st_live = set.stage >= STAGE_WORLD, st_people = set.stage >= STAGE_PEOPLE,
                   st_day = set.stage >= STAGE_DAY, st_full = set.stage >= STAGE_FULL;

        f64 now = time_now();
        f32 dt = opt.shot_path ? 1.0f / 60.0f : (f32)fmin(now - last, 0.05);
        last = now;
        // Скорость времени из интерфейса (пауза, ×1, ×2, ×5): больше шагов мира за кадр. Камера — в реальном
        f32 real_dt = dt;
        dt *= (f32)g_speed_scale[hud.speed];
        real_time += real_dt;

        if (!bench.on) camera_update(&cam, &in, real_dt);
        // Камера и интерфейс видят людей такими, какими их показал прошлый кадр (figures_view)
        i32 lead = MAX(0, squad_primary(squad));
        V3 lead_pos = fig->view[squad->member[lead]].pos;
        if (set.follow) cam.target = v3(lead_pos.x, 0, lead_pos.z);
        i32 ww, wh;
        platform_window_size(&ww, &wh);
        if (opt.shot_path) {
            ww = opt.width;
            wh = opt.height;
        }
        // Поводок от главного в отряде и край карты; точка взгляда идёт по земле, камера не уходит под неё
        if (!opt.shot_path) {
            f32 dx = cam.target.x - lead_pos.x, dz = cam.target.z - lead_pos.z, d = sqrtf(dx * dx + dz * dz);
            if (d > CAM_LEASH) cam.target = v3(lead_pos.x + dx * CAM_LEASH / d, 0, lead_pos.z + dz * CAM_LEASH / d);
        }
        cam.target.x = clampf(cam.target.x, -MAP_HALF, MAP_HALF);
        cam.target.z = clampf(cam.target.z, -MAP_HALF, MAP_HALF);
        cam_y += (world_ground(cam.target.x, cam.target.z) - cam_y) * fminf(1.0f, real_dt * 6.0f);
        cam.target.y = cam_y + cam_lift;
        f32 valley_k = map_valley(cam.target.x, cam.target.z);
        FrameView fv;
        fv.cam_pos = camera_eye(&cam);
        fv.cam_pos.y = fmaxf(fv.cam_pos.y, world_ground(fv.cam_pos.x, fv.cam_pos.z) + 1.5f);
        fv.cam_pos = showhall_clamp_eye(cam.target, fv.cam_pos);
        fv.view = m4_look_at(fv.cam_pos, v3_add(cam.target, v3(0, 1.0f, 0)), v3(0, 1, 0));
        fv.proj = m4_perspective(CAM_FOV, (f32)ww / (f32)(wh > 0 ? wh : 1), 0.3f, 6000.0f);

        ui_set_user_scale(set.ui_user, set.ui_user_text);
        ui_begin(ww, wh);
        Input hin = in;
        menu_input(&hud, &hin, &set, world, &cam, humans->on, &stats);
        editor_input(&editor, &hud, &hin, world, &cam, humans->on);
        humans_flip |= set.humans;
        bench_request |= set.bench;
        if (set.bands_test) world_bands_test(world);
        if (set.render_save) {
            set.render_save = false;
            cfg_save_api(data_dir, set.render_vk ? RB_API_VULKAN : RB_API_GL);
        }
        if (set.save) {
            if (!save_dir) log_error("СОХРАНЕНИЕ: папка для %s не создана", save_path);
            else world_save(world, save_path);
        }
        // Мир прочитан: камера к отряду, позы и портреты заново
        if (set.load && world_load(world, save_path)) {
            jumped = true;
            V3 lp = squad_body(squad, (u32)MAX(0, squad_primary(squad)))->pos;
            cam.target = v3(lp.x, cam.target.y, lp.z);
            cam_y = world_ground(cam.target.x, cam.target.z);
            for (u32 i = 0; i < PEOPLE_MAX; i++) fig->age[i] = -1.0f;
            figures_portraits_reset();
            hud_look(&hud, LOOK_NONE, 0);
        }
        set.humans = set.bench = set.bands_test = set.save = set.load = false;
        HudWorld hw = { .squad = squad, .arena = world->arena, .aview = &world->aview, .field = field, .view = fig->view,
                        .view_proj = m4_mul(fv.proj, fv.view), .time = real_time, .tod = world->tod, .day = world->day,
                        .ground = world->ground, .phys = world->phys, .ground_ask = &world->ground_ask, .alpha = world->alpha };
        bool ui_busy = hud_input(&hud, &hin, &hw);
        if (hud.focus >= 0 && !jumped) {
            V3 fp = fig->view[squad->member[hud.focus]].pos;
            cam.target = v3(fp.x, cam.target.y, fp.z);
        }
        // Камера за тем, кого правят в окне ВНЕШНОСТЬ
        if (editor_follow(&editor) >= 0 && !jumped) {
            V3 fp = fig->view[editor_follow(&editor)].pos;
            cam.target = v3(fp.x, cam.target.y, fp.z);
        }
        orders_input(&hud, &in, &hin, &hw, ui_busy, fv.cam_pos, ww, wh, world);

        world->focus = cam.target;
        // До этапа ШАГ МИРА мир стоит, до СУТОК — стоят сутки
        if (st_live) {
            bool day_run = world->day_run;
            world->day_run &= st_day;
            world_update(world, dt);
            world->day_run = day_run;
        }
        // Отряд вдали ушёл: его человека больше нет на месте, на которое смотрели
        if (hud.look_kind == LOOK_FIELD && hud.look < field->w.count && (field->flags[hud.look] & FF_ABSENT))
            hud_look(&hud, LOOK_NONE, 0);
        // Вид мира для кадра: бой между шагами, люди между двумя последними шагами
        prof_zone(PZ_FIGURES);
        if (st_live) {
            world_present(world, dt);
            figures_view(fig, world->people, field, world->alpha, world_ground);
        }
        f32 wtime = world_time(world);
        prof_zone(PZ_SOUND);
        if (set.sound_on && st_full) {
            sfx_listener(wsound, fv.cam_pos, cam.target, cam.yaw);
            sfx_field(wsound, field, fig->view);
            sfx_arena(wsound, &world->aview, world->arena);
        }

        // Список кадра: статика, люди и бойцы арены, метки, ряд людей; свет — фонари арены и факелы
        prof_zone(PZ_SCENE);
        Sky sky;
        // До этапа СУТКИ свет утра, как при замере кадра
        sky_at(st_day ? world_tod(world) : BENCH_TOD, &sky);
        u32 nl = arena_view_lights(&world->aview, sky.night, wtime, lights);
        u32 n = 0;
        items[n++] = (DrawItem){ .mesh = &world->hall };
        items[n++] = (DrawItem){ .mesh = &world->far, .no_shadow = true };
        items[n++] = (DrawItem){ .mesh = &world->town_mesh };
        items[n++] = (DrawItem){ .mesh = &world->ostov };
        // Мерильня: лампы горят всегда, фигуры — когда камера рядом
        nl += showhall_lights(wtime, lights + nl, TORCH_LIGHTS_MAX - nl);
        nl += testroom_lights(lights + nl, TORCH_LIGHTS_MAX - nl);
        if (st_people) n += showhall_draw(hall, cm, fv.cam_pos, wtime, st_live ? dt : 0.0f, items + n);
        else items[n++] = (DrawItem){ .mesh = &hall->mesh };
        n += valley_view_draw(valley, fv.cam_pos, items + n);
        n += scrap_draw(&world->scrap, world->phys, world->alpha, fv.cam_pos, items + n, RENDER_MAX_ITEMS / 8);
        n += loot_draw(&world->loot, world->ground, world->phys, cm->weapon, world->alpha, fv.cam_pos, items + n,
                       RENDER_MAX_ITEMS / 8);
        prof_zone(PZ_FIGURES);
        if (st_people) {
            people_dress_live(world->people);
            FigureView fview = { .view_proj = hw.view_proj, .eye = fv.cam_pos, .light_dir = sky.light_dir, .dt = real_dt,
                                 .time = wtime, .night = sky.night };
            n += figures_draw(fig, cm, world->people, field, &world->aview, world->arena, &fview, items + n, lights, &nl);
        }
        if (!st_day) nl = 0;
        u32 posed = st_people ? fig->jobs : 0;
        prof_zone(PZ_SCENE);
        V3 look_pos;
        bool look_field = hud.look_kind == LOOK_FIELD && hud.look < field->w.count;
        if (look_field) look_pos = fig->view[hud.look].pos;  // слот поля = номер в таблице
        bool look_ring = look_field || (hud.look_kind == LOOK_ARENA && hud_arena_pos(&world->aview, world->arena, hud.look, &look_pos));
        if (st_people) {
            n += figures_marks(fig, cm, squad, field, look_ring ? &look_pos : NULL, items + n);
            n += humans_row_draw(humans, cm, items + n);
            n += stand_draw(stand, cm, items + n);
        }

        // Показать прошлый кадр: GPU рисовал его, пока шла логика этого
        bool swap_first = set.swap_late && !set.vsync && !opt.shot_path;
        if (swap_first && swap_pending) {
            prof_zone(PZ_SWAP);
            platform_swap();
            swap_pending = false;
        }
        prof_zone(PZ_SUBMIT);
        fv.sun_dir = sky.light_dir;
        fv.sun_color = sky.light_color;
        fv.sky_amb = sky.sky_amb;
        fv.ground_amb = sky.ground_amb;
        fv.haze = sky.haze;
        // В долине свет отражается от травы, а не от песка, и дымка не такая пыльная
        fv.ground_amb = v3_hadamard(fv.ground_amb, v3_lerp(v3(1, 1, 1), v3(0.6f, 0.9f, 0.6f), valley_k));
        fv.haze = v3_hadamard(fv.haze, v3_lerp(v3(1, 1, 1), v3(0.93f, 0.97f, 1.02f), valley_k));
        fv.zenith = sky.zenith;
        fv.shadow_center = cam.target;
        fv.lights = lights;
        showhall_room(&fv.room);
        fv.light_count = nl;
        fv.time = wtime;
        fv.frame = frame;
        RenderSettings rs = stage_render(&set);
        render_frame(&fv, items, n, &rs, ww, wh);
        prof_zone(PZ_UI);
        if (st_full) draw_portraits(fig, &fv, cm, world, &hud);
        ui_set_image(render_portrait_texture());
        ui_set_time(real_time);
        hud.dbg.fps = stats.fps;
        hud.dbg.fps_ms = stats.fps_ms;
        if (st_full) {
            showhall_labels(hall, hw.view_proj, fv.cam_pos);
            hud_draw(&hud, &hin, &hw);
        }
        ui_end();
        render_frame_end();
        swap_pending = true;

        frame++;
        if (opt.shot_path && (i32)frame >= opt.shot_frames) {
            render_read_pixels(shot_buf, ww, wh);
            if (write_ppm(opt.shot_path, shot_buf, ww, wh)) log_stat("КАДР: записан %s", opt.shot_path);
            else log_error("КАДР: не записан %s", opt.shot_path);
            break;
        }
        if (!swap_first) {
            prof_zone(PZ_SWAP);
            platform_swap();
            swap_pending = false;
        }

        if (bench.on && now >= bench.measure_from) {
            if (bench_frame(&bench, now, posed, ww, wh, set.swap_late)) {
                if (stage_sweep) {
                    stage_res[set.stage] = bench.last;
                    if (set.stage < STAGE_FULL) {
                        set.stage++;
                        log_stat("ЗАМЕР ПО ЭТАПАМ: этап %u %s", set.stage, g_stage_name[set.stage]);
                        bench_begin(&bench, time_now(), BENCH_STAGE_WARMUP_S, BENCH_STAGE_S);
                        continue;
                    }
                    bench_stages_report(stage_res, STAGE_COUNT, g_stage_name);
                    stage_sweep = false;
                    set.stage = stage_before;
                }
                if (opt.bench) break;
                cam = restore.cam;
                world->tod = restore.tod;
                world->day_run = restore.day_run;
                set.follow = restore.follow;
                set.vsync = restore.vsync;
                render_set_vsync(set.vsync);
                stats_reset(&stats, time_now());
            }
            continue;
        }
        stats_frame(&stats, now, posed, set.swap_late && !set.vsync);
    }

    audio_stop();
    jobs_shutdown();
    ui_shutdown();
    render_shutdown();
    platform_shutdown();
    arena_free(&scratch);
    arena_free(&perm);
    log_info("СЕАНС ЗАВЕРШЁН");
    return status;
}
