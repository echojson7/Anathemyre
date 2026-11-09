#include "walk.h"
#include "../core/hash.h"
#include "ground.h"

static i32 round_cm(f32 m) { return f_floor_i(m * 100.0f + 0.5f); }

// Середина клетки (i, j) области: чанк и смещение
static void cell_center(const WalkArea &a, u32 i, u32 j, i32 *cx, i32 *cz, f32 *x, f32 *z) {
    *cx = a.cx0 + (i32)(i / WALK_CELLS_PER_CHUNK);
    *cz = a.cz0 + (i32)(j / WALK_CELLS_PER_CHUNK);
    *x = (f32)(i % WALK_CELLS_PER_CHUNK) * WALK_CELL_M + WALK_CELL_M * 0.5f;
    *z = (f32)(j % WALK_CELLS_PER_CHUNK) * WALK_CELL_M + WALK_CELL_M * 0.5f;
}

// Уровни одной клетки: земля и верхи коробок, у которых есть просвет и куда капсула встаёт, не задевая стен
static void build_cell(WalkArea &a, const Terrain &t, const Colliders &c, u32 i, u32 j) {
    i32 cx, cz;
    f32 x, z;
    cell_center(a, i, j, &cx, &cz, &x, &z);
    GroundSample g = ground_at(t, cx, cz, x, z);
    f32 cand[16];
    u8 cbox[16];
    u32 n = 0;
    if (g.normal.y >= 0.85f && g.kind != GROUND_WATER) { cand[n] = g.height; cbox[n++] = 0; }
    Span<const u16> near = colliders_near(c, cx, cz, x, z);
    for (u16 k : near)
        if (n < 16 && c.y1[k] > g.height + 0.05f && coll_covers(c, k, cx, cz, x, z)) { cand[n] = c.y1[k]; cbox[n++] = 1; }
    // По возрастанию высоты (вставками: порядок равных — порядок коробок)
    for (u32 p = 1; p < n; ++p)
        for (u32 q = p; q > 0 && cand[q] < cand[q - 1]; --q) {
            f32 tf = cand[q]; cand[q] = cand[q - 1]; cand[q - 1] = tf;
            u8 tb = cbox[q]; cbox[q] = cbox[q - 1]; cbox[q - 1] = tb;
        }
    u32 cell = j * WALK_N + i;
    u32 m = 0;
    i32 last = -1000000;
    for (u32 p = 0; p < n && m < WALK_LEVELS; ++p) {
        f32 s = cand[p];
        bool ok = true;
        for (u16 k : near) {
            if (c.y0[k] >= s + WALK_CLEAR_M) continue;
            if (c.y1[k] > s + 0.05f && coll_covers(c, k, cx, cz, x, z)) { ok = false; break; }
            if (c.y1[k] > s + STEP_UP_M && c.y0[k] < s + WALK_KNEE_M && coll_touches(c, k, cx, cz, x, z, WALK_FOOT_R_M)) { ok = false; break; }
            if (c.y1[k] > s + WALK_KNEE_M && c.y0[k] < s + WALK_SHOULDER_M && coll_touches(c, k, cx, cz, x, z, WALK_RADIUS_M)) { ok = false; break; }
            if (c.y1[k] > s + WALK_SHOULDER_M && coll_touches(c, k, cx, cz, x, z, WALK_HEAD_R_M)) { ok = false; break; }
        }
        if (!ok) continue;
        i32 hc = round_cm(s) - a.base_cm;
        if (hc <= -32767 || hc > 32767) continue;
        if (hc - last < 5) continue;  // тот же уровень (стык плит)
        a.h[cell * 4 + m] = (i16)hc;
        a.box[cell * 4 + m] = cbox[p];
        last = hc;
        m++;
    }
    a.nlev[cell] = (u8)m;
}

static void build_edges(WalkArea &a);

void walk_build(WalkGrid &g, const Terrain &t, const Colliders &c, Arena &arena) {
    g = WalkGrid{};
    u64 h = 0x3A1Bull;
    for (u32 k = 0; k < c.count && g.count < WALK_MAX_AREAS; ++k) {
        u32 ii, jj;
        if (walk_area_of(g, c.cx[k], c.cz[k], c.x[k], c.z[k], &ii, &jj) >= 0) continue;
        // Область 2 × 2 чанка, коробка — ближе к середине
        WalkArea &a = g.area[g.count++];
        a.cx0 = c.cx[k] - 1 + (c.x[k] >= 128.0f ? 1 : 0);
        a.cz0 = c.cz[k] - 1 + (c.z[k] >= 128.0f ? 1 : 0);
        a.base_cm = round_cm(ground_at(t, a.cx0 + 1, a.cz0 + 1, 0.0f, 0.0f).height);
        const u32 cells = WALK_N * WALK_N;
        a.nlev = arena_push<u8>(arena, cells);
        a.h = arena_push<i16>(arena, cells * WALK_LEVELS);
        a.box = arena_push<u8>(arena, cells * WALK_LEVELS);
        a.edge = arena_push<u32>(arena, cells * WALK_LEVELS);
        for (u32 i = 0; i < cells * WALK_LEVELS; ++i) a.h[i] = WALK_NONE;
        for (u32 j = 0; j < WALK_N; ++j)
            for (u32 i = 0; i < WALK_N; ++i) build_cell(a, t, c, i, j);
        build_edges(a);
        for (u32 i = 0; i < cells; ++i) g.levels_total += a.nlev[i];
        i32 head[3] = {a.cx0, a.cz0, a.base_cm};
        h = hash_combine(h, hash_bytes(head, sizeof head));
        h = hash_combine(h, hash_bytes(a.nlev, cells));
        h = hash_combine(h, hash_bytes(a.h, cells * WALK_LEVELS * sizeof(i16)));
        h = hash_combine(h, hash_bytes(a.box, cells * WALK_LEVELS));
    }
    g.hash = h;
}

i32 walk_area_of(const WalkGrid &g, i32 cx, i32 cz, f32 x, f32 z, u32 *i, u32 *j) {
    i32 fi = f_floor_i(x * (1.0f / WALK_CELL_M)), fj = f_floor_i(z * (1.0f / WALK_CELL_M));
    if (fi < 0) fi = 0; else if (fi >= (i32)WALK_CELLS_PER_CHUNK) fi = WALK_CELLS_PER_CHUNK - 1;
    if (fj < 0) fj = 0; else if (fj >= (i32)WALK_CELLS_PER_CHUNK) fj = WALK_CELLS_PER_CHUNK - 1;
    for (u32 k = 0; k < g.count; ++k) {
        const WalkArea &a = g.area[k];
        i32 di = cx - a.cx0, dj = cz - a.cz0;
        if (di < 0 || di > 1 || dj < 0 || dj > 1) continue;
        *i = (u32)(di * (i32)WALK_CELLS_PER_CHUNK + fi);
        *j = (u32)(dj * (i32)WALK_CELLS_PER_CHUNK + fj);
        return (i32)k;
    }
    return -1;
}

static u32 level_near(const WalkArea &a, u32 cell, i32 ycm) {
    u32 best = 0;
    i32 bd = 0x7FFFFFFF;
    for (u32 l = 0; l < a.nlev[cell]; ++l) {
        i32 d = a.h[cell * 4 + l] - ycm;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = l; }
    }
    return best;
}

bool walk_node_near(const WalkArea &a, u32 i, u32 j, f32 y, u32 r, u32 *node) {
    i32 ycm = round_cm(y) - a.base_cm;
    // Кольца по возрастанию радиуса; в кольце — ближайшая клетка, при равенстве — первая в обходе
    for (i32 rr = 0; rr <= (i32)r; ++rr) {
        i32 bd = 0x7FFFFFFF;
        u32 bc = 0xFFFFFFFFu;
        for (i32 dj = -rr; dj <= rr; ++dj)
            for (i32 di = -rr; di <= rr; ++di) {
                if (di != -rr && di != rr && dj != -rr && dj != rr) continue;
                i32 ci = (i32)i + di, cj = (i32)j + dj;
                if (ci < 0 || cj < 0 || ci >= (i32)WALK_N || cj >= (i32)WALK_N) continue;
                u32 cell = (u32)cj * WALK_N + (u32)ci;
                if (!a.nlev[cell]) continue;
                i32 d = di * di + dj * dj;
                if (d < bd) { bd = d; bc = cell; }
            }
        if (bc != 0xFFFFFFFFu) {
            *node = (bc << 2) | level_near(a, bc, ycm);
            return true;
        }
    }
    return false;
}

WPos walk_node_pos(const WalkArea &a, u32 node) {
    WPos p;
    cell_center(a, walk_node_i(node), walk_node_j(node), &p.cx, &p.cz, &p.x, &p.z);
    p.y = (f32)(a.base_cm + a.h[(node >> 2) * 4 + walk_node_lev(node)]) * 0.01f;
    return p;
}

// Уровень соседней клетки, куда можно шагнуть с уровня lev клетки from; −1 — нельзя
static i32 step_to(const WalkArea &a, u32 from, u32 lev, u32 to) {
    i32 h0 = a.h[from * 4 + lev];
    u8 b0 = a.box[from * 4 + lev];
    i32 best = -1, bd = 0x7FFFFFFF;
    for (u32 l = 0; l < a.nlev[to]; ++l) {
        i32 d = a.h[to * 4 + l] - h0;
        if (d < 0) d = -d;
        i32 lim = (b0 || a.box[to * 4 + l]) ? WALK_STEP_CM : WALK_SLOPE_CM;
        if (d <= lim && d < bd) { bd = d; best = (i32)l; }
    }
    return best;
}

// Шаг из узла n на (di, dj) ∈ {−1, 0, 1}²; по диагонали — только если открыты обе стороны угла. −1 — нельзя.
// Считается при построении в рёбра узла (move — чтение рёбер)
static i64 move_slow(const WalkArea &a, u32 n, i32 di, i32 dj) {
    i32 i = (i32)walk_node_i(n) + di, j = (i32)walk_node_j(n) + dj;
    if (i < 0 || j < 0 || i >= (i32)WALK_N || j >= (i32)WALK_N) return -1;
    u32 from = n >> 2, lev = walk_node_lev(n);
    u32 to = (u32)j * WALK_N + (u32)i;
    if (di && dj) {
        u32 sa = from + (u32)di, sb = (u32)((i32)from + dj * (i32)WALK_N);
        i32 la = step_to(a, from, lev, sa), lb = step_to(a, from, lev, sb);
        if (la < 0 || lb < 0) return -1;
        if (step_to(a, sa, (u32)la, to) < 0 || step_to(a, sb, (u32)lb, to) < 0) return -1;
    }
    i32 l = step_to(a, from, lev, to);
    return l < 0 ? -1 : (i64)((to << 2) | (u32)l);
}

static i32 dir_of(i32 di, i32 dj) {
    for (i32 k = 0; k < 8; ++k)
        if (WALK_DIRS[k][0] == di && WALK_DIRS[k][1] == dj) return k;
    return -1;
}

static i64 move(const WalkArea &a, u32 n, i32 di, i32 dj) {
    i32 k = dir_of(di, dj);
    u32 e = a.edge[n];
    if (k < 0 || !(e & (1u << k))) return -1;
    u32 to = (u32)((i32)(n >> 2) + di + dj * (i32)WALK_N);
    return (i64)((to << 2) | ((e >> (8 + 2 * k)) & 3u));
}

static void build_edges(WalkArea &a) {
    for (u32 cell = 0; cell < WALK_N * WALK_N; ++cell)
        for (u32 l = 0; l < a.nlev[cell]; ++l) {
            u32 n = (cell << 2) | l, e = 0;
            for (u32 k = 0; k < 8; ++k) {
                i64 m = move_slow(a, n, WALK_DIRS[k][0], WALK_DIRS[k][1]);
                if (m >= 0) e |= (1u << k) | ((u32)(m & 3) << (8 + 2 * k));
            }
            a.edge[n] = e;
        }
}

// Прямая видимость: капсула пройдёт по отрезку между серединами узлов, переходя из клетки в клетку
static bool line_ok(const WalkArea &a, u32 from, u32 to) {
    i32 i0 = (i32)walk_node_i(from), j0 = (i32)walk_node_j(from);
    i32 i1 = (i32)walk_node_i(to), j1 = (i32)walk_node_j(to);
    i32 di = i1 - i0, dj = j1 - j0;
    i32 ad = di < 0 ? -di : di, bd = dj < 0 ? -dj : dj;
    i32 steps = (ad > bd ? ad : bd) * 2;  // полклетки за шаг
    if (!steps) return from == to;
    u32 cur = from;
    i32 ci = i0, cj = j0;
    f32 inv = 1.0f / (f32)steps;
    for (i32 s = 1; s <= steps; ++s) {
        f32 u = (f32)s * inv;
        i32 ni = f_floor_i((f32)i0 + 0.5f + (f32)di * u), nj = f_floor_i((f32)j0 + 0.5f + (f32)dj * u);
        if (ni == ci && nj == cj) continue;
        i64 m = move(a, cur, ni - ci, nj - cj);
        if (m < 0) return false;
        cur = (u32)m;
        ci = ni;
        cj = nj;
    }
    return cur == to;
}

static u32 hash32(u32 v) { return (u32)hash_mix((u64)v * 0x9E3779B97F4A7C15ull); }

struct Heap {
    u32 *f, *h, *n;
    u32 count, cap;
};
static bool heap_less(const Heap &q, u32 a, u32 b) {
    if (q.f[a] != q.f[b]) return q.f[a] < q.f[b];
    if (q.h[a] != q.h[b]) return q.h[a] < q.h[b];
    return q.n[a] < q.n[b];
}
static void heap_swap(Heap &q, u32 a, u32 b) {
    u32 t;
    t = q.f[a]; q.f[a] = q.f[b]; q.f[b] = t;
    t = q.h[a]; q.h[a] = q.h[b]; q.h[b] = t;
    t = q.n[a]; q.n[a] = q.n[b]; q.n[b] = t;
}
static bool heap_push(Heap &q, u32 f, u32 h, u32 n) {
    if (q.count >= q.cap) return false;
    u32 k = q.count++;
    q.f[k] = f; q.h[k] = h; q.n[k] = n;
    while (k > 0) {
        u32 p = (k - 1) / 2;
        if (!heap_less(q, k, p)) break;
        heap_swap(q, k, p);
        k = p;
    }
    return true;
}
static void heap_pop(Heap &q) {
    q.count--;
    if (!q.count) return;
    heap_swap(q, 0, q.count);
    u32 k = 0;
    for (;;) {
        u32 l = 2 * k + 1, r = l + 1, m = k;
        if (l < q.count && heap_less(q, l, m)) m = l;
        if (r < q.count && heap_less(q, r, m)) m = r;
        if (m == k) break;
        heap_swap(q, k, m);
        k = m;
    }
}

// Посещённые узлы: открытая адресация, ключ — узел + 1
struct Visited {
    u32 *key, *g, *parent;
    u8 *closed;
    u32 mask, count;
};
static u32 vis_find(Visited &v, u32 node, bool add) {
    u32 k = hash32(node) & v.mask;
    for (;;) {
        if (v.key[k] == node + 1) return k;
        if (!v.key[k]) {
            if (!add || v.count * 4 >= (v.mask + 1) * 3) return 0xFFFFFFFFu;
            v.key[k] = node + 1;
            v.g[k] = 0xFFFFFFFFu;
            v.closed[k] = 0;
            v.count++;
            return k;
        }
        k = (k + 1) & v.mask;
    }
}

static u32 heur(u32 a, u32 b) {
    i32 di = (i32)walk_node_i(a) - (i32)walk_node_i(b), dj = (i32)walk_node_j(a) - (i32)walk_node_j(b);
    u32 x = (u32)(di < 0 ? -di : di), y = (u32)(dj < 0 ? -dj : dj);
    return x > y ? 10 * x + 4 * y : 10 * y + 4 * x;
}

u32 walk_find(const WalkArea &a, u32 from, u32 to, Arena &scratch, WalkSearch &ws, u32 *out, u32 cap) {
    ws.expanded = 0;
    ws.reached = false;
    if (from == to) { ws.reached = true; return 0; }
    usize mark = arena_mark(scratch);
    // Карта посещённых — вдвое больше предела раскрытых (заполнение ≤ 3/4); нулями — только ключи
    u32 vcap = 1u << 12;
    while (vcap < ws.max_expand * 4) vcap <<= 1;
    auto raw = [&](usize n) { return (u32 *)arena_push_bytes(scratch, n * sizeof(u32), 16); };
    Visited v{arena_push<u32>(scratch, vcap), raw(vcap), raw(vcap), (u8 *)arena_push_bytes(scratch, vcap, 16), vcap - 1, 0};
    Heap q{raw(vcap), raw(vcap), raw(vcap), 0, vcap};
    u32 s = vis_find(v, from, true);
    v.g[s] = 0;
    v.parent[s] = from;
    heap_push(q, heur(from, to), heur(from, to), from);
    u32 best = from, best_h = heur(from, to);
    while (q.count) {
        u32 n = q.n[0];
        heap_pop(q);
        u32 vn = vis_find(v, n, false);
        if (v.closed[vn]) continue;
        v.closed[vn] = 1;
        if (n == to) { best = n; ws.reached = true; break; }
        if (++ws.expanded > ws.max_expand) break;
        u32 gn = v.g[vn];
        i32 hn = a.h[(n >> 2) * 4 + walk_node_lev(n)];
        const u32 e = a.edge[n];
        for (u32 k = 0; k < 8; ++k) {
                if (!(e & (1u << k))) continue;
                u32 mn = ((u32)((i32)(n >> 2) + WALK_DIRS[k][0] + WALK_DIRS[k][1] * (i32)WALK_N) << 2) | ((e >> (8 + 2 * k)) & 3u);
                i32 dh = a.h[(mn >> 2) * 4 + walk_node_lev(mn)] - hn;
                u32 cost = (k >= 4 ? 14u : 10u) + (u32)(dh < 0 ? -dh : dh) / 5u;
                u32 vm = vis_find(v, mn, true);
                if (vm == 0xFFFFFFFFu) { q.count = 0; break; }  // карта полна: путь до лучшего узла
                if (v.closed[vm] || gn + cost >= v.g[vm]) continue;
                v.g[vm] = gn + cost;
                v.parent[vm] = n;
                u32 h = heur(mn, to);
                if (h < best_h) { best_h = h; best = mn; }
                if (!heap_push(q, gn + cost + h, h, mn)) q.count = 0;
            }
    }
    // Путь от лучшего узла назад к началу
    u32 *rev = arena_push<u32>(scratch, v.count + 1);
    u32 len = 0;
    for (u32 n = best; n != from;) {
        rev[len++] = n;
        n = v.parent[vis_find(v, n, false)];
    }
    // Сглаживание: от опоры — как можно дальше по прямой (шаги удваиваются, потом делятся пополам)
    u32 count = 0, anchor = from;
    i32 k = (i32)len - 1;  // rev[k] — следующий узел после опоры
    while (k >= 0 && count < cap) {
        i32 ok = k, step = 1;
        while (ok - step >= 0 && line_ok(a, anchor, rev[ok - step])) { ok -= step; step *= 2; }
        for (step /= 2; step >= 1; step /= 2)
            if (ok - step >= 0 && line_ok(a, anchor, rev[ok - step])) ok -= step;
        anchor = rev[ok];
        out[count++] = anchor;
        k = ok - 1;
    }
    arena_restore(scratch, mark);
    return count;
}
