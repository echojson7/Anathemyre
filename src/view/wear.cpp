#include "wear.h"
#include <string.h>
#include "view.h"
#include "../core/log.h"
#include "../core/mathf.h"
#include "../render/render.h"

const char *const WEAR_NAMES[WK_COUNT] = {
    "wear_trousers", "wear_hakama", "wear_shirt",     "wear_jacket",    "wear_coat",  "wear_vest",     "wear_sash",
    "wear_belt",     "wear_wraps_leg", "wear_wraps_arm", "wear_boots", "wear_hood",  "wear_pauldron", "wear_gloves",
    "hair_brows",    "hair_shaved",    "hair_crop",      "hair_topknot", "hair_tail", "hair_braid",   "hair_bun",
    "hair_shag",     "hair_pony",      "beard_stubble",  "beard_short",    "beard_full",   "beard_goatee", "beard_long",
};

static u32 g_force_hair = WK_COUNT;

bool wear_force_hair(const char *name) {
    for (u32 k = WK_HAIR_SHAVED; k < WK_COUNT; ++k)
        if (!strcmp(WEAR_NAMES[k], name)) return g_force_hair = k, true;
    return false;
}

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

// Зерно волос для hair.glsl: младший байт — тон кожи (смесь с кожей у линии роста, щетина), байт 1 — тёмность,
// байт 2 — седина, младшие 4 бита байта 3 — рыжина, старшие — узор
static u32 hair_seed(Dice &d, const Appear &a) {
    const u32 skin = (u32)(a.skin * 255.0f + 0.5f);
    // Тёмные волосы у всех, у смуглых — чёрные; у светлокожих изредка русые и рыжие
    f32 mel = 0.62f + 0.38f * a.skin + 0.22f * d.bell();
    const u32 light = d.byte(), red = d.byte();
    if (a.skin < 0.4f && light < 45) mel = 0.08f + 0.35f * d.uni();
    mel = mel < 0.0f ? 0.0f : mel > 1.0f ? 1.0f : mel;
    u32 rd = (a.skin < 0.45f && red < 22) ? 6u + (red & 7u) : 0u;
    // Седина — с возрастом (морф old: у стариков 0.35–0.75), у остальных редкие седые волоски
    const f32 old = a.morph[AM_OLD];
    f32 grey = old > 0.3f ? 0.25f + (old - 0.3f) * 1.6f + 0.15f * d.bell() : old * 0.25f * d.uni();
    grey = grey < 0.0f ? 0.0f : grey > 1.0f ? 1.0f : grey;
    const u32 noise = d.byte() & 0xF0u;
    return skin | ((u32)(mel * 255.0f + 0.5f) << 8) | ((u32)(grey * 255.0f + 0.5f) << 16) | ((noise | rd) << 24);
}

// Причёска и борода по полу и возрасту. Байты зерна берутся после одежды: набор одежды прежний
static void hair_pick(Dice &d, const Appear &a, bool want[WK_COUNT]) {
    want[WK_BROWS] = true;
    const u32 h = d.byte();
    const u32 bd = d.byte();
    const bool old = a.morph[AM_OLD] > 0.3f;
    if (a.female) {
        // коса, пучок, высокий хвост (карточки), лохматые, стрижка, бритая
        static const u8 cut[] = {80, 135, 185, 225, 245, 255};
        static const WearKind kind[] = {WK_HAIR_BRAID, WK_HAIR_BUN, WK_HAIR_PONY, WK_HAIR_SHAG, WK_HAIR_CROP,
                                        WK_HAIR_SHAVED};
        u32 i = 0;
        while (h >= cut[i]) ++i;
        want[want[WK_HOOD] && (kind[i] == WK_HAIR_PONY || kind[i] == WK_HAIR_BRAID) ? WK_HAIR_BUN : kind[i]] = true;
        return;
    }
    // Мужчины: бритые, стрижка, узел на макушке, хвост, коса, лохматые; старики чаще бритые и стриженые
    static const u8 cut[] = {45, 105, 140, 175, 195, 255};
    static const WearKind kind[] = {WK_HAIR_SHAVED, WK_HAIR_CROP, WK_HAIR_TOPKNOT, WK_HAIR_TAIL, WK_HAIR_BRAID,
                                    WK_HAIR_SHAG};
    u32 hh = old ? h * 2 / 3 : h;
    u32 i = 0;
    while (hh >= cut[i]) ++i;
    // Шарф и капюшон лежат там, где висят коса и хвост: с капюшоном — узел на макушке
    want[want[WK_HOOD] && (kind[i] == WK_HAIR_TAIL || kind[i] == WK_HAIR_BRAID) ? WK_HAIR_TOPKNOT : kind[i]] = true;
    // Борода: нет, щетина, короткая, полная, усы с бородкой, длинная (у стариков чаще)
    static const u8 bcut[] = {60, 120, 170, 200, 230, 255};
    static const WearKind bkind[] = {WK_COUNT, WK_BEARD_STUBBLE, WK_BEARD_SHORT, WK_BEARD_FULL, WK_BEARD_GOATEE,
                                     WK_BEARD_LONG};
    u32 bb = old ? 120 + bd * 135 / 255 : bd;
    u32 j = 0;
    while (bb >= bcut[j]) ++j;
    if (bkind[j] != WK_COUNT) want[bkind[j]] = true;
}

// Набор: штаны или хакама, рубаха, верх (куртка, длинная куртка, жилет или ничего), пояс или оби, ботинки, к ним —
// обмотки, капюшон, наплечник, перчатки. Тон вещей — около общего тона набора: один человек одет в одной гамме
Outfit wear_make(u32 seed, const Appear &a, const WearBind &b, const ViewAssets &assets, bool clothes) {
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
    // Порядок — слоями снизу вверх (порядок перечисления), зерно вещи: тон около темы, износ — свой. Одежды не
    // больше WEAR_MAX_CLOTHES (как до волос: лишняя верхняя вещь не надевается)
    // taken — счёт вещей и без одежды: кости зерна для волос те же, что у одетого
    for (u32 k = 0, taken = 0; k < WK_BROWS && taken < WEAR_MAX_CLOTHES; ++k) {
        if (!want[k] || b.at[k] < 0) continue;
        const ViewBody &w = assets.wear[b.at[k]];
        i32 tone = (i32)theme + (i32)(d.bell() * 70.0f);
        tone = tone < 0 ? 0 : tone > 255 ? 255 : tone;
        const u32 wear_amt = d.byte();
        taken++;
        if (!clothes) continue;
        o.kind[o.count] = (u8)k;
        o.seed[o.count] = ((seed & 0xFFFF0000u) ^ ((u32)k << 20)) | (wear_amt << 8) | (u32)tone;
        o.count++;
        o.hide |= w.hide;
        o.hip = f_max(o.hip, w.carry_hip);
        o.back = f_max(o.back, w.carry_back);
    }
    // Волосы — байтами после одежды (набор одежды у людей прежний). Цвет один на брови, причёску и бороду
    if (!clothes) want[WK_HOOD] = false;  // без капюшона хвост и коса не подбираются в пучок
    hair_pick(d, a, want);
    if (g_force_hair < WK_COUNT) {
        // --hair: у всех одна причёска или борода (кадры и проверка, только вид)
        const bool beard = g_force_hair >= WK_BEARD_STUBBLE;
        for (u32 k = beard ? (u32)WK_BEARD_STUBBLE : (u32)WK_HAIR_SHAVED; k < (beard ? (u32)WK_COUNT : (u32)WK_BEARD_STUBBLE); ++k)
            want[k] = false;
        want[g_force_hair] = true;
    }
    const u32 hseed = hair_seed(d, a);
    for (u32 k = WK_BROWS; k < WK_COUNT && o.count < WEAR_MAX_ITEMS; ++k) {
        if (!want[k] || b.at[k] < 0) continue;
        o.kind[o.count] = (u8)k;
        o.seed[o.count++] = hseed;
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
