#include "extract.h"

void extract_init(Extract &e, Arena &arena) {
    e = Extract{};
    e.gen = arena_push<u16>(arena, DOTS_CAPACITY);
    e.cx = arena_push<i32>(arena, DOTS_CAPACITY);
    e.cz = arena_push<i32>(arena, DOTS_CAPACITY);
    e.x = arena_push<f32>(arena, DOTS_CAPACITY);
    e.y = arena_push<f32>(arena, DOTS_CAPACITY);
    e.z = arena_push<f32>(arena, DOTS_CAPACITY);
    const u32 n = PROPS_CAPACITY;
    e.prop_gen = arena_push<u16>(arena, n);
    e.prop_mesh = arena_push<u64>(arena, n);
    e.prop_material = arena_push<u64>(arena, n);
    e.prop_cx = arena_push<i32>(arena, n);
    e.prop_cz = arena_push<i32>(arena, n);
    e.prop_x = arena_push<f32>(arena, n);
    e.prop_y = arena_push<f32>(arena, n);
    e.prop_z = arena_push<f32>(arena, n);
    e.prop_yaw = arena_push<u32>(arena, n);
    e.prop_scale = arena_push<f32>(arena, n);
    e.prop_seed = arena_push<u32>(arena, n);
}

void sim_extract(const World &w, Extract &e) {
    const Dots &d = w.dots;
    e.terrain = &w.terrain;
    e.step = w.step;
    e.clock = w.clock;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) e.gen[i] = d.alive[i] ? d.gen[i] : 0;
    memcpy(e.cx, d.cx, DOTS_CAPACITY * sizeof *e.cx);
    memcpy(e.cz, d.cz, DOTS_CAPACITY * sizeof *e.cz);
    memcpy(e.x, d.x, DOTS_CAPACITY * sizeof *e.x);
    memcpy(e.y, d.y, DOTS_CAPACITY * sizeof *e.y);
    memcpy(e.z, d.z, DOTS_CAPACITY * sizeof *e.z);
    e.attract_on = w.attract_on;
    e.attract_at = w.attract_at;

    const Props &p = w.props;
    const u32 n = p.count;
    e.props_count = n;
    for (u32 i = 0; i < n; ++i) e.prop_gen[i] = p.alive[i] ? p.gen[i] : 0;
    memcpy(e.prop_mesh, p.mesh, n * sizeof *e.prop_mesh);
    memcpy(e.prop_material, p.material, n * sizeof *e.prop_material);
    memcpy(e.prop_cx, p.cx, n * sizeof *e.prop_cx);
    memcpy(e.prop_cz, p.cz, n * sizeof *e.prop_cz);
    memcpy(e.prop_x, p.x, n * sizeof *e.prop_x);
    memcpy(e.prop_y, p.y, n * sizeof *e.prop_y);
    memcpy(e.prop_z, p.z, n * sizeof *e.prop_z);
    memcpy(e.prop_yaw, p.yaw, n * sizeof *e.prop_yaw);
    memcpy(e.prop_scale, p.scale, n * sizeof *e.prop_scale);
    memcpy(e.prop_seed, p.seed, n * sizeof *e.prop_seed);
}

void ring_init(ExtractRing &r, Arena &arena) {
    for (Extract &e : r.slot) extract_init(e, arena);
    r.head = 0;
    r.filled = 0;
}
