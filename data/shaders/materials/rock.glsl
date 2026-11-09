// Скала: слоистый камень. color — светлые слои, color2 — тёмные. texel — квадрат зерна.
// extra.x — толщина слоёв (м), extra.y — размер граней сколов (м, степень двойки), extra.z — наклон граней,
// extra.w — сила трещин
Surface surf_rock(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    // Слои по высоте, изогнутые шумом
    float bend = value_noise(i.p, 16.0, 51u) * 2.0 + value_noise(i.p, 4.0, 52u) * 0.5;
    float band = fract(i.p.y / i.m.p2.x + bend);
    float layer = smoothstep(0.2, 0.35, band) * (1.0 - smoothstep(0.75, 0.9, band));
    // Слои тоньше ячейки правила дают муар: гаснут к среднему
    layer = mix(layer, 0.45, smoothstep(i.m.p2.x * 0.08, i.m.p2.x * 0.3, i.foot));
    vec3 c = mix(i.m.p1.rgb, i.m.p0.rgb, layer * 0.8 + value_noise(i.p, 1.0, 53u) * 0.2);
    c *= 1.0 + (cell_noise(q, t, 54u) - 0.5) * 0.18;
    // Сколы: на ячейку свой наклон нормали (гранёный камень). Погашенные мельче пикселя — в шероховатость:
    // наклон равномерный ±A/2 по оси, дисперсия A²/12
    vec3 n = i.n;
    float facet = i.m.p2.y;
    float kf = 1.0 - smoothstep(facet * 0.1, facet * 0.35, i.foot);
    float nvar = i.m.p2.z * i.m.p2.z / 12.0 * (1.0 - kf * kf);
    if (kf > 0.0) {
        ivec3 cid = world_cell(i.p, facet);
        vec3 tilt = vec3(hash3(cid, 55u), hash3(cid, 56u), hash3(cid, 57u)) - 0.5;
        n = normalize(i.n + tilt * (i.m.p2.z * kf));
        c *= 1.0 + (hash3(cid, 58u) - 0.5) * 0.2 * kf;
    }
    // Трещины по границам крупных ячеек
    vec3 a = abs(i.n);
    vec3 w = i.p / (facet * 4.0) + value_noise(i.p, 2.0, 59u) * 0.3;
    vec2 pl = a.x >= a.y && a.x >= a.z ? w.yz : (a.y >= a.z ? w.xz : w.xy);  // плоскость грани
    vec2 fr = abs(fract(pl) - 0.5);
    float edge = 0.5 - max(fr.x, fr.y);
    float crack = (1.0 - smoothstep(0.0, 0.03, edge)) * i.m.p2.w * (1.0 - smoothstep(0.03, 0.12, i.foot));
    c *= 1.0 - crack * 0.5;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = n;
    s.ao = 1.0 - crack * 0.4;
    s.emissive = vec3(0.0);
    s.height = 0.85 + layer * 0.1;
    s.metal = 1.0;
    s.nvar = nvar;
    return s;
}
