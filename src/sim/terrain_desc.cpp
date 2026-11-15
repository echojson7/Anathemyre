// Разбор описания земли data/terrain.txt (docs/text.md, docs/terrain.md): зерно, размер, локации, карты дюн и
// штампы — площадки и проёмы
#include "terrain.h"
#include "../core/text.h"
#include <string.h>

static void terrain_err(char *err, usize cap, u32 line, const char *what) {
    if (!cap) return;
    usize k = 0;
    char num[12];
    u32 nd = 0;
    do { num[nd++] = (char)('0' + line % 10); line /= 10; } while (line && nd < sizeof num);
    const char *pre = "строка ";
    for (const char *p = pre; *p && k + 1 < cap; ++p) err[k++] = *p;
    while (nd && k + 1 < cap) err[k++] = num[--nd];
    if (k + 2 < cap) { err[k++] = ':'; err[k++] = ' '; }
    for (const char *p = what; *p && k + 1 < cap; ++p) err[k++] = *p;
    err[k] = 0;
}

// Метры с дробью до сотых → целые сантиметры (без float: проём одинаков на любой цели)
static bool parse_cm(Str s, i32 &out) {
    u32 k = 0;
    bool neg = k < s.n && s.p[k] == '-';
    if (neg) k++;
    i64 v = 0;
    u32 digits = 0, frac = 0;
    bool dot = false;
    for (; k < s.n; ++k) {
        char ch = s.p[k];
        if (ch == '.' && !dot) { dot = true; continue; }
        if (ch < '0' || ch > '9' || frac == 2 || digits == 9) return false;
        v = v * 10 + (ch - '0');
        digits++;
        if (dot) frac++;
    }
    if (!digits) return false;
    for (; frac < 2; ++frac) v *= 10;
    out = (i32)(neg ? -v : v);
    return true;
}

// seed <n> · chunks <n, чётное> · ring <м> · locations <valley|desert>… (с запада на восток) ·
// dunes <номер локации> <файл .dun в data/> · pad <x> <z> <w> <d> [<переход м>] · hole <x0> <z0> <x1> <z1>
bool terrain_desc_parse(const u8 *text, usize size, TerrainDesc &out, char *err, usize err_cap) {
    out = TerrainDesc{};
    TextReader r = text_reader(text, size);
    TextLine l;
    while (text_next(r, l)) {
        if (l.count >= 2 && str_eq(l.w[0], "locations")) {
            if (l.count - 1 > TERRAIN_MAX_LOCATIONS) { terrain_err(err, err_cap, l.line, "слишком много локаций"); return false; }
            out.locations = l.count - 1;
            for (u32 k = 1; k < l.count; ++k) {
                if (str_eq(l.w[k], "valley")) out.biome[k - 1] = BIOME_VALLEY;
                else if (str_eq(l.w[k], "desert")) out.biome[k - 1] = BIOME_DESERT;
                else { terrain_err(err, err_cap, l.line, "локация: valley или desert"); return false; }
            }
            continue;
        }
        if (l.count == 3 && str_eq(l.w[0], "dunes")) {
            u32 loc = 0;
            if (!parse_u32(l.w[1], loc) || loc >= TERRAIN_MAX_LOCATIONS || l.w[2].n >= sizeof out.dune_path[0]) {
                terrain_err(err, err_cap, l.line, "dunes <локация> <файл>");
                return false;
            }
            memcpy(out.dune_path[loc], l.w[2].p, l.w[2].n);
            out.dune_path[loc][l.w[2].n] = 0;
            continue;
        }
        if ((l.count == 5 || l.count == 6) && str_eq(l.w[0], "pad")) {
            if (out.pad_count >= TERRAIN_MAX_PADS) { terrain_err(err, err_cap, l.line, "слишком много площадок"); return false; }
            TerrainPad &p = out.pad[out.pad_count++];
            p.margin = 16;
            if (!parse_i32(l.w[1], p.x) || !parse_i32(l.w[2], p.z) || !parse_i32(l.w[3], p.w) || !parse_i32(l.w[4], p.d) ||
                (l.count == 6 && !parse_i32(l.w[5], p.margin)) || p.w < 1 || p.d < 1 || p.w > 512 || p.d > 512 ||
                p.margin < 1 || p.margin > 256) {
                terrain_err(err, err_cap, l.line, "pad <x> <z> <ширина 1..512> <глубина 1..512> [<переход 1..256 м>]");
                return false;
            }
            continue;
        }
        if (l.count == 5 && str_eq(l.w[0], "hole")) {
            if (out.hole_count >= TERRAIN_MAX_HOLES) { terrain_err(err, err_cap, l.line, "слишком много проёмов"); return false; }
            TerrainHole &h = out.hole[out.hole_count++];
            if (!parse_cm(l.w[1], h.x0) || !parse_cm(l.w[2], h.z0) || !parse_cm(l.w[3], h.x1) || !parse_cm(l.w[4], h.z1) ||
                h.x1 <= h.x0 || h.z1 <= h.z0 || h.x1 - h.x0 > 6400 || h.z1 - h.z0 > 6400) {
                terrain_err(err, err_cap, l.line, "hole <x0> <z0> <x1> <z1> (метры, до сантиметра; сторона до 64 м)");
                return false;
            }
            continue;
        }
        if (l.count != 2) { terrain_err(err, err_cap, l.line, "ожидается <ключ> <значение>"); return false; }
        bool ok = true;
        if (str_eq(l.w[0], "seed")) ok = parse_u32(l.w[1], out.seed);
        else if (str_eq(l.w[0], "chunks")) ok = parse_i32(l.w[1], out.chunks) && out.chunks >= 4 && out.chunks <= 200 && out.chunks % 2 == 0;
        else if (str_eq(l.w[0], "ring")) ok = parse_f32(l.w[1], out.ring) && out.ring >= 256;
        else { terrain_err(err, err_cap, l.line, "неизвестный ключ"); return false; }
        if (!ok) { terrain_err(err, err_cap, l.line, "плохое значение"); return false; }
    }
    return true;
}
