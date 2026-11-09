// Щебень: камешки двух размеров на земле. color — камешки, color2 — земля между ними. texel — квадрат зерна.
// extra.x — ячейка мелких камешков (м, степень двойки, крупные — ×4), extra.y — доля ячеек с камешком,
// extra.z — разброс цвета камешков, extra.w — наклон нормали на камешке
Surface surf_gravel(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    vec3 c = i.m.p1.rgb * (1.0 + (cell_noise(q, t, 61u) - 0.5) * 0.25 + (fbm_n(i.p, 4.0, 3, 62u) - 0.5) * 0.4);
    // Россыпь гуще и реже пятнами, а не ровным горошком
    float dens = smoothstep(0.3, 0.7, fbm_n(i.p, 8.0, 2, 66u));
    vec3 n = i.n;
    float height = 0.15 + value_noise(i.p, 0.5, 63u) * 0.15;
    float ao = 1.0;
    float nvar = 0.0;  // погашенный наклон камешков (Токсвиг): по оси ~A²/3 на доле ячеек с камешком
    // Две решётки камешков в плоскости xz; ячейка — степень двойки, делит 256 м
    for (int k = 0; k < 2; ++k) {
        float sz = i.m.p2.x * (k == 0 ? 4.0 : 1.0);
        // Камешек мельче пикселя гаснет плавно (по пикселю кадра, не по ячейке со ступенями)
        float kf = 1.0 - smoothstep(sz * 0.12, sz * 0.35, i.foot);
        nvar += i.m.p2.w * i.m.p2.w / 3.0 * i.m.p2.y * 0.4 * (1.0 - kf * kf);
        if (kf <= 0.0) continue;
        vec3 pc2 = vec3(i.p.x, 0.0, i.p.z);
        ivec3 cid = world_cell(pc2, sz);
        uint seed = 64u + uint(k);
        if (hash3(cid, seed) > i.m.p2.y * (k == 0 ? 0.5 : 1.0) * (0.25 + dens)) continue;
        vec2 local = fract(pc2.xz / sz) - 0.5;
        vec2 ctr = (vec2(hash3(cid, seed + 7u), hash3(cid, seed + 9u)) - 0.5) * 0.4;
        // Камешек — повёрнутый эллипс разного размера, а не одинаковый круг по сетке
        float rad = 0.16 + hash3(cid, seed + 11u) * 0.26;
        float ang = hash3(cid, seed + 19u) * 3.14159;
        vec2 ax = vec2(cos(ang), sin(ang));
        vec2 rl = local - ctr;
        vec2 dv = vec2(dot(rl, ax), dot(rl, vec2(-ax.y, ax.x))) / (rad * vec2(1.0, 0.55 + 0.4 * hash3(cid, seed + 23u)));
        float d2 = dot(dv, dv);
        if (d2 >= 1.0) {
            ao = min(ao, 1.0 - 0.25 * kf * (1.0 - smoothstep(1.0, 1.6, d2)));  // тень у подножия камешка
            continue;
        }
        float hp = 0.55 + 0.4 * (1.0 - d2) + float(k) * 0.05;
        if (hp < height) continue;
        height = mix(height, hp, kf);
        float tone = 0.7 + hash3(cid, seed + 13u) * 0.6 * i.m.p2.z;
        vec3 hue = mix(vec3(1.0), vec3(1.08, 0.95, 0.85), hash3(cid, seed + 17u));
        c = mix(c, i.m.p0.rgb * tone * hue, kf);
        vec3 tx = normalize(vec3(1.0, 0.0, 0.0) - i.n * i.n.x);
        vec3 tz = normalize(cross(tx, i.n));
        n = normalize(i.n + (tx * dv.x - tz * dv.y) * (i.m.p2.w * kf));
        ao = 1.0;
    }
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = n;
    s.ao = ao;
    s.emissive = vec3(0.0);
    s.height = height;
    s.metal = 1.0;
    s.nvar = nvar;
    return s;
}
