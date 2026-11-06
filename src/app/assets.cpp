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
#include "../sim/world.h"
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
    usize mark = arena_mark(m.load);
    u8 *text = nullptr;
    usize size = 0;
    char err[256];
    // Земля (data/terrain.txt) — первой: объекты сцены и точки ставятся на неё
    TerrainDesc td;
    if (!read_data("terrain.txt", m.load, &text, &size)) return false;
    if (!terrain_desc_parse(text, size, td, err, sizeof err)) {
        log_error("data/terrain.txt: %s", err);
        arena_restore(m.load, mark);
        return false;
    }
    u64 terrain_text = text_hash(text, size);
    u64 t0 = platform_time_ns();
    world_init(w, m.world, seed, td, m.load);
    char hex[17];
    log_info("земля: %d×%d чанков, сетка %u×%u, %.0f мс, хеш %s", w.terrain.chunks, w.terrain.chunks, w.terrain.n,
             w.terrain.n, (f64)(platform_time_ns() - t0) / 1e6, hex_u64(w.terrain.hash, hex));
    bool ok = read_data("scene.txt", m.load, &text, &size);
    if (ok) {
        ok = scene_load(w, text, size, err, sizeof err);
        if (!ok) log_error("data/scene.txt: %s", err);
        // Хеш описаний, которые читает шаг мира: сцена, описание земли и сама сетка (её строит код генератора)
        if (desc_hash)
            *desc_hash = hash_combine(hash_combine(hash_combine(0x5CE7Eull, text_hash(text, size)), terrain_text),
                                      w.terrain.hash);
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
    // Слои земли по GroundKind (src/sim/ground.h): песок, сухая земля, щебень, камень
    const char *layers[RENDER_GROUND_LAYERS] = {"sand", "dry_earth", "gravel", "rock"};
    for (u32 k = 0; k < RENDER_GROUND_LAYERS; ++k) {
        view.ground_layers[k] = material_find(mats, hash_str(layers[k]));
        if (!view.ground_layers[k]) log_warn("data/materials.txt: нет материала %s для слоя земли", layers[k]);
    }

    char dir[1024];
    snprintf(dir, sizeof dir, "%sdata/meshes", platform_exe_dir());
    MeshLoad L{&m, &mats, &view, 0};
    if (!platform_list_dir(dir, ".mesh", load_mesh, &L)) {
        log_error("нет папки %s (собери: make)", dir);
        return false;
    }
    const char *rocks[SCATTER_MESHES] = {"rock_a", "rock_b", "rock_c"};
    for (u32 k = 0; k < SCATTER_MESHES; ++k) {
        u64 hname = hash_str(rocks[k]);
        for (u32 i = 0; i < view.mesh_count; ++i)
            if (view.meshes[i].name == hname) view.rocks[k] = view.meshes[i].id;
        if (!view.rocks[k]) log_warn("нет сетки %s для рассыпки камней", rocks[k]);
    }
    log_info("ассеты: материалов %u (видов %u), сеток %u", mats.count, mats.kind_count, view.mesh_count);
    return L.failed == 0;
}

bool app_terrain_upload(const World &w) {
    const Terrain &t = w.terrain;
    RenderTerrain rt{};
    rt.height = t.height;
    rt.n = t.n;
    rt.mask = t.mask;
    rt.nm = t.nm;
    rt.chunk0 = t.chunk0;
    rt.size_m = t.size_m;
    static_assert(RENDER_TERRAIN_OCTAVES == TERRAIN_DETAIL_OCTAVES, "октавы деталей у кадра и шага мира");
    for (u32 k = 0; k < RENDER_TERRAIN_OCTAVES; ++k) {
        terrain_detail_octave(k, &rt.oct_lp[k], &rt.oct_amp[k]);
        rt.oct_seed[k] = t.oct_seed[k];
    }
    return render_terrain_set(rt);
}
