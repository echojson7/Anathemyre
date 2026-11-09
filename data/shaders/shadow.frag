#version 460
#extension GL_GOOGLE_include_directive : require
// Тени солнца (docs/shadow.md): высота заслонки — в тексель страницы, только у страниц, нарисованных в этом
// кадре (остальные уже готовы). Атомарный максимум: порядок кластеров не важен, страница от запуска к запуску
// одна и та же. Картинок у прохода нет
#include "shadow.glsl"
layout(location = 0) in float v_h;
layout(location = 1) flat in int v_k;

void main() {
    FrameData f = pc.frame;
    ivec2 t = ivec2(gl_FragCoord.xy);
    ivec2 page = f.sm_base[v_k] + (t >> 7);
    uint e = f.sm_table.v[sm_slot(v_k, page)];
    if (e == 0u || (e & SM_REQ) != 0u) return;
    uint phys = e - 1u;
    SmPage pg = f.sm_pages.v[phys];
    if (pg.made != f.sm_frame || uvec2(pg.key0, pg.key1) != sm_page_key(v_k, page)) return;
    atomicMax(f.sm_texels.v[phys * uint(SM_PAGE * SM_PAGE) + uint((t.y & 127) * SM_PAGE + (t.x & 127))], sm_enc(v_h));
}
