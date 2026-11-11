// ИИ поединка (этап 11, docs/people.md «Обмен ударами»). Каждый боец думает в свой тик и решает по состоянию до
// шага, пишет только своё намерение: номер места не даёт форы (урок беты). Случайность — хеш шага и номера, а не
// RNG мира: решение одного не сдвигает числа другого
#include "duel.h"
#include "../core/hash.h"
#include "../core/text.h"
#include "../sim/world.h"
#include <stddef.h>
#include <string.h>

static void duel_err(char *err, usize n, const char *what) {
    usize o = 0;
    for (const char *q = what; *q && o + 1 < n; ++q) err[o++] = *q;
    if (n) err[o] = 0;
}

bool duel_parse(const u8 *data, usize size, DuelDesc &out, char *err, usize err_size) {
    TextReader r = text_reader(data, size);
    TextLine l;
    u32 have = 0;
    out = DuelDesc{};
    while (text_next(r, l)) {
        Str k = l.w[0];
        u32 u = 0;
        f32 a = 0, b = 0;
        bool two = l.count == 3 && parse_f32(l.w[1], a) && parse_f32(l.w[2], b);
        bool one = l.count == 2 && parse_f32(l.w[1], a);
        if (str_eq(k, "think") && l.count == 2 && parse_u32(l.w[1], u) && u > 0 && u < 120) { out.think = u; have |= 1; }
        else if (str_eq(k, "notice") && two && a >= 0 && b >= 0) { out.notice[0] = a; out.notice[1] = b; have |= 2; }
        else if (str_eq(k, "noise") && two && a >= 0 && b >= 0) { out.noise[0] = a; out.noise[1] = b; have |= 4; }
        else if (str_eq(k, "attack") && two && a >= 0 && a <= 1 && b >= 0 && b <= 1) { out.attack[0] = a; out.attack[1] = b; have |= 8; }
        else if (str_eq(k, "open") && one && a >= 0 && a <= 1) { out.open = a; have |= 16; }
        else if (str_eq(k, "dodge") && one) { out.dodge = a; have |= 32; }
        else if (str_eq(k, "counter") && one) { out.counter = a; have |= 64; }
        else if (str_eq(k, "none") && one) { out.none = a; have |= 128; }
        else if (str_eq(k, "margin") && l.count == 2 && parse_u32(l.w[1], u) && u < 60) { out.margin = u; have |= 256; }
        else {
            duel_err(err, err_size, "непонятная строка (think, notice, noise, attack, open, dodge, counter, none, margin)");
            return false;
        }
    }
    if (have != 511) {
        duel_err(err, err_size, "нужны все поля: think, notice, noise, attack, open, dodge, counter, none, margin");
        return false;
    }
    out.hash = hash_bytes(&out, offsetof(DuelDesc, hash));
    return true;
}

static f32 lerp2(const f32 v[2], f32 k) { return v[0] + (v[1] - v[0]) * k; }
// Число [0, 1) из хеша шага, бойца и вопроса
static f32 roll(u64 step, u32 id, u32 q) { return (f32)(hash_combine(hash_combine(step, id), q) >> 40) * (1.0f / 16777216.0f); }

static const ActionDesc *desc_of(const World &w, u8 act) { return act && act <= w.actions.count ? &w.actions.a[act - 1] : nullptr; }

void ai_duel(const World &w, const DuelDesc &D, FixedArray<Intent> &out) {
    const People &p = w.people;
    const ActionTable &T = w.actions;
    if (!T.count || !p.live || !D.think) return;
    for (u32 s = 0; s < PEOPLE_CAPACITY; ++s) {
        if (!p.alive[s] || !p.target[s]) continue;
        const u32 id = make_id(s, p.gen[s]).v;
        if ((w.step + (hash_mix(id) & 0xFFFF)) % D.think) continue;
        u32 t;
        if (!people_valid(p, p.target[s], &t) || p.react_t[s] || p.freeze[s]) continue;
        const ActionDesc *mine = desc_of(w, p.act[s]);
        if (mine && p.act_t[s] < mine->cancel) continue;  // своё действие ещё не отменить
        const f32 sk = (f32)p.skill[s] * 0.01f;
        const f32 amp = lerp2(D.noise, sk);
        u32 q = 0;
        auto noise = [&]() { return (roll(w.step, id, q++) * 2.0f - 1.0f) * amp; };
        Vec2 d = wpos_delta_xz(p.cx[s], p.cz[s], p.x[s], p.z[s], p.cx[t], p.cz[t], p.x[t], p.z[t]);
        const f32 dist = length(d);
        const f32 face = dist > 1e-3f ? f_abs(turn_diff_rad(p.heading[s], turn_of_dir(d.x, d.y))) : 0.0f;
        u32 want = p.want[s];

        // Замах противника на меня, который я уже заметил
        const ActionDesc *their = desc_of(w, p.act[t]);
        bool threat = their && their->kind == ACT_STRIKE && !p.act_hit[t] && p.act_t[t] <= their->hit_b &&
                      p.target[t] == id && dist <= their->reach_max + 0.6f &&
                      (f32)(p.act_t[t] - their->start) >= lerp2(D.notice, sk);
        if (threat) {
            const i32 until = (i32)their->hit_a - (i32)p.act_t[t];
            f32 best = D.none + noise();
            u32 pick = 0;
            if (T.block != ACTION_NONE) {
                const ActionDesc &B = T.a[T.block];
                if ((their->line & B.line) && (i32)(B.guard_a - B.start) <= until) {
                    f32 c = f_clamp(B.chance + B.chance_skill * ((f32)p.skill[s] - (f32)p.skill[t]), 0.0f, 1.0f) + noise();
                    if (c > best) { best = c; pick = T.block + 1u; }
                }
            }
            if (T.dodge != ACTION_NONE) {
                const ActionDesc &G = T.a[T.dodge];
                f32 c = D.dodge + noise();
                if ((their->line & G.line) && (i32)(G.guard_a - G.start) <= until && c > best) { best = c; pick = T.dodge + 1u; }
            }
            for (u32 k = 0; k < T.count; ++k) {
                const ActionDesc &C = T.a[k];
                if (C.kind != ACT_STRIKE || dist < C.reach_min - 0.2f || dist > C.reach_max || face > 0.6f) continue;
                f32 c = D.counter + noise();
                if ((i32)(C.hit_a - C.start + D.margin) <= until && c > best) { best = c; pick = k + 1u; }
            }
            if (pick) want = pick;
        } else if (!(want && T.a[want - 1].kind == ACT_STRIKE)) {
            // Своего удара не ждёт: бить ли сейчас и каким. Открыт противник — почти всегда
            bool open = p.react_t[t] || (their && p.act_t[t] > (their->kind == ACT_STRIKE ? their->hit_b : their->guard_b));
            f32 chance = open ? D.open : lerp2(D.attack, sk);
            want = 0;
            if (roll(w.step, id, 1000) < chance) {
                f32 best = -1e9f;
                for (u32 k = 0; k < T.count; ++k) {
                    const ActionDesc &C = T.a[k];
                    if (C.kind != ACT_STRIKE) continue;
                    f32 fit = dist >= C.reach_min - 0.2f && dist <= C.reach_max + 0.3f ? 1.0f : 0.4f;
                    f32 c = fit * (p.last[s] == k + 1 ? 0.5f : 1.0f) + noise();
                    if (c > best) { best = c; want = k + 1; }
                }
            }
        }
        if (want != p.want[s]) out.push_back({w.step, INTENT_ACT, WPos{}, (u64)id | ((u64)want << 32)});
    }
}
