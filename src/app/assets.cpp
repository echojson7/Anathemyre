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
#include "../sim/dune_map.h"
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

// Заготовки сцены (data/prefabs/<имя>.txt): текст в арене загрузки, хеш — в хеш описаний
struct PrefabLoad {
    AppMemory *m;
    u64 hash;
};
static bool prefab_read(void *ctx, Str name, const u8 **text, usize *size) {
    PrefabLoad &L = *(PrefabLoad *)ctx;
    char rel[128];
    if (name.n == 0 || name.n > 64) return false;
    for (usize i = 0; i < name.n; ++i)
        if (name.p[i] == '/' || name.p[i] == '\\' || name.p[i] == '.') return false;  // только имя, без путей
    snprintf(rel, sizeof rel, "prefabs/%.*s.txt", (int)name.n, name.p);
    u8 *data = nullptr;
    if (!read_data(rel, L.m->load, &data, size)) return false;
    *text = data;
    L.hash = hash_combine(L.hash, text_hash(data, *size));
    return true;
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
    // Карты дюн пустынь (data/dunes/*.dun): в хеш земли входят через саму сетку
    for (u32 loc = 0; loc < TERRAIN_MAX_LOCATIONS; ++loc) {
        if (!td.dune_path[loc][0]) continue;
        u8 *dd = nullptr;
        usize ds = 0;
        u32 n = (u32)(td.chunks * 256 >> TERRAIN_FINE_SHIFT) + 1;
        if (!read_data(td.dune_path[loc], m.load, &dd, &ds) || !dune_map_check(dd, ds, n, n)) {
            log_error("data/terrain.txt: карта дюн %s не читается или не %u×%u", td.dune_path[loc], n, n);
            arena_restore(m.load, mark);
            return false;
        }
        td.dune_data[loc] = dd;
        td.dune_size[loc] = ds;
    }
    u64 t0 = platform_time_ns();
    world_init(w, m.world, seed, td, m.load);
    char hex[17];
    log_info("земля: %d×%d чанков, сетка %u×%u, %.0f мс, хеш %s", w.terrain.chunks_x, w.terrain.chunks_z, w.terrain.nx,
             w.terrain.nz, (f64)(platform_time_ns() - t0) / 1e6, hex_u64(w.terrain.hash, hex));
    bool ok = read_data("scene.txt", m.load, &text, &size);
    if (ok) {
        PrefabLoad pl{&m, 0x9EFAB0ull};
        ok = scene_load(w, text, size, prefab_read, &pl, err, sizeof err);
        if (!ok) log_error("data/scene.txt: %s", err);
        // Хеш описаний, которые читает шаг мира: сцена с заготовками, описание земли и сама сетка (её строит
        // код генератора)
        if (desc_hash)
            *desc_hash = hash_combine(hash_combine(hash_combine(hash_combine(0x5CE7Eull, text_hash(text, size)), pl.hash),
                                                   terrain_text),
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
    RenderLod lods[RENDER_MESH_MAX_LODS];
    ViewMesh vm{};
    vm.lod_count = md.lods.count < RENDER_MESH_MAX_LODS ? (u32)md.lods.count : RENDER_MESH_MAX_LODS;
    for (u32 i = 0; i < vm.lod_count; ++i) {
        lods[i] = {md.lods.data[i].first_index, md.lods.data[i].index_count};
        vm.lod_error[i] = md.lods.data[i].error;
    }
    vm.id = render_mesh_create(md.vertices, md.indices, {lods, vm.lod_count});
    arena_restore(L.m->load, mark);
    if (!vm.id) { L.failed++; return; }
    if (L.view->mesh_count >= VIEW_MAX_MESHES) { log_error("сеток больше %u", VIEW_MAX_MESHES); L.failed++; return; }
    // Имя сетки — имя файла без .mesh
    vm.name = hash_bytes(name, strlen(name) - 5);
    L.view->meshes[L.view->mesh_count++] = vm;
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
    // Слои земли по GroundKind (src/sim/ground.h): песок, сухая земля, щебень, камень, трава, вода
    const char *layers[RENDER_GROUND_LAYERS] = {"sand", "dry_earth", "gravel", "rock", "grass", "water",
                                                  "dune_sand", "desert_rock"};
    for (u32 k = 0; k < RENDER_GROUND_LAYERS; ++k) {
        view.ground_layers[k] = material_find(mats, hash_str(layers[k]));
        if (!view.ground_layers[k]) log_warn("data/materials.txt: нет материала %s для слоя земли", layers[k]);
    }

    // Небо (data/sky.txt): только кадр, в хеш описаний не входит
    if (!read_data("sky.txt", m.world, &text, &size)) return false;
    if (!sky_parse(view.sky, text, size, err, sizeof err)) {
        log_error("data/sky.txt: %s", err);
        return false;
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
    rt.dune = t.dune;
    rt.nx = t.nx;
    rt.nz = t.nz;
    rt.mask = t.mask;
    rt.mask2 = t.mask2;
    rt.fine = t.fine;
    rt.fine_count = t.fine_count;
    rt.fine_n = t.fine_n;
    rt.loc_m = t.loc_m;
    for (u32 k = 0; k < 4; ++k) rt.fine_off[k] = t.fine_off[k];
    rt.water = t.water;
    rt.nmx = t.nmx;
    rt.nmz = t.nmz;
    rt.chunk0 = t.chunk0;
    rt.size_x = t.size_x;
    rt.size_z = t.size_z;
    rt.dune_seed = t.dune_seed;
    static_assert(RENDER_TERRAIN_OCTAVES == TERRAIN_DETAIL_OCTAVES, "октавы деталей у кадра и шага мира");
    for (u32 k = 0; k < RENDER_TERRAIN_OCTAVES; ++k) {
        terrain_detail_octave(k, &rt.oct_lp[k], &rt.oct_amp[k]);
        rt.oct_seed[k] = t.oct_seed[k];
    }
    return render_terrain_set(rt);
}
