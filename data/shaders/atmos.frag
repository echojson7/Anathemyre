// Небо, высотный туман, объёмные лучи солнца, затенение углов (AO)

in vec2 v_uv;

layout(location = 32) uniform vec3 u_sun_dir;
layout(location = 33) uniform vec3 u_sun_color;
layout(location = 35) uniform float u_time;
layout(location = 38) uniform vec3 u_haze;
layout(location = 39) uniform vec3 u_zenith;
layout(binding = 0) uniform sampler2D u_color;
layout(binding = 3) uniform sampler2D u_rays;   // rays.frag, половинное разрешение
layout(binding = 4) uniform sampler2D u_ao;     // ao.frag, половинное разрешение
layout(location = 47) uniform int u_ao_half;    // 1 — AO из половинного разрешения

out vec4 o_color;

vec3 sky(vec3 dir)
{
    float h = dir.y;
    float s = max(dot(dir, u_sun_dir), 0.0);
    vec3 horizon = u_haze * 1.25;
    vec3 c = mix(horizon, u_zenith, smoothstep(0.0, 0.55, h));
    c = mix(c, u_haze * 0.7, smoothstep(0.0, -0.2, h));
    // Пыльные полосы облаков
    float band = fbm3(vec3(dir.x / max(h + 0.15, 0.05) * 1.5, dir.z / max(h + 0.15, 0.05) * 1.5, u_time * 0.01));
    c *= 0.85 + 0.3 * band * smoothstep(0.0, 0.3, h);
    // Звёзды сквозь пыль: видны только ночью
    float night = 1.0 - smoothstep(0.006, 0.04, u_zenith.g);
    if (night > 0.0 && h > 0.0) {
        vec3 q = floor(dir * 300.0);
        c += vec3(0.50, 0.52, 0.60) * step(0.997, hash13(q)) * hash13(q + 7.0) * night * smoothstep(0.0, 0.3, h);
    }
    c += u_sun_color * (pow(s, 600.0) * 6.0 + pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.12);
    return c;
}

// Объёмный свет из половинного разрешения: из четырёх соседей берём близких по глубине,
// чтобы лучи не перетекали через края фигур
float rays_at(vec2 uv, float march)
{
    vec2 f = uv * vec2(textureSize(u_rays, 0)) - 0.5;
    ivec2 i0 = ivec2(floor(f));
    vec2 t = fract(f);
    ivec2 hi = textureSize(u_rays, 0) - 1;
    float sum = 0.0, wsum = 0.0;
    for (int k = 0; k < 4; k++) {
        ivec2 o = ivec2(k & 1, k >> 1);
        vec2 r = texelFetch(u_rays, clamp(i0 + o, ivec2(0), hi), 0).rg;
        float wb = (o.x == 1 ? t.x : 1.0 - t.x) * (o.y == 1 ? t.y : 1.0 - t.y);
        float wd = 1.0 / (0.02 + abs(r.g - march) / march);
        sum += r.r * wb * wd;
        wsum += wb * wd;
    }
    return sum / max(wsum, 1e-6);
}

// AO из половинного разрешения (ao.frag): из четырёх соседей берём близких по дальности, два чтения gather
float ao_at(vec2 uv, float dist)
{
    vec2 t = fract(uv * vec2(textureSize(u_ao, 0)) - 0.5);
    vec4 ao = textureGather(u_ao, uv, 0), dd = textureGather(u_ao, uv, 1);  // порядок: (0,1) (1,1) (1,0) (0,0)
    vec4 wb = vec4((1.0 - t.x) * t.y, t.x * t.y, t.x * (1.0 - t.y), (1.0 - t.x) * (1.0 - t.y));
    vec4 w = wb / (0.02 + abs(dd - dist) / dist);
    return dot(ao, w) / max(dot(w, vec4(1.0)), 1e-6);
}

// Свечение пыли вокруг факелов и фонарей: свет, рассеянный вдоль луча взгляда до поверхности.
// Интеграл 1/d² по лучу считается точно: (atan((t1 - tc)/h) - atan(-tc/h)) / h
vec3 light_glow(vec3 dir, float dist)
{
    vec3 s = vec3(0.0);
    for (int i = 0; i < l_count.x; i++) {
        if (l_dir[i].w <= 0.0) continue;
        vec3 q = l_pos[i].xyz - u_cam;
        float tc = dot(q, dir);
        // Светится пыль только у самого огня: ореол не шире метра, к краю сходит в ноль
        const float r = 1.0;
        float h2 = dot(q, q) - tc * tc;
        if (h2 >= r * r) continue;
        h2 = max(h2, 0.0225);  // ближе 0.15 м от оси луча не растёт: пламя не тонет в пятне
        float h = sqrt(h2);
        float a = (atan((dist - tc) / h) + atan(tc / h)) / h;
        float w = 1.0 - h2 / (r * r);
        s += l_col[i].rgb * (l_dir[i].w * a * w * w);
    }
    return s;
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
        col = texture(u_color, uv).rgb;
        col *= u_ao_half != 0 ? ao_at(uv, dist) : ambient_occlusion(uv, P, dist);
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
    vec3 fog_col = u_haze + u_sun_color * sun_amt * 0.18;
    if (!is_sky) col = col * T + fog_col * (1.0 - T);
    else col = mix(col, fog_col, clamp(1.0 - T, 0.0, 0.6) * smoothstep(0.3, 0.0, dir.y));

    // Объёмный свет
    float lit = rays_at(uv, min(dist, 110.0));
    float mu = dot(dir, u_sun_dir);
    float g = 0.55;
    float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5) * 0.08;
    col += u_sun_color * lit * 0.004 * (phase + 0.02);
    if (l_count.x > 0) col += light_glow(dir, min(dist, 300.0));

    o_color = vec4(col, 1.0);
}
