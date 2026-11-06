// Песок: color — светлый, color2 — тёмный (пятна, впадины ряби). texel — квадрат зерна.
// extra.x — длина волны ряби (м), extra.y — наклон нормали на ряби, extra.z — размер пятен (м),
// extra.w — сила пятен (размер — степень двойки). Рябь поперёк ветра с запада-юго-запада (как дюны), гаснет, когда ячейка крупнее волны
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

    // Пятна плотного и рыхлого песка — плавным шумом в несколько октав (без квадратов шума значений)
    float blot = fbm_n(i.p, max(i.m.p2.z, 1.0), 4, 44u);
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, smoothstep(0.3, 0.7, blot) * i.m.p2.w);
    // Наветренные склоны ряби светлее, во впадинах ряби оседает тёмная пыль
    c *= 0.94 + 0.12 * fbm_n(i.p, 2.0, 2, 45u);
    // Полосы ветра: шум, вытянутый вдоль ветра, — светлые языки рыхлого песка и тёмные полосы плотного.
    // Мировые метры (не от чанка камеры): узор не прыгает со сменой чанка
    {
        vec2 wp = vec2(f.cam_chunk) * CHUNK_M + i.p.xz;
        vec2 r = vec2(dot(wp, wind) * 0.12, dot(wp, vec2(-wind.y, wind.x)));
        vec2 rf = fract(r / 8.0), ri = floor(r / 8.0);
        vec2 u = rf * rf * (3.0 - 2.0 * rf);
        float a00 = hash3(ivec3(int(ri.x), 0, int(ri.y)), 48u), a10 = hash3(ivec3(int(ri.x) + 1, 0, int(ri.y)), 48u);
        float a01 = hash3(ivec3(int(ri.x), 0, int(ri.y) + 1), 48u), a11 = hash3(ivec3(int(ri.x) + 1, 0, int(ri.y) + 1), 48u);
        float streak = mix(mix(a00, a10, u.x), mix(a01, a11, u.x), u.y);
        c *= 0.88 + 0.24 * streak;
    }
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
