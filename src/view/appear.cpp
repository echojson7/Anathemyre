#include "appear.h"
#include "view.h"
#include "../core/hash.h"
#include "../core/mathf.h"

const char *const APPEAR_MORPH_NAMES[AM_COUNT] = {
    "female", "full",  "thin", "strong", "old",   "belly", "female_full", "female_thin", "female_strong",
    "shoulders", "hips", "legs", "torso", "arms", "neck",  "head",        "kin_a",       "kin_b",
    "nose",   "nose_hump", "jaw", "chin", "lips", "eyes",  "cheeks",      "brow",        "ears",
};

// Байты зерна по порядку: каждая черта берёт свои, новая черта дописывается в конец и старых не сдвигает
struct Dice {
    u64 h;
    u32 left;
    u64 next;
    u32 byte() {
        if (!left) {
            h = hash_mix(next);
            next += 0x9E3779B97F4A7C15ull;
            left = 8;
        }
        u32 b = (u32)(h & 255);
        h >>= 8;
        left--;
        return b;
    }
    f32 uni() { return (f32)byte() / 255.0f; }                                 // 0..1 ровно
    f32 bell() { return ((f32)byte() + (f32)byte() - 255.0f) / 255.0f; }       // −1..1, чаще у нуля
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
    m[AM_KIN_A] = k0 - k2;
    m[AM_KIN_B] = k1 - k2;
    // Тон кожи: темнее у первого рода, все загорелые
    a.skin = f_clamp(0.3f + 0.5f * k0 - 0.12f * k2 + d.bell() * 0.25f, 0.0f, 1.0f);
    // Лицо
    m[AM_NOSE] = d.bell() * 0.9f;
    m[AM_NOSE_HUMP] = d.bell() * 0.8f;
    m[AM_JAW] = f_clamp(d.bell() * 0.8f + (a.female ? -0.2f : 0.15f), -1.0f, 1.0f);
    m[AM_CHIN] = d.bell() * 0.8f;
    m[AM_LIPS] = f_clamp(d.bell() * 0.8f + (a.female ? 0.15f : 0.0f), -1.0f, 1.0f);
    m[AM_EYES] = d.bell() * 0.9f;
    m[AM_CHEEKS] = f_clamp(d.bell() * 0.8f - m[AM_THIN] * 0.2f + m[AM_FULL] * 0.3f, -1.0f, 1.0f);
    m[AM_BROW] = f_clamp(d.bell() * 0.7f + (a.female ? -0.2f : 0.15f), -1.0f, 1.0f);
    m[AM_EARS] = d.bell() * 0.8f;
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
        // Обратная бинд-матрица [R | t]: поворот тот же, голова сдвинулась на d — t' = t − R·d
        for (u32 r = 0; r < 3; ++r) o.inv[r * 4 + 3] -= o.inv[r * 4] * dx + o.inv[r * 4 + 1] * dy + o.inv[r * 4 + 2] * dz;
    }
    f.skel.bones = {f.bones, nb};
    f.scale = height / APPEAR_MESH_HEIGHT;
    f.pelvis = 1.0f;
    if (pelvis_bone >= 0 && (u32)pelvis_bone < nb) {
        f32 y0 = base.bones.data[pelvis_bone].pos[1];
        if (y0 > 0.1f) f.pelvis = f.bones[pelvis_bone].pos[1] / y0;
    }
}
