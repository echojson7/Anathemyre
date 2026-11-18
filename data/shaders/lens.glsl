// Объектив (docs/lens.md): то, что добавляет кадру камера, а не глаз. Блики солнца в линзах (призраки — образы
// диафрагмы от двух отражений между поверхностями линз), цветная кайма к краям кадра (поперечная хроматическая
// аберрация), виньетка (закон cos⁴), зерно плёнки; дизеринг перед 8 битами экрана — всегда. Блеск (рассеяние
// вокруг ярких мест) — glare.glsl
#ifndef LENS_GLSL
#define LENS_GLSL
#include "atmosphere.glsl"

const uint LENS_FLARE = 1u, LENS_CA = 2u, LENS_VIGNETTE = 4u, LENS_GRAIN = 8u, LENS_SUN = 16u;

// --- Блики солнца ---
// Диафрагма — 7 лепестков, края чуть скруглены: призраки — семиугольники. Площадь образа с радиусом 1 (по
// вершинам) — между семиугольником (2.74) и кругом (π)
const float LENS_BLADES = 7.0, LENS_ROUND = 0.3, LENS_ROT = 0.35, LENS_AREA = 2.86;
// Ярче к кромке (сферическая аберрация собирает свет к краю образа): 0.45 + 0.55·d⁴, среднее по образу — 0.633
const float LENS_PROF_MEAN = 0.633;
const int LENS_GHOSTS = 9;
// Призрак: место на прямой «середина кадра → солнце» (1 — у солнца, 0 — в середине, меньше 0 — по ту сторону
// середины, образ перевёрнут), радиус (доля высоты кадра), доля света солнца ρ (×1e-4: два отражения от
// просветлённых поверхностей — 1e-5…1e-4 каждое, у крупных призраков сливаются несколько путей), цвет
// (просветление отражает с оттенком: зелёный, янтарный, лиловый, голубой)
const float GH_K[9] = float[9](0.62, 0.32, -0.18, -0.38, -0.62, -0.86, -1.12, -1.5, -2.2);
const float GH_R[9] = float[9](0.012, 0.03, 0.02, 0.06, 0.028, 0.11, 0.045, 0.2, 0.34);
const float GH_RHO[9] = float[9](0.3, 0.6, 0.4, 1.2, 0.6, 2.0, 0.8, 2.6, 3.5);
const vec3 GH_TINT[9] = vec3[9](vec3(1.0, 0.85, 0.6), vec3(0.45, 1.0, 0.6), vec3(1.0, 0.55, 0.9),
                                vec3(0.5, 0.75, 1.0), vec3(1.0, 0.7, 0.35), vec3(0.55, 1.0, 0.65),
                                vec3(0.85, 0.5, 1.0), vec3(1.0, 0.8, 0.55), vec3(0.6, 0.8, 1.0));

// Расстояние до края образа диафрагмы в долях радиуса (1 — край): семиугольник, скруглённый к кругу
float lens_aperture(vec2 q, float rot) {
    const float sec = 2.0 * A_PI / LENS_BLADES;
    float a = atan(q.y, q.x) - rot;
    float m = mod(a, sec) - 0.5 * sec;
    float r = length(q);
    return mix(r * cos(m) / cos(0.5 * sec), r, LENS_ROUND);
}

// Один призрак в пикселе px: центр gc и радиус rad (пиксели); кромка цветная — красный образ чуть больше синего
// (дисперсия стекла); у края кадра образ срезан оправой (круг, сдвинутый от середины, — «кошачий глаз»).
// Среднее по образу — 1
vec3 lens_ghost(vec2 px, vec2 gc, float rad, float rot, vec2 clip, float soft) {
    float d = lens_aperture((px - gc) / rad, rot);
    vec3 dd = d * vec3(0.975, 1.0, 1.025);
    vec3 fill = 1.0 - smoothstep(vec3(1.0 - soft), vec3(1.0 + soft), dd);
    vec3 d2 = dd * dd;
    vec3 prof = (0.45 + 0.55 * d2 * d2) * fill;
    float dc = length(px - clip) / (1.35 * rad);
    return prof * ((1.0 - smoothstep(1.0 - soft, 1.0 + soft, dc)) / LENS_PROF_MEAN);
}

// Свет бликов в пикселе (× экспозиция, как HDR кадра). Образ получает долю ρ света солнца, что вошёл в объектив,
// и разливает её по своей площади: яркость = ρ · E · экспозиция / площадь образа (ср). E — освещённость от солнца
// у камеры после атмосферы: у низкого солнца блики тусклее и рыжие; в тёмном зале глаз открыт шире — блики от
// солнца в окне ярче, как в жизни. Видимая доля диска и отступ от края кадра — glare.comp (lens_sun)
vec3 lens_flare(vec2 px) {
    FrameData f = pc.frame;
    vec4 s = f.lens_sun.v[0];
    float vis = s.x * s.w;
    if (vis <= 0.0) return vec3(0.0);
    vec2 c = 0.5 * f.viewport;
    float h = f.viewport.y;
    vec2 ax = s.yz - c;
    vec3 e = f.a_sun.w * f.a_sun_t.rgb * (f.sky_out.v[SKY_OUT_EYE].x * vis * f.lens_flare * 1e-4);
    float pa2 = f.pixel_angle * f.pixel_angle;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < LENS_GHOSTS; ++i) {
        float k = GH_K[i];
        vec2 gc = c + ax * k;
        float rad = GH_R[i] * h;
        vec2 dv = px - gc;
        if (dot(dv, dv) > 1.3 * rad * rad) continue;
        float soft = clamp(1.5 / rad, 0.02, 0.5) + 0.04;
        vec2 clip = gc + (gc - c) * (1.2 * GH_R[i]);
        vec3 g = lens_ghost(px, gc, rad, LENS_ROT + (k < 0.0 ? A_PI : 0.0), clip, soft);
        sum += GH_TINT[i] * g * (GH_RHO[i] / (LENS_AREA * rad * rad * pa2));
    }
    return sum * e;
}

// --- Аберрация ---
// Поперечная хроматическая аберрация: увеличение объектива чуть разное для разных длин волн — к краю кадра
// красный образ дальше от середины, синий ближе. Пять длин волн 450–650 нм (t от −1 до 1) с весами по каналам:
// сумма по каналу — 1, ровное место цвета не меняет, кайма плавная, без трёх резких копий. Сдвиг крайних длин
// волн в углу кадра — lens_ca полудиагонали (style.frag)
const float CA_T[5] = float[5](-1.0, -0.5, 0.0, 0.5, 1.0);
const vec3 CA_W[5] = vec3[5](vec3(0.0, 0.0, 0.45), vec3(0.0, 0.25, 0.40), vec3(0.15, 0.5, 0.15),
                             vec3(0.40, 0.25, 0.0), vec3(0.45, 0.0, 0.0));

// --- Виньетка ---
// Естественное затемнение к краям: освещённость кадра падает как cos⁴ угла луча к оси (наклон к площадке, её
// удаление и сжатие видимого зрачка). lens_vignette — доля закона: 1 — простой объектив без поправки, 0.2 — как
// у объектива с поправкой (−0.5 EV в углу кадра 16:9 при поле зрения 60°)
float lens_vignette(vec2 px) {
    FrameData f = pc.frame;
    vec2 nd = px * f.ndc_per_pixel - 1.0;
    vec2 t = vec2(nd.x / f.proj_x, nd.y / f.proj_y);
    return pow(1.0 / (1.0 + dot(t, t)), 2.0 * f.lens_vignette);
}

// --- Зерно и дизеринг ---
// Хеш PCG3D (Jarzynski, Olano 2020): три независимых слова на ячейку и кадр
uvec3 lens_pcg3(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}
vec3 lens_rand3(uvec3 v) { return vec3(lens_pcg3(v) >> 8u) * (1.0 / 16777216.0); }

float lens_to_srgb(float x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055; }
float lens_to_lin(float x) { return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4); }
vec3 lens_to_srgb(vec3 c) { return vec3(lens_to_srgb(c.r), lens_to_srgb(c.g), lens_to_srgb(c.b)); }
vec3 lens_to_lin(vec3 c) { return vec3(lens_to_lin(c.r), lens_to_lin(c.g), lens_to_lin(c.b)); }

// Шум зерна в точке g (в ячейках зерна), разброс 1: значения в узлах — сумма трёх равномерных (почти гаусс),
// между узлами — плавно, с поправкой разброса (в середине ячейки он не проседает)
float lens_grain_noise(vec2 g, uint frame) {
    vec2 i = floor(g), t = g - i;
    t = t * t * (3.0 - 2.0 * t);
    uvec2 b = uvec2(ivec2(i) + 65536);
    vec4 n;
    for (int k = 0; k < 4; ++k) {
        vec3 r = lens_rand3(uvec3(b + uvec2(k & 1, k >> 1), frame));
        n[k] = (r.x + r.y + r.z - 1.5) * 2.0;  // разброс суммы трёх — 1/4
    }
    vec4 w = vec4((1.0 - t.x) * (1.0 - t.y), t.x * (1.0 - t.y), (1.0 - t.x) * t.y, t.x * t.y);
    return dot(n, w) * inversesqrt(dot(w, w));
}

// После тонмаппинга (линейный цвет экрана 0–1): зерно плёнки — в шкале экрана, сильнее в средних тонах (в
// светах и глубоких тенях его не видно, как на отпечатке), одно на три канала, ячейка ~1.25 пикселя при 1080p,
// новое каждый кадр. Дизеринг ±1 ступень 8 бит треугольным шумом — всегда: плавное небо и тени без полос
vec3 lens_film(vec3 lin, vec2 px) {
    FrameData f = pc.frame;
    vec3 s = lens_to_srgb(clamp(lin, 0.0, 1.0));
    uvec2 ip = uvec2(px);
    if ((f.lens_on & LENS_GRAIN) != 0u) {
        float gs = max(1.0, f.viewport.y * (1.25 / 1080.0));
        vec3 o = lens_rand3(uvec3(f.lens_frame, 7919u, 104729u));  // сдвиг решётки зерна по кадрам
        float n = lens_grain_noise(px / gs + o.xy * 64.0, f.lens_frame);
        float y = dot(s, vec3(0.2126, 0.7152, 0.0722));
        s += n * f.lens_grain * sqrt(clamp(4.0 * y * (1.0 - y), 0.0, 1.0));
    }
    vec3 r = lens_rand3(uvec3(ip, f.lens_frame ^ 0x9E3779B9u));
    s += (r.x + r.y - 1.0) * (1.0 / 255.0);
    return lens_to_lin(clamp(s, 0.0, 1.0));
}
#endif
