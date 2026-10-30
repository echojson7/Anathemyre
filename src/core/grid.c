#include "grid.h"

#include <math.h>
#include <string.h>

void grid_init(Grid *g, Arena *a, u32 cap, u32 max_keys, f32 cell)
{
    memset(g, 0, sizeof *g);
    g->cap = cap;
    g->max_keys = max_keys;
    g->min_cell = cell;
    g->cell = cell;
    g->inv_cell = 1.0f / cell;
    g->w = g->h = g->ntags = 1;
    g->bw = g->bh = 1;
    g->start = ARENA_PUSH_ARRAY(a, u32, max_keys + 1);
    g->items = ARENA_PUSH_ARRAY(a, u32, cap);
    g->key_of = ARENA_PUSH_ARRAY(a, u32, cap);
    g->block_mask = ARENA_PUSH_ARRAY(a, u32, max_keys);
}

static i32 coord(f32 v, f32 o, f32 inv, u32 n)
{
    i32 c = (i32)floorf((v - o) * inv);
    return c < 0 ? 0 : (c >= (i32)n ? (i32)n - 1 : c);
}

void grid_build(Grid *g, const f32 *x, const f32 *z, const u8 *tag, u32 ntags, u32 count)
{
    if (count > g->cap) count = g->cap;
    if (ntags < 1) ntags = 1;
    if (ntags > GRID_MAX_TAGS) ntags = GRID_MAX_TAGS;
    g->count = count;
    g->ntags = ntags;
    f32 x0 = 0.0f, x1 = 0.0f, z0 = 0.0f, z1 = 0.0f;
    for (u32 i = 0; i < count; i++) {
        if (i == 0 || x[i] < x0) x0 = x[i];
        if (i == 0 || x[i] > x1) x1 = x[i];
        if (i == 0 || z[i] < z0) z0 = z[i];
        if (i == 0 || z[i] > z1) z1 = z[i];
    }
    // Ячейка не меньше заданной; если поле велико — укрупняется, чтобы влезть в max_keys
    u32 max_cells = g->max_keys / ntags;
    f32 cell = g->min_cell;
    for (;;) {
        u32 w = (u32)((x1 - x0) / cell) + 1, h = (u32)((z1 - z0) / cell) + 1;
        if ((u64)w * h <= max_cells) {
            g->w = w;
            g->h = h;
            break;
        }
        cell *= 1.5f;
    }
    g->cell = cell;
    g->inv_cell = 1.0f / cell;
    g->ox = x0;
    g->oz = z0;

    u32 n = g->w * g->h * ntags;
    memset(g->start, 0, sizeof(u32) * (n + 1));
    for (u32 i = 0; i < count; i++) {
        u32 c = (u32)coord(z[i], z0, g->inv_cell, g->h) * g->w + (u32)coord(x[i], x0, g->inv_cell, g->w);
        u32 t = tag ? (tag[i] < ntags ? tag[i] : ntags - 1) : 0;
        u32 k = c * ntags + t;
        g->key_of[i] = k;
        g->start[k + 1]++;
    }
    for (u32 k = 0; k < n; k++) g->start[k + 1] += g->start[k];
    // Раскладка по возрастанию номера: start[k] служит курсором, потом сдвигается обратно
    for (u32 i = 0; i < count; i++) g->items[g->start[g->key_of[i]]++] = i;
    for (u32 k = n; k > 0; k--) g->start[k] = g->start[k - 1];
    g->start[0] = 0;

    g->bw = (g->w + GRID_BLOCK - 1) / GRID_BLOCK;
    g->bh = (g->h + GRID_BLOCK - 1) / GRID_BLOCK;
    memset(g->block_mask, 0, sizeof(u32) * g->bw * g->bh);
    for (u32 i = 0; i < count; i++) {
        u32 c = g->key_of[i] / ntags;
        u32 b = (c / g->w / GRID_BLOCK) * g->bw + (c % g->w) / GRID_BLOCK;
        g->block_mask[b] |= 1u << (g->key_of[i] % ntags);
    }
}

u32 grid_query(const Grid *g, f32 x, f32 z, f32 r, u32 mask, u32 *out, u32 out_cap)
{
    i32 cx0 = coord(x - r, g->ox, g->inv_cell, g->w), cx1 = coord(x + r, g->ox, g->inv_cell, g->w);
    i32 cz0 = coord(z - r, g->oz, g->inv_cell, g->h), cz1 = coord(z + r, g->oz, g->inv_cell, g->h);
    u32 nt = g->ntags;
    u32 all = nt >= 32 ? GRID_ALL_TAGS : (1u << nt) - 1;
    u32 n = 0;
    for (i32 cz = cz0; cz <= cz1; cz++) {
        u32 row = (u32)cz * g->w;
        if ((mask & all) == all) {
            // Все метки: ячейки строки лежат подряд
            for (u32 k = g->start[(row + (u32)cx0) * nt]; k < g->start[(row + (u32)cx1 + 1) * nt]; k++) {
                if (n == out_cap) return n;
                out[n++] = g->items[k];
            }
            continue;
        }
        for (i32 cx = cx0; cx <= cx1; cx++) {
            u32 base = (row + (u32)cx) * nt;
            for (u32 m = mask & all; m; m &= m - 1) {
                u32 t = (u32)__builtin_ctz(m);
                for (u32 k = g->start[base + t]; k < g->start[base + t + 1]; k++) {
                    if (n == out_cap) return n;
                    out[n++] = g->items[k];
                }
            }
        }
    }
    return n;
}

void grid_count(const Grid *g, f32 x, f32 z, f32 r, u32 mask, const f32 *px, const f32 *pz, u32 *count)
{
    i32 cx0 = coord(x - r, g->ox, g->inv_cell, g->w), cx1 = coord(x + r, g->ox, g->inv_cell, g->w);
    i32 cz0 = coord(z - r, g->oz, g->inv_cell, g->h), cz1 = coord(z + r, g->oz, g->inv_cell, g->h);
    u32 nt = g->ntags;
    mask &= nt >= 32 ? GRID_ALL_TAGS : (1u << nt) - 1;
    f32 r2 = r * r;
    // Ячейка целиком внутри, если её дальний угол ближе r с запасом: объекты на самой границе круга
    // проверяются по одному тем же сравнением, что у вызывающих
    f32 in2 = (r - 0.01f) * (r - 0.01f);
    for (i32 cz = cz0; cz <= cz1; cz++) {
        f32 z0 = g->oz + (f32)cz * g->cell - z, z1 = z0 + g->cell;
        f32 fz = fmaxf(z0 * z0, z1 * z1);
        u32 row = (u32)cz * g->w;
        for (i32 cx = cx0; cx <= cx1; cx++) {
            f32 x0 = g->ox + (f32)cx * g->cell - x, x1 = x0 + g->cell;
            // Крайние ячейки сетки собирают и прижатых к краю: только по одному
            bool edge = cx == 0 || cz == 0 || cx == (i32)g->w - 1 || cz == (i32)g->h - 1;
            bool inside = !edge && fmaxf(x0 * x0, x1 * x1) + fz <= in2;
            u32 base = (row + (u32)cx) * nt;
            for (u32 m = mask; m; m &= m - 1) {
                u32 t = (u32)__builtin_ctz(m);
                u32 k0 = g->start[base + t], k1 = g->start[base + t + 1];
                if (inside) {
                    count[t] += k1 - k0;
                    continue;
                }
                for (u32 k = k0; k < k1; k++) {
                    u32 j = g->items[k];
                    f32 dx = px[j] - x, dz = pz[j] - z;
                    if (dx * dx + dz * dz <= r2) count[t]++;
                }
            }
        }
    }
}

u32 grid_tags(const Grid *g, f32 x, f32 z, f32 r)
{
    i32 bx0 = coord(x - r, g->ox, g->inv_cell, g->w) / GRID_BLOCK, bx1 = coord(x + r, g->ox, g->inv_cell, g->w) / GRID_BLOCK;
    i32 bz0 = coord(z - r, g->oz, g->inv_cell, g->h) / GRID_BLOCK, bz1 = coord(z + r, g->oz, g->inv_cell, g->h) / GRID_BLOCK;
    u32 m = 0;
    for (i32 bz = bz0; bz <= bz1; bz++)
        for (i32 bx = bx0; bx <= bx1; bx++) m |= g->block_mask[(u32)bz * g->bw + (u32)bx];
    return m;
}

void grid_ser(Ser *s, Grid *g)
{
    SER(s, g->cell);
    SER(s, g->inv_cell);
    SER(s, g->ox);
    SER(s, g->oz);
    SER(s, g->w);
    SER(s, g->h);
    SER(s, g->ntags);
    SER(s, g->bw);
    SER(s, g->bh);
    ser_count(s, &g->count, g->cap);
    u64 keys = (u64)g->w * g->h * g->ntags, blocks = (u64)g->bw * g->bh;
    ser_check(s, keys <= g->max_keys && blocks <= g->max_keys);
    if (s->bad) return;
    SER_N(s, g->start, keys + 1);
    SER_N(s, g->items, g->count);
    SER_N(s, g->key_of, g->count);
    SER_N(s, g->block_mask, blocks);
}
