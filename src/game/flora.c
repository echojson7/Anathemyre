#include "flora.h"

#include <string.h>

#include "../core/rng.h"

// Икосаэдр с делением граней: 0 — 20 граней, 1 — 80, 2 — 320
#define ICO_MAX_V 162
#define ICO_MAX_F 320

typedef struct Ico {
    V3  v[ICO_MAX_V];
    u16 f[ICO_MAX_F][3];
    u32 nv, nf;
} Ico;

static Ico g_ico[3];

static u16 midpoint(Ico *o, u32 a, u32 b, u32 (*edge)[3], u32 *ne)
{
    u32 lo = MIN(a, b), hi = MAX(a, b);
    for (u32 i = 0; i < *ne; i++)
        if (edge[i][0] == lo && edge[i][1] == hi) return (u16)edge[i][2];
    o->v[o->nv] = v3_norm(v3_mul(v3_add(o->v[a], o->v[b]), 0.5f));
    edge[*ne][0] = lo;
    edge[*ne][1] = hi;
    edge[*ne][2] = o->nv;
    (*ne)++;
    return (u16)o->nv++;
}

static const Ico *ico(u32 subdiv)
{
    subdiv = MIN(subdiv, 2u);
    Ico *o = &g_ico[subdiv];
    if (o->nv) return o;
    if (subdiv == 0) {
        const f32 t = 1.618034f;
        const f32 v[12][3] = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t },
                               { 0, -1, -t }, { 0, 1, -t }, { t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
        static const u16 f[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
                                      { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                      { 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
                                      { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
        for (u32 i = 0; i < 12; i++) o->v[i] = v3_norm(v3(v[i][0], v[i][1], v[i][2]));
        o->nv = 12;
        for (u32 i = 0; i < 20; i++)
            for (u32 k = 0; k < 3; k++) o->f[i][k] = f[i][k];
        o->nf = 20;
        return o;
    }
    const Ico *src = ico(subdiv - 1);
    memcpy(o->v, src->v, sizeof(V3) * src->nv);
    o->nv = src->nv;
    o->nf = 0;
    u32 edge[ICO_MAX_F * 3 / 2][3];
    u32 ne = 0;
    for (u32 i = 0; i < src->nf; i++) {
        u16 a = src->f[i][0], b = src->f[i][1], c = src->f[i][2];
        u16 ab = midpoint(o, a, b, edge, &ne), bc = midpoint(o, b, c, edge, &ne), ca = midpoint(o, c, a, edge, &ne);
        const u16 nf[4][3] = { { a, ab, ca }, { b, bc, ab }, { c, ca, bc }, { ab, bc, ca } };
        for (u32 k = 0; k < 4; k++) {
            o->f[o->nf][0] = nf[k][0];
            o->f[o->nf][1] = nf[k][1];
            o->f[o->nf][2] = nf[k][2];
            o->nf++;
        }
    }
    return o;
}

static f32 hash3(i32 x, i32 y, i32 z, u32 seed)
{
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + (u32)z * 2246822519u + seed * 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (f32)(h & 0xFFFFFF) / 16777215.0f;
}

static f32 noise3(V3 p, u32 seed)
{
    i32 ix = (i32)floorf(p.x), iy = (i32)floorf(p.y), iz = (i32)floorf(p.z);
    f32 fx = p.x - (f32)ix, fy = p.y - (f32)iy, fz = p.z - (f32)iz;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    fz = fz * fz * (3 - 2 * fz);
    f32 c[2][2][2];
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++)
            for (int d = 0; d < 2; d++) c[a][b][d] = hash3(ix + a, iy + b, iz + d, seed);
    f32 x00 = lerpf(c[0][0][0], c[1][0][0], fx), x10 = lerpf(c[0][1][0], c[1][1][0], fx);
    f32 x01 = lerpf(c[0][0][1], c[1][0][1], fx), x11 = lerpf(c[0][1][1], c[1][1][1], fx);
    return lerpf(lerpf(x00, x10, fy), lerpf(x01, x11, fy), fz);
}

// Разброс цвета вершин начиная с from: каждая темнее или светлее на amount
static void vary_colors(MeshBuilder *mb, u32 from, f32 amount, Rng *r)
{
    for (u32 i = from; i < mb->vcount; i++) {
        f32 k = 1.0f + rng_range(r, -amount, amount);
        for (u32 c = 0; c < 3; c++) mb->v[i].color[c] = (u8)clampf((f32)mb->v[i].color[c] * k, 0.0f, 255.0f);
    }
}

// Крона: нормали наружу от её центра (объём читается мягко), параметр — глубина в кроне
static void crown_shade(MeshBuilder *mb, u32 from, V3 c, V3 r)
{
    for (u32 i = from; i < mb->vcount; i++) {
        Vertex *v = &mb->v[i];
        V3 d = v3(v->pos[0] - c.x, (v->pos[1] - c.y) * r.x / r.y, v->pos[2] - c.z);
        f32 depth = clampf(v3_len(d) / r.x, 0.0f, 1.0f);
        V3 n = v3_norm(v3_add(v3_mul(v3(v->nrm[0], v->nrm[1], v->nrm[2]), 0.45f), v3_mul(v3_norm(d), 0.55f)));
        v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
        v->color[3] = (u8)(clampf(depth * depth * 1.15f + 0.1f * (n.y + 1.0f), 0.0f, 1.0f) * 255.0f);
    }
}

// Неровная сфера: точки икосаэдра сдвинуты шумом по направлению. flat — грани плоские (камень)
static void blob_ex(MeshBuilder *mb, V3 c, V3 r, u32 subdiv, f32 jitter, u32 seed, bool flat, const V4 *cuts, u32 ncuts)
{
    const Ico *o = ico(subdiv);
    V3 p[ICO_MAX_V];
    V3 off = v3((f32)(seed % 97) * 1.37f, (f32)(seed % 89) * 0.71f, (f32)(seed % 83) * 1.13f);
    for (u32 i = 0; i < o->nv; i++) {
        V3 d = o->v[i];
        f32 n = noise3(v3_add(v3_mul(d, 1.6f), off), seed) * 0.7f + noise3(v3_add(v3_mul(d, 3.7f), off), seed + 1) * 0.3f;
        V3 q = v3_mul(d, 1.0f + jitter * (n * 2.0f - 1.0f));
        // Срезы плоскостями: камень с гранями
        for (u32 k = 0; k < ncuts; k++) {
            f32 s = v3_dot(q, v3(cuts[k].x, cuts[k].y, cuts[k].z)) - cuts[k].w;
            if (s > 0.0f) q = v3_sub(q, v3_mul(v3(cuts[k].x, cuts[k].y, cuts[k].z), s));
        }
        p[i] = v3_add(c, v3_hadamard(q, r));
    }
    if (flat) {
        if (!mb_reserve(mb, o->nf * 3, o->nf * 3)) return;
        for (u32 f = 0; f < o->nf; f++) {
            V3 a = p[o->f[f][0]], b = p[o->f[f][1]], d = p[o->f[f][2]];
            V3 n = v3_norm(v3_cross(v3_sub(b, a), v3_sub(d, a)));
            u32 i0 = mb_vertex(mb, a, n), i1 = mb_vertex(mb, b, n), i2 = mb_vertex(mb, d, n);
            mb_tri(mb, i0, i1, i2);
        }
        return;
    }
    if (!mb_reserve(mb, o->nv, o->nf * 3)) return;
    u32 base = mb->vcount, ibase = mb->icount;
    for (u32 i = 0; i < o->nv; i++) mb_vertex(mb, p[i], o->v[i]);
    for (u32 f = 0; f < o->nf; f++) mb_tri(mb, base + o->f[f][0], base + o->f[f][1], base + o->f[f][2]);
    mb_smooth_normals(mb, base, ibase);
}

void flora_blob(MeshBuilder *mb, V3 c, V3 r, u32 subdiv, f32 jitter, u32 seed)
{
    blob_ex(mb, c, r, subdiv, jitter, seed, false, NULL, 0);
}

static V3 perp(V3 d)
{
    V3 ref = fabsf(d.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0);
    return v3_norm(v3_cross(d, ref));
}

// Ось d, повёрнутая на угол a к перпендикуляру, который сам повёрнут вокруг d на phi
static V3 tilt(V3 d, f32 a, f32 phi)
{
    V3 s = perp(d), f = v3_cross(d, s);
    V3 side = v3_add(v3_mul(s, cosf(phi)), v3_mul(f, sinf(phi)));
    return v3_norm(v3_add(v3_mul(d, cosf(a)), v3_mul(side, sinf(a))));
}

#define MAX_TIPS 96

typedef struct Tips {
    V3  p[MAX_TIPS];
    f32 len[MAX_TIPS];
    u32 n;
} Tips;

typedef struct LimbStyle {
    f32 spread;     // отклонение дочерних веток, рад
    f32 up;         // тяга вверх
    f32 shrink;     // длина ребёнка от родителя
    f32 kink;       // излом посередине ветки
    u32 kids_min, kids_extra;
    u32 lvl[FLORA_LODS];   // до какого поколения веток рисовать на каждом LOD (тонкие скрыты листвой)
} LimbStyle;

// Ветка двумя отрезками с изломом; на концах последнего поколения — места для листвы.
// Число вызовов ГСЧ не зависит от LOD: форма одна на всех уровнях
static void limb(MeshBuilder *mb, Rng *r, V3 a, V3 dir, f32 len, f32 rad, u32 depth, u32 level, u32 lod,
                 const LimbStyle *st, Tips *t)
{
    V3 d1 = tilt(dir, rng_range(r, 0.0f, st->kink), rng_range(r, 0.0f, 2.0f * PI_F));
    V3 mid = v3_add(a, v3_mul(d1, len * 0.5f));
    V3 d2 = v3_norm(v3_add(tilt(d1, rng_range(r, 0.0f, st->kink), rng_range(r, 0.0f, 2.0f * PI_F)), v3(0, st->up * 0.3f, 0)));
    V3 b = v3_add(mid, v3_mul(d2, len * 0.5f));
    // Силуэт: ствол одним стержнем до кроны, без веток
    if (lod == FLORA_LODS - 1 && level == 0) {
        V2 prof[2] = { { 0.0f, rad }, { len * 1.4f, rad * 0.4f } };
        mb_lathe(mb, a, v3_add(a, v3_mul(d2, len * 1.4f)), prof, 2, 3, 1, 1);
    } else if (level <= st->lvl[lod]) {
        u32 segs = lod == 2 ? 3 : (level == 0 ? (lod == 0 ? 7 : 5) : (level == 1 && lod == 0 ? 5 : 4));
        mb_capsule(mb, a, mid, rad, rad * 0.86f, segs, 1.0f, 1.0f);
        mb_capsule(mb, mid, b, rad * 0.86f, rad * 0.7f, segs, 1.0f, 1.0f);
    }
    if (depth == 0) {
        if (t && t->n < MAX_TIPS) {
            t->p[t->n] = b;
            t->len[t->n++] = len;
        }
        return;
    }
    u32 kids = st->kids_min + rng_below(r, st->kids_extra + 1);
    f32 phi0 = rng_range(r, 0.0f, 2.0f * PI_F);
    for (u32 k = 0; k < kids; k++) {
        f32 phi = phi0 + 2.0f * PI_F * (f32)k / (f32)kids + rng_range(r, -0.4f, 0.4f);
        V3 cd = v3_norm(v3_add(tilt(d2, st->spread * rng_range(r, 0.7f, 1.2f), phi), v3(0, st->up, 0)));
        V3 start = v3_lerp(mid, b, rng_range(r, 0.55f, 1.0f));
        limb(mb, r, start, cd, len * st->shrink * rng_range(r, 0.85f, 1.15f), rad * 0.6f, depth - 1, level + 1, lod, st, t);
    }
}

// Листва на концах веток: LOD 0 — гладкие комки, 1 — грубые, 2 — каждый третий, но крупнее
static void foliage(MeshBuilder *mb, Rng *r, const Tips *t, V3 leaf, f32 size, u32 lod, u32 seed)
{
    if (!t->n) return;
    V3 lo = t->p[0], hi = t->p[0];
    for (u32 i = 1; i < t->n; i++) {
        lo = v3(fminf(lo.x, t->p[i].x), fminf(lo.y, t->p[i].y), fminf(lo.z, t->p[i].z));
        hi = v3(fmaxf(hi.x, t->p[i].x), fmaxf(hi.y, t->p[i].y), fmaxf(hi.z, t->p[i].z));
    }
    V3 c = v3_mul(v3_add(lo, hi), 0.5f);
    V3 cr = v3(fmaxf(hi.x - lo.x, hi.z - lo.z) * 0.5f + size, (hi.y - lo.y) * 0.5f + size, 0);
    u32 from = mb->vcount;
    // Каждый уровень берёт часть комков, но крупнее; последний — силуэт кроны из двух
    static const u32 every[FLORA_LODS] = { 1, 2, 4, 0 };
    static const f32 grow[FLORA_LODS] = { 1.0f, 1.3f, 1.9f, 1.0f };
    for (u32 i = 0; i < t->n; i++) {
        f32 s = size * rng_range(r, 0.75f, 1.25f);
        f32 sy = rng_range(r, 0.6f, 0.85f);
        u32 bs = rng_u32(r);
        V3 shade = v3_mul(leaf, rng_range(r, 0.82f, 1.15f));
        if (!every[lod] || i % every[lod]) continue;
        f32 k = grow[lod];
        mb->color = shade;
        V3 p = v3_lerp(t->p[i], c, lod == 2 ? 0.3f : (lod == 1 ? 0.12f : 0.0f));
        u32 sub = lod == 0 && s >= 0.7f ? 1 : 0;
        blob_ex(mb, v3_add(p, v3(0, s * 0.25f, 0)), v3(s * k, s * k * sy, s * k), sub, 0.34f, bs + seed, false, NULL, 0);
    }
    if (lod == FLORA_LODS - 1) {
        mb->color = leaf;
        for (u32 k = 0; k < 2; k++) {
            V3 off = v3(cr.x * (k ? 0.3f : -0.3f), (k ? 0.15f : -0.1f) * cr.y, cr.x * (k ? -0.2f : 0.25f));
            blob_ex(mb, v3_add(c, off), v3(cr.x * 0.8f, cr.y * 0.75f, cr.x * 0.8f), 0, 0.25f, seed + k, false, NULL, 0);
        }
    }
    vary_colors(mb, from, 0.12f, r);
    crown_shade(mb, from, c, v3(cr.x, cr.y, cr.x));
}

static void tree_broad(MeshBuilder *mb, Rng *r, u32 lod, u32 seed)
{
    V3 bark = v3_mul(v3(0.17f, 0.13f, 0.10f), rng_range(r, 0.8f, 1.15f));
    V3 leaf = v3_hadamard(v3(0.075f, 0.11f, 0.034f), v3(rng_range(r, 0.85f, 1.2f), rng_range(r, 0.9f, 1.15f), rng_range(r, 0.8f, 1.2f)));
    f32 h = rng_range(r, 2.6f, 4.5f), rad = rng_range(r, 0.2f, 0.34f);
    LimbStyle st = { .spread = rng_range(r, 0.55f, 0.85f), .up = rng_range(r, 0.15f, 0.45f), .shrink = rng_range(r, 0.62f, 0.75f),
                     .kink = 0.22f, .kids_min = 2, .kids_extra = 1, .lvl = { 2, 1, 0, 0 } };
    mb_set(mb, bark, MAT_BARK, BONE_NONE);
    // Корневые наплывы
    u32 roots = 3 + rng_below(r, 3);
    for (u32 i = 0; i < roots; i++) {
        f32 a = 2.0f * PI_F * (f32)i / (f32)roots + rng_range(r, -0.3f, 0.3f);
        V3 dir = v3(sinf(a), -0.35f, cosf(a));
        if (lod < 2) mb_capsule(mb, v3(0, 0.45f, 0), v3_add(v3(0, 0.45f, 0), v3_mul(dir, rad * 3.2f)), rad * 0.55f, rad * 0.2f, 5, 1, 1);
    }
    Tips t = { .n = 0 };
    limb(mb, r, v3(0, -0.2f, 0), tilt(v3(0, 1, 0), rng_range(r, 0.0f, 0.12f), rng_range(r, 0, 2 * PI_F)), h, rad, 3, 0, lod, &st, &t);
    mb_set(mb, leaf, MAT_LEAF, BONE_NONE);
    foliage(mb, r, &t, leaf, rng_range(r, 0.85f, 1.3f), lod, seed);
}

static void tree_birch(MeshBuilder *mb, Rng *r, u32 lod, u32 seed)
{
    V3 bark = v3_mul(v3(0.50f, 0.48f, 0.43f), rng_range(r, 0.85f, 1.1f));
    V3 leaf = v3_hadamard(v3(0.11f, 0.14f, 0.04f), v3(rng_range(r, 0.9f, 1.25f), rng_range(r, 0.9f, 1.1f), rng_range(r, 0.8f, 1.1f)));
    u32 stems = 1 + rng_below(r, 3);
    LimbStyle st = { .spread = 0.7f, .up = 0.55f, .shrink = 0.6f, .kink = 0.12f, .kids_min = 1, .kids_extra = 1, .lvl = { 2, 1, 0, 0 } };
    Tips t = { .n = 0 };
    for (u32 s = 0; s < stems; s++) {
        f32 a = rng_range(r, 0, 2 * PI_F), lean = stems > 1 ? rng_range(r, 0.08f, 0.22f) : rng_range(r, 0.0f, 0.08f);
        V3 dir = tilt(v3(0, 1, 0), lean, a);
        f32 h = rng_range(r, 3.0f, 4.2f), rad = rng_range(r, 0.09f, 0.15f);
        mb_set(mb, bark, MAT_BARK, BONE_NONE);
        mb->param = 0.0f;   // светлая кора с тёмными метками
        // Ствол тремя отрезками, ветви от верхней половины
        V3 p = v3(0, -0.2f, 0);
        for (u32 k = 0; k < 3; k++) {
            V3 nd = tilt(dir, rng_range(r, 0.0f, 0.1f), rng_range(r, 0, 2 * PI_F));
            V3 q = v3_add(p, v3_mul(nd, h));
            f32 r0 = rad * (1.0f - 0.25f * (f32)k), r1 = rad * (0.75f - 0.25f * (f32)k);
            if (lod < 2 || k < 2) mb_capsule(mb, p, q, r0, r1, lod == 0 ? 6 : (lod == 3 ? 3 : 4), 1, 1);
            if (k > 0) {
                u32 nb = 1 + rng_below(r, 2);
                for (u32 b = 0; b < nb; b++) {
                    V3 bd = v3_norm(v3_add(tilt(nd, rng_range(r, 0.6f, 1.0f), rng_range(r, 0, 2 * PI_F)), v3(0, 0.3f, 0)));
                    limb(mb, r, v3_lerp(p, q, rng_range(r, 0.2f, 0.9f)), bd, rng_range(r, 0.9f, 1.6f), r1 * 0.45f, 1, 1, lod, &st, &t);
                }
            }
            p = q;
            dir = nd;
        }
        if (t.n + 3 <= MAX_TIPS) {
            // Листва и по самому стволу в верхней части
            for (u32 k = 0; k < 3; k++) {
                t.p[t.n] = v3_add(v3_mul(p, 1.0f - 0.12f * (f32)k), v3(rng_range(r, -0.5f, 0.5f), 0, rng_range(r, -0.5f, 0.5f)));
                t.len[t.n++] = 1.0f;
            }
        }
    }
    mb_set(mb, leaf, MAT_LEAF, BONE_NONE);
    foliage(mb, r, &t, leaf, rng_range(r, 0.55f, 0.8f), lod, seed);
}

static void tree_dead(MeshBuilder *mb, Rng *r, u32 lod)
{
    V3 bark = v3_mul(v3(0.21f, 0.20f, 0.18f), rng_range(r, 0.8f, 1.15f));
    mb_set(mb, bark, MAT_BARK, BONE_NONE);
    bool snag = rng_f32(r) < 0.35f;
    LimbStyle st = { .spread = rng_range(r, 0.7f, 1.1f), .up = 0.1f, .shrink = 0.7f, .kink = 0.45f, .kids_min = 1, .kids_extra = 2,
                     .lvl = { 3, 2, 1, 1 } };
    f32 h = snag ? rng_range(r, 2.5f, 5.0f) : rng_range(r, 3.0f, 5.0f);
    limb(mb, r, v3(0, -0.2f, 0), tilt(v3(0, 1, 0), rng_range(r, 0.0f, 0.15f), rng_range(r, 0, 2 * PI_F)), h,
         rng_range(r, 0.18f, 0.32f), snag ? 1 : 3, 0, lod, &st, NULL);
}

static void tree_pine(MeshBuilder *mb, Rng *r, u32 lod, u32 seed)
{
    V3 bark = v3_mul(v3(0.15f, 0.10f, 0.075f), rng_range(r, 0.85f, 1.15f));
    V3 leaf = v3_hadamard(v3(0.038f, 0.065f, 0.042f), v3(rng_range(r, 0.85f, 1.2f), rng_range(r, 0.9f, 1.15f), rng_range(r, 0.85f, 1.15f)));
    f32 H = rng_range(r, 9.0f, 15.0f), rad = rng_range(r, 0.18f, 0.3f), R = rng_range(r, 2.0f, 3.2f);
    f32 h0 = rng_range(r, 1.2f, 3.0f);
    mb_set(mb, bark, MAT_BARK, BONE_NONE);
    V2 prof[4] = { { 0.0f, rad * 1.5f }, { 0.5f, rad }, { H * 0.6f, rad * 0.6f }, { H, rad * 0.1f } };
    mb_lathe(mb, v3(0, -0.3f, 0), v3(0, H, 0), prof, 4, lod == 0 ? 7 : (lod == 3 ? 3 : 4), 1, 1);
    u32 tiers = 8 + rng_below(r, 4);
    static const u32 pts_lod[FLORA_LODS] = { 8, 6, 4, 4 }, tier_every[FLORA_LODS] = { 1, 2, 3, 5 };
    u32 pts = pts_lod[lod];
    u32 from = mb->vcount;
    for (u32 i = 0; i < tiers; i++) {
        f32 t = (f32)i / (f32)(tiers - 1);
        f32 y = h0 + (H - h0) * t * 0.9f;
        f32 tr = R * powf(1.0f - t, 0.85f) * rng_range(r, 0.85f, 1.15f) + 0.35f;
        f32 th = (H - h0) / (f32)tiers * rng_range(r, 1.6f, 2.1f);
        f32 droop = tr * rng_range(r, 0.25f, 0.45f);
        f32 a0 = rng_range(r, 0, 2 * PI_F);
        V3 shade = v3_mul(leaf, rng_range(r, 0.85f, 1.15f));
        V3 jit[16];
        for (u32 k = 0; k < 16; k++) jit[k] = v3(rng_range(r, -0.15f, 0.15f), rng_range(r, -0.1f, 0.1f), rng_range(r, -0.15f, 0.15f));
        if (i % tier_every[lod] && i + 1 != tiers) continue;
        if (lod) {
            tr *= 1.1f;
            th *= 0.8f + 0.7f * (f32)tier_every[lod];
        }
        u32 n = pts * 2;
        if (!mb_reserve(mb, n + 2, n * 6)) return;
        mb->color = shade;
        u32 ibase = mb->icount;
        u32 apex = mb_vertex(mb, v3(0, y + th, 0), v3(0, 1, 0));
        u32 under = mb_vertex(mb, v3(0, y - droop * 0.2f, 0), v3(0, -1, 0));
        u32 rim = mb->vcount;
        for (u32 k = 0; k < n; k++) {
            f32 a = a0 + PI_F * (f32)k / (f32)pts;
            f32 rr = k % 2 ? tr * 0.55f : tr;
            V3 p = v3(sinf(a) * rr, y - (k % 2 ? droop * 0.5f : droop), cosf(a) * rr);
            mb_vertex(mb, v3_add(p, jit[k % 16]), v3(sinf(a), 0.3f, cosf(a)));
        }
        for (u32 k = 0; k < n; k++) {
            u32 k1 = (k + 1) % n;
            mb_tri(mb, apex, rim + k1, rim + k);
            mb_tri(mb, under, rim + k, rim + k1);
        }
        mb_smooth_normals(mb, apex, ibase);
    }
    vary_colors(mb, from, 0.1f, r);
    crown_shade(mb, from, v3(0, (h0 + H) * 0.5f, 0), v3(R + 0.3f, (H - h0) * 0.5f + 0.5f, R + 0.3f));
    (void)seed;
}

static void tree_bush(MeshBuilder *mb, Rng *r, u32 lod, u32 seed)
{
    // Тёмно-зелёный, оливковый или с бурой осенней листвой
    static const V3 tones[3] = { { 0.055f, 0.085f, 0.03f }, { 0.09f, 0.10f, 0.04f }, { 0.10f, 0.075f, 0.04f } };
    u32 tone = rng_f32(r) < 0.65f ? 0 : 1 + rng_below(r, 2);
    V3 leaf = v3_mul(tones[tone], rng_range(r, 0.85f, 1.15f));
    mb_set(mb, v3(0.12f, 0.10f, 0.07f), MAT_BARK, BONE_NONE);
    // Прутья из одного корня веером, листва комками по их длине
    u32 n = 4 + rng_below(r, 4);
    Tips t = { .n = 0 };
    f32 spread = rng_range(r, 0.5f, 1.0f), tall = rng_range(r, 0.7f, 1.5f);
    for (u32 i = 0; i < n; i++) {
        f32 a = rng_range(r, 0, 2 * PI_F), d = rng_range(r, 0.3f, 1.0f) * spread;
        V3 tip = v3(sinf(a) * d, rng_range(r, 0.6f, 1.0f) * tall, cosf(a) * d);
        if (lod == 0) mb_capsule(mb, v3(0, -0.1f, 0), tip, 0.035f, 0.015f, 4, 1, 1);
        if (t.n + 2 > MAX_TIPS) continue;
        t.p[t.n] = tip;
        t.len[t.n++] = 0.5f;
        t.p[t.n] = v3_mul(tip, rng_range(r, 0.45f, 0.65f));
        t.len[t.n++] = 0.3f;
    }
    mb_set(mb, leaf, MAT_LEAF, BONE_NONE);
    foliage(mb, r, &t, leaf, rng_range(r, 0.38f, 0.55f) * (0.7f + 0.3f * tall), lod, seed);
}

void flora_tree(MeshBuilder *mb, TreeKind kind, u32 seed, u32 lod)
{
    Rng r;
    rng_seed(&r, seed, 7 + (u64)kind);
    switch (kind) {
    case TREE_BROAD: tree_broad(mb, &r, lod, seed); break;
    case TREE_PINE: tree_pine(mb, &r, lod, seed); break;
    case TREE_BIRCH: tree_birch(mb, &r, lod, seed); break;
    case TREE_DEAD: tree_dead(mb, &r, lod); break;
    case TREE_BUSH: tree_bush(mb, &r, lod, seed); break;
    default: break;
    }
}

// Грубый уровень пучка: каждая третья травинка, но шире (форма та же: ГСЧ тратится одинаково)
static bool g_blade_thin;
static u32  g_blade_n;

// Травинка: два отрезка и острие, изгиб наружу; цвет от основания к кончику, параметр — высота
static void blade(MeshBuilder *mb, V3 root, V3 out, f32 h, f32 w, f32 bend, V3 c0, V3 c1)
{
    if (g_blade_thin) {
        if (g_blade_n++ % 3) return;
        w *= 1.8f;
    }
    if (!mb_reserve(mb, 5, 9)) return;
    V3 side = v3_norm(v3_cross(v3(0, 1, 0), out));
    if (v3_len(side) < 0.5f) side = v3(1, 0, 0);
    V3 nrm = v3_norm(v3_add(out, v3(0, 0.6f, 0)));
    static const f32 ts[3] = { 0.0f, 0.55f, 1.0f };
    static const f32 ws[3] = { 1.0f, 0.75f, 0.0f };
    u32 idx[5], n = 0;
    for (u32 k = 0; k < 3; k++) {
        f32 t = ts[k];
        V3 c = v3_add(root, v3_add(v3_mul(v3(0, 1, 0), h * t), v3_mul(out, bend * h * t * t)));
        mb->color = v3_lerp(c0, c1, t);
        mb->param = t;
        if (k < 2) {
            idx[n++] = mb_vertex(mb, v3_sub(c, v3_mul(side, w * ws[k] * 0.5f)), nrm);
            idx[n++] = mb_vertex(mb, v3_add(c, v3_mul(side, w * ws[k] * 0.5f)), nrm);
        } else {
            idx[n++] = mb_vertex(mb, c, nrm);
        }
    }
    mb_tri(mb, idx[0], idx[1], idx[3]);
    mb_tri(mb, idx[0], idx[3], idx[2]);
    mb_tri(mb, idx[2], idx[3], idx[4]);
}

void flora_grass(MeshBuilder *mb, GrassKind kind, u32 seed, u32 lod)
{
    g_blade_thin = lod > 0;
    g_blade_n = 0;
    Rng r;
    rng_seed(&r, seed, 31 + (u64)kind);
    mb_set(mb, v3(1, 1, 1), MAT_GRASS, BONE_NONE);
    V3 c0 = v3(0.03f, 0.045f, 0.012f), c1 = v3(0.16f, 0.19f, 0.06f);
    switch (kind) {
    case GRASS_MEADOW: {
        u32 n = 16 + rng_below(&r, 7);
        for (u32 i = 0; i < n; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), d = rng_range(&r, 0.0f, 0.32f);
            V3 tip = v3_mul(c1, rng_range(&r, 0.8f, 1.2f));
            blade(mb, v3(sinf(a) * d, -0.03f, cosf(a) * d), v3(sinf(a), 0, cosf(a)), rng_range(&r, 0.2f, 0.5f),
                  rng_range(&r, 0.035f, 0.06f), rng_range(&r, 0.15f, 0.5f), c0, tip);
        }
    } break;
    case GRASS_TALL: {
        V3 dry = v3(0.24f, 0.21f, 0.10f);
        u32 n = 9 + rng_below(&r, 5);
        for (u32 i = 0; i < n; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), d = rng_range(&r, 0.0f, 0.18f);
            blade(mb, v3(sinf(a) * d, -0.03f, cosf(a) * d), v3(sinf(a), 0, cosf(a)), rng_range(&r, 0.5f, 0.95f),
                  rng_range(&r, 0.025f, 0.045f), rng_range(&r, 0.1f, 0.35f), c0, v3_lerp(c1, dry, rng_range(&r, 0.3f, 0.9f)));
        }
        // Колоски
        u32 heads = rng_below(&r, 4);
        for (u32 i = 0; i < heads; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), h = rng_range(&r, 0.8f, 1.15f);
            V3 top = v3(sinf(a) * 0.12f, h, cosf(a) * 0.12f);
            blade(mb, v3(0, 0, 0), v3(sinf(a), 0, cosf(a)), h, 0.012f, 0.1f, c0, dry);
            mb_set(mb, v3_mul(dry, 1.1f), MAT_GRASS, BONE_NONE);
            mb_capsule(mb, top, v3_add(top, v3(0, 0.1f, 0)), 0.016f, 0.008f, 4, 1, 1);
            mb->color = v3(1, 1, 1);
        }
    } break;
    case GRASS_REEDS: {
        V3 olive = v3(0.13f, 0.14f, 0.055f), pale = v3(0.25f, 0.23f, 0.11f);
        u32 n = 7 + rng_below(&r, 6);
        for (u32 i = 0; i < n; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), d = rng_range(&r, 0.0f, 0.3f);
            f32 h = rng_range(&r, 1.0f, 1.9f);
            V3 root = v3(sinf(a) * d, -0.05f, cosf(a) * d);
            blade(mb, root, v3(sinf(a), 0, cosf(a)), h, rng_range(&r, 0.02f, 0.035f), rng_range(&r, 0.03f, 0.12f), c0,
                  v3_lerp(olive, pale, rng_f32(&r)));
            if (rng_f32(&r) < 0.3f) {
                // Тёмный початок
                V3 top = v3_add(root, v3(sinf(a) * 0.05f * h, h * 0.86f, cosf(a) * 0.05f * h));
                mb_set(mb, v3(0.055f, 0.035f, 0.022f), MAT_GRASS, BONE_NONE);
                mb_capsule(mb, top, v3_add(top, v3(0, 0.18f, 0)), 0.03f, 0.03f, 5, 1, 1);
            }
        }
    } break;
    case GRASS_FERN: {
        V3 f0 = v3(0.03f, 0.05f, 0.02f), f1 = v3_mul(v3(0.07f, 0.11f, 0.035f), rng_range(&r, 0.85f, 1.2f));
        u32 n = 5 + rng_below(&r, 4);
        for (u32 i = 0; i < n; i++) {
            f32 a = 2 * PI_F * (f32)i / (f32)n + rng_range(&r, -0.3f, 0.3f);
            f32 len = rng_range(&r, 0.45f, 0.75f);
            // Вайя из двух широких травинок: дуга наружу и вниз
            V3 out = v3(sinf(a), 0, cosf(a));
            blade(mb, v3(0, 0, 0), out, len * 0.6f, 0.16f, 1.6f, f0, f1);
            blade(mb, v3_mul(out, 0.03f), out, len * 0.45f, 0.1f, 2.2f, f0, v3_mul(f1, 0.85f));
        }
    } break;
    case GRASS_FLOWERS: {
        u32 n = 7 + rng_below(&r, 4);
        for (u32 i = 0; i < n; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), d = rng_range(&r, 0.0f, 0.2f);
            blade(mb, v3(sinf(a) * d, -0.03f, cosf(a) * d), v3(sinf(a), 0, cosf(a)), rng_range(&r, 0.15f, 0.35f),
                  0.045f, 0.4f, c0, c1);
        }
        // Блёклые цветки: охра или белёсые, без ярких пятен
        V3 petal = rng_f32(&r) < 0.5f ? v3(0.34f, 0.28f, 0.12f) : v3(0.30f, 0.30f, 0.26f);
        u32 heads = 3 + rng_below(&r, 4);
        for (u32 i = 0; i < heads; i++) {
            f32 a = rng_range(&r, 0, 2 * PI_F), d = rng_range(&r, 0.05f, 0.22f), h = rng_range(&r, 0.3f, 0.5f);
            V3 top = v3(sinf(a) * d, h, cosf(a) * d);
            blade(mb, v3(sinf(a) * d * 0.5f, 0, cosf(a) * d * 0.5f), v3(sinf(a), 0, cosf(a)), h, 0.012f, 0.05f, c0, c1);
            mb_set(mb, v3_mul(petal, rng_range(&r, 0.85f, 1.1f)), MAT_GRASS, BONE_NONE);
            flora_blob(mb, top, v3(0.035f, 0.025f, 0.035f), 0, 0.2f, seed + i);
            mb->color = v3(1, 1, 1);
        }
    } break;
    default: break;
    }
}

void flora_rock(MeshBuilder *mb, u32 seed, u32 lod)
{
    Rng r;
    rng_seed(&r, seed, 53);
    static const V3 tones[4] = { { 0.24f, 0.235f, 0.22f }, { 0.22f, 0.205f, 0.18f }, { 0.26f, 0.24f, 0.20f }, { 0.18f, 0.18f, 0.175f } };
    mb_set(mb, v3_mul(tones[rng_below(&r, 4)], rng_range(&r, 0.85f, 1.15f)), MAT_ROCK, BONE_NONE);
    V4 cuts[5];
    u32 nc = 3 + rng_below(&r, 3);
    for (u32 i = 0; i < nc; i++) {
        V3 n = v3_norm(v3(rng_range(&r, -1, 1), rng_range(&r, -0.3f, 1), rng_range(&r, -1, 1)));
        cuts[i] = (V4){ n.x, n.y, n.z, rng_range(&r, 0.55f, 0.85f) };
    }
    u32 from = mb->vcount;
    V3 rad = v3(rng_range(&r, 0.8f, 1.2f), rng_range(&r, 0.5f, 0.9f), rng_range(&r, 0.8f, 1.2f));
    blob_ex(mb, v3(0, rad.y * 0.55f, 0), rad, lod == 0 ? 1 : 0, 0.3f, seed, true, cuts, nc);
    vary_colors(mb, from, 0.08f, &r);
}
