#include "debug.h"
#include "../core/log.h"
#include "../render/render.h"

void debug_keys(const Input &in, DebugState &d) {
    if (in.key_pressed[KEY_PERIOD]) d.step_once = true;
    if (in.key_pressed[KEY_L]) d.lines = !d.lines;
    if (in.key_pressed[KEY_F1]) debug_print_help();
}

void autoplay_intents(u64 step, Rng &rng, FixedArray<Intent> &out) {
    // Ритм нарочно неровный: намерения попадают на разные фазы шага
    auto at = [&rng]() {
        WPos p;
        p.x = rng_range(rng, -60.0f, 60.0f);
        p.z = rng_range(rng, -60.0f, 60.0f);
        wpos_rebase(p);
        return p;
    };
    if (step % 97 == 13) out.push_back({step, INTENT_ATTRACT, at()});
    if (step % 331 == 200) out.push_back({step, INTENT_BURST, at()});
    if (step % 500 == 499) out.push_back({step, INTENT_RELEASE, WPos{}});
}

void debug_print_help() {
    log_info("клавиши: Space пауза, . шаг на паузе, - = медленнее/быстрее, L отрезки, P ячейки, F11 весь экран, Esc выход");
    log_info("камера: правая кнопка — вращать, средняя — двигать, колесо — ближе/дальше, WASD — двигать, Shift — быстрее");
    log_info("        F — свободный полёт (WASD, Q/E вниз/вверх, правая кнопка — взгляд) и обратно на орбиту");
    log_info("точки: левая кнопка — притянуть, Ctrl + левая — вспышка, R — отпустить");
}

static void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void put32(u8 *p, u32 v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

// BMP 24 бита снизу вверх: без сторонних библиотек, ffmpeg и любой просмотрщик его читают
bool debug_write_shot(const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    u8 *rgba = nullptr;
    u32 w = 0, h = 0;
    if (!render_capture_take(scratch, &rgba, &w, &h)) {
        log_error("снимок не вышел (кадр не нарисован)");
        return false;
    }
    u32 row = (w * 3 + 3) & ~3u;
    u32 size = 54 + row * h;
    u8 *f = arena_push<u8>(scratch, size);
    f[0] = 'B'; f[1] = 'M';
    put32(f + 2, size);
    put32(f + 10, 54);
    put32(f + 14, 40);
    put32(f + 18, w);
    put32(f + 22, h);
    put16(f + 26, 1);
    put16(f + 28, 24);
    put32(f + 34, row * h);
    for (u32 y = 0; y < h; ++y) {
        const u8 *src = rgba + (usize)(h - 1 - y) * w * 4;
        u8 *dst = f + 54 + (usize)y * row;
        for (u32 x = 0; x < w; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 2];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 0];
        }
    }
    bool ok = platform_write_file(path, f, size);
    arena_restore(scratch, mark);
    if (ok) log_info("снимок: %s (%ux%u)", path, w, h);
    else log_error("не записать %s", path);
    return ok;
}
