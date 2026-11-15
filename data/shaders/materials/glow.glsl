// Светящаяся поверхность (стекло лампы, табло): color — цвет, extra.x — яркость, кд/м². Сама светится, но мир
// не освещает: свет на стены дают огни (docs/light.md, этап 6е)
Surface surf_glow(SurfIn i) {
    Surface s;
    s.albedo = i.m.p0.rgb * 0.6;
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = i.m.p0.rgb * i.m.p2.x;
    s.height = 0.5;
    s.metal = 0.0;
    s.nvar = 0.0;
    return s;
}
