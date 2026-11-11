#include "debug.h"
#include "../core/log.h"
#include "../render/render.h"
#include "../core/math3.h"
#include "../sim/ground.h"
#include "../sim/walk.h"
#include "../sim/world.h"

void debug_keys(const Input &in, DebugState &d) {
    // Просьбы копятся до debug_clear_frame: клавиши и панель за один кадр не затирают друг друга
    if (in.key_pressed[KEY_PERIOD]) d.step_once = true;
    if (in.key_pressed[KEY_L]) d.lines = !d.lines;
    if (in.key_pressed[KEY_P]) d.pixel = !d.pixel;
    if (in.key_pressed[KEY_F1]) debug_print_help();
    d.clock_hours += (in.key_pressed[KEY_RIGHTBRACKET] ? 1 : 0) - (in.key_pressed[KEY_LEFTBRACKET] ? 1 : 0);
    if (in.key_pressed[KEY_T]) d.clock_speed = true;
    if (in.key_pressed[KEY_B]) d.stand_bend = !d.stand_bend;
}

// Люди подставного игрока: 256 человек у Остова (docs/ostov.md), приказы идти — в точки вокруг, внутрь здания и
// на галерею (через марш). Номер берётся из таблицы мира: --sim идёт без кадра
static constexpr u32 AUTOPLAY_PEOPLE = 256;
// Точка у Остова: сдвиг от середины пятна, высота — над площадкой (земля под серединой)
static WPos ostov_at(const World &w, f32 dx, f32 dz, f32 y) {
    WPos c{45, -10, 11704.0f - 45.0f * 256.0f, 0.0f, -2336.0f + 10.0f * 256.0f};
    WPos p = c;
    p.x += dx;
    p.z += dz;
    p.y = ground_at(w.terrain, c).height + y;
    wpos_rebase(p);
    return p;
}

void autoplay_intents(const World &w, Rng &rng, FixedArray<Intent> &out) {
    const u64 step = w.step;
    // Ритм нарочно неровный: намерения попадают на разные фазы шага
    auto at = [&rng]() {
        WPos p;
        p.x = rng_range(rng, -60.0f, 60.0f);
        p.z = rng_range(rng, -60.0f, 60.0f);
        wpos_rebase(p);
        return p;
    };
    if (step % 97 == 13) out.push_back({step, INTENT_ATTRACT, at(), 0});
    if (step % 331 == 200) out.push_back({step, INTENT_BURST, at(), 0});
    if (step % 500 == 499) out.push_back({step, INTENT_RELEASE, WPos{}, 0});
    // Часы: прыжок на случайный час и смена скорости суток — повтор проверяет и числа намерений
    if (step % 1201 == 600) out.push_back({step, INTENT_CLOCK_SET, WPos{}, rng_u32(rng)});
    if (step % 2003 == 1000) out.push_back({step, INTENT_CLOCK_RATE, WPos{}, rng_below(rng, 200)});
    // Люди: толпа в дюнах к западу от Остова в первом шаге
    if (step == 0) out.push_back({step, INTENT_PEOPLE_SPAWN, ostov_at(w, -40.0f, 0.0f, 0.0f), AUTOPLAY_PEOPLE | (25ull << 16)});
    // Приказы: каждые 13 шагов — двоим случайным (каждому раз в ~30 с); цели — дюны вокруг, пол Остова,
    // галерея над маршем
    if (step % 13 == 3 && w.people.live) {
        for (u32 k = 0; k < 2; ++k) {
            u32 s = rng_below(rng, AUTOPLAY_PEOPLE);
            if (!w.people.alive[s]) continue;
            u32 id = s | ((u32)w.people.gen[s] << 16);
            u32 kind = rng_below(rng, 10);
            // Аргументы — отдельными строками: порядок их вычисления у clang и gcc разный, а RNG один
            f32 dx, dz, y;
            if (kind < 4) { dx = rng_range(rng, -60.0f, 60.0f); dz = rng_range(rng, -60.0f, 60.0f); y = 0.0f; }
            else if (kind < 8) { dx = rng_range(rng, -13.5f, 13.5f); dz = rng_range(rng, -8.5f, 10.5f); y = 0.25f; }
            else { dx = rng_range(rng, -13.5f, 12.0f); dz = rng_range(rng, 9.0f, 11.0f); y = 4.0f; }
            WPos t = ostov_at(w, dx, dz, y);
            u64 gait = rng_below(rng, 3);
            out.push_back({step, INTENT_GO, t, (u64)id | (gait << 32)});
        }
    }
}

void debug_print_help() {
    log_info("клавиши: Esc настройки, F3 панель отладки, F11 весь экран");
    log_info("отладка: Space пауза, . шаг на паузе, - = медленнее/быстрее, L отрезки, P ячейки");
    log_info("камера: средняя кнопка или Alt + правая — вращать, Shift + средняя — двигать, колесо — ближе/дальше,");
    log_info("        WASD — двигать, Shift — быстрее, C — за выбранным человеком");
    log_info("        F — свободный полёт (WASD, Q/E вниз/вверх, средняя кнопка — взгляд) и обратно на орбиту");
    log_info("люди: левая кнопка — выбрать, правая — идти шагом (Ctrl — трусцой, Shift — бегом)");
    log_info("точки: левая кнопка мимо людей — притянуть, Ctrl + левая — вспышка, R — отпустить");
    log_info("сутки: [ ] — час назад/вперёд, T — скорость суток (обычная, ×20, ×120, стоп)");
    log_info("стенд людей (у Остова, вкл. в F3): B — согнуть кость, скелет и морф — в F3");
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
// (песок — охра, в пустыне — оранжевее, трава — зелёная, шероховатое — серее, вода — синяя), высота — светлее. Север сверху
bool debug_write_terrain_map(const Terrain &t, const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    u32 w = (t.nx - 1) / 2, h = (t.nz - 1) / 2;
    u8 *rgba = arena_push<u8>(scratch, (usize)w * h * 4);
    for (u32 py = 0; py < h; ++py) {
        u32 j = (h - 1 - py) * 2;  // север сверху
        for (u32 px = 0; px < w; ++px) {
            u32 i = px * 2;
            auto H = [&](u32 a, u32 b) { return (f32)t.height[(usize)b * t.nx + a] * 0.05f; };
            f32 dx = (H(i + 2, j) - H(i, j)) / 16.0f, dz = (H(i, j + 2) - H(i, j)) / 16.0f;
            Vec3 nn = normalize(Vec3{-dx, 1.0f, -dz});
            f32 lit = f_clamp(dot(nn, normalize(Vec3{-0.6f, 0.55f, 0.6f})), 0.0f, 1.0f);
            usize mk = (usize)(j >> 2) * t.nmx + (i >> 2);
            u32 m = t.mask[mk];
            f32 desert = (f32)((t.mask2[mk] >> 8) & 255) / 255.0f;
            f32 rough = (f32)(m & 255) / 255.0f, sand = (f32)((m >> 8) & 255) / 255.0f;
            f32 grass = (f32)((m >> 16) & 255) / 255.0f;
            i32 wl = terrain_water(t, (i32)i * 8, (i32)j * 8);
            bool wet = wl && wl > terrain_node(t, (i32)i * 8, (i32)j * 8);
            f32 alt = f_clamp(H(i, j) / 1800.0f, 0.0f, 1.0f);
            f32 c[3] = {0.55f, 0.45f, 0.33f};
            f32 sandc[3] = {0.86f, 0.70f, 0.45f}, rockc[3] = {0.50f, 0.48f, 0.46f};
            const f32 dsand[3] = {0.95f, 0.66f, 0.38f}, drock[3] = {0.62f, 0.45f, 0.36f};
            for (u32 k = 0; k < 3; ++k) {
                sandc[k] += (dsand[k] - sandc[k]) * desert;
                rockc[k] += (drock[k] - rockc[k]) * desert;
            }
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

// Карта сетки ходимости у Остова (--walk-map): 40 × 40 м, клетка 0.5 м — 14 пикселей, север сверху. Нет уровня —
// тёмно-красный; земля — песочный; пол постройки — от синего (низ) к белому (верх плиты); две плиты над клеткой —
// полоска. Поверх — путь подставного приказа: от входа на галерею по маршу (A* + сглаживание)
bool debug_write_walk_map(const World &w, const char *path, Arena &scratch) {
    usize mark = arena_mark(scratch);
    WPos c = ostov_at(w, 0.0f, 1.0f, 0.0f);
    u32 ci, cj;
    i32 ar = walk_area_of(w.walk, c.cx, c.cz, c.x, c.z, &ci, &cj);
    if (ar < 0) { log_error("--walk-map: Остов вне сетки ходимости"); return false; }
    const WalkArea &a = w.walk.area[ar];
    const u32 cells = 80, px = 14, size = cells * px;
    u8 *rgba = arena_push<u8>(scratch, (usize)size * size * 4);
    i32 i0 = (i32)ci - (i32)cells / 2, j0 = (i32)cj - (i32)cells / 2;
    i32 base = (i32)(c.y * 100.0f) - a.base_cm;
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            i32 i = i0 + (i32)(x / px), j = j0 + (i32)(cells - 1 - y / px);
            u8 *o = rgba + ((usize)y * size + x) * 4;
            o[0] = 90; o[1] = 20; o[2] = 20; o[3] = 255;
            if (i < 0 || j < 0 || i >= (i32)WALK_N || j >= (i32)WALK_N) continue;
            u32 cell = (u32)j * WALK_N + (u32)i;
            u32 n = a.nlev[cell];
            if (!n) continue;
            // Две плиты над клеткой: верхняя половина пикселей клетки — верхний уровень
            u32 l = (n > 1 && (y % px) < px / 2) ? n - 1 : 0;
            if (a.box[cell * 4 + l]) {
                f32 t = f_clamp((f32)(a.h[cell * 4 + l] - base) / 450.0f, 0.0f, 1.0f);
                o[0] = (u8)(60 + 195 * t); o[1] = (u8)(90 + 165 * t); o[2] = (u8)(200 + 55 * t);
            } else {
                o[0] = 200; o[1] = 170; o[2] = 110;
            }
            if ((x % px) == 0 || (y % px) == 0) { o[0] = (u8)(o[0] * 7 / 8); o[1] = (u8)(o[1] * 7 / 8); o[2] = (u8)(o[2] * 7 / 8); }
        }
    // Путь: от входа на галерею
    WPos from = ostov_at(w, 3.0f, -16.0f, 0.0f), to = ostov_at(w, -6.0f, 10.0f, 4.0f);
    u32 fi, fj, ti, tj, nf, nt;
    walk_area_of(w.walk, from.cx, from.cz, from.x, from.z, &fi, &fj);
    walk_area_of(w.walk, to.cx, to.cz, to.x, to.z, &ti, &tj);
    if (walk_node_near(a, fi, fj, from.y, 8, &nf) && walk_node_near(a, ti, tj, to.y, 8, &nt)) {
        WalkSearch ws;
        ws.max_expand = 400000;
        u32 out[256];
        u32 n = walk_find(a, nf, nt, scratch, ws, out, 256);
        log_info("--walk-map: путь от входа на галерею — %s, узлов после сглаживания %u, раскрыто %u", ws.reached ? "дошёл" : "НЕ дошёл", n, ws.expanded);
        u32 prev = nf;
        for (u32 k = 0; k < n; ++k) {
            i32 ax = ((i32)walk_node_i(prev) - i0) * (i32)px + (i32)px / 2, ay = ((i32)cells - 1 - ((i32)walk_node_j(prev) - j0)) * (i32)px + (i32)px / 2;
            i32 bx = ((i32)walk_node_i(out[k]) - i0) * (i32)px + (i32)px / 2, by = ((i32)cells - 1 - ((i32)walk_node_j(out[k]) - j0)) * (i32)px + (i32)px / 2;
            i32 steps = (bx > ax ? bx - ax : ax - bx) + (by > ay ? by - ay : ay - by) + 1;
            for (i32 t = 0; t <= steps; ++t) {
                i32 x = ax + (bx - ax) * t / steps, y = ay + (by - ay) * t / steps;
                for (i32 d = -1; d <= 1; ++d) {
                    i32 xx = x + d, yy = y;
                    if (xx < 0 || yy < 0 || xx >= (i32)size || yy >= (i32)size) continue;
                    u8 *o = rgba + ((usize)yy * size + (usize)xx) * 4;
                    o[0] = 30; o[1] = 200; o[2] = 60;
                }
            }
            prev = out[k];
        }
    }
    bool ok = write_bmp(path, rgba, size, size, scratch);
    arena_restore(scratch, mark);
    return ok;
}
