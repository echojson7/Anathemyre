// Свечение (СВЕТ: BLOOM), спуск: каждая ступень вдвое меньше, 13 выборок с перекрытием (без порога:
// светится всё, но слабо, как рассеяние в глазу и линзе). Первая ступень гасит одиночные яркие точки
layout(location = 0) in vec2 v_uv;

layout(binding = 28) uniform sampler2D u_bloom_src;

layout(location = 0) out vec4 o_color;

#define u_bloom_first (u_push[0].x != 0u)

vec3 at(vec2 o) { return textureLod(u_bloom_src, v_uv + o, 0.0).rgb; }

// Вес группы по яркости (среднее Кариса): точка ярче соседей не раздувается в пятно
float karis(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }

void main()
{
    vec2 t = 1.0 / vec2(textureSize(u_bloom_src, 0));
    vec3 a = at(t * vec2(-2, 2)), b = at(t * vec2(0, 2)), c = at(t * vec2(2, 2));
    vec3 d = at(t * vec2(-2, 0)), e = at(vec2(0)), f = at(t * vec2(2, 0));
    vec3 g = at(t * vec2(-2, -2)), h = at(t * vec2(0, -2)), i = at(t * vec2(2, -2));
    vec3 j = at(t * vec2(-1, 1)), k = at(t * vec2(1, 1)), l = at(t * vec2(-1, -1)), m = at(t * vec2(1, -1));
    vec3 g0 = (j + k + l + m) * 0.25, g1 = (a + b + d + e) * 0.25, g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25, g4 = (e + f + h + i) * 0.25;
    vec3 o;
    if (u_bloom_first) {
        float w0 = karis(g0) * 0.5, w1 = karis(g1) * 0.125, w2 = karis(g2) * 0.125, w3 = karis(g3) * 0.125, w4 = karis(g4) * 0.125;
        o = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    } else {
        o = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
    }
    // Пустое небо и NaN не должны тянуть за собой ступени
    o = clamp(o, vec3(0.0), vec3(64.0));
    o_color = vec4(o, 1.0);
}
