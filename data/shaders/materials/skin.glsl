// Кожа человека (docs/people.md, «Тело»): тон — от зерна экземпляра (младший байт, appear_skin_seed): color —
// самый светлый, color2 — самый тёмный. Узора пока нет: он появится вместе с координатами позы покоя (одежда)
Surface surf_skin(SurfIn i) {
    Surface s;
    float tone = float(i.seed & 255u) / 255.0;
    // Тёмные тона насыщеннее: смесь в «гамме», а не по прямой — середина ряда не выходит серой
    s.albedo = pow(mix(pow(i.m.p0.rgb, vec3(0.5)), pow(i.m.p1.rgb, vec3(0.5)), tone), vec3(2.0));
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    s.metal = 1.0;
    s.nvar = 0.0;
    return s;
}
