// Волосы, бороды, брови (docs/hair.md). Цвет — от зерна экземпляра (src/view/wear.cpp, hair_seed): байт 1 —
// тёмность (эумеланин), байт 2 — седина, младшие 4 бита байта 3 — рыжина. Младший байт — тон кожи: у линии роста
// второй материал вершины — human_skin (смесь шумом), у щетины кожа считается здесь же.
// Узор — пряди вдоль направления: extra c = 0 — от макушки (стрижка, распущенные), 2 — назад к затылку (зачёсанные
// в хвост, косу, пучок), 3 — вбок от середины лица (брови), 1 — щетина на коже: color и color2 — тон кожи как у human_skin, extra d — густота.
// extra a — выгорание сверху, b — ширина пучка прядей, м. Узор в осях позы покоя (i.p, i.n — от бинда)
const vec3 HAIR_CROWN = vec3(0.0, 1.80, 0.06);

vec3 hair_pigment(uint seed) {
    float mel = float((seed >> 8) & 255u) / 255.0;
    float red = float((seed >> 24) & 15u) / 15.0;
    // Альбедо волос (линейное): пыльно-русые ~0.3, каштановые ~0.08, чёрные ~0.012
    vec3 c = mix(vec3(0.32, 0.24, 0.15), vec3(0.075, 0.045, 0.026), smoothstep(0.0, 0.55, mel));
    c = mix(c, vec3(0.013, 0.011, 0.010), smoothstep(0.5, 1.0, mel));
    return mix(c, vec3(0.30, 0.085, 0.03) * (1.0 - 0.65 * mel), red);
}

// Карточки (docs/hair.md, «Карточки»): атлас прядей — цвет (тон волоска с затенением), нормаль поперёк волосков,
// маска (R — корень→кончик, G — номер волоска, B — затенение, A — глубина в пряди). Волосок идёт вдоль битангенса
// (к корню). Блик — наклоном нормали вдоль волоска, у каждого волоска свой сдвиг (extra b): полоса блика рвётся по
// волоскам, как у настоящих прядей. Седина — доля волосков по номеру
Surface surf_hair_card(SurfIn i, vec3 pig, float grey, vec3 white, Surface s) {
    uint tc = i.m.tex & 0xFFu, tn = (i.m.tex >> 8) & 0xFFu, tk = (i.m.tex >> 16) & 0xFFu;
    vec4 c = tex_grad(tc, i.t.uv, i.t.duv);
    vec4 k = tk != 0u ? tex_grad(tk, i.t.uv, i.t.duv) : vec4(0.5, 0.5, 1.0, 0.5);
    vec3 T = i.t.tan.xyz - i.n * dot(i.n, i.t.tan.xyz);
    T = dot(T, T) > 1e-12 ? normalize(T) : vec3(1.0, 0.0, 0.0);
    vec3 B = cross(i.n, T) * i.t.tan.w;   // вдоль волоска, к корню
    vec3 n = i.n;
    if (tn != 0u) {
        vec2 xy = tex_grad(tn, i.t.uv, i.t.duv).rg * 2.0 - 1.0;
        n = normalize(T * xy.x + B * xy.y + i.n * sqrt(max(1.0 - dot(xy, xy), 0.0)));
    }
    float id = k.g, tip = k.r;
    // Свой тон и сдвиг блика у волоска; седые — доля волосков (у седых — меньше пигмента)
    vec3 col = pig * (0.75 + 0.5 * id);
    col = mix(col, white * (0.8 + 0.3 * id), step(id, grey) * (1.0 - 0.15 * tip));
    // Кончики выгорают и светлее (тоньше), к корню — гуще и темнее
    col = mix(col, col * vec3(1.7, 1.5, 1.3) + vec3(0.01, 0.008, 0.005), i.m.p2.x * tip * tip * 0.6);
    s.albedo = clamp(col * (0.55 + 0.75 * c.r), vec3(0.0), vec3(1.0));
    s.ao = mix(0.55, 1.0, k.b) * (1.0 - 0.3 * k.a);
    s.normal = normalize(n + B * ((id - 0.5) * 2.0 * i.m.p2.y));
    s.nvar = 0.03;
    s.rough = i.m.p0.a;
    return s;
}

Surface surf_hair(SurfIn i) {
    uint mode = uint(i.m.p2.z + 0.5);
    float grey = float((i.seed >> 16) & 255u) / 255.0;
    vec3 pig = hair_pigment(i.seed);
    vec3 white = vec3(0.52, 0.50, 0.47);
    Surface s;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 1.0;
    s.nvar = 0.0;
    s.normal = i.n;
    if (i.t.on && (i.m.tex & 0xFFu) != 0u) return surf_hair_card(i, pig, grey, white, s);
    if (mode == 1u) {
        // Щетина: кожа тона зерна и тёмные точки волосков; мельче пикселя — ровная тень по густоте
        float tone = float(i.seed & 255u) / 255.0;
        vec3 skin = pow(mix(sqrt(i.m.p0.rgb), sqrt(i.m.p1.rgb), tone), vec3(2.0));
        vec3 dot_c = mix(pig, white, grey) * 0.8;
        float dens = i.m.p2.w * (0.75 + 0.5 * value_noise(i.p, 0.03, 21u));
        float g = grain_noise(i.p, 0.0016, i.foot, 22u);
        float cov = clamp(dens * (0.55 + 1.4 * g), 0.0, 1.0);
        s.albedo = mix(skin, dot_c, cov * 0.75);
        s.rough = mix(i.m.p0.a, 0.75, cov);
        return s;
    }
    // Направление прядей: от макушки или назад; ниже ушей — вниз (свисают)
    vec3 d = mode == 2u ? vec3(0.0, -0.35, -1.0) : normalize(i.p - HAIR_CROWN + vec3(0.0, 0.0, 1e-3));
    if (mode == 3u) d = vec3(i.p.x < 0.0 ? -1.0 : 1.0, 0.25, 0.0);
    else d = normalize(mix(d, vec3(0.0, -1.0, 0.0), smoothstep(1.69, 1.58, i.p.y)));
    vec3 U = d - i.n * dot(d, i.n);
    U = dot(U, U) > 1e-6 ? normalize(U) : normalize(cross(i.n, vec3(1.0, 0.0, 0.0)));
    vec3 V = cross(i.n, U);
    float w = max(i.m.p2.y, 1e-3);
    vec2 q = vec2(dot(i.p, U) / (w * 6.0), dot(i.p, V) / w);
    // Пучки (ширина w) и волоски (в восемь раз тоньше): светлее и темнее, мельче пикселя — к среднему
    float k1 = px_fade(w, i.foot), k2 = px_fade(w * 0.125, i.foot);
    float c1 = value_noise2(q, 31u) - 0.5;
    float c2 = value_noise2(q * vec2(1.5, 8.0), 32u) - 0.5;
    vec3 c = pig * (1.0 + 0.55 * c1 * k1 + 0.7 * c2 * k2);
    // Седина: волосок белый с долей grey (вдали — смесь по доле)
    float gs = value_noise2(q * vec2(1.5, 8.0), 33u);
    float gw = mix(grey, smoothstep(grey + 0.08, grey - 0.08, gs), k2);
    c = mix(c, white * (0.85 + 0.3 * c2), gw);
    // Выгорание на солнце: верх головы светлее и рыжее, пятнами
    float fade = i.m.p2.x * max(i.n.y, 0.0) * (0.5 + value_noise(i.p, 0.08, 34u));
    c = mix(c, c * vec3(2.2, 1.9, 1.6) + vec3(0.02, 0.015, 0.008), clamp(fade, 0.0, 1.0) * 0.5);
    // Пыль пустыни в волосах: серо-бежевая, пятнами
    float dust = smoothstep(0.62, 0.9, value_noise(i.p, 0.05, 35u)) * 0.35;
    c = mix(c, vec3(0.30, 0.25, 0.19), dust);
    // Борозды поперёк прядей — наклон нормали: блик вытягивается вдоль прядей
    float e = 0.35;
    float h1 = value_noise2(q + vec2(0.0, e), 31u) - value_noise2(q - vec2(0.0, e), 31u);
    float h2 = value_noise2(q * vec2(1.5, 8.0) + vec2(0.0, e), 32u) - value_noise2(q * vec2(1.5, 8.0) - vec2(0.0, e), 32u);
    float tilt = 0.55 * h1 * k1 + 0.35 * h2 * k2;
    s.normal = normalize(i.n + V * tilt);
    s.nvar = 0.06 * (1.0 - k2) + 0.05 * (1.0 - k1);
    s.albedo = clamp(c, vec3(0.0), vec3(1.0));
    s.rough = mix(i.m.p0.a, 0.7, dust * 2.0);
    return s;
}
