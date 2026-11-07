// Forward этапов 1–4: точка фрагмента → центр ячейки → surface() → light_surface(). С этапа 5 то же
// делает освещение в compute по visibility buffer (docs/render.md, «Проходы с этапа 5»)
#include "surface.glsl"
#include "light.glsl"
#include "decal.glsl"

// rel — точка относительно камеры, mat1/blend — второй материал вершины и доля смеси (по ячейкам, вразброс)
vec3 shade(vec3 rel, vec3 n, uint mat0, uint mat1, float blend, float ao, uint seed) {
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
    if (mat1 != mat0 && blend > 0.0 && cell_noise(p, max(cell, 1.0 / 16.0), seed ^ 0x5EEDu) < blend) mat = mat1;
    Surface s = surface(mat, p, n, cell, max(depth, 0.0) * f.pixel_angle, seed);
    s = apply_decals(s, p - f.cam_off, n);
    s.ao *= ao;
    s.pos = p - f.cam_off;
    return light_surface(s);
}
