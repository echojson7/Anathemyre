// Частота шейдинга для выровненного блока 4x4: блок 2x2 или 4x4 шейдится один раз,
// только если все его пиксели берут цвет из одного центра ячейки. Иначе — каждый пиксель

layout(binding = 11) uniform usampler2D u_src_map;  // src_map.frag

layout(location = 0) out uint o_rate;  // 0 — каждый пиксель, 1 — 2x2, 2 — 4x4 (палитра в render.c)

ivec2 src_at(ivec2 p) { return ivec2(texelFetch(u_src_map, min(p, textureSize(u_src_map, 0) - 1), 0).rg); }

void main()
{
    ivec2 b0 = ivec2(gl_FragCoord.xy) * 4;
    ivec2 s4 = src_at(b0);
    bool one4 = true, one2 = true;
    for (int c = 0; c < 4; c++) {
        ivec2 q = b0 + ivec2(c & 1, c >> 1) * 2;
        ivec2 s2 = src_at(q);
        one4 = one4 && s2 == s4;
        for (int i = 1; i < 4; i++) one2 = one2 && src_at(q + ivec2(i & 1, i >> 1)) == s2;
    }
    o_rate = one2 ? (one4 ? 2u : 1u) : 0u;
}
