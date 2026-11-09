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
    const u32 np = PEOPLE_CAPACITY;
    e.person_gen = arena_push<u16>(arena, np);
    e.person_cx = arena_push<i32>(arena, np);
    e.person_cz = arena_push<i32>(arena, np);
    e.person_x = arena_push<f32>(arena, np);
    e.person_y = arena_push<f32>(arena, np);
    e.person_z = arena_push<f32>(arena, np);
    e.person_heading = arena_push<u32>(arena, np);
    e.person_speed = arena_push<f32>(arena, np);
    e.person_phase = arena_push<u32>(arena, np);
    e.person_seed = arena_push<u32>(arena, np);
    e.person_state = arena_push<u8>(arena, np);
}

void sim_extract(const World &w, Extract &e) {
    const Dots &d = w.dots;
    e.terrain = &w.terrain;
    e.decals = &w.decals;
    e.coll = &w.coll;
    e.gait = &w.gait;
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

    const People &q = w.people;
    const u32 np = PEOPLE_CAPACITY;
    for (u32 i = 0; i < np; ++i) e.person_gen[i] = q.alive[i] ? q.gen[i] : 0;
    memcpy(e.person_cx, q.cx, np * sizeof *e.person_cx);
    memcpy(e.person_cz, q.cz, np * sizeof *e.person_cz);
    memcpy(e.person_x, q.x, np * sizeof *e.person_x);
    memcpy(e.person_y, q.y, np * sizeof *e.person_y);
    memcpy(e.person_z, q.z, np * sizeof *e.person_z);
    memcpy(e.person_heading, q.heading, np * sizeof *e.person_heading);
    memcpy(e.person_speed, q.speed, np * sizeof *e.person_speed);
    memcpy(e.person_phase, q.phase, np * sizeof *e.person_phase);
    memcpy(e.person_seed, q.seed, np * sizeof *e.person_seed);
    memcpy(e.person_state, q.state, np * sizeof *e.person_state);
}

void ring_init(ExtractRing &r, Arena &arena) {
    for (Extract &e : r.slot) extract_init(e, arena);
    r.head = 0;
    r.filled = 0;
}
