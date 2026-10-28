// Стилизация: аберрация, тонмаппинг, грязный грейд, виньетка, зерно, дизеринг, помехи

in vec2 v_uv;

layout(location = 40) uniform float u_time;
layout(location = 41) uniform int u_post;
layout(location = 42) uniform int u_frame;
layout(binding = 0) uniform sampler2D u_src;

out vec4 o_color;

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

void main()
{
    vec2 uv = v_uv;
    vec2 res = vec2(textureSize(u_src, 0));

    if (u_post == 0) {
        vec3 c = aces(texture(u_src, uv).rgb * 1.2);
        o_color = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
        return;
    }

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

    // Виньетка
    vec2 q = uv - 0.5;
    c *= 1.0 - 0.75 * pow(length(q * vec2(1.1, 1.3)), 2.4);

    c = pow(max(c, 0.0), vec3(1.0 / 2.2));

    // Плёночное зерно, сильнее в тенях
    float n = hash12(gl_FragCoord.xy + vec2(float(u_frame % 97) * 13.1, float(u_frame % 89) * 7.7)) - 0.5;
    c += n * 0.10 * (1.0 - 0.5 * l);

    // Сканлайны
    c *= 1.0 - 0.05 * mod(gl_FragCoord.y, 2.0);

    // Ограниченная палитра с упорядоченным дизерингом
    float levels = 24.0;
    c = floor(c * levels + bayer4(gl_FragCoord.xy)) / levels;

    o_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
