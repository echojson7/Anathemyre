#include "mem.h"

void arena_init(Arena &a, void *memory, usize size, const char *name) {
    a.base = (u8 *)memory;
    a.size = size;
    a.used = 0;
    a.name = name;
}

void *arena_push_bytes(Arena &a, usize n, usize align) {
    usize at = (a.used + (align - 1)) & ~(align - 1);
    if (at + n > a.size) ana_fail(__FILE__, __LINE__, a.name);
    a.used = at + n;
    return a.base + at;
}
