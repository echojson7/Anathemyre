#include "weapon.h"
#include "../core/hash.h"
#include "../core/text.h"
#include <stdio.h>
#include <string.h>

static bool word_is(Str s, const char *a, const char *b, const char *c, u8 &out) {
    const char *v[3] = {a, b, c};
    for (u8 k = 0; k < 3; ++k)
        if (str_eq(s, v[k])) {
            out = k;
            return true;
        }
    return false;
}

bool weapon_parse(WeaponTable &t, const u8 *text, usize size, char *err, usize err_cap) {
    t = WeaponTable{};
    TextReader r = text_reader(text, size);
    TextLine l;
    WeaponDesc *d = nullptr;
    u32 have = 0;  // биты заданных полей текущего оружия
    constexpr u32 ALL = 0x3F;
    auto bad = [&](const char *what) {
        snprintf(err, err_cap, "строка %u: %s", l.line, what);
        return false;
    };
    auto close = [&]() { return !d || have == ALL; };
    while (text_next(r, l)) {
        if (str_eq(l.w[0], "weapon")) {
            if (!close()) return bad("у прошлого оружия не все поля (hands grip2 carry tip butt issue)");
            if (l.count != 2 || l.w[1].n >= sizeof d->name) return bad("weapon <имя> (до 23 байт)");
            if (t.count == WEAPONS_MAX) return bad("оружия больше WEAPONS_MAX");
            d = &t.w[t.count++];
            memcpy(d->name, l.w[1].p, l.w[1].n);
            d->name[l.w[1].n] = 0;
            d->hash = str_hash(l.w[1]);
            have = 0;
            continue;
        }
        if (!d) return bad("поле до строки weapon");
        if (str_eq(l.w[0], "family")) continue;  // справка: бой семейство пока не читает
        if (str_eq(l.w[0], "hands")) {
            if (l.count != 2 || !word_is(l.w[1], "1", "2", "pole", d->hands)) return bad("hands 1|2|pole");
            have |= 1;
        } else if (str_eq(l.w[0], "carry")) {
            if (l.count != 3 || !word_is(l.w[1], "hip_l", "back", "back_pole", d->carry) || !parse_f32(l.w[2], d->carry_at))
                return bad("carry hip_l|back|back_pole <y>");
            have |= 2;
        } else {
            struct Field {
                const char *name;
                f32 *v;
                u32 bit;
            };
            const Field keys[] = {{"grip2", &d->grip2, 4}, {"tip", &d->tip, 8}, {"butt", &d->butt, 16}, {"issue", &d->issue, 32}};
            const Field *k = nullptr;
            for (const Field &q : keys)
                if (str_eq(l.w[0], q.name)) k = &q;
            if (!k) return bad("неизвестный ключ");
            if (l.count != 2 || !parse_f32(l.w[1], *k->v)) return bad("ожидается одно число");
            have |= k->bit;
        }
    }
    if (!close()) return bad("у последнего оружия не все поля");
    for (u32 i = 0; i < t.count; ++i) {
        const WeaponDesc &w = t.w[i];
        if (w.tip <= w.butt || w.issue < 0.0f) {
            snprintf(err, err_cap, "%s: tip > butt, issue ≥ 0", w.name);
            return false;
        }
        t.issue_sum += w.issue;
    }
    return true;
}

i32 weapon_pick(const WeaponTable &t, u32 seed) {
    // Свой поток бит зерна (не тот, что у морфов тела): доля 0..1 по весам issue; без сетки вид пропускается
    f32 sum = 0.0f;
    for (u32 i = 0; i < t.count; ++i)
        if (t.w[i].mesh) sum += t.w[i].issue;
    if (sum <= 0.0f) return -1;
    u64 h = hash_mix((u64)seed | 0x3EA90000000000ull);
    f32 u = (f32)(h >> 40) / (f32)(1u << 24) * sum;
    i32 last = -1;
    for (u32 i = 0; i < t.count; ++i) {
        if (!t.w[i].mesh || t.w[i].issue <= 0.0f) continue;
        last = (i32)i;
        if (u < t.w[i].issue) return last;
        u -= t.w[i].issue;
    }
    return last;
}
