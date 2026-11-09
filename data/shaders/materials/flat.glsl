// Ровный цвет без узора (и «missing» — потерянный материал): color, rough
Surface surf_flat(SurfIn i) {
    Surface s;
    s.albedo = i.m.p0.rgb;
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 1.0;
    s.nvar = 0.0;
    return s;
}
