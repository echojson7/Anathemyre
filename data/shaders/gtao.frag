// УГЛЫ: GTAO (Jimenez 2016) в половинном разрешении: в каждом срезе через точку ищутся горизонты по обе стороны,
// открытость — косинусная доля неба между ними с учётом нормали. R — открытость, G — дальность (для апскейла)

layout(location = 0) out vec2 o_gtao;

const float GTAO_R = 1.0;      // радиус поиска, м: как у прежнего AO
const int   GTAO_SLICES = 2;
const int   GTAO_STEPS = 6;    // шагов в каждую сторону среза
const float HALF_PI = 1.5707963;

void main()
{
    ivec2 t = min(ivec2(gl_FragCoord.xy) * 2, textureSize(u_depth, 0) - 1);
    vec2 res = vec2(textureSize(u_depth, 0));
    vec2 uv = (vec2(t) + 0.5) / res;
    float d = texelFetch(u_depth, t, 0).r;
    if (d >= 1.0) { o_gtao = vec2(1.0, 4000.0); return; }
    vec3 P = world_at(uv, d);
    float dist = length(P - u_cam_pos);
    vec3 V = (u_cam_pos - P) / dist;
    vec3 N = depth_normal(uv);
    if (dot(N, V) < 0.0) N = -N;
    float radius_px = clamp(GTAO_R * u_proj_scale / max(dist, 0.1), 3.0, 64.0);
    // Поворот срезов и сдвиг шагов — узор 4x4 (gtao_blur.frag размывает ровно его): без зерна и мерцания
    ivec2 q = ivec2(gl_FragCoord.xy) & 3;
    float rot = float(q.x * 4 + q.y) / 16.0;
    float jit = float(((q.x * 4 + q.y) * 7) & 15) / 16.0;
    float vis = 0.0;
    for (int s = 0; s < GTAO_SLICES; s++) {
        float phi = (rot + float(s)) * 3.1415927 / float(GTAO_SLICES);
        vec2 dir = vec2(cos(phi), sin(phi));
        // Направление среза в мире, перпендикулярно взгляду
        vec3 tw = world_at(uv + dir / res, d) - P;
        vec3 T = normalize(tw - V * dot(tw, V));
        vec3 axis = cross(T, V);
        vec3 pn = N - axis * dot(N, axis);
        float plen = max(length(pn), 1e-4);
        float cos_n = clamp(dot(pn, V) / plen, -1.0, 1.0);
        float n = sign(dot(pn, T)) * acos(cos_n);
        float hc[2] = float[2](-1.0, -1.0);
        for (int side = 0; side < 2; side++) {
            vec2 sd = side == 0 ? dir : -dir;
            for (int j = 0; j < GTAO_STEPS; j++) {
                float r = 1.0 + (radius_px - 1.0) * (float(j) + jit) / float(GTAO_STEPS);
                vec2 suv = uv + sd * r / res;
                vec3 S = world_at(suv, texture(u_depth, suv).r);
                vec3 w = S - P;
                float l = length(w);
                // Дальше радиуса горизонт гаснет: стена в 5 м не затеняет угол
                float c = mix(-1.0, dot(w, V) / max(l, 1e-4), 1.0 - smoothstep(0.6 * GTAO_R, GTAO_R, l));
                hc[side] = max(hc[side], c);
            }
        }
        float h2 = n + min(acos(hc[0]) - n, HALF_PI);
        float h1 = n + max(-acos(hc[1]) - n, -HALF_PI);
        float sn = sin(n);
        vis += plen * 0.25 * ((-cos(2.0 * h1 - n) + cos_n + 2.0 * h1 * sn) + (-cos(2.0 * h2 - n) + cos_n + 2.0 * h2 * sn));
    }
    o_gtao = vec2(clamp(vis / float(GTAO_SLICES), 0.0, 1.0), dist);
}
