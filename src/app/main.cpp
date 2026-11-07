// Точка входа: разбор аргументов, журнал, выбор режима
#include "app.h"
#include "../core/log.h"
#include "../platform/platform.h"
#include "../core/text.h"
#include "../sim/record.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void log_to_file(const char *line, usize len, void *) { platform_log_append(line, len); }
static void log_file_flush(void *) { platform_log_flush(); }

static bool parse_u64(const char *s, u64 &out) {
    if (!s || !*s) return false;
    u64 v = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        v = v * 10 + (u64)(*s - '0');
    }
    out = v;
    return true;
}

static void usage() {
    log_info("anathemyre [--seed N] [--novsync] [--validation | --gpuav] [--frames N] [--record файл.rec] [--nopixel] [--forward] [--nohud]");
    log_info("           [--gpuav-skip a.comp,b.frag]                                 шейдеры без проверки на GPU");
    log_info("           [--lights] [--heat]                                          тестовые огни, огни по клеткам froxels");
    log_info("           [--nogcache]                                                 без кеша поверхности земли (сравнение)");
    log_info("           [--window W H]                                               размер окна, по умолчанию 1600 900");
    log_info("           [--frames N --shot файл.bmp] [--cam yaw pitch расстояние x z]   снимок (градусы, метры)");
    log_info("           [--work файл.txt]                                            счётчики работы освещения в файл (с --frames)");
    log_info("           [--nohud] [--open menu|keys|debug|frame|world]               без интерфейса; открыть меню или панель");
    log_info("           [--hour H]                                                   час дня в начале (запись повтора его помнит)");
    log_info("           --frames N --timelapse префикс H0 H1                         N кадров суток от H0 до H1 → префикс_0000.bmp…");
    log_info("           --sim N [--seed N] [--record файл.rec] [--hashes файл.txt]   шаг мира без окна");
    log_info("           --replay файл.rec                                            проверка повтора");
    log_info("           --terrain-map файл.bmp                                       карта земли сверху");
}

void app_memory_init(AppMemory &m, bool with_record) {
    const usize MB = 1u << 20;
    arena_init(m.world, platform_alloc(128 * MB), 128 * MB, "мир");  // земля ~80 МБ (карта дюн 2 м — 52 МБ)
    arena_init(m.step, platform_alloc(1 * MB), 1 * MB, "шаг");
    arena_init(m.frame, platform_alloc(4 * MB), 4 * MB, "кадр");
    arena_init(m.load, platform_alloc(256 * MB), 256 * MB, "загрузка");
    if (with_record) arena_init(m.record, platform_alloc(256 * MB), 256 * MB, "запись");
}

void app_memory_free(AppMemory &m) {
    Arena *all[] = {&m.world, &m.step, &m.frame, &m.load, &m.record};
    for (Arena *a : all) {
        if (a->base) platform_free(a->base);
        *a = Arena{};
    }
}

bool save_record(const Recorder &r, const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    Writer w;
    writer_init(w, scratch, recorder_bytes(r));
    recorder_save(r, w);
    bool ok = !w.overflow && platform_write_file(path, w.data, w.size);
    arena_restore(scratch, mark);
    if (!ok) log_error("не записать %s", path);
    else log_info("запись: %s, %" PRIu64 " шагов, %zu намерений%s", path, r.steps, r.intents.count,
                  r.full ? " (обрезана: кончилось место)" : "");
    return ok;
}

int main(int argc, char **argv) {
    platform_preinit();
    char log_path[1024];
    snprintf(log_path, sizeof log_path, "%sanathemyre.log", platform_exe_dir());
    platform_log_open(log_path);  // без потока или файла строки всё равно идут в stderr сразу
    log_set_sink(log_to_file, log_file_flush, nullptr);

    Args a;
    for (int i = 1; i < argc; ++i) {
        const char *s = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : nullptr;
        bool ok = true;
        if (!strcmp(s, "--sim")) { ok = parse_u64(next, a.sim_steps) && a.sim_steps > 0; i++; }
        else if (!strcmp(s, "--seed")) { ok = parse_u64(next, a.seed); i++; }
        else if (!strcmp(s, "--frames")) { ok = parse_u64(next, a.frames); i++; }
        else if (!strcmp(s, "--window")) {
            ok = parse_u64(next, a.win_w) && i + 2 < argc && parse_u64(argv[i + 2], a.win_h) &&
                 a.win_w >= 64 && a.win_h >= 64 && a.win_w <= 8192 && a.win_h <= 8192;
            i += 2;
        }
        else if (!strcmp(s, "--record")) { a.record = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--replay")) { a.replay = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--hashes")) { a.hashes = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--shot")) { a.shot = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--work")) { a.work = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--cam")) {
            a.cam_set = true;
            for (int k = 0; k < 5 && ok; ++k) {
                const char *v = i + 1 + k < argc ? argv[i + 1 + k] : nullptr;
                ok = v && parse_f32(Str{v, (u32)strlen(v)}, a.cam[k]);
            }
            i += 5;
        }
        else if (!strcmp(s, "--nopixel")) a.nopixel = true;
        else if (!strcmp(s, "--forward")) a.forward = true;
        else if (!strcmp(s, "--lights")) a.test_lights = true;
        else if (!strcmp(s, "--heat")) a.froxel_heat = true;
        else if (!strcmp(s, "--nogcache")) a.nogcache = true;
        else if (!strcmp(s, "--nohud")) a.nohud = true;
        else if (!strcmp(s, "--open")) {
            a.open = next;
            ok = next && (!strcmp(next, "menu") || !strcmp(next, "keys") || !strcmp(next, "debug") || !strcmp(next, "world") ||
                          !strcmp(next, "frame"));
            i++;
        }
        else if (!strcmp(s, "--hour")) {
            ok = next && parse_f32(Str{next, (u32)strlen(next)}, a.hour) && a.hour >= 0 && a.hour < 24;
            a.hour_set = true;
            i++;
        }
        else if (!strcmp(s, "--timelapse")) {
            a.timelapse = next;
            ok = next != nullptr;
            for (int k = 0; k < 2 && ok; ++k) {
                const char *v = i + 2 + k < argc ? argv[i + 2 + k] : nullptr;
                ok = v && parse_f32(Str{v, (u32)strlen(v)}, a.timelapse_hours[k]);
            }
            i += 3;
        }
        else if (!strcmp(s, "--terrain-map")) { a.terrain_map = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--novsync")) a.vsync = false;
        else if (!strcmp(s, "--validation")) a.validation = true;
        else if (!strcmp(s, "--gpuav")) a.gpu_validation = true;
        else if (!strcmp(s, "--gpuav-skip")) { a.gpu_validation_skip = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--help") || !strcmp(s, "-h")) { usage(); platform_shutdown(); return 0; }
        else ok = false;
        if (!ok) {
            log_error("непонятный аргумент: %s", s);
            usage();
            platform_shutdown();
            return 2;
        }
    }
#ifdef ANA_DEBUG
    a.validation = true;  // в отладке слои проверки всегда, если они есть
#endif
    AppMemory m;
    app_memory_init(m, a.record || a.replay);
    int rc = a.terrain_map ? run_terrain_map(a, m) : a.replay ? run_replay(a, m) : a.sim_steps ? run_sim(a, m) : run_window(a, m);
    app_memory_free(m);
    platform_shutdown();  // дописать журнал и закрыть SDL до выхода
    return rc;
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
// Отладка с ASan: утечки системных библиотек (Xlib в SDL, в том числе перекодировка заголовка окна, драйвер
// Vulkan) — не наши, их не показывать.
// asan_thread_start — потоки драйвера: он выгружается с instance, и имени модуля в стеке уже нет.
// Свой поток у игры один — журнал (platform_log_open), он памяти не выделяет; с пулом потоков
// (docs/architecture.md) это правило пересмотреть
extern "C" const char *__lsan_default_suppressions() {
    return "leak:libX11.so\nleak:X11_InitIM\nleak:X11_GetGlobalContentScale\nleak:libvulkan\nleak:libLLVM\n"
           "leak:libdbus\nleak:libxcb\nleak:asan_thread_start\nleak:SDL_X11_SetWindowTitle\n";
}
#endif
#endif
