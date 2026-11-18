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

    Lamps &lp = w.lamps;
    f32 **lf[] = {&lp.x,  &lp.y,    &lp.z,  &lp.ax,     &lp.ay,   &lp.az, &lp.fx, &lp.fy,
                  &lp.fz, &lp.back, &lp.lm, &lp.kelvin, &lp.size, &lp.r,  &lp.g,  &lp.b};
    for (f32 **col : lf) *col = arena_push<f32>(arena, LAMPS_CAPACITY);
    lp.cx = arena_push<i32>(arena, LAMPS_CAPACITY);
    lp.cz = arena_push<i32>(arena, LAMPS_CAPACITY);
    lp.count = 0;

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

bool lamps_add(World &w, const WPos &at, const f32 axis[3], const f32 face[3], f32 back, f32 lm, f32 kelvin, f32 size,
               const f32 rgb[3]) {
    Lamps &l = w.lamps;
    if (l.count >= LAMPS_CAPACITY) return false;
    u32 s = l.count++;
    WPos q = at;
    wpos_rebase(q);
    l.cx[s] = q.cx;
    l.cz[s] = q.cz;
    l.x[s] = q.x;
    l.y[s] = q.y;
    l.z[s] = q.z;
    l.ax[s] = axis[0];
    l.ay[s] = axis[1];
    l.az[s] = axis[2];
    l.fx[s] = face[0];
    l.fy[s] = face[1];
    l.fz[s] = face[2];
    l.back[s] = back;
    l.lm[s] = lm;
    l.kelvin[s] = kelvin;
    l.size[s] = size;
    l.r[s] = rgb ? rgb[0] : 0.0f;
    l.g[s] = rgb ? rgb[1] : 0.0f;
    l.b[s] = rgb ? rgb[2] : 0.0f;
    return true;
}

u32 world_columns(const World &w, Column *out) {
    u32 k = 0;
    out[k++] = column("world.step", &w.step, 1);
    out[k++] = column("world.clock.tick", &w.clock.tick, 1);
    out[k++] = column("world.clock.day_ticks", &w.clock.day_ticks, 1);
    out[k++] = column("world.clock.rate", &w.clock.rate, 1);
    out[k++] = column("world.rng.state", &w.rng.state, 1);
    out[k++] = column("world.rng.inc", &w.rng.inc, 1);
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
