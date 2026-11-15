// Небо (docs/sky.md): рассеянный свет из таблиц, ореол, диск солнца, луна с фазой и рельефом, звёзды по
// звёздным величинам, Млечный путь. Зовёт освещение visibility buffer (light_cells.comp) для пустых пикселей
#ifndef SKY_GLSL
#define SKY_GLSL
#include "atmosphere.glsl"

uint sky_hash3(uvec3 v) {
    uint h = v.x * 0x8DA6B343u ^ v.y * 0xD8163841u ^ v.z * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
float sky_u01(uint h) { return float(h >> 8) * (1.0 / 16777216.0); }
float sky_max3(vec3 v) { return max(v.x, max(v.y, v.z)); }

// Плавный шум по решётке (0..1) и его сумма по октавам: моря луны, облака Млечного пути
float sky_noise(vec3 p) {
    vec3 i = floor(p), w = p - i;
    w = w * w * (3.0 - 2.0 * w);
    ivec3 q = ivec3(i) + 4096;
    float a = sky_u01(sky_hash3(uvec3(q))), b = sky_u01(sky_hash3(uvec3(q + ivec3(1, 0, 0))));
    float c = sky_u01(sky_hash3(uvec3(q + ivec3(0, 1, 0)))), d = sky_u01(sky_hash3(uvec3(q + ivec3(1, 1, 0))));
    float e = sky_u01(sky_hash3(uvec3(q + ivec3(0, 0, 1)))), g = sky_u01(sky_hash3(uvec3(q + ivec3(1, 0, 1))));
    float h = sky_u01(sky_hash3(uvec3(q + ivec3(0, 1, 1)))), k = sky_u01(sky_hash3(uvec3(q + ivec3(1, 1, 1))));
    return mix(mix(mix(a, b, w.x), mix(c, d, w.x), w.y), mix(mix(e, g, w.x), mix(h, k, w.x), w.y), w.z);
}
float sky_fbm(vec3 p, int oct) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < oct; ++i) {
        s += a * sky_noise(p);
        p = p * 2.03 + vec3(17.3, 5.1, 11.7);
        a *= 0.5;
    }
    return s / (1.0 - a * 2.0);
}

// Направление в осях небесной сферы (z — к полюсу мира): звёзды и Млечный путь вращаются вместе с ней
vec3 sky_celestial(vec3 dir) {
    FrameData f = pc.frame;
    return vec3(dot(f.a_star[0].xyz, dir), dot(f.a_star[1].xyz, dir), dot(f.a_star[2].xyz, dir));
}

// Оси галактики в осях небесной сферы — как у настоящего неба: полюс галактики наклонён к полюсу мира на 63°
const vec3 GAL_POLE = vec3(-0.8676, -0.1981, 0.4560);
const vec3 GAL_CENTER = vec3(-0.0549, -0.8734, -0.4838);

// Млечный путь: полоса вдоль плоскости галактики, ярче и шире к центру, с облаками и тёмными пылевыми
// прожилками. Возвращает цвет в долях «яркости полосы у центра»; w — насколько густы звёзды (для слоя тусклых)
vec4 sky_milky(vec3 c) {
    float sb = dot(c, GAL_POLE);                      // синус галактической широты
    vec3 inp = c - GAL_POLE * sb;                     // проекция на плоскость галактики
    float cl = dot(normalize(inp + 1e-6), GAL_CENTER);  // косинус долготы от центра
    float lon = 0.5 + 0.5 * cl;                       // 1 — к центру, 0 — от него
    float width = mix(0.085, 0.17, lon * lon);        // полуширина полосы, рад
    float band = exp(-abs(sb) / width);
    float along = mix(0.32, 1.0, lon * lon);
    // Балдж: светлое пятно у центра
    float bulge = exp(-(1.0 - cl) * 14.0 - sb * sb * 40.0);
    float dens = band * along + bulge * 0.8;
    if (dens < 0.004) return vec4(0.0, 0.0, 0.0, dens);
    // Облака звёзд: крупные клубы и мелкая рябь
    float clouds = sky_fbm(c * 5.0 + 3.7, 5);
    float fine = sky_fbm(c * 21.0 - 9.1, 3);
    float grain = sky_fbm(c * 70.0 + 1.9, 2);
    float tex = 0.25 + 2.3 * clouds * clouds + 0.5 * (fine - 0.5) + 0.45 * (grain - 0.5);
    // Пыль: тёмная прожилка вдоль плоскости, извилистая, гуще к центру; рвётся шумом
    float wob = (sky_fbm(c * 3.1 + 21.0, 3) - 0.5) * 0.22;
    float rift = exp(-pow((sb + wob * (0.4 + lon)) / mix(0.025, 0.05, lon), 2.0));
    float holes = smoothstep(0.42, 0.68, sky_fbm(c * 8.5 + 40.0, 4));
    float dust = clamp(rift * (0.55 + 0.6 * holes) * mix(0.45, 1.0, lon) + holes * 0.35 * band, 0.0, 0.93);
    float v = max(dens * tex, 0.0) * (1.0 - dust);
    // Цвет: тёплый у центра (старые звёзды), холодный в рукавах; пыль краснит
    vec3 col = mix(vec3(0.72, 0.84, 1.0), vec3(1.0, 0.86, 0.66), clamp(bulge * 1.5 + lon * lon * 0.45, 0.0, 1.0));
    col = mix(col, vec3(1.0, 0.72, 0.5), dust * 0.5);
    return vec4(col * v, dens);
}

// Цвет звезды по температуре: от красно-оранжевых (3000 К) до бело-голубых (20 000 К), t в 0..1
vec3 sky_star_color(float t) {
    vec3 cool = vec3(1.0, 0.62, 0.36), mid = vec3(1.0, 0.93, 0.84), hot = vec3(0.66, 0.78, 1.0);
    return t < 0.5 ? mix(cool, mid, t * 2.0) : mix(mid, hot, t * 2.0 - 2.0 + 1.0);
}

// Слой звёзд: грани куба небесной сферы разбиты на n×n клеток, в клетке — звезда с вероятностью prob,
// звёздной величиной от m0 до m1 (тусклых больше — как у настоящего неба: втрое на величину). Возвращает
// яркость (кд/м²): освещённость от звезды размазана по пятну sigma (рад) — энергия не зависит от разрешения
vec3 sky_star_layer(vec3 c, float n, float prob, float m0, float m1, uint salt, float sigma, float twinkle) {
    vec3 a = abs(c);
    uint face;
    vec3 q;  // xy — по грани, z — к грани
    if (a.x >= a.y && a.x >= a.z) {
        face = c.x > 0.0 ? 0u : 1u;
        q = vec3(c.y, c.z, a.x);
    } else if (a.y >= a.z) {
        face = c.y > 0.0 ? 2u : 3u;
        q = vec3(c.z, c.x, a.y);
    } else {
        face = c.z > 0.0 ? 4u : 5u;
        q = vec3(c.x, c.y, a.z);
    }
    // Угол вместо тангенса: клетки почти равной площади
    vec2 uv = atan(q.xy / q.z) * (2.0 / A_PI) + 0.5;
    vec2 g = uv * n;
    vec2 cell = min(floor(g), vec2(n - 1.0));
    uint h = sky_hash3(uvec3(uint(cell.x), uint(cell.y), face + salt * 8u));
    if (sky_u01(h) >= prob) return vec3(0.0);
    // Звезда не ближе 0.2 клетки к краю: её пятно не режется границей
    vec2 sp = (cell + 0.2 + 0.6 * vec2(sky_u01(h * 7u + 1u), sky_u01(h * 13u + 2u))) / n;
    vec2 t = tan((sp - 0.5) * (0.5 * A_PI));
    vec3 sq = normalize(vec3(t, 1.0));
    vec3 cq = normalize(q);
    float d2 = dot(sq - cq, sq - cq);
    float s2 = sigma * sigma;
    if (d2 > 16.0 * s2) return vec3(0.0);
    // Величина: N(< m) ~ 10^(0.45 m)
    float u = sky_u01(h * 101u + 7u);
    float k = pow(10.0, 0.45 * (m1 - m0));
    float m = m0 + log(1.0 + u * (k - 1.0)) / (0.45 * 2.302585);
    // Освещённость от звезды, лк. Перепад между величинами сжат (0.36 вместо 0.4 на величину): экран не
    // держит тысячекратный перепад точек, а глаз видит величины ровной лестницей
    float e = 2.54e-6 * pow(10.0, -0.36 * m);
    float psf = exp(-0.5 * d2 / s2) / (2.0 * A_PI * s2);
    // Яркие звёзды — со слабым широким ореолом (глаз, оптика)
    if (m < 2.0) psf += 0.06 * exp(-0.5 * d2 / (9.0 * s2)) / (2.0 * A_PI * 9.0 * s2) * (2.0 - m);
    // Мерцание: две несоизмеримые частоты со своей фазой у звезды — плавно, без шума кадра
    float ph = sky_u01(h * 31u + 3u) * 6.2831853;
    float tw = sin(pc.frame.a_night.y * (11.0 + 9.0 * sky_u01(h * 57u + 5u)) + ph) *
               sin(pc.frame.a_night.y * (5.3 + 4.1 * sky_u01(h * 91u + 11u)) + ph * 1.7);
    float temp = sky_u01(h * 151u + 13u);
    temp = temp * temp * (3.0 - 2.0 * temp);
    return sky_star_color(temp) * (e * psf * max(1.0 + twinkle * tw, 0.0));
}

// Звёзды и Млечный путь по направлению: яркость над атмосферой, кд/м². ex — экспозиция кадра: днём звёзд не
// видно, их и не считаем
vec3 sky_stars(vec3 dir, float ex) {
    FrameData f = pc.frame;
    float gain = f.a_star[0].w;
    if (gain <= 0.0 || ex * gain < 1e-2) return vec3(0.0);
    vec3 c = sky_celestial(dir);
    // Пятно звезды: не мельче пикселя (иначе рябит в движении) и не мельче 0.9′
    float sigma = max(0.62 * f.pixel_angle, 0.00026);
    // У горизонта воздуха больше: мерцание сильнее
    float low = 1.0 - clamp(dir.y, 0.0, 1.0);
    float tw = f.a_night.w * (0.06 + 0.5 * low * low * low);
    vec4 mw = sky_milky(c);
    // Звёзды лежат неровно: где-то гуще, где-то пусто; в полосе Млечного пути тусклых больше. Плотные слои —
    // по две решётки с несоизмеримым шагом: одна решётка читается ровным рядом
    float patchy = 0.45 + 1.1 * sky_noise(c * 4.3 + 7.0);
    float faint = clamp(patchy * (0.05 + 0.3 * mw.w), 0.0, 0.5);
    vec3 L = vec3(0.0);
    L += sky_star_layer(c, 20.0, 0.075, -1.2, 3.2, 1u, sigma, tw);
    L += sky_star_layer(c, 56.0, 0.065 * patchy, 3.2, 5.2, 2u, sigma, tw);
    L += sky_star_layer(c, 47.0, 0.065 * patchy, 3.2, 5.2, 3u, sigma, tw);
    L += sky_star_layer(c, 150.0, faint, 5.2, 7.0, 4u, sigma, tw * 0.5);
    L += sky_star_layer(c, 131.0, faint, 5.2, 7.0, 5u, sigma, tw * 0.5);
    L *= gain;
    // Млечный путь: яркость — от свечения ночного неба (виден на его фоне, в лунную ночь слабее)
    L += mw.rgb * (f.a_night.z * 5.0 * f.a_ground.w * f.a_sun.w);
    return L;
}

// Диск солнца: потемнение к краю (в синем сильнее), цвет — пропускание атмосферы по направлению пикселя
// (низ диска на закате краснее верха). Яркость настоящая — освещённость на телесный угол видимого диска, но
// не выше предела по экспозиции: иначе диск всегда выжжен в белое, а закатное солнце должно быть оранжевым
vec3 sky_sun_disc(vec3 dir, float ex) {
    FrameData f = pc.frame;
    float rs = f.a_sun_halo.w;
    vec3 dv = dir - f.a_sun.xyz;
    float x = length(dv) / rs;
    float aa = 1.5 * f.pixel_angle / rs;
    if (x >= 1.0 + aa) return vec3(0.0);
    float mu = sqrt(max(1.0 - x * x, 0.0));
    const vec3 LIMB = vec3(0.40, 0.50, 0.65);  // I(μ) = μ^a, среднее по диску 2 / (a + 2)
    vec3 limb = pow(vec3(max(mu, 0.02)), LIMB) * (LIMB + 2.0) * 0.5;
    float edge = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, x);
    float r = R_GROUND + atmo_cam_alt();
    float hz = atmo_horizon(atmo_cam_alt());
    vec3 T = lut_fetch(f.sky_trans, TRANS_W, TRANS_H, trans_uv(r, max(dir.y, sin(hz) + 1e-4))).rgb;
    // Часть света диска ушла в ореол (узкий лепесток пыли)
    vec3 e0 = f.a_sun.w / (A_PI * rs * rs) * exp(-f.a_sun_halo.rgb);
    vec3 L = e0 * T * limb * edge;
    // Предел: высокое солнце — белое, в 64 раза ярче белого кадра; низкое, покрасневшее (зелёного меньше
    // красного) — тусклее, чтобы цвет дожил до экрана. Множитель — по центру диска: потемнение к краю и перепад
    // цвета по высоте остаются
    vec3 Tc = f.a_sun_t.rgb;
    float cap = mix(4.0, 64.0, smoothstep(0.45, 0.95, Tc.g / max(Tc.r, 1e-6)));
    float peak = sky_max3(e0 * Tc) * 1.2 * ex;
    return L * min(1.0, cap / max(peak, 1e-9));
}

// Луна: шар, освещённый солнцем с его стороны, — фаза выходит сама. Моря — тёмные пятна шумом, у границы света
// и тени рельеф рвёт кромку. cover — доля пикселя под диском (звёзды за луной не видны)
vec3 sky_moon_disc(vec3 dir, float ex, out float cover) {
    FrameData f = pc.frame;
    cover = 0.0;
    vec3 mo = f.a_moon.xyz;
    float rm = f.a_moon_halo.w;
    vec3 dv = dir - mo;
    float aa = 1.5 * f.pixel_angle / rm;
    if (dot(dv, dv) >= rm * rm * (1.0 + aa) * (1.0 + aa)) return vec3(0.0);
    vec3 ax = normalize(cross(mo, abs(mo.y) < 0.99 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
    vec3 ay = cross(ax, mo);
    vec2 q = vec2(dot(dv, ax), dot(dv, ay)) / rm;
    float rr = dot(q, q);
    cover = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, sqrt(rr));
    float z = sqrt(max(1.0 - rr, 0.0));
    vec3 p = vec3(q, z);                              // точка шара в осях лица луны
    vec3 n = normalize(ax * q.x + ay * q.y - mo * z);  // видимая сторона смотрит на нас
    // Моря: тёмные пятна в полдиска, больше к одной стороне лица; материки светлее и в крапинку
    float seas = sky_fbm(p * 2.3 + vec3(4.2, 1.3, 7.7), 4);
    float mare = smoothstep(0.49, 0.57, seas + 0.07 * p.x - 0.05 * p.y);
    float rough = sky_fbm(p * 7.0 + 2.0, 3);
    float albedo = mix(1.0, 0.46, mare) * (0.80 + 0.40 * rough);
    // Молодые кратеры с лучами: два светлых пятна
    vec3 k1 = normalize(vec3(-0.12, -0.72, 0.68)), k2 = normalize(vec3(-0.55, 0.22, 0.80));
    float d1 = length(p - k1), d2 = length(p - k2);
    float rays = sky_noise(vec3(atan(p.x - k1.x, p.y - k1.y) * 7.0, 3.0, 1.0));
    albedo += 0.35 * exp(-d1 * d1 * 90.0) + 0.16 * exp(-d1 * 3.2) * smoothstep(0.45, 0.8, rays) + 0.22 * exp(-d2 * d2 * 160.0);
    // Свет: пыль луны отражает почти ровно по диску (Ломмель–Зеелигер), у кромки тени рельеф её рвёт
    float mu0 = dot(n, f.a_sun.xyz) + (rough - 0.5) * 0.10 + (sky_noise(p * 19.0) - 0.5) * 0.06;
    float lit = mu0 > 0.0 ? 2.0 * mu0 / (mu0 + z + 0.02) * smoothstep(0.0, 0.06, mu0) : 0.0;
    lit = mix(lit, max(mu0, 0.0), 0.25);
    // Пепельный свет: тёмную сторону слабо освещает планета
    vec3 shade = vec3(1.0, 0.97, 0.91) * (albedo * lit) + vec3(0.55, 0.7, 1.0) * (albedo * 0.0006);
    vec3 e0 = f.a_moon_t.rgb * exp(-f.a_moon_halo.rgb) * (f.a_sun.w * f.a_ozone.w / A_PI);
    // Предел по экспозиции: ночью диск светлый, но не выжжен — моря видны; днём луна бледная, как есть
    float peak = sky_max3(e0) * ex;
    return shade * e0 * cover * min(1.0, 1.1 / max(peak, 1e-9));
}

// Яркость неба по направлению dir (мир, единичный): рассеянный свет, ореол, солнце, луна, звёзды; линейная
vec3 sky_radiance(vec3 dir) {
    FrameData f = pc.frame;
    vec4 sky = atmo_sky_lut(dir);
    vec3 L = sky.rgb;
    float hz = atmo_horizon(atmo_cam_alt());
    float above = smoothstep(hz - 0.002, hz + 0.002, asin(clamp(dir.y, -1.0, 1.0)));
    if (above <= 0.0) return L;
    float ex = f.sky_out.v[SKY_OUT_EYE].x;
    float cover;
    vec3 moon = sky_moon_disc(dir, ex, cover);
    vec3 top = atmo_halo(dir) + sky_sun_disc(dir, ex) + moon;
    top += sky_stars(dir, ex) * (sky.a * (1.0 - cover));
    return L + top * above;
}
#endif
