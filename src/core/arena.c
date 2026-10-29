#include "arena.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"

bool arena_init(Arena *a, size_t size)
{
    a->base = malloc(size);
    a->size = a->base ? size : 0;
    a->used = 0;
    return a->base != NULL;
}

void arena_free(Arena *a)
{
    free(a->base);
    a->base = NULL;
    a->size = a->used = 0;
}

void *arena_push(Arena *a, size_t size, size_t align)
{
    size_t start = (a->used + (align - 1)) & ~(align - 1);
    if (start + size > a->size) {
        log_error("АРЕНА: исчерпана (%zu из %zu байт, запрос %zu)", a->used, a->size, size);
        return NULL;
    }
    a->used = start + size;
    return a->base + start;
}

void *arena_push_zero(Arena *a, size_t size, size_t align)
{
    void *p = arena_push(a, size, align);
    if (p) memset(p, 0, size);
    return p;
}

void arena_reset(Arena *a)
{
    a->used = 0;
}
