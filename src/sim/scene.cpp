#include "scene.h"
#include "../core/text.h"

// Сообщение об ошибке без printf (запрет в шаге мира): строка + номер строки файла
static void scene_err(char *err, usize cap, u32 line, const char *what) {
    if (!cap) return;
    usize n = 0;
    const char *pre = "строка ";
    for (const char *p = pre; *p && n + 1 < cap; ++p) err[n++] = *p;
    char num[12];
    u32 k = 0;
    do { num[k++] = (char)('0' + line % 10); line /= 10; } while (line && k < sizeof num);
    while (k && n + 1 < cap) err[n++] = num[--k];
    if (n + 2 < cap) { err[n++] = ':'; err[n++] = ' '; }
    for (const char *p = what; *p && n + 1 < cap; ++p) err[n++] = *p;
    err[n] = 0;
}

// Градусы → обороты (Turn). f64 точно хранит любое f32, деление и умножение IEEE одинаковы на всех целях
static u32 turn_from_deg(f32 deg) {
    f64 t = (f64)deg / 360.0;
    t -= (f64)(i64)t;
    if (t < 0) t += 1.0;
    return (u32)(u64)(t * 4294967296.0);
}

// prop <сетка> <x> <z> [yaw <градусы>] [scale <k>] [lift <м>] [material <имя>] [seed <n>]
// x, z — метры от начала чанка 0, 0; высота — земля под точкой плюс lift
bool scene_load(World &w, const u8 *text, usize size, char *err, usize err_cap) {
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count > TEXT_MAX_WORDS) { scene_err(err, err_cap, l.line, "слишком много слов"); return false; }
        if (str_eq(l.w[0], "clock")) {
            // clock <час начала> <минут на сутки>: часы мира (docs/sky.md)
            f32 hour = 0, minutes = 0;
            if (l.count != 3 || !parse_f32(l.w[1], hour) || !parse_f32(l.w[2], minutes) || hour < 0 || hour >= 24 ||
                minutes < 1 || minutes > 100000) {
                scene_err(err, err_cap, l.line, "clock <час 0..24> <минут на сутки ≥ 1>");
                return false;
            }
            // f64 точно держит f32, деление и умножение IEEE одинаковы на всех целях
            w.clock.day_ticks = (u32)(u64)((f64)minutes * 60.0 * (f64)SIM_HZ);
            w.clock.tick = (u64)((f64)hour / 24.0 * (f64)w.clock.day_ticks);
            continue;
        }
        if (!str_eq(l.w[0], "prop")) { scene_err(err, err_cap, l.line, "неизвестное слово (ожидается prop или clock)"); return false; }
        if (l.count < 4) { scene_err(err, err_cap, l.line, "prop <сетка> <x> <z>"); return false; }
        f32 x = 0, z = 0, yaw = 0, scale = 1, lift = 0;
        u32 seed = l.line;
        u64 mat = 0;
        if (!parse_f32(l.w[2], x) || !parse_f32(l.w[3], z)) { scene_err(err, err_cap, l.line, "x и z — числа"); return false; }
        for (u32 i = 4; i < l.count; i += 2) {
            if (i + 1 >= l.count) { scene_err(err, err_cap, l.line, "у ключа нет значения"); return false; }
            Str k = l.w[i], v = l.w[i + 1];
            bool ok = true;
            if (str_eq(k, "yaw")) ok = parse_f32(v, yaw);
            else if (str_eq(k, "scale")) ok = parse_f32(v, scale) && scale > 0;
            else if (str_eq(k, "lift")) ok = parse_f32(v, lift);
            else if (str_eq(k, "seed")) ok = parse_u32(v, seed);
            else if (str_eq(k, "material")) mat = str_hash(v);
            else { scene_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
            if (!ok) { scene_err(err, err_cap, l.line, "плохое число"); return false; }
        }
        WPos at;
        at.x = x;
        at.z = z;
        if (!props_add(w, str_hash(l.w[1]), mat, at, lift, turn_from_deg(yaw), scale, seed)) {
            scene_err(err, err_cap, l.line, "таблица объектов полна");
            return false;
        }
    }
    return true;
}
