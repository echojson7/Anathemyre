// Ткань и кожа одежды (docs/wear.md). Узор — в осях вещи по бинд-позе (у сеток на костях i.p, i.n — оттуда), не
// плывёт при ходьбе. color — тон 0, color2 — тон 1: смесь в «гамме» по младшему байту зерна экземпляра (тон набора,
// src/view/wear.cpp); следующий байт — износ вещи. texel — шаг плетения, м.
// extra a — выгорание (0..1), b — шаг стёжки, м (0 — нет), c — вид: 0 полотно, 1 саржа, 2 трикотаж (рубчик вдоль),
// 3 стропа (рубчик поперёк), 4 кожа; d — пятна потёртостей (0..1).
// Пыль и грязь у подола и колен — второй материал вершины (доля смеси), складки и кромки — затенение вершины

// Оси плоскости узора поперёк главной оси нормали: u, v — орты мира
void cloth_axes(vec3 n, out vec3 U, out vec3 V) {
    vec3 a = abs(n);
    if (a.x >= a.y && a.x >= a.z) { U = vec3(0.0, 0.0, 1.0); V = vec3(0.0, 1.0, 0.0); }
    else if (a.y >= a.z) { U = vec3(1.0, 0.0, 0.0); V = vec3(0.0, 0.0, 1.0); }
    else { U = vec3(1.0, 0.0, 0.0); V = vec3(0.0, 1.0, 0.0); }
}

Surface surf_cloth(SurfIn i) {
    float tone = float(i.seed & 255u) / 255.0;
    float worn = float((i.seed >> 8) & 255u) / 255.0;
    uint kind = uint(i.m.p2.z + 0.5);
    float t = max(i.m.p1.a, 1e-4);
    vec3 c = pow(mix(sqrt(i.m.p0.rgb), sqrt(i.m.p1.rgb), tone), vec3(2.0));
    vec3 U, V;
    cloth_axes(i.n, U, V);
    float u = dot(i.p, U), v = dot(i.p, V);
    // Пятна краски: ткань крашена неровно, кожа — неровно продублена
    float mott = value_noise(i.p, 0.16, 11u) * 0.6 + value_noise(i.p, 0.06, 12u) * 0.4;
    c *= 0.86 + 0.28 * mott;
    // Выгорание: светлее и серее на том, что смотрит вверх (плечи, верх рукавов), и пятнами
    float fade = i.m.p2.x * (0.35 + 0.65 * worn) * (0.45 + 0.55 * max(i.n.y, 0.0)) * (0.5 + value_noise(i.p, 0.3, 13u));
    float g = dot(c, vec3(0.3, 0.59, 0.11));
    c = mix(c, mix(vec3(g), c, 0.45) * 1.45 + 0.012, clamp(fade, 0.0, 1.0) * 0.6);
    // Потёртости: протёртые до основы места (светлее, у кожи — светлые царапины)
    float rub = i.m.p2.w * (0.4 + 0.6 * worn);
    float rs = value_noise(i.p, 0.045, 14u) * 0.7 + value_noise(i.p, 0.016, 15u) * 0.3;
    float rubbed = aa_step(1.0 - 0.35 * rub, rs, 18.0, i.foot) * rub;
    c = mix(c, c * 1.5 + 0.02, rubbed * 0.7);
    float rough = i.m.p0.a;
    vec3 n = i.n;
    float nvar = 0.0;
    float k = kind == 4u ? 0.0 : px_fade(t * 2.0, i.foot);
    // Плетение: нити светлее и темнее (мельче пикселя — гаснет к среднему)
    float w = 0.0;
    if (kind == 0u) w = sin(u * 3.14159 / t) * sin(v * 3.14159 / t);
    else if (kind == 1u) w = sin((u + v) * 3.14159 / t);
    else if (kind == 2u) w = sin(u * 6.28318 / t);
    else if (kind == 3u) w = sin(v * 6.28318 / t);
    c *= 1.0 + 0.10 * w * k;
    if (kind == 4u) {
        // Кожа: зерно, трещинки складок, блеск вытертого
        float gr = grain_noise(i.p, 0.004, i.foot, 16u);
        c *= 1.0 + 0.25 * gr;
        float cr = value_noise(i.p * vec3(1.0, 4.0, 1.0), 0.03, 17u);
        c *= 1.0 - 0.25 * aa_step(0.72, cr, 30.0, i.foot) * px_fade(0.01, i.foot);
        rough = mix(rough, rough * 0.7, rubbed);
    }
    // Стёжка: ромбы с шагом q — тёмный шов и подушки между ними (наклон нормали в осях узора)
    float q = i.m.p2.y;
    if (q > 0.0) {
        float a = (u + v) * 0.7071 / q, b = (u - v) * 0.7071 / q;
        float fa = fract(a), fb = fract(b);
        float da = min(fa, 1.0 - fa) * q, db = min(fb, 1.0 - fb) * q;
        float seam = max(aa_line(da, 0.004, i.foot), aa_line(db, 0.004, i.foot));
        c *= 1.0 - 0.35 * seam;
        float kq = px_fade(q, i.foot) * 0.6;
        float ha = cos(fa * 6.28318) * sin(fb * 3.14159), hb = sin(fa * 3.14159) * cos(fb * 6.28318);
        vec3 gvec = (U + V) * 0.7071 * ha + (U - V) * 0.7071 * hb;
        n = normalize(n - (gvec - n * dot(gvec, n)) * kq);
        nvar += 0.04 * (1.0 - px_fade(q, i.foot));
    }
    Surface s;
    s.albedo = clamp(c, vec3(0.0), vec3(1.0));
    s.rough = rough;
    s.normal = n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 0.0;
    s.nvar = nvar;
    return s;
}
