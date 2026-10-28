// Линейная арена: вся память выделяется при старте, в цикле только push/reset
#ifndef ANA_ARENA_H
#define ANA_ARENA_H

#include "types.h"

typedef struct Arena {
    u8    *base;
    size_t size;
    size_t used;
} Arena;

bool  arena_init(Arena *a, size_t size);
void  arena_free(Arena *a);
void *arena_push(Arena *a, size_t size, size_t align);
void *arena_push_zero(Arena *a, size_t size, size_t align);
void  arena_reset(Arena *a);

#define ARENA_PUSH_ARRAY(a, T, n) ((T *)arena_push_zero((a), sizeof(T) * (size_t)(n), _Alignof(T)))

#endif
