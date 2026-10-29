// Фильтры кадра: автоэкспозиция, локальный контраст, свечение, цвет, виньетка, зерно

in vec2 v_uv;

layout(location = 40) uniform float u_time;
layout(location = 41) uniform int u_filter;   // 0 — без фильтра, 1 — первый, дальше LOOKS[u_filter - 2]
layout(location = 42) uniform int u_frame;
layout(binding = 0) uniform sampler2D u_src;
layout(binding = 1) uniform sampler2D u_lum;  // уменьшенный кадр, мипы — размытие
layout(binding = 2) uniform sampler2D u_ev;   // привыкшая средняя яркость, log2

out vec4 o_color;

const float REF_EV = -2.7;   // средняя яркость дневной улицы: на ней экспозиция ×1
const float LOCAL_LOD = 4.0; // мип размытой яркости для локального контраста

struct Look {
    float base_k;    // перепад крупных областей: < 1 — тёмные места светлеют
    float detail_k;  // мелкий контраст: > 1 — тени у предметов и в углах глубже
    float contrast;  // общий контраст вокруг средне-серого
    float sat;
    float mist;      // мягкая дымка поверх кадра (как пыль на объективе)
    float bloom;     // свечение ярких мест
    vec3  glow;      // цвет свечения: красноватый — ореол плёнки
    vec3  shadows, highs;  // оттенок теней и светов
    float split;
    vec3  lift;      // цвет, до которого подняты самые глубокие тени
    float vignette;
};

const Look LOOKS[4] = Look[4](
    // 1. Естественный: цвет как без фильтра, глубина от локального контраста
    Look(0.85, 1.45, 1.18, 1.10, 0.02, 0.25, vec3(1.0, 0.95, 0.85),
         vec3(0.96, 0.99, 1.03), vec3(1.03, 1.0, 0.96), 0.5, vec3(0.006), 0.40),
    // 2. Охра: янтарный свет, тени коричневые, пыльная дымка
    Look(0.85, 1.40, 1.22, 1.00, 0.06, 0.40, vec3(1.0, 0.75, 0.45),
         vec3(0.95, 0.92, 0.92), vec3(1.16, 1.0, 0.70), 1.0, vec3(0.022, 0.014, 0.008), 0.45),
    // 3. Закат: оранжевые света, бирюзовые тени, красный ореол
    Look(0.85, 1.40, 1.25, 1.15, 0.03, 0.55, vec3(1.0, 0.45, 0.20),
         vec3(0.74, 1.0, 1.12), vec3(1.25, 0.97, 0.68), 1.0, vec3(0.004, 0.012, 0.016), 0.45),
    // 4. Плёнка: плотный контраст, глубокие чёрные, насыщенная ржавчина, зеленоватые тени
    Look(0.90, 1.35, 1.38, 1.15, 0.02, 0.40, vec3(1.0, 0.40, 0.25),
         vec3(0.90, 1.04, 0.98), vec3(1.10, 1.02, 0.84), 0.9, vec3(0.012, 0.014, 0.010), 0.50)
);

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float bayer4(vec2 p)
{
    ivec2 i = ivec2(mod(p, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(m[i.y * 4 + i.x]) + 0.5) / 16.0;
}

// Самый первый фильтр игры: аберрация, грязный грейд, виньетка, зерно, сканлайны, дизеринг, помехи
vec3 first_filter(vec2 uv)
{
    vec2 res = vec2(textureSize(u_src, 0));
    // Редкие помехи: сдвиг строк
    float row = floor(uv.y * res.y / 3.0);
    float tick = floor(u_time * 14.0);
    float glitch = step(0.996, hash12(vec2(row, tick)));
    uv.x += glitch * (hash12(vec2(tick, row + 3.0)) - 0.5) * 0.04;

    // Хроматическая аберрация к краям
    vec2 off = (uv - 0.5) * 0.0025;
    vec3 c;
    c.r = texture(u_src, uv + off).r;
    c.g = texture(u_src, uv).g;
    c.b = texture(u_src, uv - off).b;

    c = aces(c * 1.05);

    // Грейд: приглушить, тени в холодный серый, света в болезненно-жёлтый
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, 0.72);
    c *= mix(vec3(0.88, 0.93, 0.95), vec3(1.06, 1.0, 0.80), smoothstep(0.05, 0.7, l));
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.6);
    c = max(c - 0.015, 0.0) * 1.03;

    vec2 q = uv - 0.5;
    c *= 1.0 - 0.75 * pow(length(q * vec2(1.1, 1.3)), 2.4);
    c = pow(max(c, 0.0), vec3(1.0 / 2.2));

    float n = hash12(gl_FragCoord.xy + vec2(float(u_frame % 97) * 13.1, float(u_frame % 89) * 7.7)) - 0.5;
    c += n * 0.10 * (1.0 - 0.5 * l);

    // Сканлайны и ограниченная палитра с упорядоченным дизерингом
    c *= 1.0 - 0.05 * mod(gl_FragCoord.y, 2.0);
    float levels = 24.0;
    return floor(c * levels + bayer4(gl_FragCoord.xy)) / levels;
}

void main()
{
    vec2 uv = v_uv;
    vec3 c = texture(u_src, uv).rgb;

    if (u_filter == 0) {
        c = aces(c * 1.2);
        o_color = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
        return;
    }
    if (u_filter == 1) {
        o_color = vec4(clamp(first_filter(uv), 0.0, 1.0), 1.0);
        return;
    }
    Look k = LOOKS[u_filter - 2];

    // Автоэкспозиция: глаз привыкает к темноте, но не до конца — ночь остаётся ночью
    float ev = texelFetch(u_ev, ivec2(0), 0).r;
    float expo = exp2(clamp((REF_EV - ev) * 0.5, -1.0, 0.6)) * 1.2;

    // Локальный контраст: крупный перепад сжимается, мелкий усиливается
    float L = log2(luma(c) + 1e-4);
    float base = textureLod(u_lum, uv, LOCAL_LOD).a;
    float L2 = ev + (base - ev) * k.base_k + (L - base) * k.detail_k;
    L2 = ev + (L2 - ev) * k.contrast;
    c *= exp2(L2 - L) * expo;

    // Свечение: широкое размытие кадра; дымка подмешивается целиком, свечение — только от ярких мест
    vec3 glow = (textureLod(u_lum, uv, 1.5).rgb * 0.4 + textureLod(u_lum, uv, 3.0).rgb * 0.35 +
                 textureLod(u_lum, uv, 4.5).rgb * 0.25) * expo;
    c = mix(c, glow, k.mist);
    c += max(glow - 0.9, 0.0) * k.bloom * k.glow;

    float l = luma(c);
    c = max(vec3(l) + (c - l) * k.sat, 0.0);
    c = aces(c);

    // Оттенок теней и светов, подъём чёрного
    float lt = smoothstep(0.0, 0.8, luma(c));
    c *= mix(vec3(1.0), mix(k.shadows, k.highs, lt), k.split);
    c = c + k.lift * (1.0 - c);

    vec2 q = uv - 0.5;
    c *= 1.0 - k.vignette * pow(length(q * vec2(1.1, 1.3)), 2.4);
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));

    // Плёночное зерно, сильнее в тенях
    float n = hash12(gl_FragCoord.xy + vec2(float(u_frame % 97) * 13.1, float(u_frame % 89) * 7.7)) - 0.5;
    c += n * 0.09 * (1.0 - 0.5 * luma(c));

    o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
