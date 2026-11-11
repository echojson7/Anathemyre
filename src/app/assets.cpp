// Мир из описаний и ассеты кадра: сцена (её читает шаг мира, хеш — в запись повтора), материалы и сетки
// (только кадр). Всё читается при старте из <папка игры>/data/ — туда их кладёт make
#include "app.h"
#include "../asset/material.h"
#include "../anim/clip.h"
#include "../anim/track.h"
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


// Коллайдеры построек для шага мира: коробки COLL сеток, которые стоят в сцене, — в оси мира (docs/people.md).
// Папка читается в любом порядке, коробки складываются в порядке деталей сцены
constexpr u32 STATIC_MAX_MESHES = 128;
struct CollLoad {
    AppMemory *m;
    u64 want[STATIC_MAX_MESHES];
    Span<MeshBox> boxes[STATIC_MAX_MESHES];
    u32 count;
    u32 failed;
};
static void load_coll(const char *name, void *user) {
    CollLoad &L = *(CollLoad *)user;
    u64 h = hash_bytes(name, strlen(name) - 5);
    u32 k = 0;
    while (k < L.count && L.want[k] != h) ++k;
    if (k == L.count) return;
    char rel[512];
    snprintf(rel, sizeof rel, "meshes/%s", name);
    // Разбор целиком — в арену загрузки; коробки копируются в её начало, остальное откатывается
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
    usize n = md.collider.count;
    MeshBox tmp[256];
    if (n > 256) { log_error("сетка %s: коробок коллайдера больше 256", name); L.failed++; n = 0; }
    memcpy(tmp, md.collider.data, n * sizeof(MeshBox));
    arena_restore(L.m->load, mark);
    MeshBox *keep = arena_push<MeshBox>(L.m->load, n + 1);
    memcpy(keep, tmp, n * sizeof(MeshBox));
    L.boxes[k] = {keep, n};
}

// Длина цикла шага клипа (data/anims/<клип>.game_engine.trk): сдвиг корня за петлю и её время
static bool gait_clip(AppMemory &m, const char *clip, f32 *v, f32 *len, u64 *hash) {
    char rel[128];
    snprintf(rel, sizeof rel, "anims/%s.game_engine.trk", clip);
    u8 *data = nullptr;
    usize size = 0;
    Track t;
    const char *err = "";
    if (!read_data(rel, m.load, &data, &size) || !track_parse(data, size, m.load, t, &err) || t.ticks < 2) {
        log_error("дорожки %s: %s", rel, err);
        return false;
    }
    *hash = hash_combine(*hash, hash_bytes(data, size));
    const i32 *a = t.root, *b = t.root + (t.ticks - 1) * 3;
    f32 dx = (f32)(b[0] - a[0]) * 0.001f, dz = (f32)(b[1] - a[1]) * 0.001f;
    *len = f_sqrt(dx * dx + dz * dz);
    *v = *len / ((f32)(t.ticks - 1) / (f32)TRACK_TICKS_PER_S);
    return *len > 0.1f;
}

// Дорожки клипа на game_engine (data/anims/<клип>.game_engine.trk) — в арене мира, байты — в хеш
static const Track *world_track(AppMemory &m, const char *clip, u64 *hash) {
    char rel[128];
    snprintf(rel, sizeof rel, "anims/%s.game_engine.trk", clip);
    u8 *data = nullptr;
    usize size = 0;
    const char *err = "";
    Track *t = arena_push<Track>(m.world, 1);
    if (!read_data(rel, m.load, &data, &size) || !track_parse(data, size, m.world, *t, &err)) {
        log_error("дорожки %s: %s", rel, err);
        return nullptr;
    }
    *hash = hash_combine(*hash, hash_bytes(data, size));
    return t;
}

struct ActionNames {
    char name[ACTIONS_MAX][32];
    u32 count;
    bool over;
};
static void list_action(const char *name, void *user) {
    ActionNames &L = *(ActionNames *)user;
    usize n = strlen(name) - 4;
    if (L.count >= ACTIONS_MAX || n >= 32) { L.over = true; return; }
    memcpy(L.name[L.count], name, n);
    L.name[L.count++][n] = 0;
}

bool app_actions_load(World &w, AppMemory &m) {
    ActionTable T{};
    u64 h = 0xAC7104ull;
    T.walk = world_track(m, "walk", &h);
    T.run = world_track(m, "run", &h);
    if (!T.walk || !T.run) return false;
    ActionNames names{};
    char dir[1024];
    snprintf(dir, sizeof dir, "%sdata/actions", platform_exe_dir());
    if (!platform_list_dir(dir, ".txt", list_action, &names)) log_warn("нет папки %s: действий нет", dir);
    if (names.over) { log_error("data/actions: действий больше %u", ACTIONS_MAX); return false; }
    // Порядок — по имени: номер действия в таблице людей не зависит от порядка файлов в папке
    for (u32 i = 1; i < names.count; ++i)
        for (u32 j = i; j > 0 && strcmp(names.name[j - 1], names.name[j]) > 0; --j) {
            char tmp[32];
            memcpy(tmp, names.name[j], 32);
            memcpy(names.name[j], names.name[j - 1], 32);
            memcpy(names.name[j - 1], tmp, 32);
        }
    usize mark = arena_mark(m.load);
    bool ok = true;
    for (u32 i = 0; i < names.count && ok; ++i) {
        ActionDesc &A = T.a[T.count];
        A = ActionDesc{};
        char rel[128], err[256];
        u8 *data = nullptr;
        usize size = 0;
        snprintf(rel, sizeof rel, "actions/%s.txt", names.name[i]);
        ok = read_data(rel, m.load, &data, &size);
        if (ok && !(ok = action_parse(data, size, A, err, sizeof err))) log_error("data/%s: %s", rel, err);
        if (!ok) break;
        A.name = hash_str(names.name[i]);
        snprintf(A.label, sizeof A.label, "%s", names.name[i]);
        A.track = world_track(m, A.clip_name, &h);
        // Окно удара и отмена — метки клипа (ключи 30/с → тики 60/с)
        AnimLabels lb{};
        snprintf(rel, sizeof rel, "anims/%s.txt", A.clip_name);
        ok = A.track && A.track->weapon && read_data(rel, m.load, &data, &size) && anim_labels_parse(data, size, lb, err, sizeof err) &&
             lb.hit_count > 0;
        if (!ok) {
            log_error("действие %s: у клипа %s нет дорожек с оружием или метки hit (data/%s)", names.name[i], A.clip_name, rel);
            break;
        }
        A.hit_a = (u16)(lb.hit[0].a * 2);
        A.hit_b = (u16)(lb.hit[0].b * 2);
        A.cancel = lb.cancel >= 0 ? (u16)(lb.cancel * 2) : 0xFFFF;
        if (A.end > A.track->ticks - 1) A.end = (u16)(A.track->ticks - 1);
        A.aim = action_aim(A);
        h = hash_combine(h, action_hash(A));
        if (A.name == hash_str("cut")) T.strike = T.count;
        T.count++;
    }
    arena_restore(m.load, mark);
    if (!ok) return false;
    T.hash = h;
    w.actions = T;
    log_info("действия: %u (удар по приказу — %s, клинок идёт на %.1f° от начального курса)", T.count,
             T.count ? T.a[T.strike].label : "нет", T.count ? (f64)(i32)T.a[T.strike].aim * (360.0 / 4294967296.0) : 0.0);
    return true;
}

static bool world_static_load(World &w, AppMemory &m) {
    usize mark = arena_mark(m.load);
    CollLoad L{};
    L.m = &m;
    const Props &p = w.props;
    for (u32 i = 0; i < p.count; ++i) {
        u32 k = 0;
        while (k < L.count && L.want[k] != p.mesh[i]) ++k;
        if (k == L.count) {
            if (L.count >= STATIC_MAX_MESHES) { log_error("сеток в сцене больше %u", STATIC_MAX_MESHES); return false; }
            L.want[L.count++] = p.mesh[i];
        }
    }
    char dir[1024];
    snprintf(dir, sizeof dir, "%sdata/meshes", platform_exe_dir());
    if (!platform_list_dir(dir, ".mesh", load_coll, &L) || L.failed) {
        log_error("коллайдеры: нет папки %s или сетка не читается", dir);
        arena_restore(m.load, mark);
        return false;
    }
    CollBoxIn *boxes = arena_push<CollBoxIn>(m.load, COLL_MAX_BOXES);
    u32 n = 0;
    for (u32 i = 0; i < p.count; ++i) {
        u32 k = 0;
        while (L.want[k] != p.mesh[i]) ++k;
        f32 sy, cy;
        sincos_turn(p.yaw[i], &sy, &cy);
        f32 sc = p.scale[i];
        for (const MeshBox &b : L.boxes[k]) {
            if (n >= COLL_MAX_BOXES) { log_error("коробок коллайдеров больше %u", COLL_MAX_BOXES); arena_restore(m.load, mark); return false; }
            // Наклонная коробка — её объём в осях детали; дальше поворот детали вокруг вертикали
            f32 x = b.qx, y = b.qy, z = b.qz, ww = b.qw;
            f32 r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - ww * z), 2 * (x * z + ww * y)},
                           {2 * (x * y + ww * z), 1 - 2 * (x * x + z * z), 2 * (y * z - ww * x)},
                           {2 * (x * z - ww * y), 2 * (y * z + ww * x), 1 - 2 * (x * x + y * y)}};
            f32 h[3] = {b.hx, b.hy, b.hz}, e[3];
            for (u32 a = 0; a < 3; ++a) e[a] = (f_abs(r[a][0]) * h[0] + f_abs(r[a][1]) * h[1] + f_abs(r[a][2]) * h[2]) * sc;
            f32 lx = b.cx * sc, lz = b.cz * sc;
            CollBoxIn &o = boxes[n++];
            o.at = {p.cx[i], p.cz[i], p.x[i] + lx * cy + lz * sy, p.y[i] + b.cy * sc, p.z[i] - lx * sy + lz * cy};
            o.hx = e[0];
            o.hy = e[1];
            o.hz = e[2];
            o.yaw = p.yaw[i];
        }
    }
    GaitTable gait;
    u64 gh = 0x6A17ull;
    if (!gait_clip(m, "walk", &gait.walk_v, &gait.walk_len, &gh) || !gait_clip(m, "run", &gait.run_v, &gait.run_len, &gh)) {
        arena_restore(m.load, mark);
        return false;
    }
    gait.hash = gh;
    if (!app_actions_load(w, m)) {
        arena_restore(m.load, mark);
        return false;
    }
    u64 t0 = platform_time_ns();
    bool ok = world_build_static(w, boxes, n, gait, m.world, m.load);
    if (!ok) log_error("коллайдеры: коробок или чанков с ними больше предела");
    else {
        char h1[17], h2[17];
        log_info("коллайдеры: %u коробок, %u чанков; сетка ходимости: %u областей, %u уровней, %.0f мс, хеш %s; шаг: "
                 "цикл %.2f м при %.2f м/с, бег %.2f м при %.2f м/с; хеш неизменного %s",
                 n, w.coll.tiles, w.walk.count, w.walk.levels_total, (f64)(platform_time_ns() - t0) / 1e6,
                 hex_u64(w.walk.hash, h1), gait.walk_len, gait.walk_v, gait.run_len, gait.run_v,
                 hex_u64(world_static_hash(w), h2));
    }
    arena_restore(m.load, mark);
    return ok;
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
        if (ok) ok = world_static_load(w, m);
        // Хеш описаний, которые читает шаг мира: сцена с заготовками, описание земли и сама сетка (её строит
        // код генератора)
        if (desc_hash)
            *desc_hash = hash_combine(hash_combine(hash_combine(hash_combine(0x5CE7Eull, text_hash(text, size)), pl.hash),
                                                   terrain_text),
                                      hash_combine(w.terrain.hash, world_static_hash(w)));
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

// Тело человека: на каждый скелет из SKIN — файл data/people/<сетка>.<скелет>.skel (кости по имени, в том же
// порядке, что в сетке) и источник скиннинга. Скелеты — в арене мира (живут весь запуск)
static bool load_body(MeshLoad &L, const char *file, const MeshData &md, u32 mesh) {
    if (L.view->body_count >= VIEW_MAX_BODIES) { log_error("тел людей больше %u", VIEW_MAX_BODIES); return false; }
    ViewBody &b = L.view->bodies[L.view->body_count];
    b = ViewBody{};
    usize nlen = strlen(file) - 5;
    b.name = hash_bytes(file, nlen);
    b.mesh = mesh;
    for (u32 k = 0; k < md.skin_count; ++k) {
        const MeshSkin &sk = md.skins[k];
        char rel[512];
        snprintf(rel, sizeof rel, "people/%.*s.%.*s.skel", (int)nlen, file, (int)sk.skeleton.n, sk.skeleton.p);
        u8 *data = nullptr;
        usize size = 0;
        const char *err = "";
        Skel &s = b.skel[k];
        if (!read_data(rel, L.m->load, &data, &size) || !skel_parse(data, size, L.m->world, s, &err)) {
            log_error("скелет %s: %s", rel, err);
            return false;
        }
        if (s.bones.count != sk.bones.count) { log_error("скелет %s: костей %zu, в сетке %zu", rel, s.bones.count, sk.bones.count); return false; }
        for (usize i = 0; i < s.bones.count; ++i)
            if (s.bones.data[i].hash != str_hash(sk.bones.data[i])) { log_error("скелет %s: кость %zu не та, что в сетке", rel, i); return false; }
        RenderSkinDesc d{};
        d.mesh = mesh;
        d.bone_count = (u32)s.bones.count;
        d.bones4 = sk.bones4;
        d.weights4 = sk.weights4;
        d.morph_count = md.morphs.count;
        d.morph_step = md.morphs.step;
        d.morphs = md.morphs.data;
        d.lod_vertices = md.lod_vertices;
        b.skin[k] = render_skin_create(d);
        if (!b.skin[k]) return false;
        b.skel_name[k] = str_hash(sk.skeleton);
        b.skin_count++;
    }
    b.morph_count = md.morphs.count;
    for (u32 k = 0; k < md.morphs.count; ++k) b.morph_name[k] = str_hash(md.morphs.names[k]);
    L.view->body_count++;
    log_info("тело %.*s: скелетов %u, морфов %u", (int)nlen, file, b.skin_count, b.morph_count);
    return true;
}

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
    u32 nlod = md.lods.count < RENDER_MESH_MAX_LODS ? (u32)md.lods.count : RENDER_MESH_MAX_LODS;
    for (u32 i = 0; i < nlod; ++i) {
        const MeshLod &l = md.lods.data[i];
        lods[i] = {l.first_index, l.index_count, l.first_cluster, l.cluster_count, l.error};
    }
    RenderCluster *cl = arena_push<RenderCluster>(L.m->load, md.clusters.count);
    for (usize i = 0; i < md.clusters.count; ++i) {
        const MeshCluster &c = md.clusters.data[i];
        cl[i] = {c.first_index, c.tri_count, {c.cx, c.cy, c.cz, c.radius}};
    }
    vm.id = render_mesh_create(md.vertices, md.indices, {lods, nlod}, {cl, md.clusters.count});
    if (vm.id && md.skin_count && !load_body(L, name, md, vm.id)) L.failed++;
    arena_restore(L.m->load, mark);
    if (!vm.id) { L.failed++; return; }
    if (L.view->mesh_count >= VIEW_MAX_MESHES) { log_error("сеток больше %u", VIEW_MAX_MESHES); L.failed++; return; }
    // Имя сетки — имя файла без .mesh
    vm.name = hash_bytes(name, strlen(name) - 5);
    L.view->meshes[L.view->mesh_count++] = vm;
}

// Клип data/anims/<клип>.<скелет>.anim, рядом дорожки .trk того же скелета и метки <клип>.txt (docs/anim.md).
// Всё в арене мира: живёт весь запуск
static void load_clip(const char *name, void *user) {
    MeshLoad &L = *(MeshLoad *)user;
    if (L.view->clip_count >= VIEW_MAX_CLIPS) { log_error("клипов больше %u", VIEW_MAX_CLIPS); L.failed++; return; }
    ViewClip &vc = L.view->clips[L.view->clip_count];
    vc = ViewClip{};
    usize n = strlen(name) - 5, dot = 0;
    while (dot < n && name[dot] != '.') ++dot;
    char rel[512];
    u8 *data = nullptr;
    usize size = 0;
    const char *err = "";
    snprintf(rel, sizeof rel, "anims/%s", name);
    usize mark = arena_mark(L.m->load);
    if (!read_data(rel, L.m->load, &data, &size) || !anim_parse(data, size, L.m->world, vc.clip, &err)) {
        log_error("клип %s: %s", name, err);
        L.failed++;
        arena_restore(L.m->load, mark);
        return;
    }
    snprintf(rel, sizeof rel, "anims/%.*s.trk", (int)n, name);
    if (read_data(rel, L.m->load, &data, &size) && !track_parse(data, size, L.m->world, vc.track, &err)) {
        log_error("дорожки %s: %s", rel, err);
        L.failed++;
    }
    snprintf(rel, sizeof rel, "anims/%.*s.txt", (int)dot, name);
    char why[256];
    if (read_data(rel, L.m->load, &data, &size) && !anim_labels_parse(data, size, vc.labels, why, sizeof why)) {
        log_error("метки %s: %s", rel, why);
        L.failed++;
    }
    arena_restore(L.m->load, mark);
    vc.name = hash_bytes(name, dot);
    snprintf(vc.label, sizeof vc.label, "%.*s", (int)dot, name);
    L.view->clip_count++;
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
        rm[i].metal = mats.items[i].metal;
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
    snprintf(dir, sizeof dir, "%sdata/anims", platform_exe_dir());
    if (!platform_list_dir(dir, ".anim", load_clip, &L)) log_warn("нет папки %s: клипов людей нет", dir);
    log_info("ассеты: материалов %u (видов %u), сеток %u, клипов %u", mats.count, mats.kind_count, view.mesh_count, view.clip_count);
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
