#include "view.h"
#include "../core/hash.h"
#include "../core/math3.h"
#include "../sim/ground.h"

static constexpr f32 NEAR_M = 0.1f;
static constexpr f32 PITCH_MIN = -1.45f, PITCH_MAX = 1.45f;
static constexpr f32 DIST_MIN = 0.0f, DIST_MAX = 12000.0f;
static constexpr f32 DIST_SNAP = 0.5f;          // ближе — сразу вид от глаз
static constexpr f32 EYE_ABOVE_GROUND = 0.6f;  // глаз не ниже стольких метров над землёй
static constexpr f32 PIVOT_ABOVE_GROUND = 1.7f;  // опора орбиты — на высоте глаз человека
static constexpr f32 ARM_GROW = 6.0f;         // скорость, с которой штанга возвращается к длине, 1/с
static constexpr f32 MOUSE_RAD_PER_PX = 0.005f;

// Направление от опоры к глазу по yaw и pitch
static Vec3 back_dir(const Camera &c) {
    f32 cp = cos_f(c.pitch), sp = sin_f(c.pitch);
    return {sin_f(c.yaw) * cp, sp, -cos_f(c.yaw) * cp};
}

static WPos offset(const WPos &p, Vec3 d) {
    WPos q = p;
    q.x += d.x;
    q.y += d.y;
    q.z += d.z;
    wpos_rebase(q);
    return q;
}

// Метры от a до b
static Vec3 delta(const WPos &a, i32 cx, i32 cz, f32 x, f32 y, f32 z) {
    Vec2 d = wpos_delta_xz(a.cx, a.cz, a.x, a.z, cx, cz, x, z);
    return {d.x, y - a.y, d.y};
}

static f32 fov_rad(const Camera &c) { return f_clamp(c.settings.fov_deg, 20.0f, 120.0f) * (PI_F / 180.0f); }

WPos view_pivot_at(const Terrain &ground, WPos p) {
    p.y = ground_at(ground, p).height + PIVOT_ABOVE_GROUND;
    return p;
}

WPos view_eye(const Camera &cam) {
    if (cam.free) return cam.pivot;
    return offset(cam.pivot, back_dir(cam) * (cam.arm < 0.0f ? cam.dist : cam.arm));
}

struct Basis {
    Vec3 right, up, forward;
};
static Basis basis(const Camera &cam) {
    Basis b;
    b.forward = -back_dir(cam);
    b.right = normalize(cross(Vec3{0, 1, 0}, b.forward));
    b.up = cross(b.forward, b.right);
    return b;
}

static bool eye_clear(const Terrain &ground, const WPos &p) {
    return p.y >= ground_at(ground, p).height + EYE_ABOVE_GROUND;
}

// Сколько штанги помещается до земли: шагами от опоры наружу, последний свободный шаг
static f32 arm_room(const Camera &cam, const Terrain &ground) {
    if (cam.dist <= 0.0f) return 0.0f;
    Vec3 d = back_dir(cam);
    f32 step = f_max(0.25f, cam.dist / 64.0f), ok = 0.0f;
    for (f32 t = step;; t += step) {
        f32 tt = f_min(t, cam.dist);
        if (!eye_clear(ground, offset(cam.pivot, d * tt))) return ok;
        ok = tt;
        if (tt >= cam.dist) return ok;
    }
}

void view_camera_control(Camera &cam, const Input &in, f32 dt, const Terrain &ground) {
    if (in.key_pressed[KEY_F]) {
        if (!cam.free) {
            cam.pivot = view_eye(cam);
            cam.free = true;
        } else {
            // Назад на орбиту: опора над точкой земли перед глазом, штанга — до глаза. Смотрим выше
            // горизонта — опора впереди на прежнем расстоянии
            WPos eye = cam.pivot, hit;
            Basis b = basis(cam);
            if (view_screen_to_ground(cam, 2, 2, 1.0f, 1.0f, ground, hit)) {
                cam.dist = f_clamp(length(delta(eye, hit.cx, hit.cz, hit.x, hit.y, hit.z)), DIST_MIN, DIST_MAX);
                cam.pivot = view_pivot_at(ground, hit);
            } else {
                cam.pivot = view_pivot_at(ground, offset(eye, normalize(Vec3{b.forward.x, 0.0f, b.forward.z}) * cam.dist));
            }
            cam.arm = -1.0f;
            cam.free = false;
        }
    }
    // Вращение взгляда: правая кнопка, одинаково на орбите и в полёте (мышь вверх — взгляд вверх)
    if (in.mouse_down[MOUSE_RIGHT]) {
        f32 k = MOUSE_RAD_PER_PX * f_clamp(cam.settings.mouse_sens, 0.05f, 20.0f);
        cam.yaw += in.mouse_dx * k;
        cam.pitch += (cam.settings.invert_y ? -1.0f : 1.0f) * in.mouse_dy * k;
    }
    if (cam.yaw > PI_F) cam.yaw -= TAU_F;
    if (cam.yaw < -PI_F) cam.yaw += TAU_F;
    cam.pitch = f_clamp(cam.pitch, PITCH_MIN, PITCH_MAX);

    Basis b = basis(cam);
    Vec3 flat_fwd = normalize(Vec3{b.forward.x, 0.0f, b.forward.z});
    Vec3 flat_right = b.right;
    f32 fast = in.key_down[KEY_SHIFT] ? 4.0f : 1.0f;
    Vec3 move{};
    if (in.key_down[KEY_W]) move += cam.free ? b.forward : flat_fwd;
    if (in.key_down[KEY_S]) move += -(cam.free ? b.forward : flat_fwd);
    if (in.key_down[KEY_D]) move += flat_right;
    if (in.key_down[KEY_A]) move += -flat_right;
    if (in.key_down[KEY_E]) move += Vec3{0, 1, 0};
    if (in.key_down[KEY_Q]) move += Vec3{0, -1, 0};

    if (cam.free) {
        // Высоко над землёй летать медленно скучно: скорость растёт с высотой
        f32 above = cam.pivot.y - ground_at(ground, cam.pivot).height;
        f32 speed = 12.0f * f_max(1.0f, above / 20.0f);
        cam.pivot = offset(cam.pivot, move * (speed * fast * dt));
        if (in.wheel != 0) cam.pivot = offset(cam.pivot, b.forward * (in.wheel * 2.0f * fast * f_max(1.0f, above / 20.0f)));
        f32 floor_y = ground_at(ground, cam.pivot).height + EYE_ABOVE_GROUND;
        if (cam.pivot.y < floor_y) cam.pivot.y = floor_y;
        return;
    }
    // Орбита: WASD и средняя кнопка двигают опору над землёй, колесо — длина штанги (до вида от глаз)
    f32 reach = f_max(cam.dist, 5.0f);
    move.y = 0.0f;
    cam.pivot = offset(cam.pivot, move * (reach * 0.9f * fast * dt));
    if (in.mouse_down[MOUSE_MIDDLE]) {
        f32 k = reach * 0.0018f;
        cam.pivot = offset(cam.pivot, flat_right * (-in.mouse_dx * k) + flat_fwd * (in.mouse_dy * k));
    }
    for (f32 w = in.wheel; w > 0.5f; w -= 1.0f) cam.dist = cam.dist * 0.85f < DIST_SNAP ? 0.0f : cam.dist * 0.85f;
    for (f32 w = in.wheel; w < -0.5f; w += 1.0f) cam.dist = cam.dist < DIST_SNAP ? DIST_SNAP : cam.dist / 0.85f;
    cam.dist = f_clamp(cam.dist, DIST_MIN, DIST_MAX);
    cam.pivot = view_pivot_at(ground, cam.pivot);
    // Штанга: земля укорачивает её сразу (глаз не уходит под склон), назад — плавно, чтобы на кочках
    // не дёргалась. Взгляд при этом не меняется
    f32 room = arm_room(cam, ground);
    if (cam.arm < 0.0f || room < cam.arm) cam.arm = room;
    else cam.arm += (room - cam.arm) * f_min(1.0f, dt * ARM_GROW);
}

bool view_screen_to_ground(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground, WPos &out) {
    if (width <= 0 || height <= 0) return false;
    Basis b = basis(cam);
    f32 s, c;
    sincos_turn(turn_from_rad(fov_rad(cam) * 0.5f), &s, &c);
    f32 t = s / c, aspect = (f32)width / (f32)height;
    f32 nx = (px / (f32)width) * 2.0f - 1.0f;
    f32 ny = 1.0f - (py / (f32)height) * 2.0f;
    Vec3 dir = normalize(b.forward + b.right * (nx * t * aspect) + b.up * (ny * t));
    WPos eye = view_eye(cam);
    // Шаги растут с расстоянием, на пересечении — деление пополам
    f32 k0 = 0.0f, k = 0.5f;
    for (u32 i = 0; i < 400 && k < 30000.0f; ++i) {
        WPos p = offset(eye, dir * k);
        if (p.y <= ground_at(ground, p).height) {
            for (u32 j = 0; j < 24; ++j) {
                f32 m = (k0 + k) * 0.5f;
                WPos q = offset(eye, dir * m);
                if (q.y <= ground_at(ground, q).height) k = m;
                else k0 = m;
            }
            out = offset(eye, dir * k);
            out.y = ground_at(ground, out).height;
            return true;
        }
        k0 = k;
        k += f_max(0.5f, k * 0.02f);
    }
    return false;
}

static const ViewMesh *find_mesh(const ViewAssets &a, u64 name) {
    for (u32 i = 0; i < a.mesh_count; ++i)
        if (a.meshes[i].name == name) return &a.meshes[i];
    return nullptr;
}

// LOD по ошибке на экране: самый грубый, у которого ошибка упрощения не больше LOD_PIXELS пикселя кадра
constexpr f32 LOD_PIXELS = 0.75f;
static u32 pick_lod(const ViewMesh &m, f32 scale, f32 dist, f32 pixel_angle) {
    f32 limit = LOD_PIXELS * pixel_angle * f_max(dist, 0.1f);
    u32 lod = 0;
    while (lod + 1 < m.lod_count && m.lod_error[lod + 1] * scale <= limit) lod++;
    return lod;
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

// Декали карты (docs/ruin.md): вид по имени, материал по имени, коробка относительно камеры. Дальше
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
    Basis b = basis(cam);
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
    sincos_turn(turn_from_rad(fov_rad(cam) * 0.5f), &s, &c);
    v.tan_half_fov = s / c;
    const f32 pixel_angle = 2.0f * v.tan_half_fov / (f32)(in.height > 0 ? in.height : 1080);  // радиан на пиксель
    v.near_m = NEAR_M;
    out.style.pixel = in.pixel;

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
    const Extract &p = *in.prev;
    const Extract &cur = *in.cur;
    u32 n = cur.props_count;
    if (in.scatter && cur.terrain) scatter_update(*in.scatter, *cur.terrain, eye, assets.rocks);
    u32 nscat = in.scatter ? in.scatter->count : 0;
    RenderInstance *inst = arena_push<RenderInstance>(frame, n + nscat + 1);
    RenderDraw *draws = arena_push<RenderDraw>(frame, n + nscat + 1);
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
        Vec3 rel = delta(eye, r.cx, r.cz, r.x, r.y, r.z);
        u32 lod = pick_lod(*vm, r.scale, length(rel), pixel_angle);
        draws[ni] = {render_key(LAYER_OPAQUE, PIPE_MESH, mesh, lod, dot(rel, b.forward)), ni, 0};
        ni++;
    }
    // Рассыпка камней: позади камеры не рисуется
    for (u32 i = 0; i < nscat && ni < RENDER_MAX_INSTANCES; ++i) {
        const RenderInstance &r = in.scatter->items[i];
        Vec3 rel = delta(eye, r.cx, r.cz, r.x, r.y, r.z);
        f32 depth = dot(rel, b.forward);
        if (depth < -2.0f) continue;
        inst[ni] = r;
        draws[ni] = {render_key(LAYER_OPAQUE, PIPE_MESH, r.mesh, 0, depth), ni, 0};
        ni++;
    }
    out.instances = {inst, ni};
    out.draws = {draws, ni};
    if (cur.decals) build_decals(*cur.decals, assets, eye, frame, out);

    // Точки-заглушки этапа 0 — чуть над землёй, с интерполяцией
    RenderPoint *pts = arena_push<RenderPoint>(frame, RENDER_MAX_POINTS);
    u32 np = 0;
    f32 a = in.alpha;
    for (u32 i = 0; i < DOTS_CAPACITY && np < RENDER_MAX_POINTS; ++i) {
        if (!cur.gen[i]) continue;
        Vec3 r1 = delta(eye, cur.cx[i], cur.cz[i], cur.x[i], cur.y[i] + 0.15f, cur.z[i]);
        if (p.gen[i] == cur.gen[i]) {
            Vec3 r0 = delta(eye, p.cx[i], p.cz[i], p.x[i], p.y[i] + 0.15f, p.z[i]);
            r1 = r0 + (r1 - r0) * a;
        }
        RenderPoint &q = pts[np++];
        q.x = r1.x;
        q.y = r1.y;
        q.z = r1.z;
        q.size = 4.0f;
        q.rgba = rgba8(70, 58, 44);
    }
    out.points = {pts, np};

    if (in.dbg && in.dbg->count) {
        const DebugDraw &d = *in.dbg;
        u32 nv = d.count * 2 < RENDER_MAX_LINE_VERTICES ? d.count * 2 : RENDER_MAX_LINE_VERTICES;
        RenderLineVertex *lv = arena_push<RenderLineVertex>(frame, nv);
        for (u32 i = 0; i < nv / 2; ++i) {
            const DebugLine &l = d.lines[i];
            Vec3 ra = delta(eye, l.a.cx, l.a.cz, l.a.x, l.a.y, l.a.z);
            Vec3 rb = delta(eye, l.b.cx, l.b.cz, l.b.x, l.b.y, l.b.z);
            lv[2 * i] = {ra.x, ra.y, ra.z, l.rgba};
            lv[2 * i + 1] = {rb.x, rb.y, rb.z, l.rgba};
        }
        out.lines = {lv, nv};
    }
}
