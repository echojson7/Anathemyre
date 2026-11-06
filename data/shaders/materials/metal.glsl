// Ржавый металл: color — металл (краска), color2 — ржавчина. texel — квадрат узора; extra.x — доля ржавчины,
// extra.y — размер пятен ржавчины (м), extra.z — сила разброса
Surface surf_metal(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float patch_ = value_noise(i.p, max(i.m.p2.y, t), 31u) * 0.7 + value_noise(i.p, max(i.m.p2.y, t) * 0.3, 32u) * 0.3;
    float rust = step(1.0 - i.m.p2.x, patch_ + (cell_noise(q, t, 33u) - 0.5) * 0.15);
    float grain = cell_noise(q, t, 34u) - 0.5;
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb * (0.8 + 0.4 * cell_noise(q, t, 35u)), rust);
    c *= 1.0 + grain * i.m.p2.z;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = mix(i.m.p0.a, 1.0, rust);
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    return s;
}
