// Слой rb: рецепты SPIR-V и запись их списка. Сам слой — rb_vk.c
#include "rb_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"

static const char *const k_stage[] = { "vert", "frag", "comp" };

static void put(char *s, size_t size, size_t *n, const char *a, size_t len)
{
    if (*n + len >= size) {
        *n = size;
        return;
    }
    memcpy(s + *n, a, len);
    *n += len;
    s[*n] = 0;
}

bool rb_spv_recipe(const RbPipeDesc *d, RbStage stage, char *line, size_t line_size, char *name, size_t name_size)
{
    const char *const *files = stage == RB_STAGE_VERT ? d->vs : stage == RB_STAGE_FRAG ? d->fs : d->cs;
    size_t nl = 0, nn = 0;
    line[0] = name[0] = 0;
    put(line, line_size, &nl, k_stage[stage], strlen(k_stage[stage]));
    // Определения: строки «#define ИМЯ [ЗНАЧ]» → -DИМЯ[=ЗНАЧ]
    char defs[256] = "";
    size_t nd = 0;
    for (const char *p = d->defines; p && *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len > 8 && !strncmp(p, "#define ", 8)) {
            const char *q = p + 8, *end = p + len;
            const char *sp = memchr(q, ' ', (size_t)(end - q));
            put(line, line_size, &nl, " -D", 3);
            put(defs, sizeof defs, &nd, "~", 1);
            put(line, line_size, &nl, q, (size_t)((sp ? sp : end) - q));
            put(defs, sizeof defs, &nd, q, (size_t)((sp ? sp : end) - q));
            if (sp) {
                while (sp < end && *sp == ' ') sp++;
                put(line, line_size, &nl, "=", 1);
                put(defs, sizeof defs, &nd, "=", 1);
                put(line, line_size, &nl, sp, (size_t)(end - sp));
                put(defs, sizeof defs, &nd, sp, (size_t)(end - sp));
            }
        }
        p = e ? e + 1 : p + len;
    }
    // Размер блока шейдинга нужен только шейдеру фрагментов (rb.glsl)
    if (stage == RB_STAGE_FRAG && d->frag_size) {
        put(line, line_size, &nl, " -DRB_VRS", 9);
        put(defs, sizeof defs, &nd, "~RB_VRS", 7);
    }
    for (u32 i = 0; i < RB_SHADER_FILES && files[i]; i++) {
        put(line, line_size, &nl, " ", 1);
        put(line, line_size, &nl, files[i], strlen(files[i]));
        if (i) put(name, name_size, &nn, "+", 1);
        put(name, name_size, &nn, files[i], strlen(files[i]));
    }
    put(name, name_size, &nn, defs, nd);
    put(name, name_size, &nn, ".", 1);
    put(name, name_size, &nn, k_stage[stage], strlen(k_stage[stage]));
    put(name, name_size, &nn, ".spv", 4);
    return nl < line_size && nn < name_size && files[0];
}

// ANA_SPV_LIST=файл: дописать в него рецепты всех собранных конвейеров (недостающие .spv собирает tools/spv/build.sh)
static void recipe_log(const RbPipeDesc *d)
{
    const char *path = getenv("ANA_SPV_LIST");
    if (!path || !*path) return;
    for (u32 s = 0; s < 3; s++) {
        char line[512], name[256];
        if (!rb_spv_recipe(d, (RbStage)s, line, sizeof line, name, sizeof name)) continue;
        // Уже есть — не дописывать
        bool have = false;
        FILE *f = fopen(path, "r");
        if (f) {
            char buf[512];
            while (!have && fgets(buf, sizeof buf, f)) {
                buf[strcspn(buf, "\n")] = 0;
                have = !strcmp(buf, line);
            }
            fclose(f);
        }
        if (have) continue;
        f = fopen(path, "a");
        if (!f) return;
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

RbPipe rb_pipe_make(const RbPipeDesc *d)
{
    recipe_log(d);
    return rb_vk_pipe_make(d);
}
