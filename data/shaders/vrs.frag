// Карта частоты шейдинга (GL_NV_shading_rate_image): клетка карты считается блоками 4x4 или 2x2,
// только если все её пиксели фильтр всё равно сольёт в ячейки, кратные блоку (pixel.glsl). Иначе — каждый пиксель

layout(location = 45) uniform ivec2 u_tile;  // клетка карты в пикселях кадра

out uint o_rate;  // 0 — каждый пиксель, 1 — 2x2, 2 — 4x4 (палитра в render.c)

void main()
{
    ivec2 t0 = ivec2(gl_FragCoord.xy) * u_tile;
    ivec2 size = textureSize(u_depth, 0);
    int k = 4;
    // Блок шейдинга (выровнен по своей сетке) не должен пересекать границу ячеек пиксельности
    for (int y = 0; y < u_tile.y && k > 1; y++)
        for (int x = 0; x < u_tile.x && k > 1; x++) {
            ivec2 p = t0 + ivec2(x, y);
            if (p.x >= size.x || p.y >= size.y) continue;
            int c = pixel_k(p);
            k = min(k, c % 4 == 0 ? 4 : (c % 2 == 0 ? 2 : 1));
        }
    o_rate = k == 4 ? 2u : (k == 2 ? 1u : 0u);
}
