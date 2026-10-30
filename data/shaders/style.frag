// Пиксельность по глубине и фильтр кадра: цвет без грейда, сверху виньетка, зерно, сканлайны и аберрация к краям

in vec2 v_uv;

layout(location = 41) uniform int u_post;
layout(location = 42) uniform int u_frame;
layout(binding = 0) uniform sampler2D u_src;
layout(binding = 2) uniform usampler2D u_src_map;  // откуда пиксель берёт цвет, src_map.frag

out vec4 o_color;

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    // Без пиксельности (u_pix.x = 0) карта источников не считается: пиксель берёт свой цвет
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 src = u_pix.x > 0.0 ? vec2(texelFetch(u_src_map, p, 0).rg) : vec2(p);
    vec2 uv = (src + 0.5) / vec2(textureSize(u_src, 0));
    if (u_post == 0) {
        o_color = vec4(pow(aces(texture(u_src, uv).rgb * 1.2), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    // Хроматическая аберрация к краям
    vec2 off = (uv - 0.5) * 0.0010;
    vec3 c;
    c.r = texture(u_src, uv + off).r;
    c.g = texture(u_src, uv).g;
    c.b = texture(u_src, uv - off).b;
    c = aces(c * 1.2);
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
