#include "world.h"
#include "../core/hash.h"

void world_init(World &w, Arena &arena, u64 seed) {
    w = World{};
    rng_seed(w.rng, seed, 0xA11CEull);
    Dots &d = w.dots;
    d.gen = arena_push<u16>(arena, DOTS_CAPACITY);
    d.alive = arena_push<u8>(arena, DOTS_CAPACITY);
    d.cx = arena_push<i32>(arena, DOTS_CAPACITY);
    d.cz = arena_push<i32>(arena, DOTS_CAPACITY);
    d.x = arena_push<f32>(arena, DOTS_CAPACITY);
    d.z = arena_push<f32>(arena, DOTS_CAPACITY);
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
    out[k++] = {"world.step", &w.step, sizeof w.step};
    out[k++] = {"world.rng.state", &w.rng.state, sizeof w.rng.state};
    out[k++] = {"world.rng.inc", &w.rng.inc, sizeof w.rng.inc};
    out[k++] = {"world.attract_on", &w.attract_on, sizeof w.attract_on};
    out[k++] = {"world.attract_at", &w.attract_at, sizeof w.attract_at};
    out[k++] = {"world.attract_until", &w.attract_until, sizeof w.attract_until};
    out[k++] = {"dots.gen", d.gen, n * sizeof *d.gen};
    out[k++] = {"dots.alive", d.alive, n * sizeof *d.alive};
    out[k++] = {"dots.cx", d.cx, n * sizeof *d.cx};
    out[k++] = {"dots.cz", d.cz, n * sizeof *d.cz};
    out[k++] = {"dots.x", d.x, n * sizeof *d.x};
    out[k++] = {"dots.z", d.z, n * sizeof *d.z};
    out[k++] = {"dots.vx", d.vx, n * sizeof *d.vx};
    out[k++] = {"dots.vz", d.vz, n * sizeof *d.vz};
    out[k++] = {"dots.speed", d.speed, n * sizeof *d.speed};
    out[k++] = {"dots.phase", d.phase, n * sizeof *d.phase};
    out[k++] = {"dots.omega", d.omega, n * sizeof *d.omega};
    out[k++] = {"dots.free", d.free, n * sizeof *d.free};
    out[k++] = {"dots.free_count", &d.free_count, sizeof d.free_count};
    out[k++] = {"dots.live", &d.live, sizeof d.live};
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
