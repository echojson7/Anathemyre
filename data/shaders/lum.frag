// Яркость кадра: уменьшенная копия (rgb — цвет, a — log2 яркости) и привыкание глаза.
// Мипы копии дают среднюю яркость кадра, размытую яркость и свечение

layout(binding = 0) uniform sampler2D u_src;
#ifdef ADAPT
layout(location = 45) uniform float u_dt;      // < 0 — привыкнуть сразу
layout(location = 46) uniform float u_level;   // мип 1×1
layout(binding = 1) uniform sampler2D u_prev;
#endif

out vec4 o_color;

void main()
{
#ifdef ADAPT
    float ev = textureLod(u_src, vec2(0.5), u_level).a;
    float prev = texelFetch(u_prev, ivec2(0), 0).r;
    float k = 1.0 - exp(-u_dt * 1.5);
    o_color = vec4(u_dt < 0.0 ? ev : mix(prev, ev, k));
#else
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;
    ivec2 m = textureSize(u_src, 0) - 1;
    vec3 c = (texelFetch(u_src, min(p, m), 0).rgb + texelFetch(u_src, min(p + ivec2(1, 0), m), 0).rgb +
              texelFetch(u_src, min(p + ivec2(0, 1), m), 0).rgb + texelFetch(u_src, min(p + ivec2(1, 1), m), 0).rgb) * 0.25;
    o_color = vec4(c, log2(dot(c, vec3(0.2126, 0.7152, 0.0722)) + 1e-4));
#endif
}
