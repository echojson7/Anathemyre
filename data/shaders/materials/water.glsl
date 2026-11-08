// Вода: гладь протоков. color — цвет толщи (глубокое), color2 — не используется, rough — шероховатость глади
// (блик и отражение неба считает свет). extra.x — длина ряби (м, степень двойки), extra.y — наклон нормали
// на ряби, extra.z — сила тёмных полос ветра
Surface surf_water(SurfIn i) {
    float lam = max(i.m.p2.x, 0.0625);
    // Рябь: наклон по разностям шума, гаснет мельче пикселя
    float kf = 1.0 - smoothstep(lam * 0.1, lam * 0.5, i.foot);
    float e = lam * 0.25;
    float h0 = fbm_n(i.p, lam, 3, 91u);
    float hx = fbm_n(i.p + vec3(e, 0.0, 0.0), lam, 3, 91u);
    float hz = fbm_n(i.p + vec3(0.0, 0.0, e), lam, 3, 91u);
    vec3 n = normalize(vec3(-(hx - h0), e / max(i.m.p2.y * kf, 1e-3), -(hz - h0)));
    // Погашенная рябь — в шероховатость (Токсвиг): дальняя гладь даёт широкий ровный блик, а не искры
    float slope = 0.6 / lam * i.m.p2.y;
    float nvar = slope * slope * (1.0 - kf * kf);
    // Полосы ветра: где рябь сильнее, гладь темнее и матовее
    float gust = smoothstep(0.45, 0.7, fbm_n(i.p, 32.0, 2, 92u)) * i.m.p2.z;
    Surface s;
    s.albedo = i.m.p0.rgb;
    s.rough = i.m.p0.a + gust * 0.15;
    s.normal = n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.0;
    s.metal = 0.0;
    s.nvar = nvar;
    return s;
}
