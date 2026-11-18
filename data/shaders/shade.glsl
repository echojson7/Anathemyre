// Точка сетки → surface() → декали (mesh_surface), потом light_surface(). Зовёт освещение в compute по visibility
// buffer (light_cells.comp; docs/render.md, «Проходы с этапа 5»). По умолчанию — на пиксель; с правилом ячеек
// (F3 «ячейки», --cells) точка привязывается к центру своей ячейки
#include "surface.glsl"
#include "light.glsl"
#include "decal.glsl"

// rel — точка относительно камеры, mat1/blend — второй материал вершины и доля смеси (порог плавного шума: пятна
// с рваным краем, а не квадраты). dyn — сетка движется (человек, машина, меч): узор и пятна считаются в точке
// obj её собственных осей до позы (нормаль obj_n), иначе они плыли бы по детали, пока она едет через мир.
// Свет, декали и нормаль поверхности — в мире. has_uv, uv, duv, tan — развёртка точки (resolve.glsl, Hit;
// docs/textures.md): касательная — в тех же осях, что нормаль узора (мир или оси движущейся детали)
// Вызов surface() сетки: всё, что до него и после (mesh_call_in, mesh_finish), — отдельно, чтобы смесь земли и сеток
// (light_cells) звала surface_uv() одним местом с землёй (ground_plan, ground_surface.glsl)
struct MeshCall {
    uint mat, seed;
    vec3 p, n, ps, ns;  // точка и нормаль в мире (от чанка камеры); точка и нормаль узора
    float cell, foot, ao;
    bool dyn;
    SurfUv t;
};

MeshCall mesh_call_in(vec3 rel, vec3 n, uint mat0, uint mat1, float blend, float ao, uint seed, bool dyn, vec3 obj,
                      vec3 obj_n, bool has_uv, vec2 uv, vec4 duv, vec4 tan) {
    FrameData f = pc.frame;
    float depth = dot(rel, f.forward.xyz);
    float cell = cell_size(depth);
    vec3 p = f.cam_off + rel;  // мир: от начала чанка камеры
    if (f.pixel != 0u) {
        p = cell_snap(p, n, cell);
    } else {
        cell = f.cell_min;
        p -= n * 0.002;  // как в cell_snap: внутрь грани, мимо границы решётки узора
    }
    uint mat = mat0;
    // Точка и нормаль узора, размеры и веса пятен второго материала. Вызов surface() один на оба случая:
    // шейдер встраивает её целиком, второй вызов удваивал самые тяжёлые шейдеры кадра
    vec3 ps = p, ns = n;
    float s0 = 0.25, s1 = 0.0625, w0 = 0.65, w1 = 0.35;
#ifdef GC_ORIGIN_VAR
    if (dyn) {
        // Оси детали: начало отсчёта узора — её ноль, а не чанк камеры (mesh_origin). Пятна второго материала
        // мельче, чем у построек: деталь машины — десятки сантиметров, скол краски — сантиметры
        ns = normalize(obj_n);
        ps = obj - ns * 0.002;
        s0 = 0.0625;
        s1 = 0.015625;
        w0 = 0.6;
        w1 = 0.4;
    }
#else
    dyn = false;
#endif
    if (mat1 != mat0 && blend > 0.0) {
#ifdef GC_ORIGIN_VAR
        ivec2 saved = ORIGIN_CHUNK;
        if (dyn) ORIGIN_CHUNK = ivec2(0);
#endif
        float b = value_noise(ps, s0, seed ^ 0x5EEDu) * w0 + value_noise(ps, s1, seed ^ 0x5EEEu) * w1;
#ifdef GC_ORIGIN_VAR
        ORIGIN_CHUNK = saved;
#endif
        if (b < blend) mat = mat1;
    }
    MeshCall c;
    c.mat = mat;
    c.seed = seed;
    c.p = p;
    c.n = n;
    c.ps = ps;
    c.ns = ns;
    c.cell = cell;
    c.foot = max(depth, 0.0) * f.pixel_angle;
    c.ao = ao;
    c.dyn = dyn;
    c.t.on = has_uv;
    c.t.uv = uv;
    c.t.duv = duv;
    c.t.tan = tan;
    return c;
}

// Начало отсчёта узора сетки на время surface_uv(): у движущейся детали — её ноль
ivec2 mesh_origin(MeshCall c) { return c.dyn ? ivec2(0) : ORIGIN_CHUNK; }

Surface mesh_finish(MeshCall c, Surface s) {
    FrameData f = pc.frame;
    vec3 n = c.n;
    // Нормаль узора — в осях детали (бинд-поза), свету нужна мировая: тот же наклон от нормали детали к мировой
    // (поворот ns → n; у материала без наклона нормали выходит ровно n)
    if (c.dyn) {
        vec3 cr = cross(c.ns, n);
        float d = dot(c.ns, n);
        s.normal = d > -0.999 ? normalize(s.normal * d + cross(cr, s.normal) + cr * (dot(cr, s.normal) / (1.0 + d))) : n;
    }
    s = apply_decals(s, c.p - f.cam_off, n);
    s.ao *= c.ao;
    s.pos = c.p - f.cam_off;
    return s;
}

Surface mesh_surface(vec3 rel, vec3 n, uint mat0, uint mat1, float blend, float ao, uint seed, bool dyn, vec3 obj, vec3 obj_n,
                     bool has_uv, vec2 uv, vec4 duv, vec4 tan) {
    MeshCall c = mesh_call_in(rel, n, mat0, mat1, blend, ao, seed, dyn, obj, obj_n, has_uv, uv, duv, tan);
#ifdef GC_ORIGIN_VAR
    ivec2 saved = ORIGIN_CHUNK;
    ORIGIN_CHUNK = mesh_origin(c);
#endif
    Surface s = surface_uv(c.mat, c.ps, c.ns, c.cell, c.foot, c.seed, c.t);
#ifdef GC_ORIGIN_VAR
    ORIGIN_CHUNK = saved;
#endif
    return mesh_finish(c, s);
}

