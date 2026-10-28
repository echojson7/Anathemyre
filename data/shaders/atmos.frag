// Небо, высотный туман, объёмные лучи солнца, затенение углов (AO)

in vec2 v_uv;

layout(location = 30) uniform mat4 u_inv_vp;
layout(location = 31) uniform vec3 u_cam;
layout(location = 32) uniform vec3 u_sun_dir;
layout(location = 33) uniform vec3 u_sun_color;
layout(location = 34) uniform mat4 u_light_vp;
layout(location = 35) uniform float u_time;
layout(location = 36) uniform int u_frame;
layout(location = 37) uniform float u_proj_scale;
layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_depth;
layout(binding = 2) uniform sampler2DShadow u_shadow;

out vec4 o_color;

const vec3 HAZE = vec3(0.31, 0.28, 0.22);

vec3 world_at(vec2 uv, float d)
{
    vec4 w = u_inv_vp * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return w.xyz / w.w;
}

vec3 sky(vec3 dir)
{
    float h = dir.y;
    float s = max(dot(dir, u_sun_dir), 0.0);
    vec3 zenith = vec3(0.10, 0.10, 0.10);
    vec3 horizon = HAZE * 1.25;
    vec3 c = mix(horizon, zenith, smoothstep(0.0, 0.55, h));
    c = mix(c, HAZE * 0.7, smoothstep(0.0, -0.2, h));
    // Пыльные полосы облаков
    float band = fbm3(vec3(dir.x / max(h + 0.15, 0.05) * 1.5, dir.z / max(h + 0.15, 0.05) * 1.5, u_time * 0.01));
    c *= 0.85 + 0.3 * band * smoothstep(0.0, 0.3, h);
    c += u_sun_color * (pow(s, 600.0) * 6.0 + pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.12);
    return c;
}

float light_vis(vec3 p)
{
    vec4 lp = u_light_vp * vec4(p, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 1.0;
    return texture(u_shadow, vec3(s.xy, s.z - 0.002));
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

void main()
{
    vec2 uv = v_uv;
    float d = texture(u_depth, uv).r;
    vec3 P = world_at(uv, d);
    vec3 ray = P - u_cam;
    float dist = length(ray);
    vec3 dir = ray / dist;
    bool is_sky = d >= 1.0;

    vec3 col;
    if (is_sky) {
        dist = 4000.0;
        col = sky(dir);
    } else {
        col = texture(u_color, uv).rgb * ambient_occlusion(uv, P, dist);
    }

    // Высотный туман: плотнее у земли
    float a = 0.003, b = 0.05;
    float dy = abs(dir.y) < 1e-4 ? 1e-4 : dir.y;
    float fog_amount = a / b * exp(-u_cam.y * b) * (1.0 - exp(-dist * dy * b)) / dy;
    fog_amount += dist * 0.0005;
    // Ограничение: дальние мегаструктуры остаются силуэтами, а не тонут целиком
    if (!is_sky) fog_amount = min(fog_amount, 1.25);
    float T = exp(-fog_amount);
    float sun_amt = pow(max(dot(dir, u_sun_dir), 0.0), 6.0);
    vec3 fog_col = HAZE + u_sun_color * sun_amt * 0.18;
    if (!is_sky) col = col * T + fog_col * (1.0 - T);
    else col = mix(col, fog_col, clamp(1.0 - T, 0.0, 0.6) * smoothstep(0.3, 0.0, dir.y));

    // Объёмный свет: шагаем по лучу в теневой карте
    const int STEPS = 24;
    float march = min(dist, 110.0);
    float step_len = march / float(STEPS);
    float jitter = ign(gl_FragCoord.xy, u_frame);
    float lit = 0.0;
    for (int i = 0; i < STEPS; i++) {
        float t = (float(i) + jitter) * step_len;
        vec3 p = u_cam + dir * t;
        float dens = 0.7 + 0.6 * noise3(p * 0.15 + vec3(u_time * 0.2, 0.0, u_time * 0.1));
        lit += light_vis(p) * dens * exp(-t * 0.012);
    }
    lit *= step_len;
    float mu = dot(dir, u_sun_dir);
    float g = 0.55;
    float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5) * 0.08;
    col += u_sun_color * lit * 0.004 * (phase + 0.02);

    o_color = vec4(col, 1.0);
}
