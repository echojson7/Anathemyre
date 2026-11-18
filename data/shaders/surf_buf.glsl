// Поверхность кадра (docs/perf_arch.md, шаг 4; docs/materials.md, «Материал отдельно от света»): проход материала
// (surf_cells.comp) пишет на пиксель то, что материал отдал свету, освещение (light_cells.comp) её читает. Только
// пиксели сеток и земли мимо кеша — у плиток сеток, смеси и земли на пиксель; земля из кеша и небо записи не имеют.
// Место — по плиткам 8×8 кадра, 64 подряд (как sun_vis). Запись — два uvec4 (32 Б):
//   [0] x — альбедо: корень, 11:11:10 бит; y — нормаль октаэдром, 2×16 бит; z — шероховатость 12 бит, затенение
//       10, доля металла 8, флаги SURF_* 2; w — свечение: три мантиссы по 9 бит и общая степень 5 бит
//   [1] xyz — точка света pos (м от камеры), w — глубина по лучу пикселя (rel = pixel_ray · t, Hit.t), побайтно
// Точность: альбедо и нормаль мельче, чем у кеша земли (8 бит sRGB и 2×12): после света и тонмаппинга разница
// ≤ 1/255 у отдельных пикселей (сверка — docs/perf_arch.md)
#ifndef SURF_BUF_GLSL
#define SURF_BUF_GLSL
#include "common.glsl"

const uint SURF_GROUND = 1u;  // поверхность земли (свет: g_ground)
const uint SURF_MOVING = 2u;  // сетка движется сама (свет: g_moving, ячейки мира ей не годятся)

uint surf_index(ivec2 px) {
    uint tx = (uint(pc.frame.viewport.x) + 7u) / 8u;
    return ((uint(px.y >> 3) * tx + uint(px.x >> 3)) * 64u + uint((px.y & 7) * 8 + (px.x & 7))) * 2u;
}

// Свечение (кд/м², до миллионов у стекла ламп): общая степень e, значение = m / 512 · 2^(e − 8); ноль — ровно ноль
uint surf_pack_em(vec3 c) {
    c = clamp(c, vec3(0.0), vec3(8.3e6));
    float mx = max(c.r, max(c.g, c.b));
    if (mx < exp2(-17.0)) return 0u;
    int e = clamp(int(floor(log2(mx))) + 1, -8, 23);  // mx < 2^e
    float sc = exp2(float(9 - e));
    uvec3 m = uvec3(min(round(c * sc), vec3(511.0)));
    return m.r | m.g << 9 | m.b << 18 | uint(e + 8) << 27;
}
vec3 surf_unpack_em(uint v) {
    if (v == 0u) return vec3(0.0);
    vec3 m = vec3(float(v & 511u), float((v >> 9) & 511u), float((v >> 18) & 511u));
    return m * exp2(float(int(v >> 27) - 8 - 9));
}

void surf_store(ivec2 px, Surface s, float t, uint flags) {
    vec3 a = sqrt(clamp(s.albedo, 0.0, 1.0));
    uint w0 = uint(round(a.r * 2047.0)) | uint(round(a.g * 2047.0)) << 11 | uint(round(a.b * 1023.0)) << 22;
    vec3 n = s.normal / (abs(s.normal.x) + abs(s.normal.y) + abs(s.normal.z));
    vec2 e = n.y >= 0.0 ? n.xz : (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    uint w1 = packSnorm2x16(e);
    uint w2 = uint(round(clamp(s.rough, 0.0, 1.0) * 4095.0)) | uint(round(clamp(s.ao, 0.0, 1.0) * 1023.0)) << 12 |
              uint(round(clamp(s.metal, 0.0, 1.0) * 255.0)) << 22 | (flags & 3u) << 30;
    uint i = surf_index(px);
    pc.frame.surf.v[i] = uvec4(w0, w1, w2, surf_pack_em(s.emissive));
    pc.frame.surf.v[i + 1u] = uvec4(floatBitsToUint(s.pos), floatBitsToUint(t));
}

// Флаги и глубина записи — до поверхности (освещению нужны раньше: вид пикселя, рамка огней плитки)
uint surf_flags(ivec2 px, out float t) {
    uint i = surf_index(px);
    t = uintBitsToFloat(pc.frame.surf.v[i + 1u].w);
    return pc.frame.surf.v[i].z >> 30;
}

Surface surf_load(ivec2 px) {
    uint i = surf_index(px);
    uvec4 v = pc.frame.surf.v[i];
    Surface s;
    vec3 a = vec3(float(v.x & 2047u) / 2047.0, float((v.x >> 11) & 2047u) / 2047.0, float(v.x >> 22) / 1023.0);
    s.albedo = a * a;
    vec2 e = unpackSnorm2x16(v.y);
    vec3 n = vec3(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.z >= 0.0 ? 1.0 : -1.0);
    s.normal = normalize(n);
    s.rough = float(v.z & 4095u) / 4095.0;
    s.ao = float((v.z >> 12) & 1023u) / 1023.0;
    s.metal = float((v.z >> 22) & 255u) / 255.0;
    s.emissive = surf_unpack_em(v.w);
    s.pos = uintBitsToFloat(pc.frame.surf.v[i + 1u].xyz);
    s.height = 0.0;
    s.nvar = 0.0;
    return s;
}
#endif
