// Правило пиксельности по глубине. Размер пикселя каждой точки считается раз за кадр (level.frag, level_max.frag),
// по нему источник цвета каждого пикселя (src_map.frag), откуда берёт цвет фильтр кадра (style.frag)
// и строится карта частоты шейдинга (vrs_block.frag)

layout(location = 44) uniform vec4 u_pix;   // размер пикселя k = x * (1/d)^y; m10, m14 проекции
layout(location = 46) uniform int u_pix_int;  // 1 — только целые вложенные размеры 1, 2, 4, 8 (сравнение)
layout(binding = 1) uniform sampler2D u_depth;
layout(binding = 4) uniform usampler2D u_level_map;  // level_at каждого пикселя, level.frag
layout(binding = 5) uniform usampler2D u_level_max;  // самый крупный level в блоке 8x8, level_max.frag

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
    if (u_pix_int != 0) return u_pix.x > 0.0 ? 4 << clamp(int(floor(log2(max(k, 1e-6)) + 0.5)), 0, 3) : 4;
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
// Центр ячейки не дальше 4 пикселей от p, поэтому перебор начинается с самого крупного level в этой окрестности:
// более крупные ячейки всё равно не прошли бы проверку
int pixel_q(ivec2 p)
{
    ivec2 size = textureSize(u_depth, 0) - 1;
    ivec2 b0 = max(p - 4, 0) / 8, b1 = min(p + 4, size) / 8;
    uint m = max(max(texelFetch(u_level_max, b0, 0).r, texelFetch(u_level_max, ivec2(b1.x, b0.y), 0).r),
                 max(texelFetch(u_level_max, ivec2(b0.x, b1.y), 0).r, texelFetch(u_level_max, b1, 0).r));
    int q0 = min(int(m), PIXEL_MAX_Q);
    if (u_pix_int != 0) {
        for (int q = q0; q >= 8; q /= 2) {
            ivec2 cx = cell_axis(p.x, q), cy = cell_axis(p.y, q);
            if (int(texelFetch(u_level_map, min(ivec2(cx.x + cx.y / 2, cy.x + cy.y / 2), size), 0).r) >= q) return q;
        }
        return 4;
    }
    for (int q = q0 > 8 ? q0 & ~1 : q0; q >= PIXEL_MIN_Q; q -= q > 8 ? 2 : 1) {
        ivec2 cx = cell_axis(p.x, q), cy = cell_axis(p.y, q);
        if (int(texelFetch(u_level_map, min(ivec2(cx.x + cx.y / 2, cy.x + cy.y / 2), size), 0).r) >= q) return q;
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
