// Правило пиксельности по глубине: общее для фильтра кадра (style.frag) и карты частоты шейдинга (vrs.frag)

layout(location = 44) uniform vec4 u_pix;   // размер пикселя k = x * (1/d)^y; m10, m14 проекции
layout(binding = 1) uniform sampler2D u_depth;

const int PIXEL_MAX = 8;

// Размер пикселя в точке, пикселей кадра: плавно растёт к камере, вдали 1
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = max((z + u_pix.z) / u_pix.w, 0.0);
    return int(clamp(floor(u_pix.x * pow(inv_d, u_pix.y) + 0.5), 1.0, float(PIXEL_MAX)));
}

// Одно правило для всего кадра: ячейка k×k берёт цвет своего центра, если там пиксель не мельче k.
// Возвращает k ячейки, в которую попал пиксель p (1 — пиксель берёт свой цвет)
int pixel_k(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    for (int k = PIXEL_MAX; k >= 2; k--)
        if (level_at(min((p / k) * k + k / 2, size)) >= k) return k;
    return 1;
}

ivec2 pixel_src(ivec2 p)
{
    int k = pixel_k(p);
    return k == 1 ? p : min((p / k) * k + k / 2, textureSize(u_depth, 0) - 1);
}
