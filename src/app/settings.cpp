// Настройки: свой текстовый формат (docs/text.md), запись — при закрытии меню и выходе
#include "settings.h"
#include "../core/log.h"
#include "../core/mem.h"
#include "../core/text.h"
#include "../platform/platform.h"
#include <stdio.h>

const char *settings_path() {
    static char path[1024];
    snprintf(path, sizeof path, "%ssettings.txt", platform_exe_dir());
    return path;
}

static f32 clampf(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

void settings_load(Settings &s, const char *path) {
    s = Settings{};
    static u8 buf[1 << 16];  // файл маленький: своя память, без арены
    Arena a;
    arena_init(a, buf, sizeof buf, "settings");
    u8 *d = nullptr;
    usize n = 0;
    if (!platform_read_file(path, a, &d, &n)) {
        log_info("настройки: %s нет, значения по умолчанию", path);
        return;
    }
    TextReader r = text_reader(d, n);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count != 2) continue;
        Str k = l.w[0], v = l.w[1];
        f32 f = 0;
        u32 u = 0;
        bool num = parse_f32(v, f);
        bool b = num && f != 0;
        if (str_eq(k, "fullscreen") && num) s.fullscreen = b;
        else if (str_eq(k, "vsync") && num) s.vsync = b;
        else if (str_eq(k, "ui_scale") && num) s.ui_scale = clampf(f, 0.75f, 2.0f);
        else if (str_eq(k, "text_scale") && num) s.text_scale = clampf(f, 0.75f, 1.5f);
        else if (str_eq(k, "fps") && parse_u32(v, u)) s.fps = u < FPS_MODES ? u : FPS_SHORT;
        else if (str_eq(k, "mouse_sens") && num) s.mouse_sens = clampf(f, 0.2f, 4.0f);
        else if (str_eq(k, "invert_y") && num) s.invert_y = b;
        else if (str_eq(k, "fov") && num) s.fov_deg = clampf(f, 40.0f, 100.0f);
        else if (str_eq(k, "debug") && num) s.debug = b;
        else if (str_eq(k, "debug_panel") && num) s.debug_panel = b;
        else if (str_eq(k, "lines") && num) s.lines = b;
        else if (str_eq(k, "pixel") && num) s.pixel = b;
    }
    log_info("настройки: %s", path);
}

bool settings_save(const Settings &s, const char *path) {
    char t[1024];
    int n = snprintf(t, sizeof t,
                     "# Настройки Anathemyre (меню Esc). Файл пишет игра, править можно руками\n"
                     "fullscreen %d\nvsync %d\n"
                     "ui_scale %.2f\ntext_scale %.2f\nfps %u\n"
                     "mouse_sens %.2f\ninvert_y %d\nfov %.0f\n"
                     "debug %d\ndebug_panel %d\nlines %d\npixel %d\n",
                     s.fullscreen, s.vsync, (double)s.ui_scale, (double)s.text_scale, s.fps, (double)s.mouse_sens,
                     s.invert_y, (double)s.fov_deg, s.debug, s.debug_panel, s.lines, s.pixel);
    bool ok = n > 0 && platform_write_file(path, t, (usize)n);
    if (!ok) log_error("не записать настройки %s", path);
    return ok;
}
