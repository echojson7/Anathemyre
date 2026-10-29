// Правило пиксельности по глубине: общее для фильтра кадра (style.frag) и карты частоты шейдинга (vrs.frag)

layout(location = 44) uniform vec4 u_pix;   // размер пикселя k = x * (1/d)^y; m10, m14 проекции
layout(binding = 1) uniform sampler2D u_depth;

const int PIXEL_MAX = 8;
const int PIXEL_MIN = 2;  // вдали пиксель не мельче 2x2 и дальше не меняется

// Размер пикселя в точке, пикселей кадра: растёт к камере, вдали PIXEL_MIN (1, если пиксельность выключена)
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = max((z + u_pix.z) / u_pix.w, 0.0);
    float lo = u_pix.x > 0.0 ? float(PIXEL_MIN) : 1.0;
    return int(clamp(floor(u_pix.x * pow(inv_d, u_pix.y) + 0.5), lo, float(PIXEL_MAX)));
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

// Цвет ячейки — из ближайшей к камере из четырёх точек: тонкие прутья не рвутся в пунктир
ivec2 pixel_src(ivec2 p)
{
    int k = pixel_k(p);
    if (k == 1) return p;
    ivec2 size = textureSize(u_depth, 0) - 1;
    ivec2 o = (p / k) * k;
    int a = k / 4, b = k - 1 - k / 4;
    ivec2 best = min(o + ivec2(a, a), size);
    float bd = texelFetch(u_depth, best, 0).r;
    ivec2 c[3] = ivec2[3](ivec2(b, a), ivec2(a, b), ivec2(b, b));
    for (int i = 0; i < 3; i++) {
        ivec2 q = min(o + c[i], size);
        float d = texelFetch(u_depth, q, 0).r;
        if (d < bd) { bd = d; best = q; }
    }
    return best;
}
