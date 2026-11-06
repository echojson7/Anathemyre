// Песок: color — светлый, color2 — тёмный (пятна, впадины ряби). texel — квадрат зерна.
// extra.x — длина волны мелкой ряби (м, степень двойки), крупная рябь — в 8 раз длиннее; extra.y — наклон
// нормали на ряби, extra.z — размер пятен (м), extra.w — сила пятен (размер — степень двойки).
// Рябь поперёк ветра с запада-юго-запада (как наносы долины), гаснет, когда пиксель крупнее волны. На
// подветренных склонах (склон осыпания) ряби нет — там песок гладкий, с языками осыпей вниз по склону

// Рябь с длиной волны lam: фаза вдоль ветра, искривлённая шумом; две сетки со сдвигом полволны, смешанные
// шумом, — гребни раздваиваются и обрываются. Высота 0..1 (пологий наветренный склон, крутой подветренный),
// dh — наклон по фазе
float sand_ripple(vec3 p, float lam, vec2 wind, uint seed, out float dh) {
    FrameData f = pc.frame;
    // Фаза: доля от чанка камеры отдельно (целые чанки × 256 м), иначе рябь сдвинется при смене чанка
    vec2 cc = vec2(f.cam_chunk) * (256.0 / lam);
    float ph0 = fract(cc.x * wind.x) + fract(cc.y * wind.y);
    float bend = value_noise(p, lam * 16.0, seed) * 2.0 + value_noise(p, lam * 4.0, seed + 1u) * 0.35;
    float ph = ph0 + dot(p.xz, wind) / lam + bend;
    float sw = smoothstep(0.35, 0.65, value_noise(p, lam * 8.0, seed + 2u));
    float r = fract(ph + sw * 0.5);
    // Плавные склоны (smoothstep), иначе рябь — две полосы цвета с резкой границей
    float a = r < 0.7 ? r / 0.7 : (1.0 - r) / 0.3;
    dh = 6.0 * a * (1.0 - a) * (r < 0.7 ? 1.0 / 0.7 : -1.0 / 0.3);
    return a * a * (3.0 - 2.0 * a);
}

// Песок при ветре wind (куда дует, единичный вектор по xz). Его же зовёт вид sand_dune (песок дюн пустыни)
Surface sand_surf(SurfIn i, vec2 wind) {
    FrameData f = pc.frame;
    float t = i.m.p1.a;
    vec3 q = snap(i.p, t);
    float lam = i.m.p2.x;
    vec2 wp = vec2(f.cam_chunk) * CHUNK_M + i.p.xz;  // мировые метры: узор не прыгает со сменой чанка
    // Подветренный склон: земля уходит вниз по ветру, или склон крут, как склон осыпания (~30°). Там ряби нет
    float lee = max(smoothstep(0.1, 0.28, dot(i.n.xz, wind)), 1.0 - smoothstep(0.84, 0.88, i.n.y));
    float flat_ = smoothstep(0.82, 0.95, i.n.y);
    float strength = smoothstep(0.2, 0.6, value_noise(i.p, 8.0, 43u)) * 0.6 + 0.4;
    // Две рябы: мелкая (вблизи) и крупная (видна до сотен метров); гаснут плавно по пикселю кадра
    float dh1, dh2;
    float h1 = sand_ripple(i.p, lam, wind, 41u, dh1);
    float h2 = sand_ripple(i.p, lam * 8.0, normalize(wind + vec2(0.0, 0.12)), 51u, dh2);
    float on = (1.0 - lee) * mix(0.35, 1.0, flat_);
    float k1 = (1.0 - smoothstep(lam * 0.06, lam * 0.2, i.foot)) * on * strength;
    float k2 = (1.0 - smoothstep(lam * 0.25, lam * 0.8, i.foot)) * on * (0.55 + 0.45 * strength);
    vec3 tw = normalize(vec3(wind.x, 0.0, wind.y) - i.n * dot(i.n, vec3(wind.x, 0.0, wind.y)));
    vec3 n = normalize(i.n - tw * (dh1 * i.m.p2.y * 0.2 * k1 + dh2 * i.m.p2.y * 0.07 * k2));

    // Пятна плотного и рыхлого песка — плавным шумом в несколько октав (без квадратов шума значений)
    float blot = fbm_n(i.p, max(i.m.p2.z, 1.0), 4, 44u);
    vec3 c = mix(i.m.p0.rgb, i.m.p1.rgb, smoothstep(0.3, 0.7, blot) * i.m.p2.w);
    c *= 0.94 + 0.12 * fbm_n(i.p, 2.0, 2, 45u);
    // Полосы ветра: шум, вытянутый вдоль ветра, — светлые языки рыхлого песка и тёмные полосы плотного
    {
        vec2 r = vec2(dot(wp, wind) * 0.12, dot(wp, vec2(-wind.y, wind.x)));
        vec2 rf = fract(r / 8.0), ri = floor(r / 8.0);
        vec2 u = rf * rf * (3.0 - 2.0 * rf);
        float a00 = hash3(ivec3(int(ri.x), 0, int(ri.y)), 48u), a10 = hash3(ivec3(int(ri.x) + 1, 0, int(ri.y)), 48u);
        float a01 = hash3(ivec3(int(ri.x), 0, int(ri.y) + 1), 48u), a11 = hash3(ivec3(int(ri.x) + 1, 0, int(ri.y) + 1), 48u);
        float streak = mix(mix(a00, a10, u.x), mix(a01, a11, u.x), u.y);
        c *= 0.9 + 0.2 * mix(streak, 0.5, lee);
    }
    // Склон осыпания: языки осыпей вниз по склону — полосы поперёк склона, вытянутые вдоль него
    if (lee > 0.0) {
        vec2 d = normalize(i.n.xz + vec2(1e-5, 0.0));  // вниз по склону
        vec3 sp = vec3(dot(wp, vec2(-d.y, d.x)), 0.0, dot(wp, d) * 0.08);
        float tongue = grad_noise(sp, 2.0, 49u) * 0.6 + grad_noise(sp, 0.5, 50u) * 0.4;
        float kt = lee * (1.0 - smoothstep(0.04, 0.2, i.foot));
        c *= 1.0 + (tongue - 0.5) * 0.12 * kt;
        n = normalize(n + vec3(-d.y, 0.0, d.x) * (grad_noise(sp, 0.5, 50u) - 0.5) * 0.15 * kt);
    }
    float grain = cell_noise(q, t, 46u) - 0.5;
    c *= 1.0 + grain * 0.07;
    // Наветренные склоны ряби светлее, во впадинах оседает тёмная пыль; издали рябь — чуть темнее гладкого
    c *= 1.0 - 0.06 * (1.0 - h1) * k1 - 0.05 * (1.0 - h2) * k2 - 0.03 * on;
    Surface s;
    s.albedo = max(c, vec3(0.0));
    s.rough = i.m.p0.a;
    s.normal = n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    s.height = 0.2 + 0.15 * h1 * k1;
    return s;
}

Surface surf_sand(SurfIn i) { return sand_surf(i, vec2(24.0, 7.0) / 25.0); }
