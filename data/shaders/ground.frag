#version 460
#extension GL_GOOGLE_include_directive : require
#include "terrain.glsl"
#include "surface.glsl"
#include "light.glsl"
// Земля: точка → центр ячейки → нормаль → слой → surface() → light_surface(), как shade() у сеток.
// Слой выбирают правила по уклону сетки 8 м и маскам (те же пороги, что ground_kind() у шага мира) с шумом,
// а на стыке двух лучших слоёв побеждает выступающий узор: песок затекает между камнями (docs/terrain.md)
layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in uint v_level;
layout(location = 0) out vec4 o_color;

void main() {
    FrameData f = pc.frame;
    float depth = dot(v_rel, f.forward.xyz);
    float cell = cell_size(depth);
    float foot = max(depth, 0.0) * f.pixel_angle;
    vec3 p = f.cam_off + v_rel;
    // Нормаль треугольника выбирает плоскость привязки (на обрыве ячейки встают вертикально)
    vec3 ng = normalize(cross(dFdy(v_rel), dFdx(v_rel)));
    if (ng.y < 0.0) ng = -ng;
    if (f.pixel != 0u) {
        p = cell_snap(p, ng, cell);
    } else {
        cell = f.cell_min;
        p -= ng * 0.002;
    }
    // Ближние кольца: нормаль клетки 1 м в центре ячейки (как у ground_at), дальние — из вершин
    vec3 n = v_level <= 1u ? t_patch_normal(p.xz) : normalize(v_normal);

    ivec2 g;
    vec2 fr;
    t_split(p.xz, g, fr);
    vec2 mk = vec2(t_masks(g.x, g.y)) / 255.0;
    float rough = mk.x, sand = mk.y;
    float slope = t_base_slope(vec2(g) + fr);
    float big = value_noise(p, 64.0, 71u) - 0.5;
    float mid = value_noise(p, 4.0, 72u) - 0.5;
    float w[4];
    w[0] = sand * 1.25 * (1.0 - smoothstep(0.62, 0.78, slope)) + big * 0.25;          // песок
    w[1] = 0.45 + mid * 0.2;                                                         // сухая земля
    w[2] = smoothstep(0.5, 0.8, rough) * (1.0 - smoothstep(0.6, 0.9, slope)) + mid * 0.35 + big * 0.2;  // щебень
    w[3] = smoothstep(0.5, 0.8, slope) * 1.6 + smoothstep(0.85, 1.0, rough) * 0.4 + big * 0.2;  // камень
    int a = 0;
    for (int k = 1; k < 4; ++k) if (w[k] > w[a]) a = k;
    int b = a == 0 ? 1 : 0;
    for (int k = 0; k < 4; ++k) if (k != a && w[k] > w[b]) b = k;
    const uint SEED = 0x7E77u;
    Surface s = surface(f.ground_layers[a], p, n, cell, foot, SEED);
    if (w[a] - w[b] < 0.5) {
        Surface sb = surface(f.ground_layers[b], p, n, cell, foot, SEED);
        if (w[b] + sb.height * 0.6 > w[a] + s.height * 0.6) s = sb;
    }
    s.pos = p - f.cam_off;
    o_color = vec4(light_surface(s), 1.0);
}
