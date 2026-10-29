#include "human.h"

#include "../sim/body.h"

// ---------------------------------------------------------------- поле расстояний

static f32 smin(f32 a, f32 b, f32 k)
{
    f32 h = clampf(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return lerpf(b, a, h) - k * h * (1.0f - h);
}

static f32 smax(f32 a, f32 b, f32 k) { return -smin(-a, -b, k); }

// Капсула с радиусом, меняющимся вдоль оси
static f32 sd_cone(V3 p, V3 a, V3 b, f32 ra, f32 rb)
{
    V3 pa = v3_sub(p, a), ba = v3_sub(b, a);
    f32 h = clampf(v3_dot(pa, ba) / v3_dot(ba, ba), 0.0f, 1.0f);
    return v3_len(v3_sub(pa, v3_mul(ba, h))) - lerpf(ra, rb, h);
}

static f32 sd_ell(V3 p, V3 c, V3 r)
{
    V3 q = v3_sub(p, c);
    f32 k0 = v3_len(v3(q.x / r.x, q.y / r.y, q.z / r.z));
    f32 k1 = v3_len(v3(q.x / (r.x * r.x), q.y / (r.y * r.y), q.z / (r.z * r.z)));
    return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -fminf(r.x, fminf(r.y, r.z));
}

#define L(m, f) lerpf((m), (f), F)

// Сустав плеча и запястья: у женщин плечи уже
static V3 shoulder_at(f32 F) { return v3(L(0.205f, 0.19f), 1.43f, 0.0f); }

// Тело без волос; q.x = |x| (тело симметрично)
static f32 body_sdf(V3 p, const HumanShape *s)
{
    const f32 F = s->female, M = s->muscle, A = s->fat;
    V3 q = v3(fabsf(p.x), p.y, p.z);

    // Корпус: грудная клетка, живот, таз, ягодицы. Мягкие стыки малые: крупные раздувают тело
    f32 d = sd_ell(q, v3(0, 1.30f, -0.005f), v3(L(0.148f, 0.128f) + 0.01f * M + 0.01f * A, 0.19f, L(0.102f, 0.092f) + 0.012f * A));
    d = smin(d, sd_ell(q, v3(0, 1.10f, 0.0f), v3(L(0.124f, 0.108f) + 0.03f * A, 0.15f, L(0.090f, 0.082f) + 0.035f * A)), 0.035f);
    d = smin(d, sd_ell(q, v3(0, 0.955f, -0.008f), v3(L(0.140f, 0.155f) + 0.015f * A, 0.115f, L(0.096f, 0.098f) + 0.01f * A)), 0.035f);
    d = smin(d, sd_ell(q, v3(L(0.055f, 0.06f), 0.905f, -0.045f), v3(L(0.066f, 0.074f), L(0.075f, 0.08f), L(0.052f, 0.06f) + 0.01f * A)), 0.02f);
    // Грудь: плоские мышцы у мужчин, грудь у женщин
    f32 pec = sd_ell(q, v3(0.064f, 1.36f, 0.068f), v3(0.075f, 0.055f, 0.012f + 0.008f * M));
    f32 breast = sd_ell(q, v3(0.068f, 1.335f, 0.07f), v3(0.05f, 0.046f, 0.044f));
    d = smin(d, lerpf(pec, breast, F), L(0.05f, 0.035f));
    // Лопатки, ключицы, трапеция, шея
    d = smin(d, sd_ell(q, v3(0.07f, 1.36f, -0.066f), v3(0.07f, 0.09f, 0.035f)), 0.03f);
    d = smin(d, sd_cone(q, v3(0.02f, 1.452f, 0.056f), v3(L(0.165f, 0.15f), 1.468f, 0.02f), 0.010f, 0.011f), 0.015f);
    d = smin(d, sd_cone(q, v3(0.035f, 1.52f, -0.028f), v3(L(0.17f, 0.155f), 1.455f, -0.01f), 0.035f + 0.01f * M, 0.026f), 0.03f);
    d = smin(d, sd_cone(q, v3(0, 1.42f, -0.012f), v3(0, 1.63f, 0.0f), L(0.060f, 0.047f) + 0.006f * M, L(0.054f, 0.042f)), 0.02f);
    // Пупок
    d = smax(d, -sd_ell(q, v3(0, 1.068f, 0.098f + 0.03f * A), v3(0.006f, 0.008f, 0.008f)), 0.004f);

    // Рука: дельта, плечо с бицепсом, предплечье
    V3 sh = shoulder_at(F);
    V3 el = v3(sh.x + 0.03f, 1.16f, -0.012f), wr = v3(sh.x + 0.04f, 0.955f, 0.012f);
    d = smin(d, sd_ell(q, v3(0, 1.40f, -0.012f), v3(L(0.17f, 0.15f) + 0.01f * M, 0.065f, L(0.085f, 0.078f))), 0.035f);
    d = smin(d, sd_ell(q, v3(sh.x - 0.004f, 1.40f, 0.0f), v3(0.046f + 0.01f * M, 0.075f, 0.055f + 0.008f * M)), 0.03f);
    d = smin(d, sd_cone(q, v3(sh.x, 1.43f, 0.0f), el, 0.046f + 0.008f * M + 0.01f * A, 0.035f), 0.02f);
    d = smin(d, sd_ell(q, v3_add(v3_lerp(sh, el, 0.55f), v3(0, 0, 0.016f)), v3(0.034f + 0.008f * M, 0.07f, 0.033f + 0.01f * M)), 0.02f);
    d = smin(d, sd_cone(q, el, wr, 0.042f + 0.004f * M, 0.026f), 0.02f);
    d = smin(d, sd_ell(q, v3_add(v3_lerp(el, wr, 0.25f), v3(0.004f, 0, 0.004f)), v3(0.041f, 0.07f, 0.037f)), 0.02f);
    // Кисть: ладонь ребром к бедру, четыре пальца и большой
    V3 h = v3_add(wr, v3(0.003f, -0.052f, 0.008f));
    f32 hand = sd_ell(q, h, v3(0.015f, 0.045f, 0.040f));
    static const f32 fz[4] = { 0.026f, 0.009f, -0.008f, -0.024f }, fl[4] = { 0.060f, 0.066f, 0.062f, 0.050f };
    for (int i = 0; i < 4; i++) {
        V3 a = v3(h.x, h.y - 0.032f, h.z + fz[i] * 0.85f);
        hand = smin(hand, sd_cone(q, a, v3(h.x - 0.006f, a.y - fl[i], a.z + 0.004f), 0.0085f, 0.007f), 0.006f);
    }
    hand = smin(hand, sd_cone(q, v3(h.x - 0.010f, h.y + 0.022f, h.z + 0.028f), v3(h.x - 0.018f, h.y - 0.028f, h.z + 0.046f), 0.0105f, 0.008f), 0.008f);
    d = smin(d, hand, 0.01f);

    // Нога: бедро с квадрицепсом, колено, голень с икрой, стопа
    const f32 hx = BODY_HIP_X;
    d = smin(d, sd_cone(q, v3(hx, 0.92f, 0), v3(hx, 0.53f, 0.005f), L(0.074f, 0.08f) + 0.01f * M + 0.02f * A, 0.051f), 0.03f);
    d = smin(d, sd_ell(q, v3(hx + 0.006f, 0.74f, 0.024f), v3(0.058f + 0.008f * M, 0.15f, 0.053f + 0.008f * M)), 0.03f);
    d = smin(d, sd_ell(q, v3(hx - 0.034f, 0.80f, 0.0f), v3(L(0.042f, 0.05f), 0.12f, 0.052f)), 0.03f);
    if (F > 0.05f) d = smin(d, sd_ell(q, v3(0.135f, 0.89f, -0.005f), v3(0.04f * F, 0.085f, 0.065f)), 0.03f);
    d = smin(d, sd_ell(q, v3(hx, 0.52f, 0.02f), v3(0.044f, 0.05f, 0.043f)), 0.02f);
    d = smin(d, sd_cone(q, v3(hx, 0.52f, 0.0f), v3(hx, 0.10f, -0.012f), 0.046f, 0.030f), 0.02f);
    d = smin(d, sd_ell(q, v3(hx + 0.003f, 0.40f, -0.028f), v3(0.047f + 0.006f * M, 0.10f, 0.047f + 0.006f * M)), 0.03f);
    d = smin(d, sd_ell(q, v3(hx, 0.088f, -0.01f), v3(0.034f, 0.025f, 0.030f)), 0.015f);
    f32 foot = sd_ell(q, v3(hx, 0.04f, -0.035f), v3(0.031f, 0.04f, 0.04f));
    foot = smin(foot, sd_cone(q, v3(hx, 0.045f, -0.01f), v3(hx + 0.008f, 0.022f, 0.15f), 0.034f, 0.023f), 0.02f);
    foot = smin(foot, sd_ell(q, v3(hx + 0.005f, 0.03f, 0.07f), v3(0.043f, 0.03f, 0.10f)), 0.02f);
    foot = smin(foot, sd_ell(q, v3(hx + 0.008f, 0.018f, 0.168f), v3(0.041f, 0.016f, 0.034f)), 0.01f);
    d = smin(d, foot, 0.02f);
    d = fmaxf(d, -p.y);  // подошва ровно на земле

    // Голова: череп, лицо, скулы, челюсть, подбородок
    f32 hd = sd_ell(q, v3(0, 1.695f, -0.012f), v3(L(0.074f, 0.070f), L(0.098f, 0.093f), L(0.098f, 0.093f)));
    hd = smin(hd, sd_ell(q, v3(0, 1.635f, 0.016f), v3(L(0.064f, 0.059f), 0.083f, 0.066f)), 0.035f);
    hd = smin(hd, sd_ell(q, v3(0.043f, 1.648f, 0.060f), v3(0.018f, 0.012f, 0.016f)), 0.01f);
    hd = smin(hd, sd_cone(q, v3(0.047f, 1.60f, 0.015f), v3(0.018f, 1.558f, 0.058f), L(0.020f, 0.015f), L(0.018f, 0.013f)), 0.012f);
    hd = smin(hd, sd_ell(q, v3(0, 1.558f, 0.066f), v3(L(0.020f, 0.016f), 0.018f, 0.015f)), 0.01f);
    // Надбровье, нос с крыльями, глазницы
    hd = smin(hd, sd_cone(q, v3(0.012f, 1.69f, 0.090f), v3(0.05f, 1.688f, 0.075f), L(0.010f, 0.006f), L(0.009f, 0.005f)), 0.01f);
    hd = smin(hd, sd_cone(q, v3(0, 1.676f, 0.091f), v3(0, 1.632f, 0.106f), 0.0065f, L(0.0105f, 0.009f)), 0.008f);
    hd = smin(hd, sd_ell(q, v3(0.012f, 1.630f, 0.099f), v3(0.008f, 0.007f, 0.008f)), 0.005f);
    hd = smax(hd, -sd_ell(q, v3(0.032f, 1.665f, 0.089f), v3(0.017f, 0.012f, 0.014f)), 0.005f);
    // Веки прикрывают глаз: видна щель около 8 мм
    hd = smin(hd, sd_ell(q, v3(0.032f, L(1.6715f, 1.673f), 0.078f), v3(0.0165f, 0.0075f, 0.0125f)), 0.003f);
    hd = smin(hd, sd_ell(q, v3(0.032f, 1.6575f, 0.077f), v3(0.015f, 0.0055f, 0.0115f)), 0.003f);
    // Губы и линия рта, уши
    hd = smin(hd, sd_ell(q, v3(0, 1.603f, L(0.086f, 0.087f)), v3(L(0.019f, 0.020f), L(0.0055f, 0.0062f), 0.006f)), 0.004f);
    hd = smin(hd, sd_ell(q, v3(0, 1.592f, 0.084f), v3(0.017f, L(0.006f, 0.0068f), 0.006f)), 0.004f);
    hd = smax(hd, -sd_ell(q, v3(0, 1.5975f, 0.092f), v3(0.016f, 0.0006f, 0.004f)), 0.001f);
    hd = smin(hd, sd_ell(q, v3(L(0.073f, 0.069f), 1.655f, -0.008f), v3(0.010f, 0.028f, 0.018f)), 0.006f);
    return smin(d, hd, 0.015f);
}

static f32 hash3(i32 x, i32 y, i32 z)
{
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + (u32)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (f32)((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

// Гладкий шум 0..1 по решётке шага 1
static f32 noise3(V3 p)
{
    i32 x = (i32)floorf(p.x), y = (i32)floorf(p.y), z = (i32)floorf(p.z);
    f32 fx = p.x - (f32)x, fy = p.y - (f32)y, fz = p.z - (f32)z;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
    f32 a = lerpf(lerpf(hash3(x, y, z), hash3(x + 1, y, z), fx), lerpf(hash3(x, y + 1, z), hash3(x + 1, y + 1, z), fx), fy);
    f32 b = lerpf(lerpf(hash3(x, y, z + 1), hash3(x + 1, y, z + 1), fx), lerpf(hash3(x, y + 1, z + 1), hash3(x + 1, y + 1, z + 1), fx), fy);
    return lerpf(a, b, fz);
}

// Волосы и борода отдельной оболочкой поверх черепа
static f32 hair_sdf(V3 p, const HumanShape *s)
{
    const f32 F = s->female;
    V3 q = v3(fabsf(p.x), p.y, p.z);
    V3 c = v3(0, 1.695f, -0.012f), r = v3(L(0.074f, 0.070f), L(0.098f, 0.093f), L(0.098f, 0.093f));
    f32 d0;
    {
        f32 off = s->hair_style == 1 ? 0.003f : (s->hair_style == 2 ? 0.012f : 0.010f);
        // Линия роста: высоко надо лбом, ниже у висков, у затылка до шеи
        f32 t = clampf((p.z + 0.05f) / 0.13f, 0.0f, 1.0f);
        f32 line = lerpf(1.60f, 1.722f, t * t * (3.0f - 2.0f * t)) - 0.012f * clampf(q.x / 0.07f, 0.0f, 1.0f) * t;
        // Пряди: толщина гуляет, край линии роста неровный
        off += 0.006f * (noise3(v3(p.x * 90.0f, p.y * 25.0f, p.z * 90.0f)) - 0.5f) * (s->hair_style == 1 ? 0.0f : 1.0f);
        line += 0.008f * (noise3(v3(p.x * 120.0f, 0.0f, p.z * 120.0f)) - 0.5f);
        d0 = smax(sd_ell(q, c, v3_add(r, v3(off, off, off))), line - p.y, 0.004f);
        // Уши открыты
        d0 = smax(d0, -sd_ell(q, v3(L(0.073f, 0.069f), 1.655f, -0.008f), v3(0.018f, 0.032f, 0.024f)), 0.004f);
    }
    f32 d = d0;
    if (s->hair_style == 2) {
        // Собранные в хвост
        d = smin(d, sd_ell(p, v3(0, 1.705f, -0.11f), v3(0.032f, 0.03f, 0.028f)), 0.012f);
        d = smin(d, sd_cone(p, v3(0, 1.69f, -0.12f), v3(0, 1.47f, -0.105f), 0.024f, 0.012f), 0.012f);
    }
    if (s->beard) {
        f32 b = smin(sd_cone(q, v3(0.047f, 1.60f, 0.015f), v3(0.018f, 1.558f, 0.058f), 0.030f, 0.028f),
                     sd_ell(q, v3(0, 1.555f, 0.068f), v3(0.028f, 0.027f, 0.026f)), 0.015f);
        b = smin(b, sd_ell(q, v3(0.012f, 1.611f, 0.090f), v3(0.022f, 0.006f, 0.010f)), 0.004f);  // усы
        b = smax(b, 1.546f - p.y + 0.04f * clampf(-p.z / 0.05f, 0.0f, 1.0f) - 0.04f, 0.004f);
        b = smax(b, p.y - 1.632f + 0.05f * clampf((0.06f - p.z) / 0.05f, 0.0f, 1.0f), 0.004f);
        b = smax(b, -sd_ell(q, v3(0, 1.597f, 0.092f), v3(0.019f, 0.009f, 0.02f)), 0.003f);  // рот открыт
        d = smin(d, b, 0.005f);
    }
    return d;
}

#undef L

// ---------------------------------------------------------------- куски сетки

typedef struct Piece {
    const HumanShape *s;
    u8  kind;      // 0 тело, 1 голова, 2 кисть, 3 волосы
} Piece;

#define FINE_GROW 0.0006f  // мелкие куски чуть толще тела: на стыке тело уходит внутрь

static f32 piece_sdf(V3 p, const Piece *pc)
{
    switch (pc->kind) {
    case 0: {
        // У стыка грубое тело уходит на 4 мм внутрь мелкого куска, иначе его край проступает швом
        f32 in = 0.004f * clampf((p.y - 1.545f) / 0.02f, 0.0f, 1.0f);
        if (fabsf(p.x) > 0.19f) in = fmaxf(in, 0.004f * clampf((0.995f - p.y) / 0.02f, 0.0f, 1.0f));
        f32 d = fmaxf(body_sdf(p, pc->s) + in, p.y - 1.585f);
        if (fabsf(p.x) > 0.205f) d = fmaxf(d, 0.96f - p.y);
        return d;
    }
    case 1: return fmaxf(body_sdf(p, pc->s) - FINE_GROW, 1.57f - p.y);
    case 2: return fmaxf(fmaxf(body_sdf(p, pc->s) - FINE_GROW, p.y - 0.975f), 0.205f - fabsf(p.x));
    default: return hair_sdf(p, pc->s);
    }
}

static f32 shape_sdf(V3 p, const Piece *pc) { return pc->kind == 3 ? hair_sdf(p, pc->s) : body_sdf(p, pc->s); }

// Цвет и материал вершины: кожа, бельё, губы, брови, волосы
static void paint(V3 p, const Piece *pc, V3 *col, u8 *mat)
{
    const HumanShape *s = pc->s;
    f32 ax = fabsf(p.x);
    *col = s->skin;
    *mat = MAT_SKIN;
    if (pc->kind == 3) {
        *col = s->hair;
        *mat = MAT_CLOTH;
        return;
    }
    f32 waist = lerpf(0.995f, 1.005f, s->female);
    bool briefs = ax < 0.2f && p.y < waist && p.y > 0.835f + 0.62f * fmaxf(0.0f, ax - 0.02f);
    bool bra = s->female > 0.5f && ax < 0.16f &&
               ((p.z > 0.0f && p.y > 1.262f && p.y < 1.372f) || (p.y > 1.29f && p.y < 1.325f));
    if (briefs || bra) {
        *col = s->under;
        *mat = MAT_CLOTH;
    } else if (ax < 0.024f && p.y > 1.585f && p.y < 1.609f && p.z > 0.086f) {
        *col = v3_hadamard(s->skin, v3(0.93f, 0.84f, 0.82f));
    } else if (ax > 0.012f && ax < 0.054f && p.y > 1.684f + 0.003f * s->female && p.y < 1.696f - 0.002f * s->female && p.z > 0.075f) {
        *col = v3_lerp(s->skin, s->hair, 0.75f);
    } else if (s->hair_style == 1 && p.y > 1.66f && p.z < 0.07f) {
        *col = v3_lerp(s->skin, s->hair, 0.35f);  // щетина на бритой голове
    }
}

// Ближайшая кость: по отрезку от сустава к суставу ребёнка (для будущей позы)
static u8 nearest_bone(V3 p)
{
    static const V3 leaf[BONE_COUNT] = {
        [BONE_HEAD] = { 0, 0.25f, 0 }, [BONE_HAND_L] = { 0, -0.15f, 0 }, [BONE_HAND_R] = { 0, -0.15f, 0 },
        [BONE_FOOT_L] = { 0, -0.05f, 0.15f }, [BONE_FOOT_R] = { 0, -0.05f, 0.15f },
    };
    f32 best = 1e9f;
    u8 bi = BONE_PELVIS;
    for (int i = 1; i < BONE_COUNT; i++) {
        V3 a = g_body_bones[i].joint, b = v3_add(a, leaf[i]);
        for (int j = i + 1; j < BONE_COUNT; j++)
            if (g_body_bones[j].parent == i) { b = g_body_bones[j].joint; break; }
        V3 pa = v3_sub(p, a), ba = v3_sub(b, a);
        f32 bb = v3_dot(ba, ba);
        f32 t = bb > 1e-8f ? clampf(v3_dot(pa, ba) / bb, 0.0f, 1.0f) : 0.0f;
        f32 dd = v3_len(v3_sub(pa, v3_mul(ba, t)));
        if (dd < best) { best = dd; bi = (u8)i; }
    }
    return bi;
}

static u8 to_u8(f32 x) { return (u8)(clampf(x, 0.0f, 1.0f) * 255.0f + 0.5f); }

// Surface nets: вершина в каждой ячейке, где поле меняет знак, четырёхугольник на каждое пересечённое ребро
static void nets(MeshBuilder *mb, Arena *scratch, const Piece *pc, V3 lo, V3 hi, f32 cell)
{
    i32 n[3] = { (i32)ceilf((hi.x - lo.x) / cell) + 1, (i32)ceilf((hi.y - lo.y) / cell) + 1, (i32)ceilf((hi.z - lo.z) / cell) + 1 };
    size_t mark = scratch->used;
    size_t nn = (size_t)n[0] * n[1] * n[2];
    f32 *val = ARENA_PUSH_ARRAY(scratch, f32, nn);
    i32 *vid = ARENA_PUSH_ARRAY(scratch, i32, nn);
    if (!val || !vid) { scratch->used = mark; return; }
#define NODE(i, j, k) (((size_t)(k) * n[1] + (size_t)(j)) * n[0] + (size_t)(i))
#define AT(i, j, k) v3(lo.x + (f32)(i) * cell, lo.y + (f32)(j) * cell, lo.z + (f32)(k) * cell)
    for (i32 k = 0; k < n[2]; k++)
        for (i32 j = 0; j < n[1]; j++)
            for (i32 i = 0; i < n[0]; i++) val[NODE(i, j, k)] = piece_sdf(AT(i, j, k), pc);

    static const i32 corner[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 0, 1 }, { 1, 0, 1 }, { 0, 1, 1 }, { 1, 1, 1 } };
    static const i32 edge[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (i32 k = 0; k + 1 < n[2]; k++)
        for (i32 j = 0; j + 1 < n[1]; j++)
            for (i32 i = 0; i + 1 < n[0]; i++) {
                vid[NODE(i, j, k)] = -1;
                f32 v[8];
                u32 inside = 0;
                for (int c = 0; c < 8; c++) {
                    v[c] = val[NODE(i + corner[c][0], j + corner[c][1], k + corner[c][2])];
                    inside |= (v[c] < 0.0f) << c;
                }
                if (inside == 0 || inside == 0xFF) continue;
                V3 sum = v3(0, 0, 0);
                f32 cnt = 0.0f;
                for (int e = 0; e < 12; e++) {
                    f32 a = v[edge[e][0]], b = v[edge[e][1]];
                    if ((a < 0.0f) == (b < 0.0f)) continue;
                    f32 t = a / (a - b);
                    const i32 *ca = corner[edge[e][0]], *cb = corner[edge[e][1]];
                    sum = v3_add(sum, v3((f32)ca[0] + t * (f32)(cb[0] - ca[0]), (f32)ca[1] + t * (f32)(cb[1] - ca[1]),
                                         (f32)ca[2] + t * (f32)(cb[2] - ca[2])));
                    cnt += 1.0f;
                }
                if (mb->vcount >= mb->vcap) { mb->overflow = true; continue; }
                V3 pos = v3_add(AT(i, j, k), v3_mul(sum, cell / cnt));
                f32 e = cell * 0.5f;
                // Нормаль по форме без срезов: на стыке кусков нет тёмного шва
                V3 nrm = v3_norm(v3(shape_sdf(v3_add(pos, v3(e, 0, 0)), pc) - shape_sdf(v3_sub(pos, v3(e, 0, 0)), pc),
                                    shape_sdf(v3_add(pos, v3(0, e, 0)), pc) - shape_sdf(v3_sub(pos, v3(0, e, 0)), pc),
                                    shape_sdf(v3_add(pos, v3(0, 0, e)), pc) - shape_sdf(v3_sub(pos, v3(0, 0, e)), pc)));
                V3 col;
                u8 mat;
                paint(pos, pc, &col, &mat);
                Vertex *vx = &mb->v[mb->vcount];
                *vx = (Vertex){ { pos.x, pos.y, pos.z }, { nrm.x, nrm.y, nrm.z },
                                { to_u8(col.x), to_u8(col.y), to_u8(col.z), 255 }, mat, nearest_bone(pos), 0, 0 };
                vid[NODE(i, j, k)] = (i32)mb->vcount++;
            }

    // Ребро вдоль оси ax от узла (i,j,k): четыре ячейки вокруг него
    for (int ax = 0; ax < 3; ax++) {
        int u = (ax + 1) % 3, w = (ax + 2) % 3;
        i32 c[3];
        for (c[2] = 0; c[2] < n[2]; c[2]++)
            for (c[1] = 0; c[1] < n[1]; c[1]++)
                for (c[0] = 0; c[0] < n[0]; c[0]++) {
                    if (c[ax] + 1 >= n[ax] || c[u] < 1 || c[w] < 1 || c[u] + 1 >= n[u] || c[w] + 1 >= n[w]) continue;
                    i32 d1[3] = { c[0], c[1], c[2] };
                    d1[ax]++;
                    f32 a = val[NODE(c[0], c[1], c[2])], b = val[NODE(d1[0], d1[1], d1[2])];
                    if ((a < 0.0f) == (b < 0.0f)) continue;
                    i32 q[4];
                    static const i32 du[4] = { 0, -1, -1, 0 }, dw[4] = { 0, 0, -1, -1 };
                    bool ok = true;
                    for (int t = 0; t < 4; t++) {
                        i32 cc[3] = { c[0], c[1], c[2] };
                        cc[u] += du[t];
                        cc[w] += dw[t];
                        q[t] = vid[NODE(cc[0], cc[1], cc[2])];
                        ok &= q[t] >= 0;
                    }
                    if (!ok || mb->icount + 6 > mb->icap) continue;
                    // Обход наружу: внутри у начала ребра — один порядок, иначе обратный
                    if (a >= 0.0f) { i32 tq = q[1]; q[1] = q[3]; q[3] = tq; }
                    // Делим по короткой диагонали
                    V3 p0 = v3(mb->v[q[0]].pos[0], mb->v[q[0]].pos[1], mb->v[q[0]].pos[2]);
                    V3 p1 = v3(mb->v[q[1]].pos[0], mb->v[q[1]].pos[1], mb->v[q[1]].pos[2]);
                    V3 p2 = v3(mb->v[q[2]].pos[0], mb->v[q[2]].pos[1], mb->v[q[2]].pos[2]);
                    V3 p3 = v3(mb->v[q[3]].pos[0], mb->v[q[3]].pos[1], mb->v[q[3]].pos[2]);
                    u32 *o = &mb->idx[mb->icount];
                    if (v3_len(v3_sub(p0, p2)) <= v3_len(v3_sub(p1, p3))) {
                        o[0] = (u32)q[0]; o[1] = (u32)q[1]; o[2] = (u32)q[2]; o[3] = (u32)q[0]; o[4] = (u32)q[2]; o[5] = (u32)q[3];
                    } else {
                        o[0] = (u32)q[0]; o[1] = (u32)q[1]; o[2] = (u32)q[3]; o[3] = (u32)q[1]; o[4] = (u32)q[2]; o[5] = (u32)q[3];
                    }
                    mb->icount += 6;
                }
    }
#undef NODE
#undef AT
    scratch->used = mark;
}

void human_build(MeshBuilder *mb, Arena *scratch, const HumanShape *s)
{
    Piece body = { s, 0 }, head = { s, 1 }, hands = { s, 2 }, hair = { s, 3 };
    nets(mb, scratch, &body, v3(-0.36f, -0.01f, -0.17f), v3(0.36f, 1.60f, 0.22f), 0.013f);
    nets(mb, scratch, &head, v3(-0.12f, 1.56f, -0.15f), v3(0.12f, 1.84f, 0.17f), 0.0055f);
    nets(mb, scratch, &hands, v3(0.20f, 0.72f, -0.08f), v3(0.33f, 0.98f, 0.11f), 0.0045f);
    nets(mb, scratch, &hands, v3(-0.33f, 0.72f, -0.08f), v3(-0.20f, 0.98f, 0.11f), 0.0045f);
    nets(mb, scratch, &hair, v3(-0.13f, 1.42f, -0.17f), v3(0.13f, 1.84f, 0.17f), 0.0065f);
    // Глаза: белок и тёмная радужка, на кости головы
    for (int side = -1; side <= 1; side += 2) {
        V3 c = v3(0.032f * (f32)side, 1.665f, 0.073f);
        mb_set(mb, v3(0.46f, 0.43f, 0.39f), MAT_SKIN, BONE_HEAD);
        mb_capsule(mb, c, v3_add(c, v3(0, 0, 0.0005f)), 0.0125f, 0.0125f, 12, 1.0f, 1.0f);
        mb_set(mb, v3(0.07f, 0.05f, 0.04f), MAT_SKIN, BONE_HEAD);
        mb_capsule(mb, v3_add(c, v3(0, 0, 0.0082f)), v3_add(c, v3(0, 0, 0.0088f)), 0.0068f, 0.0068f, 10, 1.0f, 1.0f);
    }
}
