// Пиксельность по глубине и фильтр кадра: цвет без грейда, сверху виньетка, зерно, сканлайны и аберрация к краям

in vec2 v_uv;

layout(location = 41) uniform int u_post;
layout(location = 42) uniform int u_frame;
layout(location = 44) uniform vec4 u_pix;   // пикселей кадра на PIXEL_M и на PIXEL_FAR на 1 м; m10, m14 проекции
layout(binding = 0) uniform sampler2D u_src;
layout(binding = 1) uniform sampler2D u_depth;

out vec4 o_color;

vec3 aces(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Размер пикселя в точке, пикселей кадра: 4 у самой камеры (пиксель мира не мельче PIXEL_M),
// 2 обычно, 1 вдали, где блок 2x2 был бы крупнее PIXEL_FAR в мире. Сетки 4/2/1 вложены
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = (z + u_pix.z) / u_pix.w;
    if (u_pix.x * inv_d >= 4.0) return 4;
    return u_pix.y * inv_d >= 2.0 ? 2 : 1;
}

// Одно правило для всего кадра: ячейка k×k берёт цвет своего центра, если там пиксель не мельче k
ivec2 pixel_src(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    for (int k = 4; k >= 2; k /= 2) {
        ivec2 c = min((p / k) * k + k / 2, size);
        if (level_at(c) >= k) return c;
    }
    return p;
}

void main()
{
    vec2 uv = (vec2(pixel_src(ivec2(gl_FragCoord.xy))) + 0.5) / vec2(textureSize(u_src, 0));
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
