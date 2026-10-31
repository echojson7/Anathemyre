// Затенение углов (AO) по глубине: атмосфера (полное разрешение) и GTAO

layout(binding = 7) uniform sampler2D u_depth;

vec3 world_at(vec2 uv, float d)
{
    vec4 w = u_inv_vp * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return w.xyz / w.w;
}

// Нормаль по глубине соседних пикселей: по каждой оси берём соседа ближе по глубине, чтобы не перескочить
// через край предмета. Не dFdx: при шейдинге блоками производная шагает через блок 2–4 пикселя и на краю
// предмета смешивает две поверхности, нормаль выходит ложной и блок у края чернеет
vec3 depth_normal(vec2 uv)
{
    vec2 res = vec2(textureSize(u_depth, 0));
    ivec2 t = ivec2(uv * res), hi = ivec2(res) - 1;
    float d = texelFetch(u_depth, t, 0).r;
    vec3 C = world_at((vec2(t) + 0.5) / res, d);
    vec3 a[2];
    for (int i = 0; i < 2; i++) {
        ivec2 e = ivec2(i == 0, i == 1);
        ivec2 t0 = max(t - e, ivec2(0)), t1 = min(t + e, hi);
        float d0 = texelFetch(u_depth, t0, 0).r, d1 = texelFetch(u_depth, t1, 0).r;
        bool use1 = t0 == t || (t1 != t && abs(d1 - d) < abs(d0 - d));
        ivec2 tn = use1 ? t1 : t0;
        vec3 S = world_at((vec2(tn) + 0.5) / res, use1 ? d1 : d0);
        a[i] = use1 ? S - C : C - S;
    }
    vec3 n = cross(a[0], a[1]);
    // Тонкий предмет на фоне неба: соседей на поверхности нет, нормаль — к камере
    return dot(n, n) > 1e-24 && !any(isinf(n)) ? normalize(n) : normalize(u_cam_pos - C);
}

float ambient_occlusion(vec2 uv, vec3 P, float view_dist)
{
    vec3 N = depth_normal(uv);
    if (dot(N, u_cam_pos - P) < 0.0) N = -N;
    vec2 res = vec2(textureSize(u_depth, 0));
    float radius_px = clamp(0.9 * u_proj_scale / max(view_dist, 0.1), 2.0, 40.0);
    float rnd = ign(gl_FragCoord.xy, u_frame) * 6.2831;
    float occ = 0.0;
    const int N_SAMPLES = 8;
    for (int i = 0; i < N_SAMPLES; i++) {
        float a = rnd + float(i) * 2.399963;
        float r = radius_px * sqrt((float(i) + 0.5) / float(N_SAMPLES));
        vec2 suv = uv + vec2(cos(a), sin(a)) * r / res;
        float d = texture(u_depth, suv).r;
        vec3 S = world_at(suv, d);
        vec3 v = S - P;
        float l2 = dot(v, v);
        float o = max(dot(v, N) * inversesqrt(l2 + 1e-4) - 0.1, 0.0);
        occ += o * (1.0 - smoothstep(0.5, 1.4, l2));
    }
    return clamp(1.0 - occ / float(N_SAMPLES) * 1.6, 0.0, 1.0);
}

