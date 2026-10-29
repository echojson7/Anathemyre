// Правило пиксельности по глубине. Источник цвета каждого пикселя считается раз за кадр (src_map.frag),
// по нему берёт цвет фильтр кадра (style.frag) и строится карта частоты шейдинга (vrs_block.frag)

layout(location = 44) uniform vec4 u_pix;   // размер пикселя k = x * (1/d)^y; m10, m14 проекции
layout(binding = 1) uniform sampler2D u_depth;

// Размеры ячеек идут шагом 0.5 пикселя кадра; храним их в половинках: h = 2k
const int PIXEL_MAX_H = 16;  // 8 пикселей
const int PIXEL_MIN_H = 3;   // вдали 1.5 и дальше не меняется

// Размер пикселя в точке, в половинках пикселя кадра: растёт к камере, вдали PIXEL_MIN_H (2, если пиксельность выключена)
int level_at(ivec2 t)
{
    float z = texelFetch(u_depth, t, 0).r * 2.0 - 1.0;
    float inv_d = max((z + u_pix.z) / u_pix.w, 0.0);
    float lo = u_pix.x > 0.0 ? float(PIXEL_MIN_H) : 2.0;
    return int(clamp(floor(2.0 * u_pix.x * pow(inv_d, u_pix.y) + 0.5), lo, float(PIXEL_MAX_H)));
}

// Ячейка размера h/2 по одной оси: начало и ширина ячейки, в которую попала координата x.
// Границы на floor(i·k), поэтому при дробном k ширины чередуются (1.5 — 1 и 2)
ivec2 cell_axis(int x, int h)
{
    int c = (2 * x + 1) / h;
    int x0 = c * h / 2;
    return ivec2(x0, (c + 1) * h / 2 - x0);
}

// Одно правило для всего кадра: ячейка берёт цвет своего центра, если там пиксель не мельче ячейки.
// Возвращает h ячейки, в которую попал пиксель p (2 — пиксель берёт свой цвет)
int pixel_h(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    for (int h = PIXEL_MAX_H; h >= 3; h--) {
        ivec2 cx = cell_axis(p.x, h), cy = cell_axis(p.y, h);
        if (level_at(min(ivec2(cx.x + cx.y / 2, cy.x + cy.y / 2), size)) >= h) return h;
    }
    return 2;
}

// Цвет ячейки — из ближайшей к камере из четырёх точек: тонкие прутья не рвутся в пунктир
ivec2 pixel_src(ivec2 p)
{
    int h = pixel_h(p);
    if (h == 2) return p;
    ivec2 size = textureSize(u_depth, 0) - 1;
    ivec2 cx = cell_axis(p.x, h), cy = cell_axis(p.y, h);
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
