// Бетон: color — основной, color2 — потёки и грязь. texel — квадрат узора; extra.x — шаг швов опалубки по
// высоте (м, 0 — без швов), extra.y — сила потёков, extra.z — сила разброса
Surface surf_concrete(SurfIn i) {
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float grain = cell_noise(q, t, 21u) - 0.5;
    float blot = value_noise(i.p, 0.5, 22u) - 0.5;
    vec3 c = i.m.p0.rgb * (1.0 + grain * i.m.p2.z + blot * 0.25);
    // Поры: редкие тёмные квадраты
    if (cell_noise(q, t, 23u) > 0.97) c *= 0.7;
    // Швы опалубки: тёмная строка квадратов на каждом шаге по высоте
    float step_m = i.m.p2.x;
    if (step_m > 0.0) {
        float k = fract(q.y / step_m);
        if (k < t / step_m) c *= 0.78;
    }
    // Потёки вниз: столбцы по горизонтали с разной длиной, сильнее у верха плиты
    if (abs(i.n.y) < 0.5) {
        vec3 col = vec3(q.x, 0.0, q.z);
        float streak = cell_noise(col, t * 2.0, 24u);
        float run = value_noise(vec3(q.x, q.y * 0.15, q.z), 1.0, 25u);
        float amt = smoothstep(0.55, 0.95, streak) * smoothstep(0.3, 0.8, run) * i.m.p2.y;
        c = mix(c, i.m.p1.rgb, amt);
    }
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = i.n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.5;
    return s;
}
