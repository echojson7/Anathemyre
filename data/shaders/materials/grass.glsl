// Травяной покров (без травинок-объектов): color — зелёная трава, color2 — сухая, выгоревшая. texel — квадрат
// мелкого узора. extra.x — размер пятен сухой травы (м, степень двойки), extra.y — доля сухой,
// extra.z — размер кочки (м, степень двойки), extra.w — наклон нормали на кочке.
// Издали — массы зелёного и соломенного, вблизи — кочки двух размеров разного оттенка с тенью у основания,
// между ними проглядывает земля
float grass_tufts(SurfIn i, float sz, uint seed, out vec2 dir, out float tone) {
    ivec3 c0 = world_cell(vec3(i.p.x, 0.0, i.p.z), sz);
    vec2 l = fract(i.p.xz / sz);
    float best = 0.0;
    dir = vec2(0.0);
    tone = 0.5;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dx = -1; dx <= 1; ++dx) {
            ivec3 cc = c0 + ivec3(dx, 0, dz);
            if (hash3(cc, seed + 3u) < 0.18) continue;  // пустая ячейка: проплешина
            vec2 ctr = vec2(dx, dz) + vec2(hash3(cc, seed), hash3(cc, seed + 1u));
            float rad = 0.4 + hash3(cc, seed + 2u) * 0.55;
            vec2 dv = (l - ctr) / rad;
            float d2 = dot(dv, dv);
            if (d2 < 1.0) {
                float hgt = (1.0 - d2) * (0.5 + 0.5 * hash3(cc, seed + 4u));
                if (hgt > best) { best = hgt; dir = dv; tone = hash3(cc, seed + 5u); }
            }
        }
    return best;
}

Surface surf_grass(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float ps = max(i.m.p2.x, 1.0);
    // Сухие пятна: крупные и рваные
    float dry = fbm_n(i.p, ps * 4.0, 5, 81u);
    float dk = smoothstep(i.m.p2.y - 0.2, i.m.p2.y + 0.2, dry);
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, dk * 0.45);
    // Оттенок гуляет: желтее, оливковее, темнее
    float hue = fbm_n(i.p, 4.0, 3, 82u) - 0.5;
    c *= vec3(1.0 + hue * 0.25, 1.0 + hue * 0.12, 1.0 - hue * 0.1);
    c *= 0.9 + 0.2 * fbm_n(i.p, 64.0, 3, 83u);
    vec3 n = i.n;
    float height = 0.35;
    float ao = 1.0;
    float sz = i.m.p2.z;
    float kf = 1.0 - smoothstep(sz * 0.15, sz * 0.5, i.foot);
    if (kf > 0.0) {
        vec2 d1, d2;
        float t1, t2;
        float a = grass_tufts(i, sz * 2.0, 84u, d1, t1);   // крупные кочки
        float b = grass_tufts(i, sz, 94u, d2, t2);         // мелкие между ними
        float tuft = max(a, b * 0.8);
        vec2 dir = a >= b * 0.8 ? d1 : d2;
        float tone = a >= b * 0.8 ? t1 : t2;
        // Между кочками — тёмная земля и сухая ветошь
        vec3 gap = mix(c * 0.6, vec3(0.16, 0.12, 0.08), 0.35);
        vec3 tc = c * mix(vec3(0.85, 0.95, 0.85), vec3(1.15, 1.08, 0.9), tone);
        vec3 cc = mix(gap, tc, smoothstep(0.0, 0.25, tuft));
        cc *= 0.93 + 0.14 * tuft;
        c = mix(c, cc, kf);
        ao = mix(1.0, 0.7 + 0.3 * smoothstep(0.0, 0.4, tuft), kf);
        vec3 tx = normalize(vec3(1.0, 0.0, 0.0) - i.n * i.n.x);
        vec3 tz = normalize(cross(tx, i.n));
        n = normalize(i.n + (tx * dir.x - tz * dir.y) * (i.m.p2.w * kf * tuft));
        height += tuft * 0.55 * kf;
    }
    // Пряди: штрихи, вытянутые в своём направлении на каждом пятне (полёгшая трава); гаснут мельче пикселя
    float bf = 1.0 - smoothstep(t * 0.5, t * 2.0, i.foot);
    if (bf > 0.0) {
        float ang = fbm_n(i.p, 2.0, 2, 89u) * 6.2831853;
        vec2 ax = vec2(cos(ang), sin(ang));
        vec2 wp = vec2(ORIGIN_CHUNK) * CHUNK_M + i.p.xz;  // мировые метры: узор не прыгает со сменой чанка
        vec2 r = vec2(dot(wp, ax), dot(wp, vec2(-ax.y, ax.x)));
        vec2 cl = floor(r / vec2(t * 3.0, t * 0.5));
        float blade = hash3(ivec3(int(cl.x), 0, int(cl.y)), 88u) - 0.5;
        c *= 1.0 + blade * 0.45 * bf;
    }
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = n;
    s.ao = ao;
    s.emissive = vec3(0.0);
    s.height = height;
    s.metal = 1.0;
    s.nvar = 0.0;
    return s;
}
