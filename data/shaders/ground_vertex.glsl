// Вершина земли clipmap'ом (docs/terrain.md): кольцо L — сетка (2n+1)² вершин с шагом 0.5·2^L м вокруг камеры,
// середину закрывает кольцо мельче (дыру вырезает буфер индексов). Высота — целая функция узла (terrain.glsl);
// у внешнего края вершины плавно переходят в высоту следующего кольца, щелей нет. Зовут ground.vert и
// разбор visibility buffer (resolve.glsl): та же вершина из номера кольца и номера вершины
#ifndef GROUND_VERTEX_GLSL
#define GROUND_VERTEX_GLSL
#include "terrain.glsl"

// Высота кольца L в точке g (полуметры от угла карты), метры. Кольцо 0 — между узлами 1 м билинейно
float level_height(ivec2 g, int L) {
    if (L == 0) {
        ivec2 n = g >> 1, odd = g & 1;
        float h = float(t_node(n.x, n.y, 0));
        if (odd.x != 0 && odd.y != 0)
            h = (h + float(t_node(n.x + 1, n.y, 0)) + float(t_node(n.x, n.y + 1, 0)) + float(t_node(n.x + 1, n.y + 1, 0))) * 0.25;
        else if (odd.x != 0) h = (h + float(t_node(n.x + 1, n.y, 0))) * 0.5;
        else if (odd.y != 0) h = (h + float(t_node(n.x, n.y + 1, 0))) * 0.5;
        return h * T_M_PER_U;
    }
    // Кольца от 16 м — узлы сглаженной сетки того же шага (vk_terrain.cpp): точки сетки 8 м пилили бы
    // обрывы уступов зубцами. Слой деталей мельче шага там не виден
    if (L >= 5) {
        FrameData f = pc.frame;
        int m = L - 4;
        ivec2 nm = ivec2(((f.t_nx - 1) >> m) + 1, ((f.t_nz - 1) >> m) + 1);
        ivec2 k = clamp((g >> 1) >> (m + 3), ivec2(0), nm - 1);
        return float(t_hgt(f.t_mip[m] + k.y * nm.x + k.x)) * (64.0 * T_M_PER_U);
    }
    // Октава с периодом меньше двух шагов кольца мельтешила бы: отброшена
    return float(t_node(g.x >> 1, g.y >> 1, L)) * T_M_PER_U;
}

struct GroundVtx {
    vec3 rel;      // относительно камеры
    vec3 normal;   // по соседям для колец от 2 м, ближе — вверх (нормаль берёт фрагмент)
    float wdepth;  // глубина воды над землёй, м (< 0 — суша)
};

// Вершина vi кольца L: узел сетки кольца, высота с переходом к следующему кольцу и водой
GroundVtx ground_vertex(int L, int vi) {
    FrameData f = pc.frame;
    int n2 = 2 * f.clip_n;
    int i = vi % (n2 + 1), j = vi / (n2 + 1);
    int sh = 1 << L;  // шаг кольца в полуметрах
    ivec2 g = f.clip[L].xy + ivec2(i, j) * sh;
    float h = level_height(g, L);

    // Камера в полуметрах карты
    ivec2 cam_g = (f.cam_chunk - ivec2(f.t_chunk0)) * 512;
    vec2 to = vec2(g - cam_g) * 0.5 - f.cam_off.xz;  // метры от камеры
    // Переход к следующему кольцу: по расстоянию до камеры (не по номеру вершины), чтобы сдвиг кольца
    // при движении камеры не дёргал высоты. На внешнем крае доля 1 — ровно высоты кольца L+1
    if (L + 1 < f.clip_levels) {
        float d = max(abs(to.x), abs(to.y)) / (0.5 * float(sh));
        float w = float(f.clip_morph);
        float m = clamp((d - (float(f.clip_n - 2) - w)) / w, 0.0, 1.0);
        if (m > 0.0) {
            bool ox = (i & 1) != 0, oz = (j & 1) != 0;
            float hc;
            if (ox && oz) hc = 0.5 * (level_height(g - ivec2(sh), L + 1) + level_height(g + ivec2(sh), L + 1));
            else if (ox) hc = 0.5 * (level_height(g - ivec2(sh, 0), L + 1) + level_height(g + ivec2(sh, 0), L + 1));
            else if (oz) hc = 0.5 * (level_height(g - ivec2(0, sh), L + 1) + level_height(g + ivec2(0, sh), L + 1));
            else hc = level_height(g, L + 1);
            h = mix(h, hc, m);
        }
    }
    // Вода (docs/terrain.md, «Вода»): где уровень воды выше земли, вершина встаёт на уровень — сетка земли
    // сама становится ровной гладью, фрагмент узнаёт воду по глубине. Под водой ничего не рисуется
    int wl = t_water(g.x >> 1, g.y >> 1);
    float wdepth = -100.0;
    if (wl != 0) {
        float w = float(wl) * T_M_PER_U;
        wdepth = w - h;
        if (wdepth > 0.0) h = w;
    }
    vec3 nrm = vec3(0.0, 1.0, 0.0);
    if (L >= 2) {
        float hx0 = level_height(g - ivec2(sh, 0), L), hx1 = level_height(g + ivec2(sh, 0), L);
        float hz0 = level_height(g - ivec2(0, sh), L), hz1 = level_height(g + ivec2(0, sh), L);
        float s = float(sh);  // 2 шага в полуметрах = sh метров
        nrm = normalize(vec3(-(hx1 - hx0) / s, 1.0, -(hz1 - hz0) / s));
    }
    GroundVtx o;
    o.rel = vec3(to.x, h - f.cam_off.y, to.y);
    o.normal = nrm;
    o.wdepth = wdepth;
    return o;
}
#endif
