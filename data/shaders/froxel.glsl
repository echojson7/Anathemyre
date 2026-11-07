// Froxels (docs/frame.md «Froxels»): клетки 64×64 пикселя × froxel_z слоёв глубины. Слой 0 — до froxel_near,
// дальше степенями до froxel_far, последний — до бесконечности. Клетка — FROXEL_STRIDE слов: число огней и их
// номера, число декалей и их номера. Число FROXEL_ALL — в клетку не влезли все, перебирать весь список кадра.
// Для света пользователя: froxel_at(s.pos) → froxel_light_count() и froxel_light() → pc.frame.lights.v[номер]
#ifndef FROXEL_GLSL
#define FROXEL_GLSL
#include "common.glsl"

const uint FROXEL_TILE = 64u, FROXEL_STRIDE = 96u;
const uint FROXEL_LIGHTS = 31u, FROXEL_DECALS = 63u;  // мест под номера в клетке
const uint FROXEL_ALL = 0xFFFFFFFFu;
const uint FROXEL_NONE = 0xFFFFFFFFu;                 // точка вне сетки (позади камеры)

// Слой по глубине вдоль взгляда
uint froxel_slice(float depth) {
    FrameData f = pc.frame;
    if (depth < f.froxel_near) return 0u;
    float k = log2(depth / f.froxel_near) / log2(f.froxel_far / f.froxel_near) * float(f.froxel_z - 1u);
    return min(uint(k) + 1u, f.froxel_z - 1u);
}
// Границы слоя k по глубине
vec2 froxel_depths(uint k) {
    FrameData f = pc.frame;
    float r = f.froxel_far / f.froxel_near, n = float(f.froxel_z - 1u);
    float lo = k == 0u ? 0.0 : f.froxel_near * pow(r, float(k - 1u) / n);
    float hi = k + 1u >= f.froxel_z ? 1e6 : f.froxel_near * pow(r, float(k) / n);
    return vec2(lo, hi);
}

uint froxel_index(uvec2 tile, uint slice) {
    FrameData f = pc.frame;
    return (slice * f.froxel_y + tile.y) * f.froxel_x + tile.x;
}

// Клетка точки rel (относительно камеры), FROXEL_NONE — позади камеры, за краем кадра — крайняя клетка
uint froxel_at(vec3 rel) {
    FrameData f = pc.frame;
    if (f.froxel_on == 0u) return FROXEL_NONE;
    float vz = dot(rel, f.forward.xyz);
    if (vz <= 1e-4) return FROXEL_NONE;
    vec2 ndc = vec2(dot(rel, f.right.xyz) * f.proj_x, -dot(rel, f.up.xyz) * f.proj_y) / vz;
    vec2 px = (ndc + 1.0) / f.ndc_per_pixel;
    uvec2 t = uvec2(clamp(ivec2(floor(px / float(FROXEL_TILE))), ivec2(0), ivec2(f.froxel_x, f.froxel_y) - 1));
    return froxel_index(t, froxel_slice(vz));
}

uint froxel_light_count(uint fi) {
    FrameData f = pc.frame;
    if (fi == FROXEL_NONE) return 0u;
    uint n = f.froxels.v[fi * FROXEL_STRIDE];
    return n == FROXEL_ALL ? f.light_count : n;
}
// k-й огонь клетки — номер в pc.frame.lights
uint froxel_light(uint fi, uint k) {
    FrameData f = pc.frame;
    return f.froxels.v[fi * FROXEL_STRIDE] == FROXEL_ALL ? k : f.froxels.v[fi * FROXEL_STRIDE + 1u + k];
}
uint froxel_decal_count(uint fi) {
    FrameData f = pc.frame;
    if (fi == FROXEL_NONE) return 0u;
    uint n = f.froxels.v[fi * FROXEL_STRIDE + 32u];
    return n == FROXEL_ALL ? f.decal_count : n;
}
uint froxel_decal(uint fi, uint k) {
    FrameData f = pc.frame;
    return f.froxels.v[fi * FROXEL_STRIDE + 32u] == FROXEL_ALL ? k : f.froxels.v[fi * FROXEL_STRIDE + 33u + k];
}
#endif
