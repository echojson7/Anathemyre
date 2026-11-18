#include "appear.h"
#include "view.h"
#include "../sim/world.h"
#include "../core/hash.h"
#include "../core/mathf.h"

const char *const APPEAR_MORPH_NAMES[AM_COUNT] = {
    "female", "full",  "thin", "strong", "old",   "belly", "female_full", "female_thin", "female_strong",
    "shoulders", "hips", "legs", "torso", "arms", "neck",  "head",        "kin_a",       "kin_b",
    "nose",   "nose_hump", "jaw", "chin", "lips", "eyes",  "cheeks",      "brow",        "ears",
};

Appear appear_make(u32 seed) {
    Appear a{};
    Dice d{0, 0, (u64)seed | 0xB0D1000000000ull};
    f32 *m = a.morph;
    a.female = d.byte() < 128;
    const f32 fem = a.female ? 1.0f : 0.0f;
    m[AM_FEMALE] = fem;
    // Рост: мужчины 1.65–1.91, женщины 1.54–1.78 (в бете было 1.49–1.94)
    a.height = a.female ? 1.66f + d.bell() * 0.12f : 1.78f + d.bell() * 0.13f;
    // Сложение: выжившие в пустыне чаще сухие, чем полные
    f32 w = d.bell() * 1.1f - 0.15f;
    m[AM_FULL] = f_clamp(w, 0.0f, 0.9f);
    m[AM_THIN] = f_clamp(-w, 0.0f, 0.8f);
    f32 mu = d.uni();
    m[AM_STRONG] = mu * mu * (a.female ? 0.55f : 0.85f);
    // Возраст: каждый седьмой старый, остальные — чуть старше или моложе
    f32 age = d.uni(), age2 = d.uni();
    m[AM_OLD] = age > 0.857f ? 0.35f + age2 * 0.4f : age2 * 0.18f;
    m[AM_BELLY] = f_clamp(m[AM_FULL] * 0.6f + m[AM_OLD] * 0.3f + d.bell() * 0.35f - (a.female ? 0.25f : 0.1f), 0.0f, 0.8f);
    m[AM_FEMALE_FULL] = fem * m[AM_FULL];
    m[AM_FEMALE_THIN] = fem * m[AM_THIN];
    m[AM_FEMALE_STRONG] = fem * m[AM_STRONG];
    // Пропорции
    m[AM_SHOULDERS] = f_clamp(d.bell() * 0.7f + m[AM_STRONG] * 0.3f, -0.8f, 0.9f);
    m[AM_HIPS] = d.bell() * 0.6f;
    m[AM_LEGS] = d.bell() * 0.32f;
    m[AM_TORSO] = d.bell() * 0.25f;
    m[AM_ARMS] = d.bell() * 0.25f;
    m[AM_NECK] = d.bell() * 0.5f;
    m[AM_HEAD] = d.bell() * 0.4f;
    // Род: точка между тремя наборами черт MakeHuman (квадраты — чаще ближе к одному из них)
    f32 k0 = d.uni(), k1 = d.uni(), k2 = d.uni();
    k0 *= k0;
    k1 *= k1;
    k2 *= k2;
    f32 ks = k0 + k1 + k2;
    if (ks < 1e-4f) k0 = k1 = k2 = 1.0f / 3.0f;
    else { k0 /= ks; k1 /= ks; k2 /= ks; }
    // Вполсилы (2026-11-17): у краёв наборы MakeHuman карикатурны, лицо беты — середина
    m[AM_KIN_A] = (k0 - k2) * 0.6f;
    m[AM_KIN_B] = (k1 - k2) * 0.6f;
    // Тон кожи: темнее у первого рода, все загорелые
    a.skin = f_clamp(0.3f + 0.5f * k0 - 0.12f * k2 + d.bell() * 0.25f, 0.0f, 1.0f);
    // Лицо: черты мягче (2026-11-17, лица беты нейтральные) — разные, но без карикатуры
    m[AM_NOSE] = d.bell() * 0.5f;
    m[AM_NOSE_HUMP] = d.bell() * 0.4f;
    m[AM_JAW] = f_clamp(d.bell() * 0.45f + (a.female ? -0.15f : 0.1f), -1.0f, 1.0f);
    m[AM_CHIN] = d.bell() * 0.45f;
    m[AM_LIPS] = f_clamp(d.bell() * 0.45f + (a.female ? 0.15f : 0.0f), -1.0f, 1.0f);
    m[AM_EYES] = d.bell() * 0.45f;
    m[AM_CHEEKS] = f_clamp(d.bell() * 0.45f - m[AM_THIN] * 0.2f + m[AM_FULL] * 0.3f, -1.0f, 1.0f);
    m[AM_BROW] = f_clamp(d.bell() * 0.4f + (a.female ? -0.15f : 0.1f), -1.0f, 1.0f);
    m[AM_EARS] = d.bell() * 0.5f;
    return a;
}

// Ряд показа: женщины слева направо, потом мужчины. Морфы сложения — те же, что даёт appear_make, у краёв её разброса
struct ShowBody {
    bool female;
    f32 height, full, thin, strong, old, belly, shoulders;
};
static const ShowBody SHOW_BODIES[] = {
    {true, 1.66f, 0.0f, 0.1f, 0.15f, 0.05f, 0.0f, 0.0f},   // средняя
    {true, 1.55f, 0.0f, 0.75f, 0.0f, 0.0f, 0.0f, -0.3f},   // невысокая худая
    {true, 1.77f, 0.0f, 0.0f, 0.55f, 0.05f, 0.0f, 0.4f},   // высокая сильная
    {true, 1.63f, 0.8f, 0.0f, 0.1f, 0.1f, 0.4f, 0.0f},     // полная
    {true, 1.60f, 0.0f, 0.2f, 0.0f, 0.7f, 0.3f, -0.2f},    // старая
    {false, 1.78f, 0.0f, 0.1f, 0.25f, 0.05f, 0.0f, 0.1f},  // средний
    {false, 1.90f, 0.0f, 0.75f, 0.0f, 0.05f, 0.0f, -0.2f}, // высокий худой
    {false, 1.82f, 0.0f, 0.0f, 0.85f, 0.05f, 0.0f, 0.6f},  // сильный
    {false, 1.74f, 0.85f, 0.0f, 0.2f, 0.15f, 0.7f, 0.1f},  // полный
    {false, 1.70f, 0.0f, 0.2f, 0.1f, 0.75f, 0.4f, -0.1f},  // старый
};
static_assert(ANA_COUNTOF(SHOW_BODIES) <= SCENE_SHOW_MAX, "обликов показа не больше ряда сцены");

Appear appear_show(u32 k, u32 seed, u32 *look_seed) {
    const ShowBody &b = SHOW_BODIES[k % ANA_COUNTOF(SHOW_BODIES)];
    // Зерно того же пола: лицо и челюсть у appear_make зависят от пола
    u32 s = seed;
    Appear a = appear_make(s);
    for (u32 t = 1; a.female != b.female && t < 64; ++t) a = appear_make(s = seed + t * 0x9E3779B9u);
    f32 *m = a.morph;
    const f32 fem = a.female ? 1.0f : 0.0f;
    a.height = b.height;
    m[AM_FULL] = b.full;
    m[AM_THIN] = b.thin;
    m[AM_STRONG] = b.strong;
    m[AM_OLD] = b.old;
    m[AM_BELLY] = b.belly;
    m[AM_SHOULDERS] = b.shoulders;
    m[AM_FEMALE_FULL] = fem * b.full;
    m[AM_FEMALE_THIN] = fem * b.thin;
    m[AM_FEMALE_STRONG] = fem * b.strong;
    // Пропорции от зерна — вполсилы: в ряду сравнивают сложение, а не случайные длины
    m[AM_HIPS] *= 0.5f;
    m[AM_LEGS] *= 0.5f;
    m[AM_TORSO] *= 0.5f;
    m[AM_ARMS] *= 0.5f;
    if (look_seed) *look_seed = s;
    return a;
}

void appear_bind(AppearBind &b, const ViewBody &body) {
    for (u32 q = 0; q < AM_COUNT; ++q) {
        b.at[q] = -1;
        u64 h = hash_str(APPEAR_MORPH_NAMES[q]);
        for (u32 j = 0; j < body.morph_count && j < RENDER_MAX_MORPHS; ++j)
            if (body.morph_name[j] == h) b.at[q] = (i16)j;
    }
}

void appear_weights(const AppearBind &b, const Appear &a, f32 *out) {
    for (u32 j = 0; j < RENDER_MAX_MORPHS; ++j) out[j] = 0.0f;
    for (u32 q = 0; q < AM_COUNT; ++q)
        if (b.at[q] >= 0) out[b.at[q]] = a.morph[q];
}

void appear_fit(AppearFit &f, const Skel &base, const f32 *joints, u32 morph_count, const f32 *weights, f32 height,
                i32 pelvis_bone) {
    u32 nb = (u32)base.bones.count;
    if (nb > APPEAR_MAX_BONES) nb = APPEAR_MAX_BONES;
    for (u32 i = 0; i < nb; ++i) {
        SkelBone &o = f.bones[i];
        o = base.bones.data[i];
        if (!joints) continue;
        f32 dx = 0, dy = 0, dz = 0, dl = 0;
        for (u32 k = 0; k < morph_count; ++k) {
            f32 w = weights[k];
            if (w == 0.0f) continue;
            const f32 *j = joints + ((usize)k * base.bones.count + i) * 4;
            dx += j[0] * w;
            dy += j[1] * w;
            dz += j[2] * w;
            dl += j[3] * w;
        }
        o.pos[0] += dx;
        o.pos[1] += dy;
        o.pos[2] += dz;
        o.length += dl;
        // Обратная бинд-матрица [R | t]: поворот тот же, голова сдвинулась на d (в осях бинда, A-поза) — в осях
        // кости это Bᵀ·d (B — поворот бинда), t' = t − Bᵀ·d. Сетка людей вылеплена в другой позе (стойка беты,
        // docs/body.md), поэтому не R·d: в осях кости сдвиг у сетки и у бинда один
        const f32 x = o.rot[0], y = o.rot[1], z = o.rot[2], w = o.rot[3];
        const f32 bt[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w),
                           2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                           2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
        const f32 l[3] = {bt[0] * dx + bt[1] * dy + bt[2] * dz, bt[3] * dx + bt[4] * dy + bt[5] * dz,
                          bt[6] * dx + bt[7] * dy + bt[8] * dz};
        for (u32 r = 0; r < 3; ++r) o.inv[r * 4 + 3] -= l[r];
    }
    f.skel.bones = {f.bones, nb};
    f.scale = height / APPEAR_MESH_HEIGHT;
    f.pelvis = 1.0f;
    if (pelvis_bone >= 0 && (u32)pelvis_bone < nb) {
        f32 y0 = base.bones.data[pelvis_bone].pos[1];
        if (y0 > 0.1f) f.pelvis = f.bones[pelvis_bone].pos[1] / y0;
    }
}

// Сдвиг поверхности тела у мест ношения на вес морфа (бедро +1, бедро −1, спина +1, спина −1), м, относительно
// головы кости места; порядок AppearMorph. Мерит tools/humanconv/carry.py — перемерить после правки MORPHS
static const f32 APPEAR_CARRY[AM_COUNT][4] = {
    {0.0012f, 0.0143f, 0.0212f, 0.0026f},  // female
    {0.0132f, -0.0101f, 0.0224f, -0.0195f},  // full
    {-0.0036f, 0.0052f, 0.0033f, -0.0028f},  // thin
    {0.0004f, -0.0001f, 0.0057f, -0.0021f},  // strong
    {0.0190f, -0.0169f, 0.0065f, 0.0096f},  // old
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // belly
    {0.0040f, 0.0043f, -0.0181f, 0.0210f},  // female_full
    {-0.0042f, 0.0136f, 0.0042f, 0.0106f},  // female_thin
    {-0.0042f, 0.0054f, 0.0106f, -0.0057f},  // female_strong
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // shoulders
    {0.0359f, -0.0359f, 0.0000f, 0.0000f},  // hips
    {-0.0039f, 0.0080f, -0.0083f, 0.0078f},  // legs
    {-0.0112f, 0.0157f, -0.0025f, 0.0044f},  // torso
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // arms
    {-0.0039f, 0.0067f, 0.0008f, -0.0010f},  // neck
    {-0.0024f, 0.0014f, 0.0013f, 0.0004f},  // head
    {0.0004f, -0.0013f, 0.0001f, 0.0019f},  // kin_a
    {-0.0019f, 0.0008f, 0.0012f, 0.0007f},  // kin_b
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // nose
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // nose_hump
    {0.0006f, -0.0006f, 0.0002f, 0.0016f},  // jaw
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // chin
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // lips
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // eyes
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // cheeks
    {-0.0022f, 0.0012f, 0.0014f, 0.0003f},  // brow
    {0.0000f, 0.0000f, 0.0000f, 0.0000f},  // ears
};

AppearCarry appear_carry(const Appear &a) {
    f32 hip = 0.0f, back = 0.0f;
    for (u32 q = 0; q < AM_COUNT; ++q) {
        const f32 w = a.morph[q], *t = APPEAR_CARRY[q];
        hip += w > 0.0f ? w * t[0] : -w * t[1];
        back += w > 0.0f ? w * t[2] : -w * t[3];
    }
    // Ближе к телу — немного (у сетки зазор 2 см у бедра), дальше — сколько нарастило
    return AppearCarry{f_clamp(hip, -0.012f, 0.08f), f_clamp(back, -0.012f, 0.08f)};
}
