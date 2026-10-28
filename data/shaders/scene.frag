// Освещение и процедурные материалы. Выход в линейном HDR

in vec3 v_world;
in vec3 v_nrm;
in vec3 v_color;
flat in uint v_mat;
in float v_local_y;

layout(location = 17) uniform int u_skinned;
layout(location = 18) uniform mat4 u_light_vp;
layout(location = 20) uniform vec3 u_cam_pos;
layout(location = 21) uniform vec3 u_sun_dir;
layout(location = 22) uniform vec3 u_sun_color;
layout(location = 23) uniform float u_time;
layout(binding = 0) uniform sampler2DShadow u_shadow;

out vec4 o_color;

const uint MAT_CONCRETE = 0u, MAT_RUST = 1u, MAT_SAND = 2u, MAT_CLOTH = 3u, MAT_LEATHER = 4u,
           MAT_GLYPH = 5u, MAT_EMISSIVE = 6u, MAT_SKIN = 7u, MAT_METAL = 8u;

float sun_shadow(vec3 p, vec3 n)
{
    vec3 off = p + n * 0.06 + u_sun_dir * 0.03;
    vec4 lp = u_light_vp * vec4(off, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    float texel = 1.0 / 2048.0;
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            sum += texture(u_shadow, vec3(s.xy + vec2(x, y) * texel * 1.2, s.z));
    return sum / 9.0;
}

// Знаки на неизвестном языке: штрихи в ячейке по хешу
float glyph_ink(vec2 uv)
{
    vec2 cell_size = vec2(0.55, 0.8);
    vec2 cell = floor(uv / cell_size);
    vec2 f = fract(uv / cell_size);
    f = (f - 0.12) / 0.76;
    if (any(lessThan(f, vec2(0.0))) || any(greaterThan(f, vec2(1.0)))) return 0.0;
    if (hash12(cell + 7.1) < 0.18) return 0.0; // пробел
    float ink = 0.0;
    float w = 0.09;
    for (int i = 0; i < 3; i++) {
        float x = 0.1 + 0.4 * float(i);
        float h = hash12(cell * 1.31 + float(i) * 3.7);
        if (h > 0.4) {
            float y0 = floor(hash12(cell + float(i) * 11.3) * 3.0) / 4.0;
            float y1 = y0 + 0.25 + floor(h * 3.0) / 4.0;
            ink = max(ink, step(abs(f.x - x), w) * step(y0, f.y) * step(f.y, y1));
        }
    }
    for (int i = 0; i < 4; i++) {
        float y = 0.08 + 0.28 * float(i);
        float h = hash12(cell * 2.17 + float(i) * 5.3);
        if (h > 0.5) {
            float x0 = floor(hash12(cell + float(i) * 17.9) * 2.0) * 0.4;
            float x1 = x0 + 0.4 + floor(h * 2.0) * 0.4;
            ink = max(ink, step(abs(f.y - y), w * 0.8) * step(x0 - w, f.x) * step(f.x, x1 + w));
        }
    }
    if (hash12(cell + 91.0) > 0.8) {
        float r = length(f - vec2(0.5, 0.5));
        ink = max(ink, step(abs(r - 0.3), w * 0.7));
    }
    return ink;
}

void main()
{
    vec3 N = normalize(v_nrm);
    if (!gl_FrontFacing) N = -N;
    vec3 P = v_world;
    vec3 V = normalize(u_cam_pos - P);
    vec3 L = u_sun_dir;

    vec3 albedo = v_color;
    vec3 emissive = vec3(0.0);
    float spec = 0.0;
    float gloss = 16.0;
    float ao = 1.0;

    // Мелкий шум в мировых координатах, стабилен при движении
    float fine = noise3(P * 9.0);

    if (v_mat == MAT_CONCRETE) {
        float big = fbm3(P * 0.18);
        float mid = fbm3(P * 1.1 + 4.0);
        // Опалубка: швы между плитами
        vec2 pan = abs(fract(vec2(P.x + P.z, P.y) / vec2(1.6, 1.2)) - 0.5);
        float seam = smoothstep(0.485, 0.5, max(pan.x, pan.y));
        // Потёки сверху вниз на стенах
        float streak = noise3(vec3(P.x * 3.0, P.y * 0.15, P.z * 3.0));
        float vertical = 1.0 - abs(N.y);
        albedo *= 0.75 + 0.45 * big;
        albedo *= mix(1.0, 0.65 + 0.5 * streak, vertical * 0.8);
        albedo *= 1.0 - seam * 0.35 * vertical;
        // Плиты пола 4x4 м и пятна
        vec2 tile = abs(fract(P.xz / 4.0) - 0.5);
        float tseam = smoothstep(0.488, 0.5, max(tile.x, tile.y));
        float stain = smoothstep(0.45, 0.75, fbm3(vec3(P.xz * 0.35, 1.0)));
        albedo *= 1.0 - (tseam * 0.4 + stain * 0.3) * (1.0 - vertical);
        albedo *= 0.88 + 0.24 * mid;
        // Ржавые подтёки
        float rust = smoothstep(0.62, 0.8, mid * streak + 0.2 * big) * vertical;
        albedo = mix(albedo, vec3(0.30, 0.13, 0.05), rust * 0.6);
        albedo *= 0.92 + 0.16 * fine;
        // Грязь у основания стен
        if (u_skinned == 0) ao = mix(1.0, smoothstep(-0.2, 2.5, P.y), vertical * 0.6);
    } else if (v_mat == MAT_RUST) {
        float m = fbm3(P * 2.5);
        float flake = smoothstep(0.45, 0.6, fbm3(P * 7.0 + 3.0));
        albedo = mix(albedo, vec3(0.28, 0.10, 0.04), m * 0.8);
        albedo = mix(albedo, vec3(0.16, 0.15, 0.14), flake * 0.6);
        albedo *= 0.85 + 0.3 * fine;
        spec = 0.25 * flake;
        gloss = 24.0;
    } else if (v_mat == MAT_SAND) {
        float dunes = fbm3(vec3(P.x * 0.05, 0.0, P.z * 0.05));
        float ripples = noise3(vec3(P.x * 2.5 + P.z * 0.6, 0.0, P.z * 0.4));
        albedo *= 0.75 + 0.4 * dunes;
        albedo *= 0.9 + 0.15 * ripples;
        albedo *= 0.9 + 0.2 * fine;
    } else if (v_mat == MAT_CLOTH) {
        float weave = noise3(P * 40.0);
        float dirt = fbm3(P * 3.0);
        albedo *= 0.8 + 0.25 * weave;
        albedo *= 0.75 + 0.4 * dirt;
        // Подол и низ в пыли
        albedo = mix(albedo, vec3(0.33, 0.29, 0.22), smoothstep(1.0, 0.2, v_local_y) * 0.5);
    } else if (v_mat == MAT_LEATHER) {
        albedo *= 0.75 + 0.4 * fbm3(P * 6.0);
        spec = 0.15;
        gloss = 12.0;
    } else if (v_mat == MAT_SKIN) {
        albedo *= 0.9 + 0.15 * fine;
        spec = 0.05;
    } else if (v_mat == MAT_METAL) {
        float m = fbm3(P * 4.0);
        albedo *= 0.7 + 0.5 * m;
        spec = 0.6;
        gloss = 40.0;
    } else if (v_mat == MAT_GLYPH) {
        // Знаки на плите; цвет вершины — цвет свечения
        vec3 t = normalize(cross(vec3(0, 1, 0), N));
        if (abs(N.y) > 0.9) t = vec3(1, 0, 0);
        vec2 uv = vec2(dot(P, t), P.y);
        float ink = glyph_ink(uv);
        float flicker = 0.75 + 0.25 * step(0.15, noise3(vec3(u_time * 7.0, uv.y * 0.5, 0.0)));
        float dim = 0.6 + 0.4 * noise3(vec3(uv * 0.7, u_time * 0.3));
        albedo = vec3(0.07, 0.07, 0.065) * (0.8 + 0.4 * fbm3(P * 3.0));
        emissive = v_color * ink * flicker * dim * 1.1;
        albedo *= 1.0 - ink * 0.6;
    } else if (v_mat == MAT_EMISSIVE) {
        float flicker = 0.85 + 0.15 * noise3(vec3(u_time * 11.0, P.x, P.z));
        emissive = v_color * 3.0 * flicker;
        albedo = vec3(0.05);
    }

    float ndl = max(dot(N, L), 0.0);
    float sh = ndl > 0.0 ? sun_shadow(P, N) : 0.0;

    // Небо и отражённый от песка свет
    vec3 sky = vec3(0.16, 0.16, 0.15);
    vec3 ground = vec3(0.20, 0.15, 0.09);
    vec3 ambient = mix(ground, sky, N.y * 0.5 + 0.5) * 0.9;
    vec3 Lflat = normalize(vec3(-L.x, 0.0, -L.z));
    float bounce = max(dot(N, -Lflat), 0.0) * 0.08 + max(-N.y, 0.0) * 0.05;
    ambient += vec3(0.30, 0.22, 0.12) * bounce;

    vec3 H = normalize(L + V);
    float sp = spec * pow(max(dot(N, H), 0.0), gloss) * sh;

    vec3 col = albedo * (u_sun_color * ndl * sh + ambient * ao) + u_sun_color * sp + emissive;
    // Контровой свет от дымки: фигуры читаются силуэтом
    if (u_skinned != 0) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        col += vec3(0.36, 0.31, 0.23) * rim * 0.35;
    }
    o_color = vec4(col, 1.0);
}
