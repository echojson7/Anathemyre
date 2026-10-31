// Тени огней группы СВЕТ (docs/lighting.md): кубические карты ближних огней, по слою на грань.
// Блок Torch — render.c, GpuTorch: матрицы граней, где нарисован слот, и слот у каждого огня кадра
layout(std140, binding = 42) uniform Torch {
    mat4  u_torch_vp[24];     // слот·6 + грань: +X −X +Y −Y +Z −Z
    vec4  u_torch_pos[4];     // xyz — откуда нарисована карта, w — радиус
    ivec4 u_torch_slot[4];    // огонь i: [i/4][i%4], −1 — без тени
};
layout(binding = 43) uniform sampler2DArrayShadow u_torch;

int torch_slot(int i) { return u_torch_slot[i >> 2][i & 3]; }

// Видимость точки p от огня слота s: грань по главной оси, сдвиг по нормали под размер текселя на этом
// расстоянии (грань 90°, 256 текселей), 4 выборки 2×2
float torch_shadow(int s, vec3 p, vec3 n)
{
    vec3 d = p - u_torch_pos[s].xyz;
    vec3 a = abs(d);
    float dist = max(max(a.x, a.y), a.z);
    int f = a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
    float texel_m = dist * (2.0 / 256.0);
    vec3 off = p + n * texel_m * 1.5 - d * (texel_m * 0.5 / max(length(d), 1e-3));
    vec4 lp = u_torch_vp[s * 6 + f] * vec4(off, 1.0);
    vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
    if (c.z >= 1.0) return 1.0;
    float sum = 0.0;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            sum += texture(u_torch, vec4(c.xy + (vec2(x, y) - 0.5) / 256.0, float(s * 6 + f), c.z));
    return sum * 0.25;
}
