// Голая земля: color — сухая земля, color2 — пыль и песок пятнами. texel — квадрат узора,
// extra.x — размер крупных пятен (м), extra.y — доля пятен пыли, extra.z — сила мелкого разброса
Surface surf_ground(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float big = value_noise(i.p, max(i.m.p2.x, 1.0), 11u) * 0.6 + value_noise(i.p, max(i.m.p2.x, 1.0) * 0.25, 12u) * 0.4;
    float dust = smoothstep(i.m.p2.y - 0.12, i.m.p2.y + 0.12, big);
    float grain = cell_noise(q, t, 13u) - 0.5;
    float clump = value_noise(i.p, t * 4.0, 14u) - 0.5;
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, dust);
    c *= 1.0 + (grain * 0.6 + clump * 0.8) * i.m.p2.z;
    // Редкие камешки: тёмные квадраты узора
    if (cell_noise(q, t, 15u) > 0.985) c *= 0.55;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    return s;
}
