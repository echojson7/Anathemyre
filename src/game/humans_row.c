#include "humans_row.h"

#include <stdio.h>

#include "../core/log.h"
#include "camera.h"

void humans_row_init(HumansRow *h, Arena *perm, GroundFn ground)
{
    *h = (HumansRow){ 0 };
    h->bones = ARENA_PUSH_ARRAY(perm, M4, HUMANS_N * RENDER_MAX_BONES);
    h->looks = ARENA_PUSH_ARRAY(perm, M4, HUMANS_LOOKS * RENDER_MAX_BONES);
    for (u32 j = 0; j < HUMANS_LOOKS; j++) {
        f32 x = -1.0f - 0.75f * (f32)j;
        for (u32 b = 0; b < RENDER_MAX_BONES; b++) h->looks[j * RENDER_MAX_BONES + b] = m4_translate(v3(x, ground(x, 22.0f), 22.0f));
        CharLook lk;
        f32 r = (f32)((j * 37u) % 11u) / 10.0f;
        look_make(&lk, j % BODY_KIND_COUNT, 300u + j, 58.0f + 34.0f * r, 1.8f, 10.0f + 60.0f * (1.0f - r));
        h->looks[j * RENDER_MAX_BONES + RENDER_LOOK_SLOT] = look_matrix(&lk, j % BODY_KIND_COUNT);
    }
}

void humans_row_toggle(HumansRow *h, MeshBuilder *mb, Arena *perm, const char *data_dir, GroundFn ground)
{
    h->on = !h->on;
    camera_close(h->on);
    size_t mark = perm->used;
    bool build = h->on && h->count == 0;
    for (u32 k = 0; build && k < HUMANS_N; k++) {
        char path[700], suf[16] = "";
        u32 lod = k % CHAR_LODS, body = k / CHAR_LODS;
        if (lod) snprintf(suf, sizeof suf, "_lod%u", lod);
        snprintf(path, sizeof path, "%s/meshes/%s%s.mesh", data_dir, g_body_file[body], suf);
        if (!mb_load(mb, perm, path)) continue;
        gpu_mesh_upload(&h->mesh[h->count], mb);
        log_info("ЧЕЛОВЕК %u: %u вершин, %u треугольников", k, mb->vcount, mb->icount / 3);
        perm->used = mark;
        snprintf(path, sizeof path, "%s/meshes/hair/%s_%s%s.mesh", data_dir, g_body_file[body], g_hair_file[body][0], suf);
        h->has_hair[h->count] = body != BODY_THIN && mb_load(mb, perm, path);
        if (h->has_hair[h->count]) gpu_mesh_upload(&h->hair[h->count], mb);
        perm->used = mark;
        f32 x = 0.3f + (0.7f * CHAR_LODS + 0.3f) * (f32)body + 0.7f * (f32)lod;
        M4 *b = &h->bones[h->count * RENDER_MAX_BONES];
        for (u32 j = 0; j < RENDER_MAX_BONES; j++) b[j] = m4_translate(v3(x, ground(x, 22.0f), 22.0f));
        b[RENDER_LOOK_SLOT] = (M4){ 0 };
        h->count++;
    }
}

u32 humans_row_draw(const HumansRow *h, const CharMeshes *m, DrawItem *items)
{
    u32 n = 0;
    if (!h->on) return 0;
    for (u32 k = 0; k < h->count; k++) {
        items[n++] = (DrawItem){ .mesh = &h->mesh[k], .bones = h->bones + k * RENDER_MAX_BONES };
        if (h->has_hair[k]) items[n++] = (DrawItem){ .mesh = &h->hair[k], .bones = h->bones + k * RENDER_MAX_BONES };
    }
    for (u32 j = 0; j < HUMANS_LOOKS; j++) {
        // Каждый в одной вещи по порядку GEAR_*, последний — во всём сразу
        static const u8 kit[] = { GEAR_QUILT, GEAR_PLATE, GEAR_BUCKET, GEAR_GREAVES, GEAR_BOOTS, GEAR_MITTS, GEAR_PACK, GEAR_BELT };
        u8 one = (u8)j;
        const M4 *lb = h->looks + j * RENDER_MAX_BONES;
        const GpuMesh *row = m->gear + (j % BODY_KIND_COUNT) * GEAR_ROW;
        items[n++] = (DrawItem){ .mesh = &m->body[j % BODY_KIND_COUNT], .bones = lb };
        // И причёски по кругу, кроме худого (он бритый) и тех, кто в кожухе
        u8 cut = (u8)(GEAR_COUNT + (j / BODY_KIND_COUNT) % (j % BODY_KIND_COUNT == BODY_FEMALE ? 4u : 3u));
        if (j % BODY_KIND_COUNT != BODY_THIN && j != GEAR_BUCKET && j < GEAR_COUNT) items[n++] = (DrawItem){ .mesh = &row[cut], .bones = lb };
        if (j < GEAR_COUNT) items[n++] = (DrawItem){ .mesh = &row[one], .bones = lb };
        else
            for (u32 g = 0; g < ARRAY_COUNT(kit); g++) items[n++] = (DrawItem){ .mesh = &row[kit[g]], .bones = lb };
    }
    return n;
}
