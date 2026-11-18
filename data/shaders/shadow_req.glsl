// Заявки страниц теней солнца (docs/shadow.md): отметка или заявка страницы и квадрат страниц. Зовут разбор плиток
// (точки пикселей, light_tiles.comp) и клетки воздуха (точки лучей солнца в пыли, air_cells.comp). Без циклов:
// lavapipe под GPU-AV падает на доступе к буферу в цикле шейдера с barrier() (docs/graph.md, «Циклы с буфером»)
#ifndef SHADOW_REQ_GLSL
#define SHADOW_REQ_GLSL
#include "shadow.glsl"

// Страница теней нужна кадру: прочитана — отметка кадра, нет — заявка (как gc_touch). bg — набор нового шага
// светила, что строится в фоне: его заявки — в свой список, место им дают не больше sm_build_cap за кадр
void sm_touch(int k, ivec2 page, bool bg) {
    FrameData f = pc.frame;
    uvec2 key = sm_page_key(k, page);
    uint slot = sm_slot(k, page);
    uint req = SM_REQ | (f.sm_frame & 0x7FFFFFFFu);
    uint e = f.sm_table.v[slot];
    if (e == req) return;
    if (e != 0u && (e & SM_REQ) == 0u) {
        uint phys = e - 1u;
        SmPage pg = f.sm_pages.v[phys];
        if (uvec2(pg.key0, pg.key1) == key) {
            if (pg.used != f.sm_frame && atomicExchange(f.sm_pages.v[phys].used, f.sm_frame) != f.sm_frame)
                atomicAdd(f.stats.v[STAT_SM_USED], 1u);
            return;
        }
    }
    if (atomicCompSwap(f.sm_table.v[slot], e, req) != e) return;  // заявил другой пиксель
    uint i = bg ? atomicAdd(f.sm_req.bg_n, 1u) : atomicAdd(f.sm_req.req_n, 1u);
    if (i < f.sm_req_cap) {
        uint at = bg ? 2u * (f.sm_req_cap + i) : 2u * i;
        f.sm_req.v[at] = key.x;
        f.sm_req.v[at + 1u] = key.y;
    } else {
        f.sm_table.v[slot] = 0u;  // список заявок полон: страница без места (свет там — без тени)
    }
}

// Страницы места k под квадратом w ± r (тексели окна, r меньше полустраницы): не больше 2×2, без цикла
void sm_touch_square(int k, vec2 w, float r, bool bg) {
    ivec2 lo, hi;
    if (!sm_rect(w, r, lo, hi)) return;
    ivec2 b = pc.frame.sm_base[k];
    sm_touch(k, b + lo, bg);
    if (hi.x > lo.x) sm_touch(k, b + ivec2(lo.x + 1, lo.y), bg);
    if (hi.y > lo.y) sm_touch(k, b + ivec2(lo.x, lo.y + 1), bg);
    if (hi.x > lo.x && hi.y > lo.y) sm_touch(k, b + lo + 1, bg);
}
#endif
