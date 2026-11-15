// Точка сетки → surface() → декали (mesh_surface), потом light_surface(). Зовёт освещение в compute по visibility
// buffer (light_cells.comp; docs/render.md, «Проходы с этапа 5»). По умолчанию — на пиксель; с правилом ячеек
// (F3 «ячейки», --cells) точка привязывается к центру своей ячейки
#include "surface.glsl"
#include "light.glsl"
#include "decal.glsl"

// rel — точка относительно камеры, mat1/blend — второй материал вершины и доля смеси (порог плавного шума: пятна
// с рваным краем, а не квадраты)
Surface mesh_surface(vec3 rel, vec3 n, uint mat0, uint mat1, float blend, float ao, uint seed) {
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

