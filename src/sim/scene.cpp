#include "scene.h"
#include "../core/text.h"
#include "ground.h"

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

// Ключи вида «слово значение» после обязательных слов: yaw, scale, lift, material, seed
struct SceneKeys {
    f32 yaw = 0, scale = 1, lift = 0;
    u32 seed = 0;
    u64 material = 0;
};
static bool scene_keys(const TextLine &l, u32 from, SceneKeys &k, const char *allowed, char *err, usize err_cap) {
    for (u32 i = from; i < l.count; i += 2) {
        if (i + 1 >= l.count) { scene_err(err, err_cap, l.line, "у ключа нет значения"); return false; }
        Str key = l.w[i], v = l.w[i + 1];
        // allowed — ключи через пробел; ключ должен быть среди них
        bool known = false;
        for (const char *a = allowed; *a;) {
            const char *e = a;
            while (*e && *e != ' ') ++e;
            if ((usize)(e - a) == key.n && !memcmp(a, key.p, key.n)) known = true;
            a = *e ? e + 1 : e;
        }
        if (!known) { scene_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
        bool ok = true;
        if (str_eq(key, "yaw")) ok = parse_f32(v, k.yaw);
        else if (str_eq(key, "scale")) ok = parse_f32(v, k.scale) && k.scale > 0;
        else if (str_eq(key, "lift")) ok = parse_f32(v, k.lift);
        else if (str_eq(key, "seed")) ok = parse_u32(v, k.seed);
        else if (str_eq(key, "material")) k.material = str_hash(v);
        if (!ok) { scene_err(err, err_cap, l.line, "плохое число"); return false; }
    }
    return true;
}

// Сдвиг (dx, dz) в осях заготовки, повёрнутой на quarter четвертей оборота, — как quat_yaw: +90° ведёт x в −z.
// Только четверти: поворот точный, без sin/cos
static void quarter_rotate(u32 quarter, f32 &dx, f32 &dz) {
    f32 x = dx, z = dz;
    switch (quarter & 3) {
        case 0: break;
        case 1: dx = z; dz = -x; break;
        case 2: dx = -x; dz = -z; break;
        default: dx = -z; dz = x; break;
    }
}

static WPos offset_pos(const WPos &o, f32 dx, f32 dy, f32 dz) {
    WPos q = o;
    q.x += dx;
    q.y += dy;
    q.z += dz;
    wpos_rebase(q);
    return q;
}

// Заготовка (data/prefabs/<имя>.txt, docs/text.md) в точке o (с высотой земли), повёрнутая на quarter четвертей:
//   part <сетка> <x> <y> <z> [yaw градусы] [material имя] [seed n]
//   decal <вид> <материал> <x> <y> <z> <sx> <sy> <sz> [yaw градусы] [seed n]
static bool prefab_load(World &w, const u8 *text, usize size, const WPos &o, u32 quarter, u32 seed0, char *err,
                        usize err_cap) {
    const u32 turn0 = quarter * 0x40000000u;
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count > TEXT_MAX_WORDS) { scene_err(err, err_cap, l.line, "слишком много слов"); return false; }
        bool part = str_eq(l.w[0], "part"), decal = str_eq(l.w[0], "decal");
        if (!part && !decal) { scene_err(err, err_cap, l.line, "заготовка: ожидается part или decal"); return false; }
        u32 need = part ? 5 : 9;
        if (l.count < need) {
            scene_err(err, err_cap, l.line, part ? "part <сетка> <x> <y> <z>" : "decal <вид> <материал> <x> <y> <z> <sx> <sy> <sz>");
            return false;
        }
        u32 at = part ? 2 : 3;
        f32 v[6] = {};
        for (u32 i = 0; i < (part ? 3u : 6u); ++i)
            if (!parse_f32(l.w[at + i], v[i])) { scene_err(err, err_cap, l.line, "координаты и размеры — числа"); return false; }
        SceneKeys k;
        k.seed = seed0 * 4099u + l.line;
        if (!scene_keys(l, need, k, part ? "yaw material seed" : "yaw seed", err, err_cap)) return false;
        f32 dx = v[0], dz = v[2];
        quarter_rotate(quarter, dx, dz);
        WPos q = offset_pos(o, dx, v[1], dz);
        u32 yaw = turn0 + turn_from_deg(k.yaw);
        if (part) {
            if (!props_add_at(w, str_hash(l.w[1]), k.material, q, yaw, 1.0f, k.seed)) {
                scene_err(err, err_cap, l.line, "таблица объектов полна");
                return false;
            }
        } else {
            f32 h[3] = {v[3] * 0.5f, v[4] * 0.5f, v[5] * 0.5f};
            if (h[0] <= 0 || h[1] <= 0 || h[2] <= 0) { scene_err(err, err_cap, l.line, "размеры декали > 0"); return false; }
            if (!decals_add(w, str_hash(l.w[1]), str_hash(l.w[2]), q, yaw, h, k.seed)) {
                scene_err(err, err_cap, l.line, "таблица декалей полна");
                return false;
            }
        }
    }
    return true;
}

// prop <сетка> <x> <z> [yaw <градусы>] [scale <k>] [lift <м>] [material <имя>] [seed <n>]
// prefab <имя> <x> <z> [yaw 0|90|180|270]
// x, z — метры от начала чанка 0, 0; высота — земля под точкой плюс lift
bool scene_load(World &w, const u8 *text, usize size, ScenePrefabFn prefab, void *ctx, char *err, usize err_cap) {
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
        bool is_prop = str_eq(l.w[0], "prop"), is_prefab = str_eq(l.w[0], "prefab");
        if (!is_prop && !is_prefab) {
            scene_err(err, err_cap, l.line, "неизвестное слово (ожидается prop, prefab или clock)");
            return false;
        }
        if (l.count < 4) { scene_err(err, err_cap, l.line, is_prop ? "prop <сетка> <x> <z>" : "prefab <имя> <x> <z>"); return false; }
        f32 x = 0, z = 0;
        if (!parse_f32(l.w[2], x) || !parse_f32(l.w[3], z)) { scene_err(err, err_cap, l.line, "x и z — числа"); return false; }
        SceneKeys k;
        k.seed = l.line;
        if (!scene_keys(l, 4, k, is_prop ? "yaw scale lift material seed" : "yaw", err, err_cap)) return false;
        WPos at;
        at.x = x;
        at.z = z;
        if (is_prop) {
            if (!props_add(w, str_hash(l.w[1]), k.material, at, k.lift, turn_from_deg(k.yaw), k.scale, k.seed)) {
                scene_err(err, err_cap, l.line, "таблица объектов полна");
                return false;
            }
            continue;
        }
        // Заготовка: поворот — только четвертями, высота — земля под серединой (площадка ровная)
        i32 deg = (i32)k.yaw;
        if ((f32)deg != k.yaw || deg % 90 != 0) { scene_err(err, err_cap, l.line, "yaw заготовки — 0, 90, 180 или 270"); return false; }
        u32 quarter = (u32)(((deg / 90) % 4 + 4) % 4);
        const u8 *pt = nullptr;
        usize ps = 0;
        if (!prefab || !prefab(ctx, l.w[1], &pt, &ps)) { scene_err(err, err_cap, l.line, "нет такой заготовки"); return false; }
        wpos_rebase(at);
        at.y = ground_at(w.terrain, at).height;
        char sub[192];
        if (!prefab_load(w, pt, ps, at, quarter, l.line, sub, sizeof sub)) {
            scene_err(err, err_cap, l.line, "заготовка, ");
            usize n = 0;
            while (err[n] && n + 1 < err_cap) ++n;
            for (const char *p = sub; *p && n + 1 < err_cap; ++p) err[n++] = *p;
            err[n] = 0;
            return false;
        }
    }
    return true;
}
