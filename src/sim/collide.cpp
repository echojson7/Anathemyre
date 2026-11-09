#include "collide.h"
#include "../core/hash.h"
#include "ground.h"

// Глобальный номер клетки 2 м: чанк·128 + клетка в чанке
static i32 gcell(i32 chunk, f32 off) {
    i32 k = f_floor_i(off * (1.0f / COLL_CELL_M));
    return chunk * (i32)COLL_TILE_N + k;
}
static i32 floor_div(i32 a, i32 b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static i32 tile_find(const Colliders &c, i32 cx, i32 cz) {
    for (u32 t = 0; t < c.tiles; ++t)
        if (c.tile_cx[t] == cx && c.tile_cz[t] == cz) return (i32)t;
    return -1;
}

// Обойти клетки, которые задевает коробка k с запасом; fn(номер клетки во всех плитках)
template <typename Fn> static bool box_cells(Colliders &c, u32 k, bool make_tiles, Fn fn) {
    f32 ac = f_abs(c.c[k]), as = f_abs(c.s[k]);
    f32 ex = ac * c.hx[k] + as * c.hz[k] + COLL_MARGIN_M;
    f32 ez = as * c.hx[k] + ac * c.hz[k] + COLL_MARGIN_M;
    i32 gx0 = gcell(c.cx[k], c.x[k] - ex), gx1 = gcell(c.cx[k], c.x[k] + ex);
    i32 gz0 = gcell(c.cz[k], c.z[k] - ez), gz1 = gcell(c.cz[k], c.z[k] + ez);
    for (i32 gz = gz0; gz <= gz1; ++gz)
        for (i32 gx = gx0; gx <= gx1; ++gx) {
            i32 tcx = floor_div(gx, (i32)COLL_TILE_N), tcz = floor_div(gz, (i32)COLL_TILE_N);
            i32 t = tile_find(c, tcx, tcz);
            if (t < 0) {
                if (!make_tiles) continue;
                if (c.tiles >= COLL_MAX_TILES) return false;
                t = (i32)c.tiles++;
                c.tile_cx[t] = tcx;
                c.tile_cz[t] = tcz;
            }
            u32 lx = (u32)(gx - tcx * (i32)COLL_TILE_N), lz = (u32)(gz - tcz * (i32)COLL_TILE_N);
            fn((u32)t * COLL_TILE_N * COLL_TILE_N + lz * COLL_TILE_N + lx);
        }
    return true;
}

bool colliders_build(Colliders &c, const CollBoxIn *boxes, u32 n, Arena &arena, Arena &scratch) {
    c = Colliders{};
    if (n > COLL_MAX_BOXES) return false;
    c.count = n;
    c.cx = arena_push<i32>(arena, n + 1);
    c.cz = arena_push<i32>(arena, n + 1);
    c.x = arena_push<f32>(arena, n + 1);
    c.z = arena_push<f32>(arena, n + 1);
    c.y0 = arena_push<f32>(arena, n + 1);
    c.y1 = arena_push<f32>(arena, n + 1);
    c.hx = arena_push<f32>(arena, n + 1);
    c.hz = arena_push<f32>(arena, n + 1);
    c.c = arena_push<f32>(arena, n + 1);
    c.s = arena_push<f32>(arena, n + 1);
    for (u32 k = 0; k < n; ++k) {
        WPos p = boxes[k].at;
        wpos_rebase(p);
        c.cx[k] = p.cx;
        c.cz[k] = p.cz;
        c.x[k] = p.x;
        c.z[k] = p.z;
        c.y0[k] = p.y - boxes[k].hy;
        c.y1[k] = p.y + boxes[k].hy;
        c.hx[k] = boxes[k].hx;
        c.hz[k] = boxes[k].hz;
        sincos_turn(boxes[k].yaw, &c.s[k], &c.c[k]);
    }
    // Два прохода: плитки и число коробок в клетках, потом списки (коробки по возрастанию номера)
    usize mark = arena_mark(scratch);
    for (u32 k = 0; k < n; ++k)
        if (!box_cells(c, k, true, [](u32) {})) return false;
    const u32 cells = c.tiles * COLL_TILE_N * COLL_TILE_N;
    u32 *cnt = arena_push<u32>(scratch, cells + 1);
    for (u32 k = 0; k < n; ++k) box_cells(c, k, false, [&](u32 cell) { cnt[cell]++; });
    c.cell_start = arena_push<u32>(arena, cells + 1);
    u32 total = 0;
    for (u32 i = 0; i < cells; ++i) { c.cell_start[i] = total; total += cnt[i]; cnt[i] = 0; }
    c.cell_start[cells] = total;
    c.items = arena_push<u16>(arena, total + 1);
    for (u32 k = 0; k < n; ++k) box_cells(c, k, false, [&](u32 cell) { c.items[c.cell_start[cell] + cnt[cell]++] = (u16)k; });
    arena_restore(scratch, mark);

    u64 h = hash_bytes(&n, sizeof n);
    const f32 *cols[] = {c.x, c.z, c.y0, c.y1, c.hx, c.hz, c.c, c.s};
    for (const f32 *col : cols) h = hash_combine(h, hash_bytes(col, n * sizeof(f32)));
    h = hash_combine(h, hash_bytes(c.cx, n * sizeof(i32)));
    h = hash_combine(h, hash_bytes(c.cz, n * sizeof(i32)));
    c.hash = h;
    return true;
}

Span<const u16> colliders_near(const Colliders &c, i32 cx, i32 cz, f32 x, f32 z) {
    if (!c.tiles) return {};
    i32 gx = gcell(cx, x), gz = gcell(cz, z);
    i32 tcx = floor_div(gx, (i32)COLL_TILE_N), tcz = floor_div(gz, (i32)COLL_TILE_N);
    i32 t = tile_find(c, tcx, tcz);
    if (t < 0) return {};
    u32 lx = (u32)(gx - tcx * (i32)COLL_TILE_N), lz = (u32)(gz - tcz * (i32)COLL_TILE_N);
    u32 cell = (u32)t * COLL_TILE_N * COLL_TILE_N + lz * COLL_TILE_N + lx;
    return {c.items + c.cell_start[cell], c.cell_start[cell + 1] - c.cell_start[cell]};
}

void coll_local(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z, f32 *lx, f32 *lz) {
    Vec2 d = wpos_delta_xz(c.cx[k], c.cz[k], c.x[k], c.z[k], cx, cz, x, z);
    *lx = d.x * c.c[k] - d.y * c.s[k];
    *lz = d.x * c.s[k] + d.y * c.c[k];
}

bool coll_covers(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z) {
    f32 lx, lz;
    coll_local(c, k, cx, cz, x, z, &lx, &lz);
    return f_abs(lx) <= c.hx[k] && f_abs(lz) <= c.hz[k];
}

bool coll_touches(const Colliders &c, u32 k, i32 cx, i32 cz, f32 x, f32 z, f32 r) {
    f32 lx, lz;
    coll_local(c, k, cx, cz, x, z, &lx, &lz);
    f32 qx = f_clamp(lx, -c.hx[k], c.hx[k]) - lx, qz = f_clamp(lz, -c.hz[k], c.hz[k]) - lz;
    return qx * qx + qz * qz < r * r;
}

Support support_at(const Terrain &t, const Colliders &c, i32 cx, i32 cz, f32 x, f32 z, f32 from_y) {
    Support s{ground_at(t, cx, cz, x, z).height, 0};
    const f32 top = from_y + STEP_UP_M;
    for (u16 k : colliders_near(c, cx, cz, x, z)) {
        f32 y1 = c.y1[k];
        if (y1 <= s.height || y1 > top) continue;
        if (coll_covers(c, k, cx, cz, x, z)) s = {y1, (u32)k + 1};
    }
    return s;
}

bool coll_push_circle(const Colliders &c, i32 cx, i32 cz, f32 &x, f32 &z, f32 r, f32 lo, f32 hi) {
    bool pushed = false;
    for (u16 k : colliders_near(c, cx, cz, x, z)) {
        if (c.y1[k] <= lo || c.y0[k] >= hi) continue;
        f32 lx, lz;
        coll_local(c, k, cx, cz, x, z, &lx, &lz);
        f32 hx = c.hx[k], hz = c.hz[k];
        f32 px = f_clamp(lx, -hx, hx), pz = f_clamp(lz, -hz, hz);
        f32 dx = lx - px, dz = lz - pz;
        f32 d2 = dx * dx + dz * dz;
        f32 mx, mz;  // сдвиг в осях коробки
        if (d2 > 0.0f) {
            if (d2 >= r * r) continue;
            f32 d = f_sqrt(d2);
            f32 k2 = (r - d) / d;
            mx = dx * k2;
            mz = dz * k2;
        } else {
            // Середина внутри следа: наружу через ближнюю сторону
            f32 ox = hx - f_abs(lx), oz = hz - f_abs(lz);
            if (ox < oz) { mx = (lx < 0.0f ? -1.0f : 1.0f) * (ox + r); mz = 0.0f; }
            else { mz = (lz < 0.0f ? -1.0f : 1.0f) * (oz + r); mx = 0.0f; }
        }
        // Обратно в оси мира: мир = lx·(c, −s) + lz·(s, c)
        x += mx * c.c[k] + mz * c.s[k];
        z += -mx * c.s[k] + mz * c.c[k];
        pushed = true;
    }
    return pushed;
}
