// Правило пиксельности по глубине. Источник цвета каждого пикселя считается раз за кадр (src_map.frag),
// по нему берёт цвет фильтр кадра (style.frag) и строится карта частоты шейдинга (vrs_block.frag)

layout(location = 44) uniform vec4 u_pix;   // размер пикселя k = x * (1/d)^y; m10, m14 проекции
layout(binding = 1) uniform sampler2D u_depth;

// Размеры ячеек: шаг 0.5 пикселя кадра, ниже 2 — шаг 0.25; храним их в четвертях: q = 4k
const int PIXEL_MAX_Q = 32;  // 8 пикселей
const int PIXEL_MIN_Q = 5;   // вдали 1.25 и дальше не меняется

// Размер пикселя в точке, в четвертях пикселя кадра: растёт к камере, вдали PIXEL_MIN_Q (4, если пиксельность выключена)
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = max((z + u_pix.z) / u_pix.w, 0.0);
    float k = u_pix.x * pow(inv_d, u_pix.y);
    float q = k >= 2.0 ? 2.0 * floor(2.0 * k + 0.5) : floor(4.0 * k + 0.5);
    float lo = u_pix.x > 0.0 ? float(PIXEL_MIN_Q) : 4.0;
    return int(clamp(q, lo, float(PIXEL_MAX_Q)));
}

// Ячейка размера q/4 по одной оси: начало и ширина ячейки, в которую попала координата x.
// Границы на floor(i·k), поэтому при дробном k ширины чередуются (1.5 — 1 и 2)
ivec2 cell_axis(int x, int q)
{
    int c = (4 * x + 3) / q;
    int x0 = c * q / 4;
    return ivec2(x0, (c + 1) * q / 4 - x0);
}

// Одно правило для всего кадра: ячейка берёт цвет своего центра, если там пиксель не мельче ячейки.
// Возвращает q ячейки, в которую попал пиксель p (4 — пиксель берёт свой цвет)
int pixel_q(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    for (int q = PIXEL_MAX_Q; q >= PIXEL_MIN_Q; q -= q > 8 ? 2 : 1) {
        ivec2 cx = cell_axis(p.x, q), cy = cell_axis(p.y, q);
        if (level_at(min(ivec2(cx.x + cx.y / 2, cy.x + cy.y / 2), size)) >= q) return q;
    }
    return 4;
}

// Цвет ячейки — из ближайшей к камере из четырёх точек: тонкие прутья не рвутся в пунктир
ivec2 pixel_src(ivec2 p)
{
    int q = pixel_q(p);
    if (q == 4) return p;
    ivec2 size = textureSize(u_depth, 0) - 1;
    ivec2 cx = cell_axis(p.x, q), cy = cell_axis(p.y, q);
    ivec2 o = ivec2(cx.x, cy.x);
    ivec2 a = ivec2(cx.y / 4, cy.y / 4), b = ivec2(cx.y - 1 - cx.y / 4, cy.y - 1 - cy.y / 4);
    ivec2 best = min(o + a, size);
    float bd = texelFetch(u_depth, best, 0).r;
    ivec2 c[3] = ivec2[3](ivec2(b.x, a.y), ivec2(a.x, b.y), b);
    for (int i = 0; i < 3; i++) {
        ivec2 q = min(o + c[i], size);
        float d = texelFetch(u_depth, q, 0).r;
        if (d < bd) { bd = d; best = q; }
    }
    return best;
}
