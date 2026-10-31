// Карта частоты шейдинга (VK_KHR_fragment_shading_rate): клетка карты — самая мелкая частота её блоков 4x4.
// Уровень 0..2 — блок 1, 2, 4 пикселя; в карте Vulkan размер (log2 w << 2) | log2 h, то есть уровень * 5

layout(binding = 14) uniform usampler2D u_block;  // vrs_block.frag

layout(location = 0) out uint o_rate;

void main()
{
    ivec2 b0 = ivec2(gl_FragCoord.xy) * u_tile.xy;
    ivec2 size = textureSize(u_block, 0);
    uint r = 2u;
    for (int y = 0; y < u_tile.y; y++)
        for (int x = 0; x < u_tile.x; x++) {
            ivec2 b = b0 + ivec2(x, y);
            if (b.x < size.x && b.y < size.y) r = min(r, texelFetch(u_block, b, 0).r);
        }
    o_rate = r * 5u;
}
