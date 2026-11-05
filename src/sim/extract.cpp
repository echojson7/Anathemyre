#include "extract.h"

void extract_init(Extract &e, Arena &arena) {
    e = Extract{};
    e.gen = arena_push<u16>(arena, DOTS_CAPACITY);
    e.cx = arena_push<i32>(arena, DOTS_CAPACITY);
    e.cz = arena_push<i32>(arena, DOTS_CAPACITY);
    e.x = arena_push<f32>(arena, DOTS_CAPACITY);
    e.z = arena_push<f32>(arena, DOTS_CAPACITY);
}

void sim_extract(const World &w, Extract &e) {
    const Dots &d = w.dots;
    e.step = w.step;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) e.gen[i] = d.alive[i] ? d.gen[i] : 0;
    memcpy(e.cx, d.cx, DOTS_CAPACITY * sizeof *e.cx);
    memcpy(e.cz, d.cz, DOTS_CAPACITY * sizeof *e.cz);
    memcpy(e.x, d.x, DOTS_CAPACITY * sizeof *e.x);
    memcpy(e.z, d.z, DOTS_CAPACITY * sizeof *e.z);
    e.attract_on = w.attract_on;
    e.attract_at = w.attract_at;
}

void ring_init(ExtractRing &r, Arena &arena) {
    for (Extract &e : r.slot) extract_init(e, arena);
    r.head = 0;
    r.filled = 0;
}
