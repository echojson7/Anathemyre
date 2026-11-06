#include "debug.h"
#include "../core/log.h"
#include "../render/render.h"
#include "../core/math3.h"

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
static bool write_bmp(const char *path, const u8 *rgba, u32 w, u32 h, Arena &scratch) {
    usize mark = arena_mark(scratch);
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
    return ok;
}

bool debug_write_shot(const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    u8 *rgba = nullptr;
    u32 w = 0, h = 0;
    if (!render_capture_take(scratch, &rgba, &w, &h)) {
        log_error("снимок не вышел (кадр не нарисован)");
        return false;
    }
    bool ok = write_bmp(path, rgba, w, h, scratch);
    arena_restore(scratch, mark);
    if (ok) log_info("снимок: %s (%ux%u)", path, w, h);
    else log_error("не записать %s", path);
    return ok;
}

// Карта земли сверху: пиксель — 2×2 узла сетки 8 м, отмывка рельефа с северо-запада, цвет — по маскам
// (песок — охра, трава — зелёная, шероховатое — серее, вода — синяя), высота — светлее. Север сверху
bool debug_write_terrain_map(const Terrain &t, const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    u32 w = (t.n - 1) / 2, h = w;
    u8 *rgba = arena_push<u8>(scratch, (usize)w * h * 4);
    for (u32 py = 0; py < h; ++py) {
        u32 j = (h - 1 - py) * 2;  // север сверху
        for (u32 px = 0; px < w; ++px) {
            u32 i = px * 2;
            auto H = [&](u32 a, u32 b) { return (f32)t.height[(usize)b * t.n + a] * 0.05f; };
            f32 dx = (H(i + 2, j) - H(i, j)) / 16.0f, dz = (H(i, j + 2) - H(i, j)) / 16.0f;
            Vec3 nn = normalize(Vec3{-dx, 1.0f, -dz});
            f32 lit = f_clamp(dot(nn, normalize(Vec3{-0.6f, 0.55f, 0.6f})), 0.0f, 1.0f);
            usize mk = (usize)(j >> 2) * t.nm + (i >> 2);
            u32 m = t.mask[mk];
            f32 rough = (f32)(m & 255) / 255.0f, sand = (f32)((m >> 8) & 255) / 255.0f;
            f32 grass = (f32)((m >> 16) & 255) / 255.0f;
            i32 wl = terrain_water(t, (i32)i * 8, (i32)j * 8);
            bool wet = wl && wl > terrain_node(t, (i32)i * 8, (i32)j * 8);
            f32 alt = f_clamp(H(i, j) / 1800.0f, 0.0f, 1.0f);
            f32 c[3] = {0.55f, 0.45f, 0.33f};
            const f32 sandc[3] = {0.86f, 0.70f, 0.45f}, rockc[3] = {0.50f, 0.48f, 0.46f};
            const f32 grassc[3] = {0.33f, 0.40f, 0.20f}, waterc[3] = {0.25f, 0.38f, 0.50f};
            for (u32 k = 0; k < 3; ++k) {
                c[k] += (sandc[k] - c[k]) * sand;
                c[k] += (grassc[k] - c[k]) * grass;
                c[k] += (rockc[k] - c[k]) * rough * 0.6f;
                if (wet) c[k] = waterc[k];
                c[k] = c[k] * (0.25f + 0.85f * lit) * (0.8f + 0.4f * alt);
            }
            u8 *o = rgba + ((usize)py * w + px) * 4;
            for (u32 k = 0; k < 3; ++k) o[k] = (u8)(f_clamp(c[k], 0.0f, 1.0f) * 255.0f);
            o[3] = 255;
        }
    }
    bool ok = write_bmp(path, rgba, w, h, scratch);
    arena_restore(scratch, mark);
    if (ok) log_info("карта земли: %s (%ux%u, пиксель 16 м)", path, w, h);
    else log_error("не записать %s", path);
    return ok;
}
