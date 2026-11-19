// Вершина земли clipmap'ом (docs/terrain.md): кольцо L — сетка (2n+1)² вершин с шагом 0.5·2^L м вокруг камеры,
// середину закрывает кольцо мельче (дыру вырезает буфер индексов). Высота — целая функция узла (terrain.glsl);
// у внешнего края вершины плавно переходят в высоту следующего кольца, щелей нет. Зовут ground.vert и
// разбор visibility buffer (resolve.glsl): та же вершина из номера кольца и номера вершины
#ifndef GROUND_VERTEX_GLSL
#define GROUND_VERTEX_GLSL
#include "terrain.glsl"

// Высота кольца L в точке g (полуметры от угла карты), метры. Кольцо 0 — между узлами 1 м билинейно.
// t_node — один вызов в цикле: драйвер встраивает функции целиком, и четыре вызова на каждый из семи
// level_height в ground_vertex (35 копий высоты узла с дюнами) Radeon 780M собирал 22 с (docs/startup.md)
float level_height(ivec2 g, int L) {
    // Кольца от 16 м — узлы сглаженной сетки того же шага (vk_terrain.cpp): точки сетки 8 м пилили бы
    // обрывы уступов зубцами. Слой деталей мельче шага там не виден
    if (L >= 5) {
        FrameData f = pc.frame;
        int m = L - 4;
        ivec2 nm = ivec2(((f.t_nx - 1) >> m) + 1, ((f.t_nz - 1) >> m) + 1);
        ivec2 k = clamp((g >> 1) >> (m + 3), ivec2(0), nm - 1);
        return float(t_hgt(f.t_mip[m] + k.y * nm.x + k.x)) * (64.0 * T_M_PER_U);
    }
    // Кольца 1–4 — узел как есть, октава с периодом меньше двух шагов кольца мельтешила бы: отброшена.
    // Кольцо 0 — середина 1, 2 или 4 узлов; сумма в том же порядке, что раньше (бит в бит)
    ivec2 n = g >> 1, odd = L == 0 ? g & 1 : ivec2(0);
    int cnt = (1 + odd.x) * (1 + odd.y);
    float h = 0.0;
    [[dont_unroll]] for (int k = 0; k < cnt; ++k) {
        ivec2 o = k == 0 ? ivec2(0) : (k == 1 ? (odd.x != 0 ? ivec2(1, 0) : ivec2(0, 1)) : (k == 2 ? ivec2(0, 1) : ivec2(1)));
        h += float(t_node(n.x + o.x, n.y + o.y, L));
    }
    h *= cnt == 4 ? 0.25 : (cnt == 2 ? 0.5 : 1.0);
    return h * T_M_PER_U;
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
    // Камера в полуметрах карты
    ivec2 cam_g = (f.cam_chunk - ivec2(f.t_chunk0)) * 512;
    vec2 to = vec2(g - cam_g) * 0.5 - f.cam_off.xz;  // метры от камеры
    // Точки высот одним циклом (одна копия level_height): [0] — сама вершина, потом 1–2 точки кольца L+1
    // для перехода и 4 соседа для нормали
    ivec2 pg[7];
    int pl[7];
    pg[0] = g;
    pl[0] = L;
    int np = 1;
    // Переход к следующему кольцу: по расстоянию до камеры (не по номеру вершины), чтобы сдвиг кольца
    // при движении камеры не дёргал высоты. На внешнем крае доля 1 — ровно высоты кольца L+1
    float m = 0.0;
    if (L + 1 < f.clip_levels) {
        float d = max(abs(to.x), abs(to.y)) / (0.5 * float(sh));
        float w = float(f.clip_morph);
        m = clamp((d - (float(f.clip_n - 2) - w)) / w, 0.0, 1.0);
    }
    int nm = 0;  // точек перехода
    if (m > 0.0) {
        bool ox = (i & 1) != 0, oz = (j & 1) != 0;
        ivec2 e = ivec2(ox ? sh : 0, oz ? sh : 0);
        nm = ox || oz ? 2 : 1;
        pg[1] = g - e;
        pg[2] = g + e;
        pl[1] = pl[2] = L + 1;
        np += nm;
    }
    int nb = np;  // первый сосед для нормали
    if (L >= 2) {
        pg[np] = g - ivec2(sh, 0);
        pg[np + 1] = g + ivec2(sh, 0);
        pg[np + 2] = g - ivec2(0, sh);
        pg[np + 3] = g + ivec2(0, sh);
        pl[np] = pl[np + 1] = pl[np + 2] = pl[np + 3] = L;
        np += 4;
    }
    float hv[7];
    [[dont_unroll]] for (int k = 0; k < np; ++k) hv[k] = level_height(pg[k], pl[k]);
    float h = hv[0];
    if (nm > 0) h = mix(h, nm == 2 ? 0.5 * (hv[1] + hv[2]) : hv[1], m);
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
        float hx0 = hv[nb], hx1 = hv[nb + 1], hz0 = hv[nb + 2], hz1 = hv[nb + 3];
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
