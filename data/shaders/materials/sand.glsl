// Песок: color — светлый, color2 — тёмный (пятна, впадины ряби). texel — квадрат зерна.
// extra.x — длина волны ряби (м), extra.y — наклон нормали на ряби, extra.z — размер пятен (м),
// extra.w — сила пятен. Рябь поперёк ветра с запада-юго-запада (как дюны), гаснет, когда ячейка крупнее волны
Surface surf_sand(SurfIn i) {
    FrameData f = pc.frame;
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float lam = i.m.p2.x;
    vec2 wind = vec2(24.0, 7.0) / 25.0;
    // Фаза: доля от чанка камеры отдельно (целые чанки × 256 м), иначе рябь сдвинется при смене чанка
    vec2 cc = vec2(f.cam_chunk) * (256.0 / lam);
    float ph0 = fract(cc.x * wind.x) + fract(cc.y * wind.y);
    float bend = value_noise(i.p, 4.0, 41u) * 2.0 + value_noise(i.p, 1.0, 42u) * 0.35;
    // Две сетки ряби со сдвигом половины волны, смешанные шумом: гребни раздваиваются и обрываются
    float ph = ph0 + dot(i.p.xz, wind) / lam + bend;
    float sw = smoothstep(0.35, 0.65, value_noise(i.p, 1.5, 47u));
    float r = fract(ph + sw * 0.5);
    // Пологий наветренный склон и крутой подветренный
    float hr = r < 0.7 ? r / 0.7 : (1.0 - r) / 0.3;
    float dh = r < 0.7 ? 1.0 / 0.7 : -1.0 / 0.3;
    float strength = smoothstep(0.2, 0.6, value_noise(i.p, 8.0, 43u)) * 0.75 + 0.25;
    // Гаснет плавно по пикселю кадра (не по ячейке: та растёт ступенями и дала бы видимую границу)
    float k = (1.0 - smoothstep(lam * 0.06, lam * 0.2, i.foot)) * smoothstep(0.82, 0.95, i.n.y) * strength;
    vec3 tw = normalize(vec3(wind.x, 0.0, wind.y) - i.n * dot(i.n, vec3(wind.x, 0.0, wind.y)));
    vec3 n = normalize(i.n - tw * (dh * i.m.p2.y * 0.3 * k));

    float blot = value_noise(i.p, max(i.m.p2.z, 1.0), 44u) * 0.7 + value_noise(i.p, max(i.m.p2.z, 1.0) * 0.25, 45u) * 0.3;
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, smoothstep(0.35, 0.75, blot) * i.m.p2.w);
    float grain = cell_noise(q, t, 46u) - 0.5;
    c *= 1.0 + grain * 0.07;
    c *= 1.0 - 0.05 * (1.0 - hr) * k;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.2 + 0.15 * hr * k;
    return s;
}
