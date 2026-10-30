// Сетки оружия по описаниям: в своей системе — рукоять в начале координат, острие по +Y, лезвие по +Z
#include "anim.h"

#include <string.h>

// Брусок вдоль оси от d0 до d1 (м от кулака); ширина к лезвию, толщина поперёк, сдвиг к лезвию,
// наклон бруска в плоскости лезвия
static void wbox(MeshBuilder *mb, f32 d0, f32 d1, f32 width, f32 thick, f32 off, f32 tilt)
{
    V3 a = v3_norm(v3(0.0f, 1.0f, tilt));
    V3 f = v3(0.0f, -a.z, a.y);
    V3 c = v3_add(v3_mul(v3(0, 1, 0), 0.5f * (d0 + d1)), v3(0, 0, off));
    M4 m = m4_identity();
    V3 sy = v3_mul(a, d1 - d0), sz = v3_mul(f, width);
    m.m[0] = thick;
    m.m[4] = sy.x; m.m[5] = sy.y; m.m[6] = sy.z;
    m.m[8] = sz.x; m.m[9] = sz.y; m.m[10] = sz.z;
    m.m[12] = c.x; m.m[13] = c.y; m.m[14] = c.z;
    mb_box(mb, m);
}

static void wcyl(MeshBuilder *mb, f32 d0, f32 d1, f32 r, f32 off)
{
    mb_cylinder(mb, v3(0, d0, off), v3(0, d1, off), r, 10);
}

// Треугольник с плоской нормалью; обход разворачивается так, чтобы нормаль смотрела по out
static void ftri(MeshBuilder *mb, V3 a, V3 b, V3 c, V3 out)
{
    V3 n = v3_cross(v3_sub(b, a), v3_sub(c, a));
    f32 l = v3_len(n);
    if (l < 1e-9f) return;
    n = v3_mul(n, 1.0f / l);
    if (v3_dot(n, out) < 0.0f) {
        V3 t = b;
        b = c, c = t, n = v3_mul(n, -1.0f);
    }
    if (!mb_reserve(mb, 3, 3)) return;
    u32 i = mb_vertex(mb, a, n);
    mb_vertex(mb, b, n);
    mb_vertex(mb, c, n);
    mb_tri(mb, i, i + 1, i + 2);
}

// Детерминированное «случайное» 0..1 от номеров: щербины и ржавчина одинаковы при каждом старте
static f32 whash(u32 a, u32 b)
{
    u32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
    return (f32)(h & 0xFFFFu) / 65535.0f;
}

// Клинок протяжкой: станции (ось, обух, лезвие, толщина, толщина обуха, ребро). Сечение как у кованого клинка:
// плоскость с долом, линия спуска, узкая подводка у кромки (светлее), у однолезвийного — обух со скосами.
// Обоюдоострый (толщина обуха 0): спуски к обеим кромкам, плоскость посередине
enum { ST_N = 6, ST_MAX = 96, SEC_HALF = 8, SEC_N = SEC_HALF * 2 + 1 };

typedef struct BladeForm {
    f32 flat;              // обоюдоострый: доля полуширины без спуска (0 — ромб)
    f32 edge;              // ширина подводки у кромки, м
    f32 ful_y0, ful_y1;    // дол: от и до по оси
    f32 ful_w, ful_d;      // ширина и глубина дола
} BladeForm;

// Половина сечения (x >= 0) от обуха к лезвию; вторая — зеркально. facet[k] — грань от точки k к k+1:
// подводка, дол, спуск (см. ниже)
static void blade_section(const f32 *s, const BladeForm *f, V3 *out, u8 *facet)
{
    f32 y = s[0], zb = s[1], zf = s[2], t = s[3], ts = s[4];
    f32 w = zf - zb;
    bool two = ts <= 0.0f;
    // Дол: по оси плавно сходит на нет у концов
    f32 fd = 0.0f, fw = 0.0f;
    if (f->ful_d > 0.0f && y > f->ful_y0 && y < f->ful_y1) {
        f32 u = (y - f->ful_y0) / (f->ful_y1 - f->ful_y0);
        f32 k = clampf(minf(u, 1.0f - u) * 8.0f, 0.0f, 1.0f);
        fd = minf(f->ful_d * k, t * 0.6f), fw = f->ful_w * k;
    }
    f32 ew = minf(f->edge, w * 0.25f);
    // Толщина на начале подводки — чуть больше прямой линии спуска: у кромки свой угол
    V3 p[SEC_HALF];
    if (two) {
        f32 zm = 0.5f * (zb + zf), fl = f->flat * 0.5f * w;
        fw = minf(fw, fl * 1.8f);
        f32 g = maxf(0.5f * w - fl, 1e-4f), eb = t * ew / g * 1.6f;
        p[0] = v3(0.0f, y, zb);
        p[1] = v3(minf(eb, t), y, zb + ew);
        p[2] = v3(t, y, zm - fl);
        p[3] = v3(t, y, zm - 0.5f * fw);
        p[4] = v3(t - fd, y, zm);
        p[5] = v3(t, y, zm + 0.5f * fw);
        p[6] = v3(t, y, zm + fl);
        p[7] = v3(minf(eb, t), y, zf - ew);
    } else {
        f32 zr = zb + s[5] * w, ch = minf(ts * 0.8f, w * 0.1f), zc = 0.5f * (zb + ch + zr);
        f32 g = maxf(zf - zr, 1e-4f), eb = t * ew / g * 1.6f;
        fw = minf(fw, (zr - zb - ch) * 0.8f);
        p[0] = v3(ts, y, zb);
        p[1] = v3(t, y, zb + ch);
        p[2] = v3(t, y, zc - 0.5f * fw);
        p[3] = v3(t - fd, y, zc);
        p[4] = v3(t, y, zc + 0.5f * fw);
        p[5] = v3(t, y, zr);
        p[6] = v3(t, y, zr);
        p[7] = v3(minf(eb, t), y, zf - ew);
    }
    // По кругу: p0..p7 (x > 0), кромка, p7'..p0' (x < 0); у обоюдоострого p0' совпадает с p0
    u32 m = 0;
    for (u32 k = 0; k < SEC_HALF; k++) out[m++] = p[k];
    out[m++] = v3(0.0f, y, zf);
    for (i32 k = SEC_HALF - 1; k >= 0; k--) out[m++] = v3(-p[k].x, y, p[k].z);
    // Грани половины сечения и зеркально (грань k и 15 - k): 1 подводка, 2 дол, 3 спуск, 0 плоскость и обух
    static const u8 two_f[SEC_HALF] = { 1, 3, 0, 2, 2, 0, 3, 1 }, one_f[SEC_HALF] = { 0, 0, 2, 2, 0, 0, 3, 1 };
    for (u32 k = 0; k < SEC_HALF; k++) facet[k] = facet[SEC_N - 2 - k] = two ? two_f[k] : one_f[k];
    facet[SEC_N - 1] = 0;
}

static void blade_build(MeshBuilder *mb, const f32 *st, u32 n, const BladeForm *f, f32 clean)
{
    // Подводку точат: светлее и чище; дол темнее, спуск чуть светлее плоскости — так клинок читается гранями
    static const f32 shade[4] = { 1.0f, 1.6f, 0.7f, 1.12f };
    V3 base = mb->color;
    for (u32 i = 0; i + 1 < n; i++) {
        V3 a[SEC_N], b[SEC_N];
        u8 fa[SEC_N], fb[SEC_N];
        blade_section(&st[i * ST_N], f, a, fa);
        blade_section(&st[(i + 1) * ST_N], f, b, fb);
        // Середина участка: от неё нормали наружу
        V3 c = v3(0.0f, 0.5f * (st[i * ST_N] + st[(i + 1) * ST_N]),
                  0.25f * (st[i * ST_N + 1] + st[i * ST_N + 2] + st[(i + 1) * ST_N + 1] + st[(i + 1) * ST_N + 2]));
        for (u32 k = 0; k < SEC_N; k++) {
            u32 k1 = (k + 1) % SEC_N;
            V3 m = v3_mul(v3_add(v3_add(a[k], a[k1]), v3_add(b[k], b[k1])), 0.25f);
            V3 o = v3_sub(m, c);
            o.y = 0.0f;
            mb->color = v3_mul(base, shade[fa[k]]);
            mb->param = fa[k] == 1 ? minf(clean + 0.3f, 1.0f) : clean;
            ftri(mb, a[k], a[k1], b[k1], o);
            ftri(mb, a[k], b[k1], b[k], o);
        }
    }
    mb->color = base, mb->param = clean;
    // Торцы: первая и последняя станции, если сечение не сходится в точку
    for (u32 e = 0; e < 2; e++) {
        u32 i = e ? n - 1 : 0, j = e ? n - 2 : 1;
        V3 a[SEC_N];
        u8 fa[SEC_N];
        blade_section(&st[i * ST_N], f, a, fa);
        V3 o = v3(0.0f, st[i * ST_N] - st[j * ST_N], 0.0f);
        for (u32 k = 1; k + 1 < SEC_N; k++) ftri(mb, a[0], a[k], a[k + 1], o);
    }
}

// Щербины по кромке: вставить три станции и в средней увести кромку к ребру
static u32 blade_nicks(f32 *st, u32 n, u32 nicks, u32 seed)
{
    if (n < 2) return n;
    f32 y0 = st[0], y1 = st[(n - 1) * ST_N];
    for (u32 k = 0; k < nicks && n + 3 <= ST_MAX; k++) {
        f32 y = y0 + (y1 - y0) * (0.12f + 0.7f * whash(seed, k)), w = 0.007f + 0.008f * whash(seed, k + 50);
        f32 depth = 0.15f + 0.25f * whash(seed, k + 100);
        f32 ys[3] = { y - w, y, y + w };
        for (u32 q = 0; q < 3; q++) {
            // Участок, куда попадает ys[q] (станции могут идти и в обратную сторону)
            u32 i = 0;
            while (i + 2 < n && (st[(i + 1) * ST_N] - ys[q]) * (y1 - y0) < 0.0f) i++;
            f32 a = st[i * ST_N], b = st[(i + 1) * ST_N];
            if (fabsf(b - a) < 1e-5f) break;
            f32 t = clampf((ys[q] - a) / (b - a), 0.0f, 1.0f);
            f32 nw[ST_N];
            for (u32 c = 0; c < ST_N; c++) nw[c] = st[i * ST_N + c] + (st[(i + 1) * ST_N + c] - st[i * ST_N + c]) * t;
            if (q == 1) {
                f32 zr = nw[1] + nw[5] * (nw[2] - nw[1]);
                nw[2] -= (nw[2] - zr) * depth;
            }
            memmove(&st[(i + 2) * ST_N], &st[(i + 1) * ST_N], sizeof(f32) * ST_N * (n - i - 1));
            memcpy(&st[(i + 1) * ST_N], nw, sizeof nw);
            n++;
        }
    }
    return n;
}

// ---------------------------------------------------------------- пластина с прорезями

enum { PL_MAX = 512 };

static f32 cross2(V2 a, V2 b, V2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

static bool seg_cross(V2 a, V2 b, V2 c, V2 d)
{
    f32 d1 = cross2(c, d, a), d2 = cross2(c, d, b), d3 = cross2(a, b, c), d4 = cross2(a, b, d);
    return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f));
}

static bool in_tri(V2 p, V2 a, V2 b, V2 c)
{
    return cross2(a, b, p) > 0.0f && cross2(b, c, p) > 0.0f && cross2(c, a, p) > 0.0f;
}

static f32 area2(const V2 *p, const u16 *ix, u32 n)
{
    f32 s = 0.0f;
    for (u32 i = 0; i < n; i++) {
        V2 a = p[ix[i]], b = p[ix[(i + 1) % n]];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

// Контур (против часовой) с дырами (по часовой) — один многоугольник через перемычки, потом уши
static u32 plate_triangulate(const V2 *p, u16 *poly, u32 n, const u16 *hole_at, const u16 *hole_n, u32 holes,
                             u16 *tri)
{
    bool used[8] = { 0 };
    for (u32 h = 0; h < holes; h++) {
        // Дыра с самой дальней по оси точкой — первой
        u32 best = 0;
        f32 by = -1e9f;
        for (u32 k = 0; k < holes; k++) {
            if (used[k]) continue;
            for (u32 i = 0; i < hole_n[k]; i++)
                if (p[hole_at[k] + i].x > by) by = p[hole_at[k] + i].x, best = k;
        }
        used[best] = true;
        u32 hm = hole_at[best];
        for (u32 i = 0; i < hole_n[best]; i++)
            if (p[hole_at[best] + i].x > p[hm].x) hm = hole_at[best] + i;
        // Ближайшая видимая вершина многоугольника
        i32 pick = -1;
        f32 pd = 1e9f;
        for (u32 i = 0; i < n; i++) {
            V2 a = p[hm], b = p[poly[i]];
            f32 d = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
            if (d >= pd) continue;
            bool ok = true;
            for (u32 j = 0; j < n && ok; j++) {
                u16 e0 = poly[j], e1 = poly[(j + 1) % n];
                if (e0 == poly[i] || e1 == poly[i]) continue;
                if (seg_cross(a, b, p[e0], p[e1])) ok = false;
            }
            for (u32 k = 0; k < holes && ok; k++)
                for (u32 j = 0; j < hole_n[k] && ok; j++) {
                    u16 e0 = (u16)(hole_at[k] + j), e1 = (u16)(hole_at[k] + (j + 1) % hole_n[k]);
                    if (e0 == hm || e1 == hm) continue;
                    if (seg_cross(a, b, p[e0], p[e1])) ok = false;
                }
            if (ok) pick = (i32)i, pd = d;
        }
        if (pick < 0 || n + hole_n[best] + 2 > PL_MAX) continue;
        // poly[..pick], дыра от hm по кругу, hm, poly[pick..]
        u16 tmp[PL_MAX];
        u32 m = 0;
        for (u32 i = 0; i <= (u32)pick; i++) tmp[m++] = poly[i];
        u32 off = hm - hole_at[best];
        for (u32 i = 0; i <= hole_n[best]; i++) tmp[m++] = (u16)(hole_at[best] + (off + i) % hole_n[best]);
        for (u32 i = (u32)pick; i < n; i++) tmp[m++] = poly[i];
        memcpy(poly, tmp, sizeof(u16) * m);
        n = m;
    }
    // Уши
    u32 nt = 0, guard = 0;
    while (n > 3 && guard++ < 4 * PL_MAX) {
        bool cut = false;
        for (u32 i = 0; i < n; i++) {
            u16 ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
            V2 a = p[ia], b = p[ib], c = p[ic];
            if (cross2(a, b, c) <= 1e-12f) continue;
            bool ear = true;
            for (u32 j = 0; j < n && ear; j++) {
                u16 q = poly[j];
                if (q == ia || q == ib || q == ic) continue;
                V2 v = p[q];
                if ((v.x == a.x && v.y == a.y) || (v.x == b.x && v.y == b.y) || (v.x == c.x && v.y == c.y)) continue;
                if (in_tri(v, a, b, c)) ear = false;
            }
            if (!ear) continue;
            tri[nt * 3] = ia, tri[nt * 3 + 1] = ib, tri[nt * 3 + 2] = ic;
            nt++;
            memmove(&poly[i], &poly[i + 1], sizeof(u16) * (n - i - 1));
            n--;
            cut = true;
            break;
        }
        // Остались вырожденные (на одной прямой) — выбросить вершину
        if (!cut) {
            memmove(&poly[0], &poly[1], sizeof(u16) * (n - 1));
            n--;
        }
    }
    if (n == 3) {
        tri[nt * 3] = poly[0], tri[nt * 3 + 1] = poly[1], tri[nt * 3 + 2] = poly[2];
        nt++;
    }
    return nt;
}

// Пластина толщиной th: лицевые стороны по ±X, стенки по контуру и прорезям
static void plate_build(MeshBuilder *mb, const WeaponPart *pl, const WeaponPart *holes, u32 nholes)
{
    V2 p[PL_MAX];
    u16 poly[PL_MAX], hole_at[8], hole_n[8], tri[PL_MAX * 3];
    u32 n = pl->npt / 2;
    if (n < 3 || n > PL_MAX / 2) return;
    for (u32 i = 0; i < n; i++) p[i] = (V2){ g_weapon_pts[pl->pt + i * 2], g_weapon_pts[pl->pt + i * 2 + 1] };
    for (u32 i = 0; i < n; i++) poly[i] = (u16)i;
    if (area2(p, poly, n) < 0.0f)
        for (u32 i = 0; i < n / 2; i++) {
            V2 t = p[i];
            p[i] = p[n - 1 - i], p[n - 1 - i] = t;
        }
    u32 np = n, nh = 0;
    for (u32 h = 0; h < nholes && h < 8; h++) {
        const f32 *v = holes[h].v;
        const u32 segs = 14;
        if (np + segs > PL_MAX / 2) break;
        hole_at[nh] = (u16)np, hole_n[nh] = (u16)segs;
        // По часовой
        for (u32 i = 0; i < segs; i++) {
            f32 a = -2.0f * PI_F * (f32)i / (f32)segs;
            p[np++] = (V2){ v[0] + cosf(a) * v[2], v[1] + sinf(a) * v[3] };
        }
        nh++;
    }
    u32 nt = plate_triangulate(p, poly, n, hole_at, hole_n, nh, tri);
    f32 hx = pl->v[0] * 0.5f;
    for (u32 t = 0; t < nt; t++)
        for (u32 sd = 0; sd < 2; sd++) {
            f32 x = sd ? -hx : hx;
            V2 a = p[tri[t * 3]], b = p[tri[t * 3 + 1]], c = p[tri[t * 3 + 2]];
            ftri(mb, v3(x, a.x, a.y), v3(x, b.x, b.y), v3(x, c.x, c.y), v3(sd ? -1.0f : 1.0f, 0, 0));
        }
    // Стенки: у контура наружу (против часовой — нормаль вправо от хода), у прорезей внутрь дыры — так же
    for (u32 r = 0; r <= nh; r++) {
        u32 at = r ? hole_at[r - 1] : 0, cnt = r ? hole_n[r - 1] : n;
        for (u32 i = 0; i < cnt; i++) {
            V2 a = p[at + i], b = p[at + (i + 1) % cnt];
            V3 o = v3(0.0f, b.y - a.y, -(b.x - a.x));
            V3 a0 = v3(hx, a.x, a.y), a1 = v3(-hx, a.x, a.y), b0 = v3(hx, b.x, b.y), b1 = v3(-hx, b.x, b.y);
            ftri(mb, a0, b0, b1, o);
            ftri(mb, a0, b1, a1, o);
        }
    }
}

// ---------------------------------------------------------------- вид по качеству

// Ржавчина: доля железных частей (гарды, навершия) в рыжем и насколько; чистота стали клинка
// (альфа цвета, шейдер MAT_STEEL: меньше — больше пятен ржавчины); щербин на кромку
static const f32 g_rust_share[WEAPON_LOOKS] = { 0.85f, 0.4f, 0.0f };
static const f32 g_rust_mix[WEAPON_LOOKS] = { 0.8f, 0.55f, 0.0f };
static const f32 g_clean[WEAPON_LOOKS] = { 0.5f, 0.68f, 0.97f };
static const u32 g_nicks[WEAPON_LOOKS] = { 3, 1, 0 };

void weapon_build(MeshBuilder *mb, u8 weapon, u8 look, u8 bone)
{
    // Форма из описания (data/defs/weapons.txt) по порядку частей
    const WeaponDef *d = &g_weapons[weapon];
    if (d->unique || look >= WEAPON_LOOKS) look = WEAPON_LOOKS - 1;
    for (u32 k = 0; k < d->parts; k++) {
        const WeaponPart *p = &g_weapon_parts[d->part + k];
        V3 col = v3(p->color[0], p->color[1], p->color[2]);
        Material mat = (Material)p->mat;
        u32 seed = (u32)weapon * 131u + k;
        if (mat == MAT_METAL && whash(seed, 7) < g_rust_share[look]) {
            f32 m = g_rust_mix[look] * (0.75f + 0.25f * whash(seed, 8));
            col = v3_lerp(col, v3(0.27f, 0.16f, 0.10f), m);
        }
        // Обмотка у хлама — тряпьё
        if (p->kind == WP_WRAP && look == 0) col = v3(0.24f, 0.21f, 0.16f), mat = MAT_CLOTH;
        mb_set(mb, col, mat, bone);
        f32 clean = mat == MAT_STEEL ? g_clean[look] * (0.92f + 0.08f * whash(seed, 9)) : 1.0f;
        mb->param = clean;
        switch (p->kind) {
        case WP_ROD: wcyl(mb, p->v[0], p->v[1], p->v[2], p->v[3]); break;
        case WP_BOX: wbox(mb, p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5]); break;
        case WP_BLADE: {
            f32 st[ST_MAX * ST_N];
            u32 n = p->npt / ST_N;
            if (n < 2 || n > ST_MAX) break;
            memcpy(st, &g_weapon_pts[p->pt], sizeof(f32) * n * ST_N);
            if (mat == MAT_METAL || mat == MAT_STEEL) n = blade_nicks(st, n, g_nicks[look], seed);
            BladeForm f = { p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5] };
            blade_build(mb, st, n, &f, clean);
            break;
        }
        case WP_PLATE: {
            u32 h = 0;
            while (k + 1 + h < d->parts && g_weapon_parts[d->part + k + 1 + h].kind == WP_HOLE) h++;
            plate_build(mb, p, &g_weapon_parts[d->part + k + 1], h);
            break;
        }
        case WP_WRAP: {
            // Витки обмотки: наклонные кольца поверх рукояти; доля ширины витка и наклон — свои у каждой
            u32 turns = (u32)p->v[3];
            if (!turns) break;
            f32 pitch = (p->v[1] - p->v[0]) / (f32)turns, band = p->v[4] > 0.0f ? p->v[4] : 0.8f;
            V3 ax = v3_norm(v3(0.0f, 1.0f, p->v[5] != 0.0f ? p->v[5] : 0.32f));
            for (u32 i = 0; i < turns; i++) {
                V3 c = v3(0.0f, p->v[0] + ((f32)i + 0.5f) * pitch, 0.0f);
                f32 r = p->v[2] * (look == 0 ? 0.95f + 0.15f * whash(seed, i) : 1.0f);
                f32 h = pitch * band * 0.5f;
                mb_cylinder(mb, v3_sub(c, v3_mul(ax, h)), v3_add(c, v3_mul(ax, h)), r, 8);
            }
            break;
        }
        case WP_KNOB: {
            // Навершие: 0 — бочонок, 1 — шайба с прямыми боками, 2 — шар; граней v[4] (по умолчанию 8)
            f32 len = p->v[1] - p->v[0], r = p->v[2];
            u32 segs = p->v[4] >= 3.0f ? (u32)p->v[4] : 8;
            u32 form = (u32)p->v[5];
            V2 prof[8];
            u32 np = 0;
            prof[np++] = (V2){ 0.0f, 0.0f };
            if (form == 1) {
                prof[np++] = (V2){ 0.0f, r }, prof[np++] = (V2){ len, r };
            } else if (form == 2) {
                for (u32 i = 1; i < 6; i++) {
                    f32 a = PI_F * (f32)i / 6.0f;
                    prof[np++] = (V2){ len * 0.5f * (1.0f - cosf(a)), r * sinf(a) };
                }
            } else {
                prof[np++] = (V2){ 0.0f, r * 0.6f }, prof[np++] = (V2){ len * 0.3f, r };
                prof[np++] = (V2){ len * 0.7f, r }, prof[np++] = (V2){ len, r * 0.6f };
            }
            prof[np++] = (V2){ len, 0.0f };
            mb_lathe(mb, v3(0, p->v[0], p->v[3]), v3(0, p->v[1], p->v[3]), prof, np, segs, 1.0f, 1.0f);
            break;
        }
        default: break;
        }
    }
}

void weapon_build_xf(MeshBuilder *mb, u8 weapon, u8 look, M4 xf)
{
    u32 v0 = mb->vcount;
    weapon_build(mb, weapon, look, BONE_NONE);
    for (u32 i = v0; i < mb->vcount; i++) {
        Vertex *v = &mb->v[i];
        V3 p = m4_point(xf, v3(v->pos[0], v->pos[1], v->pos[2]));
        V3 n = v3_norm(m4_dir(xf, v3(v->nrm[0], v->nrm[1], v->nrm[2])));
        v->pos[0] = p.x, v->pos[1] = p.y, v->pos[2] = p.z;
        v->nrm[0] = n.x, v->nrm[1] = n.y, v->nrm[2] = n.z;
    }
}

void torch_build(MeshBuilder *mb, bool flame)
{
    const u8 B = CHAR_BONE_TORCH;
    if (!flame) {
        // Обломок древка, сверху ветошь в мазуте, перетянутая проволокой
        mb_set(mb, v3(0.22f, 0.15f, 0.09f), MAT_LEATHER, B);
        wcyl(mb, -0.16f, 0.44f, 0.017f, 0.0f);
        mb_set(mb, v3(0.07f, 0.06f, 0.05f), MAT_CLOTH, B);
        wcyl(mb, 0.36f, 0.52f, 0.034f, 0.0f);
        mb_set(mb, v3(0.20f, 0.19f, 0.18f), MAT_METAL, B);
        wcyl(mb, 0.40f, 0.415f, 0.037f, 0.0f);
        wcyl(mb, 0.47f, 0.485f, 0.037f, 0.0f);
        return;
    }
    // Пламя капли: жёлтый низ у ветоши, выше оранжевый язык, по краям тёмно-красные отрывы.
    // Своя кость CHAR_BONE_FLAME: начало у ветоши, ось +Y всегда вверх
    const u8 F = CHAR_BONE_FLAME;
    static const V2 base[] = { { -0.01f, 0.0f }, { 0.01f, 0.030f }, { 0.04f, 0.042f }, { 0.11f, 0.038f } };
    static const V2 top[] = { { 0.10f, 0.040f }, { 0.18f, 0.030f }, { 0.26f, 0.013f }, { 0.33f, 0.0f } };
    static const V2 lick[] = { { 0.0f, 0.016f }, { 0.07f, 0.011f }, { 0.13f, 0.0f } };
    mb_set(mb, v3(0.52f, 0.28f, 0.06f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0, 0, 0), v3(0, 1, 0), base, 4, 6, 1.0f, 1.0f);
    mb_set(mb, v3(0.40f, 0.13f, 0.03f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0, 0, 0), v3(0.05f, 1, 0.02f), top, 4, 6, 1.0f, 1.0f);
    mb_set(mb, v3(0.24f, 0.05f, 0.01f), MAT_EMISSIVE, F);
    mb_lathe(mb, v3(0.022f, 0.15f, 0.01f), v3(0.022f + 0.25f, 1.15f, 0.11f), lick, 3, 5, 1.0f, 1.0f);
    mb_lathe(mb, v3(-0.02f, 0.13f, -0.015f), v3(-0.02f - 0.2f, 1.13f, -0.215f), lick, 3, 5, 1.0f, 1.0f);
}

V3 torch_flame_pos(const M4 *bones) { return m4_point(bones[CHAR_BONE_FLAME], v3(0.0f, 0.12f, 0.0f)); }

