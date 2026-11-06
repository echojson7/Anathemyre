// Мир из описаний и ассеты кадра: сцена (её читает шаг мира, хеш — в запись повтора), материалы и сетки
// (только кадр). Всё читается при старте из <папка игры>/data/ — туда их кладёт make
#include "app.h"
#include "../asset/material.h"
#include "../asset/mesh.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../core/text.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/scene.h"
#include "../view/view.h"
#include <stdio.h>

static bool read_data(const char *rel, Arena &a, u8 **data, usize *size) {
    char path[1024];
    snprintf(path, sizeof path, "%sdata/%s", platform_exe_dir(), rel);
    if (platform_read_file(path, a, data, size)) return true;
    log_error("нет файла %s (собери: make)", path);
    return false;
}

bool app_world_create(World &w, AppMemory &m, u64 seed, u64 *desc_hash) {
    world_init(w, m.world, seed);
    usize mark = arena_mark(m.load);
    u8 *text = nullptr;
    usize size = 0;
    bool ok = read_data("scene.txt", m.load, &text, &size);
    if (ok) {
        char err[256];
        ok = scene_load(w, text, size, err, sizeof err);
        if (!ok) log_error("data/scene.txt: %s", err);
        // Хеш описаний, которые читает шаг мира (пока одна сцена)
        if (desc_hash) *desc_hash = hash_combine(0x5CE7Eull, text_hash(text, size));
    }
    arena_restore(m.load, mark);
    return ok;
}

struct MeshLoad {
    AppMemory *m;
    const MaterialTable *mats;
    ViewAssets *view;
    u32 failed;
};

static void load_mesh(const char *name, void *user) {
    MeshLoad &L = *(MeshLoad *)user;
    char rel[512];
    snprintf(rel, sizeof rel, "meshes/%s", name);
    usize mark = arena_mark(L.m->load);
    u8 *data = nullptr;
    usize size = 0;
    MeshData md;
    const char *err = "";
    if (!read_data(rel, L.m->load, &data, &size) || !mesh_parse(data, size, L.m->load, md, &err)) {
        log_error("сетка %s: %s", name, err);
        L.failed++;
        arena_restore(L.m->load, mark);
        return;
    }
    // Материалы вершин по имени → номера таблицы; неизвестный — missing (виден сразу)
    u16 global[MESH_MAX_MATERIALS];
    for (usize i = 0; i < md.material_names.count; ++i) {
        u32 k = material_find(*L.mats, str_hash(md.material_names.data[i]));
        if (!k) log_warn("сетка %s: нет материала %.*s", name, (int)md.material_names.data[i].n, md.material_names.data[i].p);
        global[i] = (u16)k;
    }
    mesh_remap_materials(md, global);
    u32 id = render_mesh_create(md.vertices, md.indices);
    arena_restore(L.m->load, mark);
    if (!id) { L.failed++; return; }
    if (L.view->mesh_count >= VIEW_MAX_MESHES) { log_error("сеток больше %u", VIEW_MAX_MESHES); L.failed++; return; }
    // Имя сетки — имя файла без .mesh
    usize n = strlen(name) - 5;
    L.view->meshes[L.view->mesh_count++] = {hash_bytes(name, n), id};
}

bool app_assets_load(AppMemory &m, MaterialTable &mats, ViewAssets &view) {
    u8 *kinds = nullptr, *text = nullptr;
    usize kinds_size = 0, size = 0;
    if (!read_data("material_kinds.txt", m.world, &kinds, &kinds_size) || !read_data("materials.txt", m.world, &text, &size))
        return false;
    char err[256];
    if (!material_table_parse(kinds, kinds_size, text, size, m.world, mats, err, sizeof err)) {
        log_error("data/materials.txt: %s", err);
        return false;
    }
    usize mark = arena_mark(m.load);
    RenderMaterial *rm = arena_push<RenderMaterial>(m.load, mats.count);
    for (u32 i = 0; i < mats.count; ++i) {
        rm[i].kind = mats.items[i].kind;
        memcpy(rm[i].p, mats.items[i].p, sizeof rm[i].p);
    }
    bool ok = render_set_materials({rm, mats.count});
    arena_restore(m.load, mark);
    if (!ok) return false;
    view.materials = &mats;
    view.ground_material = material_find(mats, hash_str("ground"));
    if (!view.ground_material) log_warn("data/materials.txt: нет материала ground для земли");

    char dir[1024];
    snprintf(dir, sizeof dir, "%sdata/meshes", platform_exe_dir());
    MeshLoad L{&m, &mats, &view, 0};
    if (!platform_list_dir(dir, ".mesh", load_mesh, &L)) {
        log_error("нет папки %s (собери: make)", dir);
        return false;
    }
    log_info("ассеты: материалов %u (видов %u), сеток %u", mats.count, mats.kind_count, view.mesh_count);
    return L.failed == 0;
}
