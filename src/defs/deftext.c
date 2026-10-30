#include "deftext.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../platform/platform.h"

typedef struct Names {
    Arena *mem;
    u32 n;
    const char *name[DEF_FILES];
    bool dirs, overflow;  // dirs: собираются подпапки, иначе файлы *.txt
} Names;

static void collect(const char *name, bool dir, void *user)
{
    Names *s = user;
    size_t len = strlen(name);
    if (dir != s->dirs || name[0] == '.') return;
    if (!dir && (len < 5 || strcmp(name + len - 4, ".txt"))) return;
    if (s->n == DEF_FILES) {
        s->overflow = true;
        return;
    }
    char *copy = arena_push(s->mem, len + 1, 1);
    if (!copy) {
        s->overflow = true;
        return;
    }
    memcpy(copy, name, len + 1);
    s->name[s->n++] = copy;
}

static int cmp_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static bool list(Names *s, Arena *mem, const char *dir, bool dirs)
{
    *s = (Names){ .mem = mem, .dirs = dirs };
    if (!platform_list_dir(dir, collect, s)) return false;
    qsort(s->name, s->n, sizeof s->name[0], cmp_name);
    return true;
}

static bool add_file(DefText *t, Arena *mem, const char *dir, const char *name)
{
    if (t->files == DEF_FILES) {
        log_error("ОПИСАНИЯ: файлов больше %u", DEF_FILES);
        return false;
    }
    size_t plen = strlen(dir) + strlen(name) + 2;
    char *path = arena_push(mem, plen, 1);
    if (!path) return false;
    snprintf(path, plen, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("ОПИСАНИЯ: %s не открывается", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = size >= 0 ? arena_push(mem, (size_t)size + 1, 1) : NULL;
    bool ok = text && fread(text, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok) {
        log_error("ОПИСАНИЯ: %s не читается или не помещается в память описаний", path);
        return false;
    }
    text[size] = 0;
    t->path[t->files] = path;
    t->text[t->files] = text;
    t->size[t->files] = (u32)size;
    t->files++;
    return true;
}

bool deftext_open(DefText *t, Arena *mem, const char *base_dir, const char *mods_dir)
{
    memset(t, 0, sizeof *t);
    static Names files, mods;
    if (!list(&files, mem, base_dir, false)) {
        log_error("ОПИСАНИЯ: папки %s нет", base_dir);
        return false;
    }
    if (files.overflow) return false;
    for (u32 i = 0; i < files.n; i++)
        if (!add_file(t, mem, base_dir, files.name[i])) return false;
    // Моды: каждая подпапка — мод, читаются после встроенных и правят их записи по имени
    if (!mods_dir || !list(&mods, mem, mods_dir, true)) return true;
    for (u32 m = 0; m < mods.n; m++) {
        size_t dlen = strlen(mods_dir) + strlen(mods.name[m]) + 2;
        char *dir = arena_push(mem, dlen, 1);
        if (!dir) return false;
        snprintf(dir, dlen, "%s/%s", mods_dir, mods.name[m]);
        if (!list(&files, mem, dir, false) || files.overflow) return false;
        for (u32 i = 0; i < files.n; i++)
            if (!add_file(t, mem, dir, files.name[i])) return false;
        log_info("ОПИСАНИЯ: мод %s, файлов %u", mods.name[m], files.n);
        t->mods++;
    }
    return true;
}

void deftext_rewind(DefText *t) { t->at_file = t->at_pos = t->at_line = 0; }

void def_error(const DefLine *l, const char *fmt, ...)
{
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    log_error("ОПИСАНИЯ: %s:%u: %s", l->file, l->line, msg);
}

static bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

bool deftext_next(DefText *t, DefLine *l, bool *bad)
{
    *bad = false;
    while (t->at_file < t->files) {
        const char *text = t->text[t->at_file];
        u32 size = t->size[t->at_file];
        if (t->at_pos >= size) {
            t->at_file++;
            t->at_pos = t->at_line = 0;
            continue;
        }
        u32 p = t->at_pos, end = p;
        while (end < size && text[end] != '\n') end++;
        t->at_pos = end + 1;
        t->at_line++;
        *l = (DefLine){ .file = t->path[t->at_file], .line = t->at_line };
        while (p < end) {
            while (p < end && space(text[p])) p++;
            if (p >= end || text[p] == '#') break;
            if (l->n == DEF_TOKENS) {
                def_error(l, "больше %u слов в строке", DEF_TOKENS);
                *bad = true;
                return false;
            }
            DefTok *k = &l->tok[l->n++];
            if (text[p] == '"') {
                u32 q = p + 1;
                while (q < end && text[q] != '"') q++;
                if (q >= end) {
                    def_error(l, "кавычка не закрыта");
                    *bad = true;
                    return false;
                }
                *k = (DefTok){ text + p + 1, q - p - 1, true };
                p = q + 1;
            } else {
                u32 q = p;
                while (q < end && !space(text[q]) && text[q] != '#' && text[q] != '"') q++;
                *k = (DefTok){ text + p, q - p, false };
                p = q;
            }
        }
        if (l->n) return true;
    }
    return false;
}

bool def_is(const DefTok *t, const char *word) { return !t->quoted && strlen(word) == t->n && !memcmp(t->s, word, t->n); }

i32 def_word(const DefTok *t, const char *const *words, u32 count)
{
    for (u32 i = 0; i < count; i++)
        if (words[i] && def_is(t, words[i])) return (i32)i;
    return -1;
}

bool def_num(const DefTok *t, f32 *out)
{
    // Целая мантисса и число знаков после точки: значение = m / 10^frac, деление в double точное до
    // округления, затем к float. Не зависит от локали и от strtof системы
    u32 i = 0, digits = 0, frac = 0;
    bool neg = false, dot = false;
    u64 m = 0;
    if (t->quoted || !t->n) return false;
    if (t->s[0] == '-') {
        neg = true;
        i = 1;
    }
    for (; i < t->n; i++) {
        char c = t->s[i];
        if (c == '.' && !dot) {
            dot = true;
            continue;
        }
        if (c < '0' || c > '9' || digits == 18) return false;
        m = m * 10u + (u64)(c - '0');
        digits++;
        if (dot) frac++;
    }
    if (!digits) return false;
    static const double p10[19] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
                                    1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18 };
    double v = (double)m / p10[frac];
    *out = (f32)(neg ? -v : v);
    return true;
}

bool def_uint(const DefTok *t, u32 *out)
{
    u64 v = 0;
    if (t->quoted || !t->n || t->n > 9) return false;
    for (u32 i = 0; i < t->n; i++) {
        if (t->s[i] < '0' || t->s[i] > '9') return false;
        v = v * 10u + (u64)(t->s[i] - '0');
    }
    *out = (u32)v;
    return true;
}

const char *def_copy(Arena *mem, const DefTok *t)
{
    char *s = arena_push(mem, t->n + 1u, 1);
    if (!s) return NULL;
    memcpy(s, t->s, t->n);
    s[t->n] = 0;
    return s;
}
