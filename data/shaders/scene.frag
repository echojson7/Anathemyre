// Освещение и процедурные материалы. Выход в линейном HDR

in vec3 v_world;
in vec3 v_nrm;
in vec4 v_color;   // a — параметр материала (1 по умолчанию): мох на бетоне, светлая кора, глубина листвы
flat in uint v_mat;
in vec3 v_local;
flat in mat3 v_rot;
flat in vec3 v_tint;

layout(location = 17) uniform int u_skinned;
layout(location = 18) uniform mat4 u_light_vp;
layout(location = 20) uniform vec3 u_cam_pos;
layout(location = 21) uniform vec3 u_sun_dir;
layout(location = 22) uniform vec3 u_sun_color;
layout(location = 23) uniform float u_time;
layout(location = 24) uniform vec3 u_sky_amb;
layout(location = 25) uniform vec3 u_ground_amb;
layout(location = 27) uniform float u_texel_on; // 0 — пиксельность выключена (RenderSettings.pixel)
layout(location = 26) uniform int u_unlit;   // портреты интерфейса: свой неподвижный свет
layout(binding = 0) uniform sampler2DShadow u_shadow;

out vec4 o_color;

const uint MAT_CONCRETE = 0u, MAT_RUST = 1u, MAT_SAND = 2u, MAT_CLOTH = 3u, MAT_LEATHER = 4u,
           MAT_GLYPH = 5u, MAT_EMISSIVE = 6u, MAT_SKIN = 7u, MAT_METAL = 8u,
           MAT_TERRAIN = 9u, MAT_ROCK = 10u, MAT_WATER = 11u, MAT_BARK = 12u, MAT_LEAF = 13u, MAT_GRASS = 14u;

const float TEXEL = 1.0 / 10.0;  // «пиксель» на поверхностях, м

// Сдвиг точки p к центру квадрата сетки t в плоскости грани с нормалью n.
// Вдоль оси грани точка не прыгает, а возвращается на плоскость грани
vec3 texel_snap(vec3 p, vec3 n, float t)
{
    vec3 a = abs(n);
    vec3 d = (floor(p / t) + 0.5) * t - p;
    if (a.x >= a.y && a.x >= a.z) d.x = -(n.y * d.y + n.z * d.z) / n.x;
    else if (a.y >= a.z) d.y = -(n.x * d.x + n.z * d.z) / n.y;
    else d.z = -(n.x * d.x + n.y * d.y) / n.z;
    return d;
}

float sun_shadow(vec3 p, vec3 n)
{
    vec3 off = p + n * 0.06 + u_sun_dir * 0.03;
    vec4 lp = u_light_vp * vec4(off, 1.0);
    vec3 s = lp.xyz / lp.w * 0.5 + 0.5;
    if (any(lessThan(s.xy, vec2(0.0))) || any(greaterThan(s.xy, vec2(1.0)))) return 1.0;
    float texel = 1.0 / 2048.0;
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++)
            sum += texture(u_shadow, vec3(s.xy + vec2(x, y) * texel * 1.2, s.z));
    return sum / 9.0;
}

// Знаки на неизвестном языке пиксельным шрифтом: ячейка 6x8 квадратов TEXEL (знак 5x7 и зазор),
// штрих шириной в квадрат, поэтому знак читается при любой дальности
float glyph_ink(vec2 uv)
{
    vec2 g = floor(uv / TEXEL);
    vec2 cell = floor(g / vec2(6.0, 8.0));
    vec2 f = g - cell * vec2(6.0, 8.0);
    if (f.x > 4.0 || f.y > 6.0) return 0.0;
    if (hash12(cell + 7.1) < 0.18) return 0.0; // пробел
    float ink = 0.0;
    // Вертикальные штрихи в столбцах 0, 2, 4
    for (int i = 0; i < 3; i++) {
        float h = hash12(cell * 1.31 + float(i) * 3.7);
        if (h > 0.4) {
            float y0 = floor(hash12(cell + float(i) * 11.3) * 2.0) * 3.0;
            float y1 = min(y0 + 3.0 + floor(h * 2.0) * 3.0, 6.0);
            ink = max(ink, float(f.x == float(i * 2) && f.y >= y0 && f.y <= y1));
        }
    }
    // Горизонтальные в строках 0, 3, 6
    for (int i = 0; i < 3; i++) {
        float h = hash12(cell * 2.17 + float(i) * 5.3);
        if (h > 0.5) {
            float x0 = floor(hash12(cell + float(i) * 17.9) * 2.0) * 2.0;
            float x1 = min(x0 + 2.0 + floor(h * 2.0) * 2.0, 4.0);
            ink = max(ink, float(f.y == float(i * 3) && f.x >= x0 && f.x <= x1));
        }
    }
    // Иногда точка-метка в середине
    if (hash12(cell + 91.0) > 0.8) ink = max(ink, float(f.x == 2.0 && (f.y == 4.0 || f.y == 2.0)));
    return ink;
}

void main()
{
    vec3 N = normalize(v_nrm);
    if (!gl_FrontFacing) N = -N;
    // Материал и свет считаются в центре квадрата TEXEL (как текстура низкого разрешения),
    // одинаково на всех поверхностях. Где квадрат мельче пикселя кадра, пикселем служит пиксель кадра
    vec3 Nl = normalize(transpose(v_rot) * N);
    float mpp = max(length(dFdx(v_local)), length(dFdy(v_local)));
#ifdef VRS
    // При шейдинге блоком производные берутся через блок: приводим к пикселю кадра
    mpp /= float(gl_FragmentSizeNV.x);
#endif
    // На людях квадрат вдвое мельче, чтобы читались лицо и кисти
    float base = u_skinned == 1 ? TEXEL * 0.5 : TEXEL;
    bool snap = mpp < base * u_texel_on;
    vec3 dl = snap ? texel_snap(v_local, Nl, base) : vec3(0.0);
    vec3 P = v_local + dl;           // материал: прилипает к телу
    vec3 W = v_world + v_rot * dl;   // свет и тени
    vec3 V = normalize(u_cam_pos - W);
    vec3 L = u_sun_dir;

    vec3 albedo = v_color.rgb * v_tint;
    bool foliage = false;
    vec3 emissive = vec3(0.0);
    float spec = 0.0;
    float gloss = 16.0;
    float ao = 1.0;

    // Мелкий шум в мировых координатах, стабилен при движении
    float fine = noise3(P * 9.0);

    if (v_mat == MAT_CONCRETE) {
        float big = fbm3(P * 0.18);
        float mid = fbm3(P * 1.1 + 4.0);
        // Опалубка: швы между плитами
        vec2 pan = abs(fract(vec2(P.x + P.z, P.y) / vec2(1.6, 1.2)) - 0.5);
        float seam = smoothstep(0.485, 0.5, max(pan.x, pan.y));
        // Потёки сверху вниз на стенах
        float streak = noise3(vec3(P.x * 3.0, P.y * 0.15, P.z * 3.0));
        float vertical = 1.0 - abs(N.y);
        albedo *= 0.75 + 0.45 * big;
        albedo *= mix(1.0, 0.65 + 0.5 * streak, vertical * 0.8);
        albedo *= 1.0 - seam * 0.35 * vertical;
        // Плиты пола 4x4 м и пятна
        vec2 tile = abs(fract(P.xz / 4.0) - 0.5);
        float tseam = smoothstep(0.488, 0.5, max(tile.x, tile.y));
        float stain = smoothstep(0.45, 0.75, fbm3(vec3(P.xz * 0.35, 1.0)));
        albedo *= 1.0 - (tseam * 0.4 + stain * 0.3) * (1.0 - vertical);
        albedo *= 0.88 + 0.24 * mid;
        // Ржавые подтёки
        float rust = smoothstep(0.62, 0.8, mid * streak + 0.2 * big) * vertical;
        albedo = mix(albedo, vec3(0.30, 0.13, 0.05), rust * 0.6);
        albedo *= 0.92 + 0.16 * fine;
        // Мох на заросшем бетоне: сверху и пятнами
        float mossy = 1.0 - v_color.a;
        if (mossy > 0.0) {
            float mm = smoothstep(0.4, 0.62, fbm3(P * 0.6 + 11.0) + N.y * 0.3 - vertical * 0.1);
            albedo = mix(albedo, vec3(0.055, 0.08, 0.03) * (0.8 + 0.4 * fine), mm * mossy);
        }
        // Грязь у основания стен
        if (u_skinned == 0) ao = mix(1.0, smoothstep(-0.2, 2.5, P.y), vertical * 0.6);
    } else if (v_mat == MAT_RUST) {
        float m = fbm3(P * 2.5);
        float flake = smoothstep(0.45, 0.6, fbm3(P * 7.0 + 3.0));
        albedo = mix(albedo, vec3(0.28, 0.10, 0.04), m * 0.8);
        albedo = mix(albedo, vec3(0.16, 0.15, 0.14), flake * 0.6);
        albedo *= 0.85 + 0.3 * fine;
        spec = 0.25 * flake;
        gloss = 24.0;
    } else if (v_mat == MAT_SAND) {
        float dunes = fbm3(vec3(P.x * 0.05, 0.0, P.z * 0.05));
        float ripples = noise3(vec3(P.x * 2.5 + P.z * 0.6, 0.0, P.z * 0.4));
        albedo *= 0.75 + 0.4 * dunes;
        albedo *= 0.9 + 0.15 * ripples;
        albedo *= 0.9 + 0.2 * fine;
    } else if (v_mat == MAT_CLOTH) {
        float weave = noise3(P * 40.0);
        float dirt = fbm3(P * 3.0);
        albedo *= 0.8 + 0.25 * weave;
        albedo *= 0.75 + 0.4 * dirt;
        // Подол и низ в пыли
        albedo = mix(albedo, vec3(0.33, 0.29, 0.22), smoothstep(1.0, 0.2, P.y) * 0.5);
    } else if (v_mat == MAT_LEATHER) {
        albedo *= 0.75 + 0.4 * fbm3(P * 6.0);
        spec = 0.15;
        gloss = 12.0;
    } else if (v_mat == MAT_SKIN) {
        albedo *= 0.9 + 0.15 * fine;
        spec = 0.05;
    } else if (v_mat == MAT_METAL) {
        float m = fbm3(P * 4.0);
        albedo *= 0.7 + 0.5 * m;
        spec = 0.6;
        gloss = 40.0;
    } else if (v_mat == MAT_GLYPH) {
        // Знаки на плите; цвет вершины — цвет свечения
        // На стене строки идут по высоте, на полу — по плоскости XZ
        vec3 t = abs(Nl.y) > 0.9 ? vec3(1, 0, 0) : normalize(cross(vec3(0, 1, 0), Nl));
        vec2 uv = abs(Nl.y) > 0.9 ? P.xz : vec2(dot(P, t), P.y);
        float ink = glyph_ink(uv);
        float flicker = 0.75 + 0.25 * step(0.15, noise3(vec3(u_time * 7.0, uv.y * 0.5, 0.0)));
        float dim = 0.6 + 0.4 * noise3(vec3(uv * 0.7, u_time * 0.3));
        albedo = vec3(0.07, 0.07, 0.065) * (0.8 + 0.4 * fbm3(P * 3.0));
        emissive = v_color.rgb * ink * flicker * dim * 1.1;
        albedo *= 1.0 - ink * 0.6;
    } else if (v_mat == MAT_EMISSIVE) {
        float flicker = 0.85 + 0.15 * noise3(vec3(u_time * 11.0, P.x, P.z));
        emissive = v_color.rgb * 3.0 * flicker;
        // Почти чёрный цвет — проём в темноту (двери, окна): солнце его не освещает
        albedo = vec3(max(v_color.r, max(v_color.g, v_color.b)) < 0.02 ? 0.006 : 0.05);
    } else if (v_mat == MAT_TERRAIN) {
        // Земля: цвет вершин — трава, сухая трава, глина, ил; на крутом склоне слоистая порода
        float spots = fbm3(vec3(P.xz * 0.45, 3.0));
        albedo *= 0.78 + 0.32 * spots;
        albedo *= 0.88 + 0.22 * noise3(vec3(P.xz * 2.3, 7.0));
        albedo *= 0.92 + 0.16 * fine;
        float rock = smoothstep(0.80, 0.62, N.y + 0.1 * (spots - 0.5));
        float strata = noise3(vec3(P.x * 0.15, P.y * 1.6 + fbm3(P * 0.2) * 2.0, P.z * 0.15));
        vec3 stone = vec3(0.14, 0.135, 0.125) * (0.6 + 0.6 * strata) * (0.85 + 0.3 * fbm3(P * 1.3));
        stone = mix(stone, vec3(0.30, 0.19, 0.11), smoothstep(0.6, 0.8, fbm3(P * 0.4 + 9.0)) * 0.5);
        albedo = mix(albedo, stone, rock);
        spec = 0.04;
    } else if (v_mat == MAT_ROCK) {
        albedo *= 0.7 + 0.5 * fbm3(P * 1.7);
        albedo *= 0.9 + 0.2 * fine;
        // Лишайник пятнами и мох сверху
        albedo = mix(albedo, vec3(0.24, 0.25, 0.18), smoothstep(0.64, 0.72, fbm3(P * 2.3 + 5.0)) * 0.5);
        float moss = smoothstep(0.45, 0.85, N.y) * smoothstep(0.35, 0.6, fbm3(P * 0.9 + 2.0));
        albedo = mix(albedo, vec3(0.06, 0.09, 0.03), moss * 0.85);
        spec = 0.08;
        gloss = 20.0;
    } else if (v_mat == MAT_BARK) {
        // Кора: продольные борозды по оси ствола, мох у комля
        albedo *= 0.6 + 0.6 * noise3(vec3(P.x * 14.0, P.y * 1.2, P.z * 14.0));
        albedo *= 0.85 + 0.3 * fbm3(P * 2.0);
        if (v_color.a < 0.5) albedo *= 1.0 - 0.8 * step(0.78, noise3(vec3(P.x * 7.0, P.y * 11.0, P.z * 7.0)));
        float moss = smoothstep(0.5, 0.75, fbm3(P * 1.5 + 1.0)) * smoothstep(2.5, 0.0, P.y);
        albedo = mix(albedo, vec3(0.06, 0.09, 0.03), moss * 0.7);
    } else if (v_mat == MAT_LEAF) {
        // Листва: глубже в кроне темнее
        albedo *= 0.72 + 0.5 * noise3(P * 2.2);
        albedo *= 0.9 + 0.2 * fine;
        ao = mix(0.4, 1.0, v_color.a);
        spec = 0.08;
        gloss = 10.0;
        foliage = true;
    } else if (v_mat == MAT_GRASS) {
        albedo *= 0.85 + 0.3 * noise3(P * 5.0);
        ao = mix(0.5, 1.0, v_color.a);
        foliage = true;
    } else if (v_mat == MAT_WATER) {
        // Вода: рябь по течению (+Z), небо по Френелю, блик солнца; цвет вершин — толща воды
        vec2 q = P.xz * vec2(0.9, 0.6) + vec2(0.0, -u_time * 0.45);
        float tq = u_time * 0.25, e = 0.15;
        float h0 = fbm3(vec3(q, tq));
        float hx = fbm3(vec3(q + vec2(e, 0.0), tq));
        float hz = fbm3(vec3(q + vec2(0.0, e), tq));
        N = normalize(vec3(-(hx - h0) / e * 0.3, 1.0, -(hz - h0) / e * 0.3));
        float fres = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        albedo *= 1.0 - fres;
        emissive = u_sky_amb * 2.0 * fres;
        spec = 2.0;
        gloss = 300.0;
    }

    // Разброс яркости от квадрата к квадрату: поверхность читается как текстура низкого разрешения
    if (snap) albedo *= 0.95 + 0.10 * hash13(floor(P / base) + float(v_mat) * 17.0);

    // Листва и трава просвечивают: свет заходит и на обратную сторону
    float ndl = foliage ? 0.25 + 0.5 * max(dot(N, L), 0.0) + 0.25 * abs(dot(N, L)) : max(dot(N, L), 0.0);
    float sh = ndl > 0.0 ? sun_shadow(W, N) : 0.0;

    // Небо и отражённый от песка свет (0.6 — тени темнее, решение пользователя 2026-10-29)
    vec3 ambient = mix(u_ground_amb, u_sky_amb, N.y * 0.5 + 0.5) * 0.6;
    vec3 Lflat = normalize(vec3(-L.x, 0.0, -L.z));
    float bounce = max(dot(N, -Lflat), 0.0) * 0.08 + max(-N.y, 0.0) * 0.05;
    ambient += u_ground_amb * 1.5 * bounce;

    vec3 H = normalize(L + V);
    float sp = spec * pow(max(dot(N, H), 0.0), gloss) * sh;

    vec3 col = albedo * (u_sun_color * ndl * sh + ambient * ao) + u_sun_color * sp + emissive;
    // Контровой свет от дымки: фигуры читаются силуэтом
    if (u_skinned == 1) {
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        col += (u_sky_amb + u_ground_amb) * rim * 0.35;
    }
    // Портрет: неподвижный свет спереди-сверху-слева, без солнца и теней мира
    if (u_unlit != 0) col = albedo * (0.25 + 0.75 * max(dot(N, normalize(vec3(-0.5, 0.7, 0.6))), 0.0)) + emissive;
    // Альфа выше 1 — метка размытия на коже (atmos.frag); в портрете не нужна
    o_color = vec4(col, 1.0 + (v_mat == MAT_SKIN && u_unlit == 0 ? 1.0 - v_color.a : 0.0));
}
