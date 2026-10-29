// Правило пиксельности по глубине: общее для фильтра кадра (style.frag) и карты частоты шейдинга (vrs.frag)

layout(location = 44) uniform vec4 u_pix;   // пикселей кадра на PIXEL_M и на PIXEL_FAR на 1 м; m10, m14 проекции
layout(binding = 1) uniform sampler2D u_depth;

// Размер пикселя в точке, пикселей кадра: 4 у самой камеры (пиксель мира не мельче PIXEL_M),
// 2 обычно, 1 вдали, где блок 2x2 был бы крупнее PIXEL_FAR в мире. Сетки 4/2/1 вложены
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = (z + u_pix.z) / u_pix.w;
    if (u_pix.x * inv_d >= 4.0) return 4;
    return u_pix.y * inv_d >= 2.0 ? 2 : 1;
}

// Одно правило для всего кадра: ячейка k×k берёт цвет своего центра, если там пиксель не мельче k.
// Возвращает k ячейки, в которую попал пиксель p (1 — пиксель берёт свой цвет)
int pixel_k(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    for (int k = 4; k >= 2; k /= 2)
        if (level_at(min((p / k) * k + k / 2, size)) >= k) return k;
    return 1;
}

ivec2 pixel_src(ivec2 p)
{
    int k = pixel_k(p);
    return k == 1 ? p : min((p / k) * k + k / 2, textureSize(u_depth, 0) - 1);
}
