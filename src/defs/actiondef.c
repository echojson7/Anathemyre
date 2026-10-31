#include "actiondef.h"

#include <string.h>

#include "../sim/action.h"
#include "../sim/anim.h"

static const char *const g_set_word[AS_COUNT] = { "human", "blade", "cleaver", "blunt", "polearm", "heavy" };
static const char *const g_clip_word[] = { "none", "fall", "collapse", "lie", "getup", "kneel", "crawl",
                                           "die", "bleed", "sink", "turn", "pickup" };
_Static_assert(ARRAY_COUNT(g_clip_word) == CLIP_PICKUP + 1, "g_clip_word по номерам CharClip");

static bool s_has[AS_COUNT][ACT_COUNT];  // действие задано или взято у родителя
static u32 s_set;                         // набор текущей записи
static i32 s_act;                         // действие текущей строки action, -1 — ещё нет

void actiondef_reset(void)
{
    memset(s_has, 0, sizeof s_has);
    for (u32 s = 0; s < AS_COUNT; s++)
        for (u32 a = 0; a < ACT_COUNT; a++) g_actions[s][a] = (ActionDef){ .clip = CLIP_NONE, .speed = 1.0f };
}

bool actiondef_header(const DefLine *l)
{
    i32 s = def_word(&l->tok[1], g_set_word, AS_COUNT);
    if (s < 0) {
        def_error(l, "набор действий: human или класс оружия (blade, cleaver, blunt, polearm, heavy)");
        return false;
    }
    s_set = (u32)s;
    s_act = -1;
    return true;
}

bool actiondef_parent(const DefLine *l, u32 lines)
{
    i32 p = l->n == 2 ? def_word(&l->tok[1], g_set_word, AS_COUNT) : -1;
    if (lines != 1 || p < 0 || (u32)p == s_set) {
        def_error(l, "parent — первой строкой набора, родитель — другой набор");
        return false;
    }
    memcpy(g_actions[s_set], g_actions[p], sizeof g_actions[0]);
    memcpy(s_has[s_set], s_has[p], sizeof s_has[0]);
    return true;
}

static bool vals(const DefLine *l, u32 n, f32 *out)
{
    if (l->n != n + 1) {
        def_error(l, "у %.*s нужно значений: %u", (int)l->tok[0].n, l->tok[0].s, n);
        return false;
    }
    for (u32 i = 0; i < n; i++)
        if (!def_num(&l->tok[1 + i], &out[i])) {
            def_error(l, "не число: %.*s", (int)l->tok[1 + i].n, l->tok[1 + i].s);
            return false;
        }
    return true;
}

bool actiondef_field(const DefLine *l)
{
    const DefTok *k = &l->tok[0];
    if (def_is(k, "action")) {
        s_act = l->n == 2 ? def_word(&l->tok[1], g_action_word, ACT_COUNT) : -1;
        if (s_act < 0) {
            def_error(l, "action: имя действия из списка docs/combat.md");
            return false;
        }
        s_has[s_set][s_act] = true;
        return true;
    }
    if (s_act < 0) {
        def_error(l, "поле до первой строки action");
        return false;
    }
    ActionDef *a = &g_actions[s_set][s_act];
    if (def_is(k, "clip")) {
        i32 c = l->n == 2 ? def_word(&l->tok[1], g_clip_word, ARRAY_COUNT(g_clip_word)) : -1;
        if (c < 0) {
            def_error(l, "clip: none, fall, collapse, lie, getup, kneel, crawl, die, bleed, sink, turn или pickup");
            return false;
        }
        a->clip = (u8)c;
        return true;
    }
    if (def_is(k, "key")) {
        u32 v;
        if (l->n != 2 || !def_uint(&l->tok[1], &v) || v > 255) {
            def_error(l, "key: номер ключа клипа");
            return false;
        }
        a->key = (u8)v;
        return true;
    }
    f32 v[2];
    if (def_is(k, "time")) return vals(l, 1, &a->time);
    if (def_is(k, "vary")) return vals(l, 1, &a->vary);
    if (def_is(k, "max")) return vals(l, 1, &a->max);
    if (def_is(k, "tough")) return vals(l, 1, &a->tough);
    if (def_is(k, "speed")) return vals(l, 1, &a->speed);
    if (def_is(k, "contact")) return vals(l, 1, &a->contact);
    if (def_is(k, "per_kg")) {
        if (!vals(l, 2, v)) return false;
        a->per_kg = v[0], a->kg_ref = v[1];
        return true;
    }
    if (def_is(k, "kg")) {
        if (!vals(l, 2, v)) return false;
        a->kg_add = v[0], a->kg_div = v[1];
        return true;
    }
    def_error(l, "у действия нет поля %.*s", (int)k->n, k->s);
    return false;
}

bool actiondef_check(void)
{
    bool ok = true;
    for (u32 s = 0; s < AS_COUNT; s++)
        for (u32 a = 0; a < ACT_COUNT; a++) {
            const ActionDef *d = &g_actions[s][a];
            if (!s_has[s][a]) {
                log_error("ОПИСАНИЯ: в наборе %s нет действия %s (нужно своё или parent)", g_set_word[s], g_action_word[a]);
                ok = false;
            } else if ((a != ACT_STANCE && d->time <= 0.0f) || d->speed <= 0.0f || (d->per_kg != 0.0f && d->max < d->time) ||
                       d->contact < 0.0f || d->contact > 1.0f) {
                log_error("ОПИСАНИЯ: действие %s набора %s: нужно time > 0, speed > 0, max не меньше time, contact 0..1",
                          g_action_word[a], g_set_word[s]);
                ok = false;
            }
        }
    return ok;
}
