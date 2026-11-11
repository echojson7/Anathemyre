#include "world.h"
#include "ground.h"
#include "../core/hash.h"

void world_init(World &w, Arena &arena, u64 seed, const TerrainDesc &terrain, Arena &scratch) {
    w = World{};
    terrain_generate(w.terrain, terrain, arena, scratch);
    rng_seed(w.rng, seed, 0xA11CEull);
    w.clock.day_ticks = CLOCK_DAY_TICKS_DEFAULT;
    w.clock.tick = (u64)CLOCK_DAY_TICKS_DEFAULT * 7 / 24;  // 7:00, сцена может задать своё
    w.clock.rate = 1;
    Dots &d = w.dots;
    d.gen = arena_push<u16>(arena, DOTS_CAPACITY);
    d.alive = arena_push<u8>(arena, DOTS_CAPACITY);
    d.cx = arena_push<i32>(arena, DOTS_CAPACITY);
    d.cz = arena_push<i32>(arena, DOTS_CAPACITY);
    d.x = arena_push<f32>(arena, DOTS_CAPACITY);
    d.z = arena_push<f32>(arena, DOTS_CAPACITY);
    d.y = arena_push<f32>(arena, DOTS_CAPACITY);
    d.vx = arena_push<f32>(arena, DOTS_CAPACITY);
    d.vz = arena_push<f32>(arena, DOTS_CAPACITY);
    d.speed = arena_push<f32>(arena, DOTS_CAPACITY);
    d.phase = arena_push<u32>(arena, DOTS_CAPACITY);
    d.omega = arena_push<u32>(arena, DOTS_CAPACITY);
    d.free = arena_push<u16>(arena, DOTS_CAPACITY);
    // Свободные места со дна стека: первым родится место 0
    d.free_count = DOTS_CAPACITY;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) d.free[i] = (u16)(DOTS_CAPACITY - 1 - i);
    d.live = 0;
    for (u32 i = 0; i < DOTS_TARGET; ++i) dots_spawn(w, WPos{}, 40.0f);

    Props &p = w.props;
    p.gen = arena_push<u16>(arena, PROPS_CAPACITY);
    p.alive = arena_push<u8>(arena, PROPS_CAPACITY);
    p.mesh = arena_push<u64>(arena, PROPS_CAPACITY);
    p.material = arena_push<u64>(arena, PROPS_CAPACITY);
    p.cx = arena_push<i32>(arena, PROPS_CAPACITY);
    p.cz = arena_push<i32>(arena, PROPS_CAPACITY);
    p.x = arena_push<f32>(arena, PROPS_CAPACITY);
    p.y = arena_push<f32>(arena, PROPS_CAPACITY);
    p.z = arena_push<f32>(arena, PROPS_CAPACITY);
    p.yaw = arena_push<u32>(arena, PROPS_CAPACITY);
    p.scale = arena_push<f32>(arena, PROPS_CAPACITY);
    p.seed = arena_push<u32>(arena, PROPS_CAPACITY);
    p.count = 0;

    Decals &dc = w.decals;
    dc.kind = arena_push<u64>(arena, DECALS_CAPACITY);
    dc.material = arena_push<u64>(arena, DECALS_CAPACITY);
    dc.cx = arena_push<i32>(arena, DECALS_CAPACITY);
    dc.cz = arena_push<i32>(arena, DECALS_CAPACITY);
    dc.x = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.y = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.z = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.yaw = arena_push<u32>(arena, DECALS_CAPACITY);
    dc.hx = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.hy = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.hz = arena_push<f32>(arena, DECALS_CAPACITY);
    dc.seed = arena_push<u32>(arena, DECALS_CAPACITY);
    dc.count = 0;

    people_init(w.people, arena);
}

bool world_build_static(World &w, const CollBoxIn *boxes, u32 n, const GaitTable &gait, Arena &arena, Arena &scratch) {
    if (!colliders_build(w.coll, boxes, n, arena, scratch)) return false;
    walk_build(w.walk, w.terrain, w.coll, arena);
    w.gait = gait;
    return true;
}

u64 world_static_hash(const World &w) {
    return hash_combine(hash_combine(hash_combine(w.coll.hash, w.walk.hash), w.gait.hash), w.actions.hash);
}

bool props_add(World &w, u64 mesh, u64 material, const WPos &at, f32 lift, u32 yaw, f32 scale, u32 seed) {
    WPos q = at;
    wpos_rebase(q);
    q.y = ground_at(w.terrain, q).height + lift;
    return props_add_at(w, mesh, material, q, yaw, scale, seed);
}

bool props_add_at(World &w, u64 mesh, u64 material, const WPos &at, u32 yaw, f32 scale, u32 seed) {
    Props &p = w.props;
    if (p.count >= PROPS_CAPACITY) return false;
    u32 s = p.count++;
    WPos q = at;
    wpos_rebase(q);
    p.gen[s] = (u16)(p.gen[s] + 1);
    p.alive[s] = 1;
    p.mesh[s] = mesh;
    p.material[s] = material;
    p.cx[s] = q.cx;
    p.cz[s] = q.cz;
    p.x[s] = q.x;
    p.z[s] = q.z;
    p.y[s] = q.y;
    p.yaw[s] = yaw;
    p.scale[s] = scale;
    p.seed[s] = seed;
    return true;
}

bool decals_add(World &w, u64 kind, u64 material, const WPos &at, u32 yaw, const f32 h[3], u32 seed) {
    Decals &d = w.decals;
    if (d.count >= DECALS_CAPACITY) return false;
    u32 s = d.count++;
    WPos q = at;
    wpos_rebase(q);
    d.kind[s] = kind;
    d.material[s] = material;
    d.cx[s] = q.cx;
    d.cz[s] = q.cz;
    d.x[s] = q.x;
    d.y[s] = q.y;
    d.z[s] = q.z;
    d.yaw[s] = yaw;
    d.hx[s] = h[0];
    d.hy[s] = h[1];
    d.hz[s] = h[2];
    d.seed[s] = seed;
    return true;
}

bool dots_spawn(World &w, const WPos &center, f32 radius) {
    Dots &d = w.dots;
    if (d.free_count == 0) return false;
    u32 s = d.free[--d.free_count];
    d.gen[s] = (u16)(d.gen[s] + 1);
    if (d.gen[s] == 0) d.gen[s] = 1;
    d.alive[s] = 1;
    // Точка в круге: отбор из квадрата, в среднем 1.27 попытки
    f32 ox, oz;
    do {
        ox = rng_range(w.rng, -1.0f, 1.0f);
        oz = rng_range(w.rng, -1.0f, 1.0f);
    } while (ox * ox + oz * oz > 1.0f);
    d.cx[s] = center.cx;
    d.cz[s] = center.cz;
    d.x[s] = center.x + ox * radius;
    d.z[s] = center.z + oz * radius;
    wpos_rebase(d.cx[s], d.x[s]);
    wpos_rebase(d.cz[s], d.z[s]);
    d.y[s] = ground_at(w.terrain, d.cx[s], d.cz[s], d.x[s], d.z[s]).height;
    d.vx[s] = d.vz[s] = 0.0f;
    d.speed[s] = rng_range(w.rng, 2.0f, 6.0f);
    d.phase[s] = rng_u32(w.rng);
    // Полный оборот курса за 2..8 с, в любую сторону
    u32 om = (u32)(4294967296ull / (u64)(120 + rng_below(w.rng, 361)));
    d.omega[s] = (rng_u32(w.rng) & 1u) ? om : (u32)(0u - om);
    d.live++;
    return true;
}

void dots_kill(World &w, u32 slot) {
    Dots &d = w.dots;
    ANA_ASSERT(slot < DOTS_CAPACITY && d.alive[slot]);
    d.alive[slot] = 0;
    d.free[d.free_count++] = (u16)slot;
    d.live--;
}

u32 world_columns(const World &w, Column *out) {
    const Dots &d = w.dots;
    const u32 n = DOTS_CAPACITY;
    u32 k = 0;
    out[k++] = column("world.step", &w.step, 1);
    out[k++] = column("world.clock.tick", &w.clock.tick, 1);
    out[k++] = column("world.clock.day_ticks", &w.clock.day_ticks, 1);
    out[k++] = column("world.clock.rate", &w.clock.rate, 1);
    out[k++] = column("world.rng.state", &w.rng.state, 1);
    out[k++] = column("world.rng.inc", &w.rng.inc, 1);
    out[k++] = column("world.attract_on", &w.attract_on, 1);
    out[k++] = column("world.attract_at", &w.attract_at, 1);
    out[k++] = column("world.attract_until", &w.attract_until, 1);
    out[k++] = column("dots.gen", d.gen, n);
    out[k++] = column("dots.alive", d.alive, n);
    out[k++] = column("dots.cx", d.cx, n);
    out[k++] = column("dots.cz", d.cz, n);
    out[k++] = column("dots.x", d.x, n);
    out[k++] = column("dots.z", d.z, n);
    out[k++] = column("dots.y", d.y, n);
    out[k++] = column("dots.vx", d.vx, n);
    out[k++] = column("dots.vz", d.vz, n);
    out[k++] = column("dots.speed", d.speed, n);
    out[k++] = column("dots.phase", d.phase, n);
    out[k++] = column("dots.omega", d.omega, n);
    out[k++] = column("dots.free", d.free, n);
    out[k++] = column("dots.free_count", &d.free_count, 1);
    out[k++] = column("dots.live", &d.live, 1);
    const Props &p = w.props;
    const u32 np = PROPS_CAPACITY;
    out[k++] = column("props.gen", p.gen, np);
    out[k++] = column("props.alive", p.alive, np);
    out[k++] = column("props.mesh", p.mesh, np);
    out[k++] = column("props.material", p.material, np);
    out[k++] = column("props.cx", p.cx, np);
    out[k++] = column("props.cz", p.cz, np);
    out[k++] = column("props.x", p.x, np);
    out[k++] = column("props.y", p.y, np);
    out[k++] = column("props.z", p.z, np);
    out[k++] = column("props.yaw", p.yaw, np);
    out[k++] = column("props.scale", p.scale, np);
    out[k++] = column("props.seed", p.seed, np);
    out[k++] = column("props.count", &p.count, 1);
    const Decals &dc = w.decals;
    const u32 nd = DECALS_CAPACITY;
    out[k++] = column("decals.kind", dc.kind, nd);
    out[k++] = column("decals.material", dc.material, nd);
    out[k++] = column("decals.cx", dc.cx, nd);
    out[k++] = column("decals.cz", dc.cz, nd);
    out[k++] = column("decals.x", dc.x, nd);
    out[k++] = column("decals.y", dc.y, nd);
    out[k++] = column("decals.z", dc.z, nd);
    out[k++] = column("decals.yaw", dc.yaw, nd);
    out[k++] = column("decals.half", dc.hx, nd);
    out[k++] = column("decals.half_y", dc.hy, nd);
    out[k++] = column("decals.half_z", dc.hz, nd);
    out[k++] = column("decals.seed", dc.seed, nd);
    out[k++] = column("decals.count", &dc.count, 1);
    k += people_columns(w.people, out + k);
    ANA_CHECK(k <= MAX_COLUMNS);
    return k;
}

void world_column_hashes(const World &w, u64 *out, u32 count) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    ANA_CHECK(n == count);
    for (u32 i = 0; i < n; ++i) out[i] = hash_bytes(cols[i].data, cols[i].bytes);
}

u64 world_hash(const World &w) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    u64 h = 0;
    for (u32 i = 0; i < n; ++i) h = hash_combine(h, hash_bytes(cols[i].data, cols[i].bytes));
    return h;
}
