// Карта частоты шейдинга (GL_NV_shading_rate_image): блок 2x2 или 4x4 шейдится один раз,
// только если все его пиксели берут цвет из одного центра ячейки (pixel_src, pixel.glsl). Иначе — каждый пиксель

layout(location = 45) uniform ivec2 u_tile;  // клетка карты в пикселях кадра

out uint o_rate;  // 0 — каждый пиксель, 1 — 2x2, 2 — 4x4 (палитра в render.c)

void main()
{
    ivec2 t0 = ivec2(gl_FragCoord.xy) * u_tile;
    ivec2 size = textureSize(u_depth, 0);
    int k = 4;
    // Выровненные блоки 4x4, в них блоки 2x2: у всех пикселей блока pixel_src должен совпадать
    for (int by = 0; by < u_tile.y && k > 1; by += 4)
        for (int bx = 0; bx < u_tile.x && k > 1; bx += 4) {
            ivec2 s4 = pixel_src(min(t0 + ivec2(bx, by), size - 1));
            for (int cy = 0; cy < 4 && k > 1; cy += 2)
                for (int cx = 0; cx < 4 && k > 1; cx += 2) {
                    ivec2 s2 = pixel_src(min(t0 + ivec2(bx + cx, by + cy), size - 1));
                    if (s2 != s4) k = min(k, 2);
                    for (int i = 1; i < 4; i++) {
                        ivec2 p = min(t0 + ivec2(bx + cx + (i & 1), by + cy + (i >> 1)), size - 1);
                        if (pixel_src(p) != s2) k = 1;
                    }
                }
        }
    o_rate = k == 4 ? 2u : (k == 2 ? 1u : 0u);
}
