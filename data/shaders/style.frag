// Пиксельность по глубине и фильтр кадра: цвет без грейда, сверху виньетка, зерно, сканлайны и аберрация к краям

in vec2 v_uv;

layout(binding = 8) uniform sampler2D u_src;
layout(binding = 11) uniform usampler2D u_src_map;  // откуда пиксель берёт цвет, src_map.frag

layout(binding = 30) uniform sampler2D u_bloom;     // свечение: сумма ступеней, bloom_up.frag
layout(std430, binding = 27) readonly buffer Expo { float u_expo, u_expo_lum, u_expo_init, u_expo_pad; };  // expo.comp

out vec4 o_color;

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// AgX (приближение Вренша) с контрастом «Punchy» (степень 1.35): плёночная кривая, яркое уходит в белый
// без сдвига оттенка, тени не задавлены, как у ACES
vec3 agx(vec3 x)
{
    const mat3 m = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                        0.0784335999999992, 0.878468636469772, 0.0784336,
                        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 mi = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                         -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                         -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float lo = -12.47393, hi = 4.026069;
    x = clamp(log2(max(m * x, vec3(1e-10))), lo, hi);
    x = (x - lo) / (hi - lo);
    vec3 x2 = x * x, x4 = x2 * x2;
    x = 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
    x = pow(max(x, vec3(0.0)), vec3(1.35));
    return clamp(pow(max(mi * x, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
}

// Khronos PBR Neutral: до 0.76 цвет как есть, выше мягкое сжатие к белому
vec3 neutral(vec3 c)
{
    float x = min(c.r, min(c.g, c.b));
    c -= x < 0.08 ? x - 6.25 * x * x : 0.04;
    float peak = max(c.r, max(c.g, c.b));
    if (peak < 0.76) return max(c, vec3(0.0));
    const float d = 0.24;
    float np = 1.0 - d * d / (peak + d - 0.76);
    c *= np / peak;
    return mix(c, vec3(np), 1.0 - 1.0 / (0.15 * (peak - np) + 1.0));
}

// Кривая группы СВЕТ. Множители AgX и Neutral подобраны так, что яркость 0.1 (обычная для наших
// кадров, сумрачных) выходит той же, что у ACES: кривые сравниваются по форме, а не по яркости (docs/lighting.md)
vec3 tonemap(vec3 x)
{
    // Прежний путь отдельно: постоянная 1.2 сворачивается в кривую, и кадр остаётся бит в бит
    if (u_light.y == 0 && u_light.z == 0) return aces(x * 1.2);
    float e = u_light.z != 0 ? u_expo : 1.2;
    if (u_light.y == 1) return agx(x * (e * 1.809));
    if (u_light.y == 2) return neutral(x * (e * 1.658));
    return aces(x * e);
}

// Свечение поверх кадра до кривой: доля u_lightf.y от суммы ступеней
vec3 bloom(vec3 c, vec2 uv)
{
    return u_light.w != 0 ? c + (textureLod(u_bloom, uv, 0.0).rgb - c * u_lightf.z) * u_lightf.y : c;
}

void main()
{
    // Без пиксельности (u_pix.x = 0) карта источников не считается: пиксель берёт свой цвет
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 src = u_pix.x > 0.0 ? vec2(texelFetch(u_src_map, p, 0).rg) : vec2(p);
    vec2 uv = (src + 0.5) / vec2(textureSize(u_src, 0));
    if (u_post == 0) {
        o_color = vec4(pow(tonemap(bloom(texture(u_src, uv).rgb, uv)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    // Хроматическая аберрация к краям
    vec2 off = (uv - 0.5) * 0.0010;
    vec3 c;
    c.r = texture(u_src, uv + off).r;
    c.g = texture(u_src, uv).g;
    c.b = texture(u_src, uv - off).b;
    c = tonemap(bloom(c, uv));
    float l = dot(c, vec3(0.299, 0.587, 0.114));

    // Виньетка
    vec2 q = uv - 0.5;
    c *= 1.0 - 0.35 * pow(length(q * vec2(1.1, 1.3)), 2.4);
    c = pow(c, vec3(1.0 / 2.2));

    // Плёночное зерно, сильнее в тенях
    float n = hash12(gl_FragCoord.xy + vec2(float(u_frame % 97) * 13.1, float(u_frame % 89) * 7.7)) - 0.5;
    c += n * 0.05 * (1.0 - 0.5 * l);

    // Сканлайны: каждая вторая строка чуть темнее
    c *= 1.0 - 0.01 * mod(gl_FragCoord.y, 2.0);

    o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
