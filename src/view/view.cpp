#include "view.h"
#include "../core/hash.h"
#include "../core/math3.h"
#include "../sim/collide.h"
#include "../sim/ground.h"
#include "lamps.h"
#include "people.h"

static constexpr f32 NEAR_M = 0.1f;

// Метры от a до b
static Vec3 delta(const WPos &a, i32 cx, i32 cz, f32 x, f32 y, f32 z) {
    Vec2 d = wpos_delta_xz(a.cx, a.cz, a.x, a.z, cx, cz, x, z);
    return {d.x, y - a.y, d.y};
}

static const ViewMesh *find_mesh(const ViewAssets &a, u64 name) {
    for (u32 i = 0; i < a.mesh_count; ++i)
        if (a.meshes[i].name == name) return &a.meshes[i];
    return nullptr;
}

// Кватернион → два u32 по два snorm16 (как unpackSnorm2x16 в шейдере)
static u32 pack_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

// Декали карты (docs/ostov.md): вид по имени, материал по имени, коробка относительно камеры. Дальше
// DECAL_FAR м не нужны: узор декали мельче пикселя
constexpr f32 DECAL_FAR = 250.0f;
static void build_decals(const Decals &d, const ViewAssets &assets, const WPos &eye, Arena &frame, RenderFrame &out) {
    static const char *const names[DECAL_KINDS] = {"streak", "grime", "dust", "stain"};
    u64 kinds[DECAL_KINDS];
    for (u32 k = 0; k < DECAL_KINDS; ++k) kinds[k] = hash_bytes(names[k], strlen(names[k]));
    RenderDecal *dst = arena_push<RenderDecal>(frame, d.count);
    u32 n = 0;
    for (u32 i = 0; i < d.count && n < RENDER_MAX_DECALS; ++i) {
        u32 kind = DECAL_KINDS;
        for (u32 k = 0; k < DECAL_KINDS; ++k)
            if (d.kind[i] == kinds[k]) kind = k;
        if (kind == DECAL_KINDS) continue;
        Vec3 rel = delta(eye, d.cx[i], d.cz[i], d.x[i], d.y[i], d.z[i]);
        f32 r = f_max(d.hx[i], f_max(d.hy[i], d.hz[i]));
        if (length(rel) - r * 1.8f > DECAL_FAR) continue;
        RenderDecal &g = dst[n++];
        g.x = rel.x;
        g.y = rel.y;
        g.z = rel.z;
        // Ось x коробки после поворота quat_yaw: (cos, 0, −sin)
        f32 s, c;
        sincos_turn(d.yaw[i], &s, &c);
        g.ux = c;
        g.uz = -s;
        g.hx = d.hx[i];
        g.hy = d.hy[i];
        g.hz = d.hz[i];
        g.kind = kind;
        g.material = assets.materials ? material_find(*assets.materials, d.material[i]) : 0;
        g.seed = d.seed[i];
        g.pad = 0;
    }
    out.decals = {dst, n};
}

void view_build(const Camera &cam, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderFrame &out) {
    out = RenderFrame{};

    WPos eye = view_eye(cam);
    CameraBasis b = view_camera_basis(cam);
    RenderView &v = out.view;
    v.cx = eye.cx;
    v.cz = eye.cz;
    v.x = eye.x;
    v.y = eye.y;
    v.z = eye.z;
    const Vec3 axes[3] = {b.right, b.up, b.forward};
    f32 *dst[3] = {v.right, v.up, v.forward};
    for (u32 i = 0; i < 3; ++i) {
        dst[i][0] = axes[i].x;
        dst[i][1] = axes[i].y;
        dst[i][2] = axes[i].z;
    }
    f32 s, c;
    sincos_turn(turn_from_rad(view_camera_fov(cam) * 0.5f), &s, &c);
    v.tan_half_fov = s / c;
    v.near_m = NEAR_M;
    out.style.pixel = in.pixel;
    out.style.froxel_heat = in.froxel_heat;
    out.style.gcache = !in.gcache_off;
    out.style.shadow_pages = in.shadow_pages;
    out.style.sdf_view = in.sdf_view;
    out.style.gi_view = in.gi_view;
    out.style.dt = in.dt;
    out.style.eye_snap = in.eye_snap;
    out.style.tonemap = in.tonemap;
    out.style.light_quality = in.light_quality;

    // Небо, светила и временный главный свет — из часов мира (docs/sky.md). Воздух пустыни (пыль, тёплая
    // дымка) — по маске пустыни под камерой, на перевале — плавно
    f32 desert = 0.0f;
    if (in.cur->terrain) {
        const Terrain &t = *in.cur->terrain;
        i32 gx = (eye.cx - t.chunk0) * (i32)CHUNK_M + f_floor_i(eye.x);
        i32 gz = (eye.cz - t.chunk0) * (i32)CHUNK_M + f_floor_i(eye.z);
        desert = (f32)terrain_masks2(t, gx, gz).desert / 255.0f;
    }
    f64 ticks = sky_clock_ticks(in.prev->clock, in.cur->clock, in.alpha);
    sky_build(assets.sky, desert, in.cur->clock, ticks, eye.y, out.sky, out.light);
    out.ground = in.cur->terrain != nullptr;
    for (u32 k = 0; k < RENDER_GROUND_LAYERS; ++k) out.ground_layers[k] = assets.ground_layers[k];

    // Статичные объекты — из последней выписки (не двигаются, интерполяция не нужна)
    const Extract &cur = *in.cur;
    u32 n = cur.props_count;
    if (in.scatter && cur.terrain) scatter_update(*in.scatter, *cur.terrain, eye, assets.rocks);
    u32 nscat = in.scatter ? in.scatter->count : 0;
    RenderInstance *inst = arena_push<RenderInstance>(frame, n + nscat + 1 + (in.people ? 2 * VIEW_BODIES_MAX : 0));  // тело и меч
    u32 ni = 0;
    for (u32 i = 0; i < n && ni < RENDER_MAX_INSTANCES; ++i) {
        if (!cur.prop_gen[i]) continue;
        const ViewMesh *vm = find_mesh(assets, cur.prop_mesh[i]);
        if (!vm) continue;
        u32 mesh = vm->id;
        RenderInstance &r = inst[ni];
        r.cx = cur.prop_cx[i];
        r.cz = cur.prop_cz[i];
        r.x = cur.prop_x[i];
        r.y = cur.prop_y[i];
        r.z = cur.prop_z[i];
        Quat q = quat_yaw(cur.prop_yaw[i]);
        r.rot_xy = pack_snorm2(q.x, q.y);
        r.rot_zw = pack_snorm2(q.z, q.w);
        r.scale = cur.prop_scale[i];
        r.mesh = mesh;
        r.material = cur.prop_material[i] && assets.materials ? material_find(*assets.materials, cur.prop_material[i])
                                                             : RENDER_NO_MATERIAL;
        r.seed = cur.prop_seed[i];
        r.lod_min = 0;
        ni++;
    }
    // Рассыпка камней: экземпляры — все (кадр мира отсекает и выбирает LOD сам, а постоянный список на GPU не
    // должен сдвигаться от поворота камеры)
    for (u32 i = 0; i < nscat && ni < RENDER_MAX_INSTANCES; ++i) inst[ni++] = in.scatter->items[i];
    // Люди: тела с позой (docs/people.md); метки выбора и приказа — линиями
    RenderLineVertex *plines = nullptr;
    u32 npl = 0;
    if (in.people && cur.person_gen) {
        plines = arena_push<RenderLineVertex>(frame, VIEW_PEOPLE_LINES);
        people_view_build(*in.people, assets, in, eye, frame, inst, ni, out, plines, npl, in.people_debug);
    }
    out.instances = {inst, ni};
    if (cur.decals) build_decals(*cur.decals, assets, eye, frame, out);
    lamps_build(in.lamps_off ? nullptr : cur.lamps, in.test_lights, cam, cur.terrain, eye, frame, out);

    u32 ndbg = in.dbg ? in.dbg->count : 0;
    if (ndbg || npl) {
        const DebugDraw *d = in.dbg;
        u32 nv = ndbg * 2 + npl < RENDER_MAX_LINE_VERTICES ? ndbg * 2 + npl : RENDER_MAX_LINE_VERTICES;
        RenderLineVertex *lv = arena_push<RenderLineVertex>(frame, nv);
        u32 npk = npl < nv ? npl : nv;
        if (npk) memcpy(lv, plines, npk * sizeof *lv);
        lv += npk;
        for (u32 i = 0; i < (nv - npk) / 2; ++i) {
            const DebugLine &l = d->lines[i];
            Vec3 ra = delta(eye, l.a.cx, l.a.cz, l.a.x, l.a.y, l.a.z);
            Vec3 rb = delta(eye, l.b.cx, l.b.cz, l.b.x, l.b.y, l.b.z);
            lv[2 * i] = {ra.x, ra.y, ra.z, l.rgba};
            lv[2 * i + 1] = {rb.x, rb.y, rb.z, l.rgba};
        }
        out.lines = {lv - npk, npk + (nv - npk) / 2 * 2};
    }
}
