#version 460
#extension GL_GOOGLE_include_directive : require
#include "terrain.glsl"
#include "surface.glsl"
#include "light.glsl"
#include "post.glsl"
// Земля: точка → центр ячейки → нормаль → слой → surface() → light_surface(), как shade() у сеток.
// Слой выбирают правила по уклону сетки 8 м и маскам (те же пороги, что ground_kind() у шага мира) с плавным
// шумом, а на стыке двух лучших слоёв побеждает выступающий узор: песок затекает между камнями и травой
// (docs/terrain.md). Вода — гладь на уровне воды (ground.vert), под ней на мелком видно дно
layout(location = 0) in vec3 v_rel;
layout(location = 1) in vec3 v_normal;
layout(location = 2) flat in uint v_level;
layout(location = 3) in float v_wdepth;
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
    bool water = v_wdepth > 0.0;
    // Плавная нормаль сетки 8 м (+ слой деталей у ближних колец), дальние кольца — из вершин
    vec3 n = v_level <= 5u ? t_smooth_normal(p.xz, v_level <= 1u) : normalize(v_normal);
    if (water) n = vec3(0.0, 1.0, 0.0);

    ivec2 g;
    vec2 fr;
    t_split(p.xz, g, fr);
    vec4 mk = vec4(t_masks(g.x, g.y)) / 255.0;
    float rough = mk.x, sand = mk.y, grass = mk.z, wet = mk.w;
    float slope = t_base_slope(vec2(g) + fr);
    // Плавные пятна (градиентный шум): сотни метров — против повторяемости, метры — рваный край слоёв
    float big = fbm_n(p, 128.0, 3, 71u) - 0.5;
    float mid = fbm_n(p, 16.0, 4, 72u) - 0.5;
    float fine = fbm_n(p, 2.0, 3, 74u) - 0.5;  // рваный край слоёв на метрах
    float steep = smoothstep(0.55, 0.85, slope);
    float w[5];
    w[0] = sand * 0.95 * (1.0 - smoothstep(0.62, 0.78, slope)) + big * 0.3 + mid * 0.9 + fine * 0.4;   // песок
    w[1] = 0.42 + mid * 0.25 - grass * 0.15;                                                     // сухая земля
    w[2] = smoothstep(0.5, 0.8, rough) * (1.0 - steep) * (1.0 - grass) + mid * 0.3 + big * 0.2;  // щебень
    w[3] = steep * 1.6 + smoothstep(0.85, 1.0, rough) * 0.4 + big * 0.2;                         // камень
    w[4] = grass * 1.0 * (1.0 - smoothstep(0.5, 0.85, slope)) + big * 0.25 - mid * 0.3;          // трава
    int a = 0;
    for (int k = 1; k < 5; ++k) if (w[k] > w[a]) a = k;
    int b = a == 0 ? 1 : 0;
    for (int k = 0; k < 5; ++k) if (k != a && w[k] > w[b]) b = k;
    const uint SEED = 0x7E77u;
    Surface s = surface(f.ground_layers[a], p, n, cell, foot, SEED);
    // Широкий стык: кочки травы и камни прорастают сквозь песок, песок затекает между ними
    if (w[a] - w[b] < 0.7) {
        Surface sb = surface(f.ground_layers[b], p, n, cell, foot, SEED);
        if (w[b] + sb.height * 0.9 > w[a] + s.height * 0.9) s = sb;
    }
    // Сырость у воды: земля темнее, трава гуще
    s.albedo *= 1.0 - wet * 0.3 * (0.6 + 0.4 * fbm_n(p, 16.0, 2, 73u));
    if (water) {
        // Гладь: свой узор и блик; на мелком сквозь неё видно дно
        Surface sw = surface(f.ground_layers[5], p, n, cell, foot, SEED);
        float k = 1.0 - exp(-v_wdepth * 2.5);
        sw.albedo = mix(s.albedo * 0.6, sw.albedo, k);
        s = sw;
    }
    s.pos = p - f.cam_off;
    o_color = vec4(post_color(light_surface(s)), 1.0);
}
