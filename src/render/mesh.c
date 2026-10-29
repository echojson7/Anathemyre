#include "mesh.h"

#include "../core/log.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(Vertex) == 32, "формат .mesh рассчитан на вершину в 32 байта");

bool mb_init(MeshBuilder *mb, Arena *arena, u32 vcap, u32 icap)
{
    mb->v = ARENA_PUSH_ARRAY(arena, Vertex, vcap);
    mb->idx = ARENA_PUSH_ARRAY(arena, u32, icap);
    mb->skin = NULL;
    mb->morph = NULL;
    mb->morph_count = 0;
    mb->vcount = mb->icount = 0;
    mb->vcap = vcap;
    mb->icap = icap;
    mb->overflow = false;
    mb->color = v3(1, 1, 1);
    mb->mat = MAT_CONCRETE;
    mb->bone = BONE_NONE;
    return mb->v && mb->idx;
}

bool mb_load(MeshBuilder *mb, Arena *arena, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("СЕТКА: нет файла %s", path);
        return false;
    }
    char magic[4];
    u32 head[3];
    bool ok = fread(magic, 4, 1, f) == 1 && fread(head, sizeof head, 1, f) == 1 && !memcmp(magic, "AMSH", 4) &&
              head[0] >= 1 && head[0] <= 3 && mb_init(mb, arena, head[1], head[2]) &&
              fread(mb->v, sizeof(Vertex), head[1], f) == head[1] && fread(mb->idx, sizeof(u32), head[2], f) == head[2];
    if (ok && head[0] >= 2) {
        mb->skin = ARENA_PUSH_ARRAY(arena, VertexSkin, head[1]);
        ok = mb->skin && fread(mb->skin, sizeof(VertexSkin), head[1], f) == head[1];
    }
    if (ok && head[0] >= 3) {
        u32 k;
        ok = fread(&k, sizeof k, 1, f) == 1 && k <= MESH_MAX_MORPHS;
        size_t n = (size_t)head[1] * k * 3;
        if (ok && k) {
            mb->morph = ARENA_PUSH_ARRAY(arena, u32, n);
            ok = mb->morph && fread(mb->morph, sizeof(u32), n, f) == n;
        }
        mb->morph_count = ok ? k : 0;
    }
    fclose(f);
    if (!ok) {
        log_error("СЕТКА: файл %s повреждён", path);
        return false;
    }
    mb->vcount = head[1];
    mb->icount = head[2];
    for (u32 i = 0; i < mb->icount; i++)
        if (mb->idx[i] >= mb->vcount) {
            log_error("СЕТКА: индекс вне сетки в %s", path);
            return false;
        }
    return true;
}

void mb_set(MeshBuilder *mb, V3 color, Material mat, u8 bone)
{
    mb->color = color;
    mb->mat = (u8)mat;
    mb->bone = bone;
}

static bool reserve(MeshBuilder *mb, u32 nv, u32 ni)
{
    if (mb->vcount + nv > mb->vcap || mb->icount + ni > mb->icap) {
        if (!mb->overflow) log_error("ГЕОМЕТРИЯ: буфер переполнен");
        mb->overflow = true;
        return false;
    }
    return true;
}

static u8 to_u8(f32 x)
{
    return (u8)(clampf(x, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static u32 put(MeshBuilder *mb, V3 p, V3 n)
{
    Vertex *v = &mb->v[mb->vcount];
    v->pos[0] = p.x; v->pos[1] = p.y; v->pos[2] = p.z;
    v->nrm[0] = n.x; v->nrm[1] = n.y; v->nrm[2] = n.z;
    v->color[0] = to_u8(mb->color.x);
    v->color[1] = to_u8(mb->color.y);
    v->color[2] = to_u8(mb->color.z);
    v->color[3] = 255;
    v->mat = mb->mat;
    v->bone = mb->bone;
    v->tint = v->pad1 = 0;
    return mb->vcount++;
}

static void tri(MeshBuilder *mb, u32 a, u32 b, u32 c)
{
    mb->idx[mb->icount++] = a;
    mb->idx[mb->icount++] = b;
    mb->idx[mb->icount++] = c;
}

// Плоский четырёхугольник; сам разворачивает обход наружу относительно center
static void flat_face(MeshBuilder *mb, V3 p0, V3 p1, V3 p2, V3 p3, V3 center)
{
    V3 n = v3_norm(v3_cross(v3_sub(p1, p0), v3_sub(p2, p0)));
    V3 fc = v3_mul(v3_add(v3_add(p0, p1), v3_add(p2, p3)), 0.25f);
    bool flip = v3_dot(n, v3_sub(fc, center)) < 0.0f;
    if (flip) n = v3_mul(n, -1.0f);
    u32 b = mb->vcount;
    put(mb, p0, n); put(mb, p1, n); put(mb, p2, n); put(mb, p3, n);
    if (flip) {
        tri(mb, b, b + 2, b + 1);
        tri(mb, b, b + 3, b + 2);
    } else {
        tri(mb, b, b + 1, b + 2);
        tri(mb, b, b + 2, b + 3);
    }
}

void mb_box(MeshBuilder *mb, M4 xf)
{
    if (!reserve(mb, 24, 36)) return;
    V3 c[8];
    for (int i = 0; i < 8; i++) {
        V3 l = v3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
        c[i] = m4_point(xf, l);
    }
    V3 center = m4_point(xf, v3(0, 0, 0));
    static const int faces[6][4] = {
        { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, // +x -x
        { 2, 6, 7, 3 }, { 0, 1, 5, 4 }, // +y -y
        { 4, 5, 7, 6 }, { 0, 2, 3, 1 }, // +z -z
    };
    for (int f = 0; f < 6; f++)
        flat_face(mb, c[faces[f][0]], c[faces[f][1]], c[faces[f][2]], c[faces[f][3]], center);
}

static void axis_frame(V3 axis, V3 *side, V3 *fwd)
{
    V3 ref = fabsf(axis.z) < 0.9f ? v3(0, 0, 1) : v3(1, 0, 0);
    *side = v3_norm(v3_cross(axis, ref));
    *fwd = v3_cross(*side, axis);
}

void mb_lathe(MeshBuilder *mb, V3 a, V3 b, const V2 *profile, u32 n, u32 segs, f32 ex, f32 ez)
{
    if (n < 2 || segs < 3) return;
    if (!reserve(mb, n * segs, (n - 1) * segs * 6)) return;
    V3 axis = v3_norm(v3_sub(b, a));
    V3 side, fwd;
    axis_frame(axis, &side, &fwd);

    u32 base = mb->vcount;
    for (u32 i = 0; i < n; i++) {
        V3 c = v3_add(a, v3_mul(axis, profile[i].x));
        for (u32 j = 0; j < segs; j++) {
            f32 t = 2.0f * PI_F * (f32)j / (f32)segs;
            V3 off = v3_add(v3_mul(side, cosf(t) * profile[i].y * ex), v3_mul(fwd, sinf(t) * profile[i].y * ez));
            put(mb, v3_add(c, off), v3(0, 0, 0));
        }
    }
    u32 ibase = mb->icount;
    for (u32 i = 0; i + 1 < n; i++)
        for (u32 j = 0; j < segs; j++) {
            u32 j1 = (j + 1) % segs;
            u32 p00 = base + i * segs + j, p01 = base + i * segs + j1;
            u32 p10 = base + (i + 1) * segs + j, p11 = base + (i + 1) * segs + j1;
            tri(mb, p00, p10, p01);
            tri(mb, p01, p10, p11);
        }
    mb_smooth_normals(mb, base, ibase);
}

void mb_smooth_normals(MeshBuilder *mb, u32 vfirst, u32 ifirst)
{
    for (u32 k = vfirst; k < mb->vcount; k++) {
        Vertex *v = &mb->v[k];
        v->nrm[0] = v->nrm[1] = v->nrm[2] = 0.0f;
    }
    // Гладкие нормали внутри детали: сумма нормалей граней
    for (u32 k = ifirst; k < mb->icount; k += 3) {
        Vertex *v0 = &mb->v[mb->idx[k]], *v1 = &mb->v[mb->idx[k + 1]], *v2 = &mb->v[mb->idx[k + 2]];
        V3 p0 = v3(v0->pos[0], v0->pos[1], v0->pos[2]);
        V3 p1 = v3(v1->pos[0], v1->pos[1], v1->pos[2]);
        V3 p2 = v3(v2->pos[0], v2->pos[1], v2->pos[2]);
        V3 fn = v3_cross(v3_sub(p1, p0), v3_sub(p2, p0));
        Vertex *vs[3] = { v0, v1, v2 };
        for (int q = 0; q < 3; q++) {
            vs[q]->nrm[0] += fn.x; vs[q]->nrm[1] += fn.y; vs[q]->nrm[2] += fn.z;
        }
    }
    for (u32 k = vfirst; k < mb->vcount; k++) {
        Vertex *v = &mb->v[k];
        V3 nn = v3_norm(v3(v->nrm[0], v->nrm[1], v->nrm[2]));
        if (v3_len(nn) < 0.5f) nn = v3(0, 1, 0);
        v->nrm[0] = nn.x; v->nrm[1] = nn.y; v->nrm[2] = nn.z;
    }
}

void mb_capsule(MeshBuilder *mb, V3 a, V3 b, f32 r0, f32 r1, u32 segs, f32 ex, f32 ez)
{
    // Колец на полусферу: четверть сегментов по кругу (16 -> 4), не меньше одного
    enum { CAP_MAX = 8 };
    u32 CAP = segs / 4 < 1 ? 1 : (segs / 4 > CAP_MAX ? CAP_MAX : segs / 4);
    V2 prof[2 * (CAP_MAX + 1)];
    f32 len = v3_len(v3_sub(b, a));
    u32 n = 0;
    for (u32 i = 0; i <= CAP; i++) {
        f32 phi = -0.5f * PI_F + 0.5f * PI_F * (f32)i / CAP;
        prof[n].x = r0 * sinf(phi);
        prof[n].y = r0 * cosf(phi);
        n++;
    }
    for (u32 i = 0; i <= CAP; i++) {
        f32 phi = 0.5f * PI_F * (f32)i / CAP;
        prof[n].x = len + r1 * sinf(phi);
        prof[n].y = r1 * cosf(phi);
        n++;
    }
    mb_lathe(mb, a, b, prof, n, segs, ex, ez);
}

void mb_cylinder(MeshBuilder *mb, V3 a, V3 b, f32 r, u32 segs)
{
    if (!reserve(mb, segs * 4 + 2, segs * 12)) return;
    V3 axis = v3_norm(v3_sub(b, a));
    V3 side, fwd;
    axis_frame(axis, &side, &fwd);
    u32 base = mb->vcount;
    for (u32 j = 0; j < segs; j++) {
        f32 t = 2.0f * PI_F * (f32)j / (f32)segs;
        V3 d = v3_add(v3_mul(side, cosf(t)), v3_mul(fwd, sinf(t)));
        put(mb, v3_add(a, v3_mul(d, r)), d);
        put(mb, v3_add(b, v3_mul(d, r)), d);
    }
    for (u32 j = 0; j < segs; j++) {
        u32 j1 = (j + 1) % segs;
        u32 a0 = base + j * 2, b0 = a0 + 1, a1 = base + j1 * 2, b1 = a1 + 1;
        tri(mb, a0, b0, a1);
        tri(mb, a1, b0, b1);
    }
    // Торцы
    V3 na = v3_mul(axis, -1.0f);
    u32 ca = put(mb, a, na);
    u32 cb = put(mb, b, axis);
    u32 rb = mb->vcount;
    for (u32 j = 0; j < segs; j++) {
        f32 t = 2.0f * PI_F * (f32)j / (f32)segs;
        V3 d = v3_add(v3_mul(side, cosf(t)), v3_mul(fwd, sinf(t)));
        put(mb, v3_add(a, v3_mul(d, r)), na);
        put(mb, v3_add(b, v3_mul(d, r)), axis);
    }
    for (u32 j = 0; j < segs; j++) {
        u32 j1 = (j + 1) % segs;
        tri(mb, ca, rb + j * 2, rb + j1 * 2);
        tri(mb, cb, rb + j1 * 2 + 1, rb + j * 2 + 1);
    }
}

void mb_quad(MeshBuilder *mb, V3 c, V3 right, V3 up)
{
    if (!reserve(mb, 4, 6)) return;
    V3 n = v3_norm(v3_cross(right, up));
    u32 b = mb->vcount;
    put(mb, v3_sub(v3_sub(c, right), up), n);
    put(mb, v3_sub(v3_add(c, right), up), n);
    put(mb, v3_add(v3_add(c, right), up), n);
    put(mb, v3_add(v3_sub(c, right), up), n);
    tri(mb, b, b + 1, b + 2);
    tri(mb, b, b + 2, b + 3);
}

void mb_terrain(MeshBuilder *mb, f32 half_size, u32 cells, f32 (*height)(f32 x, f32 z, void *u), void *user)
{
    u32 n = cells + 1;
    if (!reserve(mb, n * n, cells * cells * 6)) return;
    u32 base = mb->vcount;
    for (u32 i = 0; i < n; i++)
        for (u32 j = 0; j < n; j++) {
            // Квадратичная развёртка: мелкая сетка в центре, крупная к горизонту
            f32 u = (f32)j / (f32)cells * 2.0f - 1.0f;
            f32 w = (f32)i / (f32)cells * 2.0f - 1.0f;
            f32 x = u * fabsf(u) * half_size, z = w * fabsf(w) * half_size;
            f32 e = 0.5f + 0.02f * sqrtf(x * x + z * z);
            f32 h = height(x, z, user);
            f32 dx = height(x + e, z, user) - height(x - e, z, user);
            f32 dz = height(x, z + e, user) - height(x, z - e, user);
            put(mb, v3(x, h, z), v3_norm(v3(-dx, 2.0f * e, -dz)));
        }
    for (u32 i = 0; i < cells; i++)
        for (u32 j = 0; j < cells; j++) {
            u32 p00 = base + i * n + j, p01 = p00 + 1, p10 = p00 + n, p11 = p10 + 1;
            tri(mb, p00, p10, p11);
            tri(mb, p00, p11, p01);
        }
}
