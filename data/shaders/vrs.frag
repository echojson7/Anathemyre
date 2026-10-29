// Карта частоты шейдинга (GL_NV_shading_rate_image): клетка карты считается блоками 4x4 или 2x2,
// только если все её пиксели фильтр всё равно сольёт в такие блоки (pixel.glsl). Иначе — каждый пиксель

layout(location = 45) uniform ivec2 u_tile;  // клетка карты в пикселях кадра

out uint o_rate;  // 0 — каждый пиксель, 1 — 2x2, 2 — 4x4 (палитра в render.c)

void main()
{
    ivec2 t0 = ivec2(gl_FragCoord.xy) * u_tile;
    ivec2 size = textureSize(u_depth, 0);
    int k = 4;
    // Внутри ячейки 2x2 выбор k одинаков, поэтому хватает одного пикселя на ячейку
    for (int y = 0; y < u_tile.y && k > 1; y += 2)
        for (int x = 0; x < u_tile.x && k > 1; x += 2) {
            ivec2 p = t0 + ivec2(x, y);
            if (p.x < size.x && p.y < size.y) k = min(k, pixel_k(p));
        }
    o_rate = k == 4 ? 2u : (k == 2 ? 1u : 0u);
}
