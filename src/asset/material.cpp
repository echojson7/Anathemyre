#include "material.h"
#include "../core/hash.h"
#include <stdio.h>

static bool fail(char *err, usize cap, u32 line, const char *what) {
    snprintf(err, cap, "строка %u: %s", line, what);
    return false;
}

static bool numbers(const TextLine &l, f32 *dst, u32 n) {
    if (l.count != n + 1) return false;
    for (u32 i = 0; i < n; ++i)
        if (!parse_f32(l.w[i + 1], dst[i])) return false;
    return true;
}

// material <имя> <вид>
//     color r g b | rough k | color2 r g b | texel м | extra a b c d
bool material_table_parse(const u8 *kinds_text, usize kinds_size, const u8 *text, usize size, Arena &arena,
                          MaterialTable &out, char *err, usize err_cap) {
    out = MaterialTable{};
    TextReader kr = text_reader(kinds_text, kinds_size);
    TextLine l;
    while (text_next(kr, l)) {
        if (l.count != 1 || out.kind_count >= MATERIAL_MAX_KINDS) return fail(err, err_cap, l.line, "список видов испорчен");
        out.kinds[out.kind_count++] = l.w[0];
    }
    out.items = arena_push<MaterialDesc>(arena, MATERIAL_MAX);
    // Номер 0 — missing: вид 0 (первый по имени файла) не важен, цвет кричит
    MaterialDesc &m0 = out.items[out.count++];
    m0.name = {"missing", 7};
    m0.hash = hash_str("missing");
    m0.p[0] = 1.0f; m0.p[1] = 0.0f; m0.p[2] = 1.0f; m0.p[3] = 1.0f; m0.p[7] = 0.25f;
    for (u32 k = 0; k < out.kind_count; ++k)
        if (str_eq(out.kinds[k], "flat")) m0.kind = k;

    TextReader r = text_reader(text, size);
    MaterialDesc *cur = nullptr;
    while (text_next(r, l)) {
        if (l.count > TEXT_MAX_WORDS) return fail(err, err_cap, l.line, "слишком много слов");
        Str k = l.w[0];
        if (str_eq(k, "material")) {
            if (l.count != 3) return fail(err, err_cap, l.line, "material <имя> <вид>");
            if (out.count >= MATERIAL_MAX) return fail(err, err_cap, l.line, "материалов больше 1024");
            u64 h = str_hash(l.w[1]);
            for (u32 i = 0; i < out.count; ++i)
                if (out.items[i].hash == h) return fail(err, err_cap, l.line, "материал с таким именем уже есть");
            cur = &out.items[out.count++];
            cur->name = l.w[1];
            cur->hash = h;
            u32 kind = MATERIAL_MAX_KINDS;
            for (u32 i = 0; i < out.kind_count; ++i)
                if (out.kinds[i].n == l.w[2].n && memcmp(out.kinds[i].p, l.w[2].p, l.w[2].n) == 0) kind = i;
            if (kind == MATERIAL_MAX_KINDS) return fail(err, err_cap, l.line, "нет такого вида (файла data/shaders/materials/<вид>.glsl)");
            cur->kind = kind;
            cur->p[3] = 0.8f;   // rough
            cur->p[7] = 0.1f;   // texel: квадраты узора 10 см
            continue;
        }
        if (!cur) return fail(err, err_cap, l.line, "параметр до первого material");
        bool ok;
        if (str_eq(k, "color")) ok = numbers(l, cur->p + 0, 3);
        else if (str_eq(k, "rough")) ok = numbers(l, cur->p + 3, 1);
        else if (str_eq(k, "color2")) ok = numbers(l, cur->p + 4, 3);
        else if (str_eq(k, "texel")) ok = numbers(l, cur->p + 7, 1) && cur->p[7] > 0.0f;
        else if (str_eq(k, "extra")) ok = numbers(l, cur->p + 8, 4);
        else return fail(err, err_cap, l.line, "неизвестный параметр");
        if (!ok) return fail(err, err_cap, l.line, "не те числа у параметра");
    }
    return true;
}

u32 material_find(const MaterialTable &t, u64 hash) {
    for (u32 i = 0; i < t.count; ++i)
        if (t.items[i].hash == hash) return i;
    return 0;
}
