// Затенение углов (AO) по глубине: общее для атмосферы (полное разрешение) и ao.frag (половинное)

layout(location = 30) uniform mat4 u_inv_vp;
layout(location = 31) uniform vec3 u_cam;
layout(location = 36) uniform int u_frame;
layout(location = 37) uniform float u_proj_scale;
layout(binding = 1) uniform sampler2D u_depth;

vec3 world_at(vec2 uv, float d)
{
    vec4 w = u_inv_vp * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return w.xyz / w.w;
}

float ambient_occlusion(vec2 uv, vec3 P, float view_dist)
{
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, u_cam - P) < 0.0) N = -N;
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

