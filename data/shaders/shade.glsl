// Точка сетки → surface() → декали (mesh_surface), потом light_surface(). Зовёт освещение в compute по visibility
// buffer (light_cells.comp; docs/render.md, «Проходы с этапа 5»). По умолчанию — на пиксель; с правилом ячеек
// (F3 «ячейки», --cells) точка привязывается к центру своей ячейки
#include "surface.glsl"
#include "light.glsl"
#include "decal.glsl"

// rel — точка относительно камеры, mat1/blend — второй материал вершины и доля смеси (порог плавного шума: пятна
// с рваным краем, а не квадраты). dyn — сетка движется (человек, машина, меч): узор и пятна считаются в точке
// obj её собственных осей до позы (нормаль obj_n), иначе они плыли бы по детали, пока она едет через мир.
// Свет, декали и нормаль поверхности — в мире
Surface mesh_surface(vec3 rel, vec3 n, uint mat0, uint mat1, float blend, float ao, uint seed, bool dyn, vec3 obj, vec3 obj_n) {
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
    if (dyn) {
#ifdef GC_ORIGIN_VAR
        // Оси детали: начало отсчёта узора — её ноль, а не чанк камеры. Пятна второго материала мельче, чем у
        // построек: деталь машины — десятки сантиметров, скол краски — сантиметры
        ivec2 saved = ORIGIN_CHUNK;
        ORIGIN_CHUNK = ivec2(0);
        vec3 on = normalize(obj_n);
        vec3 po = obj - on * 0.002;
        if (mat1 != mat0 && blend > 0.0) {
            float b = value_noise(po, 0.0625, seed ^ 0x5EEDu) * 0.6 + value_noise(po, 0.015625, seed ^ 0x5EEEu) * 0.4;
            if (b < blend) mat = mat1;
        }
        Surface sd = surface(mat, po, on, cell, max(depth, 0.0) * f.pixel_angle, seed);
        ORIGIN_CHUNK = saved;
        sd.normal = n;
        sd = apply_decals(sd, p - f.cam_off, n);
        sd.ao *= ao;
        sd.pos = p - f.cam_off;
        return sd;
#endif
    }
    if (mat1 != mat0 && blend > 0.0) {
        float b = value_noise(p, 0.25, seed ^ 0x5EEDu) * 0.65 + value_noise(p, 0.0625, seed ^ 0x5EEEu) * 0.35;
        if (b < blend) mat = mat1;
    }
    Surface s = surface(mat, p, n, cell, max(depth, 0.0) * f.pixel_angle, seed);
    s = apply_decals(s, p - f.cam_off, n);
    s.ao *= ao;
    s.pos = p - f.cam_off;
    return s;
}

