// Выписка для кадра: в конце шага симуляция копирует то, что нужно кадру. Кадр читает только выписки,
// не таблицы мира (кольцо из трёх: две читает вид, третью пишет шаг). docs/architecture.md
#pragma once
#include "world.h"

struct Extract {
    u64 step;
    u16 *gen;      // 0 — места нет или точка мертва
    i32 *cx, *cz;
    f32 *x, *z;
    u32 attract_on;
    WPos attract_at;
};

void extract_init(Extract &e, Arena &arena);
void sim_extract(const World &w, Extract &e);

struct ExtractRing {
    Extract slot[3];
    u32 head = 0;    // последняя записанная
    u32 filled = 0;
};

void ring_init(ExtractRing &r, Arena &arena);
// Место под следующую выписку и пометка, что она записана
inline Extract &ring_next(ExtractRing &r) { return r.slot[(r.head + 1) % 3]; }
inline void ring_commit(ExtractRing &r) { r.head = (r.head + 1) % 3; if (r.filled < 3) r.filled++; }
inline const Extract &ring_cur(const ExtractRing &r) { return r.slot[r.head]; }
inline const Extract &ring_prev(const ExtractRing &r) { return r.slot[r.filled > 1 ? (r.head + 2) % 3 : r.head]; }
