// Карты ассета (docs/textures.md): поверхность из текстур, запечённых с детальной модели, по развёртке сетки (VUV0)
// плюс деталь скана CC0 и износ по кривизне. Карты — строка maps материала: цвет (BC7 sRGB, в альфе затенение),
// нормаль (BC5, касательная развёртки), кривизна (BC4, 0.5 — плоско, выше — выпукло), шероховатость (BC4).
//   color   множитель цвета карты          rough   шероховатость, если нет её карты
//   color2  цвет износа (голый металл, светлый скол)
//   extra a — износ по кривизне 0..1 (доля выпуклых кромок, с которых сошла краска), b — грязь в углублениях 0..1,
//         c — сила детали скана 0..1 (строка scan материала, с --scans), d — доля металла в износе 0..1
// Сетка без развёртки (или пиксель без неё) — ровный цвет color: так видно, что карты не дошли
Surface surf_baked(SurfIn i) {
    Surface s;
    s.albedo = i.m.p0.rgb;
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 0.0;
    s.nvar = 0.0;
    if (!i.t.on) return s;
    uint tc = i.m.tex & 0xFFu, tn = (i.m.tex >> 8) & 0xFFu, tk = (i.m.tex >> 16) & 0xFFu, tr = i.m.tex >> 24;
    vec2 uv = i.t.uv;
    vec4 duv = i.t.duv;
    if (tc != 0u) {
        vec4 c = tex_grad(tc, uv, duv);
        s.albedo *= c.rgb;
        s.ao = c.a;
    }
    if (tn != 0u) {
        // Нормаль карты: x вдоль касательной, y — вдоль битангенса (к верху картинки), z — по нормали
        vec2 xy = tex_grad(tn, uv, duv).rg * 2.0 - 1.0;
        float z = sqrt(max(1.0 - dot(xy, xy), 0.0));
        vec3 T = i.t.tan.xyz - i.n * dot(i.n, i.t.tan.xyz);
        T = dot(T, T) > 1e-12 ? normalize(T) : vec3(1.0, 0.0, 0.0);
        vec3 B = cross(i.n, T) * i.t.tan.w;
        s.normal = normalize(T * xy.x + B * xy.y + i.n * z);
    }
    if (tr != 0u) s.rough = tex_grad(tr, uv, duv).r;
    // Износ и грязь по кривизне: краска сходит с выпуклых кромок рвано (плавный шум по точке узора), грязь
    // копится в углублениях
    float k = tk != 0u ? tex_grad(tk, uv, duv).r : 0.5;
    float convex = clamp((k - 0.5) * 2.0, 0.0, 1.0), concave = clamp((0.5 - k) * 2.0, 0.0, 1.0);
    float wa = i.m.p2.x;
    if (wa > 0.0) {
        float breakup = value_noise(i.p, 0.05, i.seed ^ 0xB4CEu) * 0.6 + value_noise(i.p, 0.0125, i.seed ^ 0xB4CFu) * 0.4;
        float wear = smoothstep(1.0 - wa, 1.0 - wa + 0.2, convex * (0.55 + 0.9 * breakup));
        s.albedo = mix(s.albedo, i.m.p1.rgb, wear);
        s.rough = mix(s.rough, s.rough * 0.6, wear);
        s.metal = wear * i.m.p2.w;
    }
    float grime = concave * i.m.p2.y;
    s.albedo *= 1.0 - 0.6 * grime;
    s.rough = mix(s.rough, 0.95, grime * 0.5);
    s.ao *= 1.0 - 0.3 * grime;
    // Деталь скана CC0 (docs/scans.md): яркость к среднему и наклон, с силой extra c
    if (pc.frame.scans != 0u && i.m.scan != 0u && i.m.p2.z > 0.0) {
        vec4 d = scan_detail(i);
        s.albedo *= mix(1.0, clamp(d.w, 0.25, 2.0), i.m.p2.z);
        s.normal = normalize(s.normal + d.xyz * i.m.p2.z);
    }
    return s;
}
