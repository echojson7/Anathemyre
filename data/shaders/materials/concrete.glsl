// Монолитный бетон по концепту руины (docs/ruin.md): щиты опалубки рядами, у каждого щита свой оттенок, швы
// между щитами, ряды анкерных отверстий посередине щитов, поры, тёмные пятна сырости и светлые выцветшие
// пятна. Ржавчины нет. Потёки и грязь — декали (data/shaders/decal.glsl), не узор материала.
// color — бетон, color2 — пятна сырости и грязь; texel — зерно (квадрат узора, м).
// extra.x — высота щита опалубки (м, 0 — без опалубки: низ плиты, излом), extra.y — ширина щита (м),
// extra.z — шаг анкерных отверстий в ряду (м, 0 — без них), extra.w — сила пятен.
// Ряды щитов и анкеров — по мировым метрам: детали стоят на сетке 1 м, площадка — на высоте, кратной 0.5 м
// (docs/terrain.md, «Штампы»), поэтому швы ложатся от пола и от торцов стен.

// Точка для плоского шума (fbm_n берёт x и z): у стены — плоскость грани (вдоль, высота). Сдвиг на чанк камеры
// снимает то, что world_cell прибавит, — узор не прыгает при смене чанка
vec3 concrete_plane(SurfIn i, bool vert, vec3 an) {
    if (!vert) return i.p;
    vec2 ch = vec2(ORIGIN_CHUNK) * CHUNK_M;
    float u = an.x > an.z ? i.p.z + ch.y - ch.x : i.p.x;
    return vec3(u, 0.0, i.p.y - ch.y);
}

Surface surf_concrete(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    vec3 an = abs(i.n);
    bool vert = an.y < 0.7;
    // Мировые метры для рядов щитов: x, z — от начала карты чанков; y — высота
    vec2 wc = vec2(ORIGIN_CHUNK) * CHUNK_M;
    float wh = (an.x > an.z ? i.p.z + wc.y : i.p.x + wc.x);  // вдоль грани
    float wv = i.p.y;                                       // по высоте

    // Тон: крупные пятна 2 м и 0.5 м, зерно по квадратам texel
    vec3 fp = concrete_plane(i, vert, an);
    float big = fbm_n(fp, 2.0, 2, 31u) - 0.5;
    float mid = value_noise(i.p, 0.5, 32u) - 0.5;
    float grain = cell_noise(q, t, 33u) - 0.5;
    float fine_fade = 1.0 - smoothstep(t * 0.6, t * 2.0, i.foot);
    vec3 c = i.m.p0.rgb * (1.0 + big * 0.22 + mid * 0.10 + grain * 0.10 * fine_fade);

    float ph = i.m.p2.x, pw = i.m.p2.y;
    if (vert && ph > 0.0) {
        float row = floor(wv / ph);
        float col = floor(wh / pw);
        // Каждый щит — свой оттенок; каждый ряд (заливка) чуть светлее к низу
        float tone = hash3(ivec3(int(col), int(row), 7), 34u) - 0.5;
        float lift = fract(wv / ph);
        c *= 1.0 + tone * 0.10 + (0.5 - lift) * 0.05;
        // Швы: горизонтальный — тёмная полоса и светлый наплыв над ней, вертикальный — слабее
        float sw = max(t, i.cell);
        float fade = 1.0 - smoothstep(sw * 1.5, sw * 4.0, i.foot);
        float dv = lift * ph, dh = fract(wh / pw) * pw;
        if (dv < sw) c *= mix(1.0, 0.84, fade);
        else if (dv < sw * 2.0) c *= mix(1.0, 1.04, fade);
        if (dh < sw) c *= mix(1.0, 0.93, fade);
        // Анкерные отверстия: ряд посередине щита, шаг extra.z, сдвиг и пропуски по хешу
        float sp = i.m.p2.z;
        if (sp > 0.0) {
            float k = floor(wh / sp);
            ivec3 hk = ivec3(int(k), int(row), 11);
            float jx = (hash3(hk, 35u) - 0.5) * 0.06, jy = (hash3(hk, 36u) - 0.5) * 0.04;
            vec2 d = vec2(wh - (k + 0.5) * sp - jx, wv - (row + 0.5) * ph - jy);
            float r = 0.019;
            float l = length(d);
            float hfade = 1.0 - smoothstep(0.012, 0.05, i.foot);
            float kind = hash3(hk, 37u);
            if (kind < 0.88) {
                if (l < r) c *= mix(1.0, kind < 0.12 ? 0.75 : 0.32, hfade);  // заделанные — светлее
                else if (l < r * 1.9) c *= mix(1.0, 0.9, hfade);            // тёмный ободок
            }
        }
    }
    // Поры (раковины от воздуха у опалубки): редкие тёмные точки на стенах
    if (vert && cell_noise(q, t, 38u) > 0.996) c *= mix(1.0, 0.72, fine_fade);
    // Пятна: тёмные (сырость, грязь) и светлые (выцветшие, высолы)
    float bs = i.m.p2.w;
    float wet = fbm_n(fp, 4.0, 3, 39u);
    float a = smoothstep(0.52, 0.75, wet) * bs;
    if (i.n.y < -0.5) a = max(a, smoothstep(0.5, 0.7, fbm_n(i.p + vec3(0.0, 0.0, 3.1), 1.0, 3, 40u)) * bs * 0.6);
    c = mix(c, i.m.p1.rgb, clamp(a, 0.0, 0.7));
    float pale = smoothstep(0.62, 0.8, fbm_n(fp, 2.0, 3, 41u)) * bs * 0.5;
    c = mix(c, i.m.p0.rgb * 1.25, clamp(pale, 0.0, 0.5));

    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 1.0;
    s.nvar = 0.0;
    return s;
}
