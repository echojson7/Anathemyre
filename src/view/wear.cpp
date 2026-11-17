#include "wear.h"
#include "view.h"
#include "../core/log.h"
#include "../core/mathf.h"
#include "../render/render.h"

const char *const WEAR_NAMES[WK_COUNT] = {
    "wear_trousers", "wear_hakama", "wear_shirt",     "wear_jacket",    "wear_coat",  "wear_vest",     "wear_sash",
    "wear_belt",     "wear_wraps_leg", "wear_wraps_arm", "wear_boots", "wear_hood",  "wear_pauldron", "wear_gloves",
};

void wear_bind(WearBind &b, const ViewAssets &a, const ViewBody &human) {
    for (u32 k = 0; k < WK_COUNT; ++k) {
        b.at[k] = -1;
        u64 h = hash_str(WEAR_NAMES[k]);
        for (u32 i = 0; i < a.wear_count; ++i) {
            const ViewBody &w = a.wear[i];
            if (w.name != h) continue;
            // Морфы вещи — те же и в том же порядке, что у тела: скиннинг берёт веса тела как есть
            bool same = w.morph_count == human.morph_count && w.skin_count == human.skin_count;
            for (u32 m = 0; same && m < w.morph_count; ++m) same = w.morph_name[m] == human.morph_name[m];
            for (u32 s = 0; same && s < w.skin_count; ++s) same = w.skel_name[s] == human.skel_name[s];
            if (!same) { log_warn("одежда %s: морфы или скелеты не как у тела людей", WEAR_NAMES[k]); break; }
            b.at[k] = (i16)i;
        }
    }
}

// Набор: штаны или хакама, рубаха, верх (куртка, длинная куртка, жилет или ничего), пояс или оби, ботинки, к ним —
// обмотки, капюшон, наплечник, перчатки. Тон вещей — около общего тона набора: один человек одет в одной гамме
Outfit wear_make(u32 seed, const Appear &a, const WearBind &b, const ViewAssets &assets) {
    (void)a;
    Outfit o{};
    Dice d{0, 0, (u64)seed | 0x3EA2000000000ull};
    const u32 theme = d.byte();
    bool want[WK_COUNT] = {};
    const bool hakama = d.byte() < 100;
    want[hakama ? WK_HAKAMA : WK_TROUSERS] = true;
    want[WK_SHIRT] = true;
    want[WK_BOOTS] = true;
    const u32 top = d.byte();
    if (top < 110) want[WK_JACKET] = true;
    else if (top < 175) want[WK_COAT] = true;
    else if (top < 215) want[WK_VEST] = true;
    if (want[WK_JACKET] && d.byte() < 60) want[WK_VEST] = true;
    if (hakama || d.byte() < 90) want[WK_SASH] = true;
    if (!want[WK_SASH] || d.byte() < 70) want[WK_BELT] = true;
    if (d.byte() < 90) want[WK_WRAPS_LEG] = true;
    const u32 hands = d.byte();
    if (hands < 75) want[WK_WRAPS_ARM] = true;
    else if (hands < 140) want[WK_GLOVES] = true;
    if (d.byte() < 90) want[WK_HOOD] = true;
    if (d.byte() < 55) want[WK_PAULDRON] = true;
    // Порядок — слоями снизу вверх (порядок перечисления), зерно вещи: тон около темы, износ — свой
    for (u32 k = 0; k < WK_COUNT && o.count < WEAR_MAX_ITEMS; ++k) {
        if (!want[k] || b.at[k] < 0) continue;
        const ViewBody &w = assets.wear[b.at[k]];
        i32 tone = (i32)theme + (i32)(d.bell() * 70.0f);
        tone = tone < 0 ? 0 : tone > 255 ? 255 : tone;
        const u32 wear_amt = d.byte();
        o.kind[o.count] = (u8)k;
        o.seed[o.count] = ((seed & 0xFFFF0000u) ^ ((u32)k << 20)) | (wear_amt << 8) | (u32)tone;
        o.count++;
        o.hide |= w.hide;
        o.hip = f_max(o.hip, w.carry_hip);
        o.back = f_max(o.back, w.carry_back);
    }
    return o;
}

void wear_take(WearCopies &c, const Outfit &o, u8 copy[WEAR_MAX_ITEMS], const ViewAssets &assets, const WearBind &b,
               u32 skin) {
    for (u32 k = 0; k < o.count; ++k) {
        const u32 w = o.kind[k];
        u32 i = 0;
        while (i < c.count[w] && c.used[w][i]) ++i;
        if (i == c.count[w]) {
            u32 mesh = i < WEAR_COPIES_MAX ? render_skinned_create(assets.wear[b.at[w]].skin[skin]) : 0;
            if (!mesh) { copy[k] = WEAR_NO_COPY; continue; }
            c.mesh[w][c.count[w]++] = mesh;
        }
        c.used[w][i] = true;
        copy[k] = (u8)i;
    }
}

void wear_release(WearCopies &c, Outfit &o, const u8 copy[WEAR_MAX_ITEMS]) {
    for (u32 k = 0; k < o.count; ++k)
        if (copy[k] != WEAR_NO_COPY) c.used[o.kind[k]][copy[k]] = false;
    o.count = 0;
    o.hide = 0;
}
