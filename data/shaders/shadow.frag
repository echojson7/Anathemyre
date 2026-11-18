#version 460
#extension GL_GOOGLE_include_directive : require
// Тени солнца (docs/shadow.md): высота заслонки — в тексель страницы, только у страниц, нарисованных в этом
// кадре (остальные уже готовы). Атомарный максимум: порядок кластеров не важен, страница от запуска к запуску
// одна и та же. Картинок у прохода нет.
// Пишут обе стороны: грань, повёрнутая от светила, — свою высоту (нижняя грань заслонки, как раньше); грань,
// повёрнутая к светилу, — высоту с отступом вниз (SM_FRONT, наклон грани на тексель). Без неё тексель над
// сплошным бетоном (стык стены и потолка, угол двух стен) хранил только дальний выход луча из тела, и выборка
// рядом со стыком читала «свет» сквозь толщу — светлая линия по стыкам. С отступом освещённая грань себя не затеняет.
// pc.a = 1 — слой подвижных («Подвижные заслонки»): только в копии страниц этого кадра (места сверх pages_n), иначе
// человек остался бы в кеше неподвижного; машина под мороком — ситом по текселю с долей v_keep (узор неподвижен:
// фильтр выборок сводит его в полутень, а кромка не кипит)
#include "shadow.glsl"
layout(location = 0) in float v_h;
layout(location = 1) flat in int v_k;
layout(location = 2) flat in float v_keep;

void main() {
    FrameData f = pc.frame;
    // Наклон грани — до выходов: сито морока выходит по текселю, и производная у соседей четвёрки была бы неопределена
    float slope = fwidth(v_h);
    ivec2 t = ivec2(gl_FragCoord.xy);
    ivec2 page = f.sm_base[v_k] + (t >> 7);
    uint e = f.sm_table.v[sm_slot(v_k, page)];
    if (e == 0u || (e & SM_REQ) != 0u) return;
    uint phys = e - 1u;
    SmPage pg = f.sm_pages.v[phys];
    if (pg.made != f.sm_frame || uvec2(pg.key0, pg.key1) != sm_page_key(v_k, page)) return;
    if (pc.a != 0u) {
        if (phys < f.sm_pages_n) return;
        if (v_keep < 1.0 && fract(52.9829189 * fract(dot(vec2(t), vec2(0.06711056, 0.00583715)))) >= v_keep) return;
    }
    float h = v_h;
    if (gl_FrontFacing) h -= SM_FRONT * sm_texel(v_k) + min(SM_FRONT_SLOPE * slope, SM_FRONT_MAX * sm_texel(v_k));
    uint he = sm_enc(h);
    atomicMax(f.sm_texels.v[phys * uint(SM_PAGE * SM_PAGE) + uint((t.y & 127) * SM_PAGE + (t.x & 127))], he);
    // Самая высокая заслонка страницы (sm_clear): атомарно — только если выше уже записанной, иначе все фрагменты
    // страницы толпились бы на одном слове
    if (he > pg.top && he > f.sm_pages.v[phys].top) atomicMax(f.sm_pages.v[phys].top, he);
}
