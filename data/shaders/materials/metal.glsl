// Ржавый металл: color — металл (краска), color2 — ржавчина. texel — размер зерна (м); extra.x — доля ржавчины,
// extra.y — размер пятен ржавчины (м), extra.z — сила разброса.
// Край пятна ржавчины рваный (зерно в пороге), но плавный в пиксель: без квадратов и лесенки
Surface surf_metal(SurfIn i) {
    float t = i.m.p1.a;
    float ps = max(i.m.p2.y, t);
    float patch_ = value_noise(i.p, ps, 31u) * 0.7 + value_noise(i.p, ps * 0.3, 32u) * 0.3;
    float edge = patch_ + grain_noise(i.p, t, i.foot, 33u) * 0.15;
    // Скорость порога за метр: пятна ~1/ps, зерно ~0.15/t (пока зерно не погасло)
    float dx = 1.0 / ps + 0.15 / t * px_fade(t, i.foot);
    float rust = aa_step(1.0 - i.m.p2.x, edge, dx, i.foot);
    float grain = grain_noise(i.p, t, i.foot, 34u);
    vec3 rc = i.m.p1.rgb * (1.0 + 0.4 * grain_noise(i.p, t * 2.0, i.foot, 35u));
    vec3 c = mix(i.m.p0.rgb, rc, rust);
    c *= 1.0 + grain * i.m.p2.z;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = mix(i.m.p0.a, 1.0, rust);
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 1.0 - rust;
    s.nvar = 0.0;
    return s;
}
