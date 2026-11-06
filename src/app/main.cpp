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
    log_info("anathemyre [--seed N] [--novsync] [--validation] [--frames N] [--record файл.rec] [--nopixel]");
    log_info("           [--frames N --shot файл.bmp] [--cam yaw pitch расстояние x z]   снимок (градусы, метры)");
    log_info("           --sim N [--seed N] [--record файл.rec] [--hashes файл.txt]   шаг мира без окна");
    log_info("           --replay файл.rec                                            проверка повтора");
    log_info("           --terrain-map файл.bmp                                       карта земли сверху");
}

void app_memory_init(AppMemory &m, bool with_record) {
    const usize MB = 1u << 20;
    arena_init(m.world, platform_alloc(64 * MB), 64 * MB, "мир");  // земля ~27 МБ
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
    if (platform_log_open(log_path)) log_set_sink(log_to_file, nullptr);

    Args a;
    for (int i = 1; i < argc; ++i) {
        const char *s = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : nullptr;
        bool ok = true;
        if (!strcmp(s, "--sim")) { ok = parse_u64(next, a.sim_steps) && a.sim_steps > 0; i++; }
        else if (!strcmp(s, "--seed")) { ok = parse_u64(next, a.seed); i++; }
        else if (!strcmp(s, "--frames")) { ok = parse_u64(next, a.frames); i++; }
        else if (!strcmp(s, "--record")) { a.record = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--replay")) { a.replay = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--hashes")) { a.hashes = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--shot")) { a.shot = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--cam")) {
            a.cam_set = true;
            for (int k = 0; k < 5 && ok; ++k) {
                const char *v = i + 1 + k < argc ? argv[i + 1 + k] : nullptr;
                ok = v && parse_f32(Str{v, (u32)strlen(v)}, a.cam[k]);
            }
            i += 5;
        }
        else if (!strcmp(s, "--nopixel")) a.nopixel = true;
        else if (!strcmp(s, "--terrain-map")) { a.terrain_map = next; ok = next != nullptr; i++; }
        else if (!strcmp(s, "--novsync")) a.vsync = false;
        else if (!strcmp(s, "--validation")) a.validation = true;
        else if (!strcmp(s, "--help") || !strcmp(s, "-h")) { usage(); return 0; }
        else ok = false;
        if (!ok) {
            log_error("непонятный аргумент: %s", s);
            usage();
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
    return rc;
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
// Отладка с ASan: утечки системных библиотек (Xlib в SDL, в том числе перекодировка заголовка окна, драйвер
// Vulkan) — не наши, их не показывать.
// asan_thread_start — потоки драйвера: он выгружается с instance, и имени модуля в стеке уже нет.
// Своих потоков у игры пока нет; с пулом потоков (docs/architecture.md) это правило пересмотреть
extern "C" const char *__lsan_default_suppressions() {
    return "leak:libX11.so\nleak:X11_InitIM\nleak:X11_GetGlobalContentScale\nleak:libvulkan\nleak:libLLVM\n"
           "leak:libdbus\nleak:libxcb\nleak:asan_thread_start\nleak:SDL_X11_SetWindowTitle\n";
}
#endif
#endif
