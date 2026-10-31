#include "loot.h"

#include "../core/log.h"
#include "anim.h"

// ---------------------------------------------------------------- рука → земля

// Поза оружия, выпавшего из правой руки стоящего в (x, y, z) лицом по h: рукоять у бедра, острие вперёд-вверх
static M4 hand_xf(f32 x, f32 y, f32 z, f32 h, Rng *r)
{
    V3 fwd = v3(sinf(h), 0, cosf(h)), right = v3(-cosf(h), 0, sinf(h));
    V3 at = v3_add(v3(x, y + 0.95f, z), v3_add(v3_mul(fwd, 0.3f), v3_mul(right, 0.22f)));
    return m4_mul(m4_translate(at), m4_mul(m4_rot_y(h + rng_range(r, -0.6f, 0.6f)), m4_rot_x(rng_range(r, 0.4f, 1.1f))));
}

// Выпало из руки: чуть вперёд и вбок, кувырком
static void hand_vel(f32 h, Rng *r, V3 *v, V3 *w)
{
    V3 fwd = v3(sinf(h), 0, cosf(h)), right = v3(-cosf(h), 0, sinf(h));
    *v = v3_add(v3_mul(fwd, rng_range(r, 0.3f, 1.0f)), v3(right.x * rng_range(r, 0.0f, 0.5f), 0.3f, right.z * 0.3f));
    *w = v3_add(v3_mul(right, rng_range(r, -4.0f, -1.5f)), v3(0, rng_range(r, -1.5f, 1.5f), 0));
}

// Рука бойца i: на поле — слот оружия листа (номер слота поля — номер человека), на арене — рука самого боя
static ItemPlace hand_of(const CombatWorld *w, u32 i, CharSheet *sh)
{
    ItemPlace h = place_hand(sh, sh ? i : ITEM_NOBODY);
    if (!sh) h.it = item_of_weapon(w->weapon[i], w->weapon_q[i]);
    return h;
}

// Общее для поля и арены: что с оружием бойца i в этом шаге. sh — лист (поле) или NULL (арена: только возврат).
// Выронил — вещь уходит из руки на землю (item_move), поднял — обратно; бой видит только gitem
static void combat_hand(Ground *g, Phys *p, CombatWorld *w, u32 i, CharSheet *sh, u16 src, V3 origin, f32 y,
                        u64 tick)
{
    u8 st = w->state[i];
    bool dead = st == FS_DEAD, gone = dead || w->disarmed[i];
    // Подбирает сам: к середине подбора оружие снова в руке
    bool picking = st == FS_PICKUP && w->dur[i] - w->timer[i] >= 0.55f * w->dur[i];
    bool lost = gone || st == FS_UNCONSCIOUS || st == FS_SURRENDER || (w->loose[i] && !picking);
    i32 k = w->gitem[i];
    if (k == COMBAT_IN_HAND && lost) {
        ItemPlace hand = hand_of(w, i, sh);
        if (item_weapon(item_at(g, &hand)) == COMP_NONE) return;  // нечего ронять
        Rng r;
        rng_seed(&r, tick, src + i);
        f32 x = origin.x + w->x[i], z = origin.z + w->z[i];
        V3 v, av;
        hand_vel(w->heading[i], &r, &v, &av);
        ItemPlace drop = place_drop(hand_xf(x, y, z, w->heading[i], &r), v, av, (u16)(src + i + 1));
        k = item_move(g, p, &hand, &drop);
        if (k < 0) return;  // места нет: остаётся в руке
        w->gitem[i] = k;
        // Встанет — поднимет (после подъёма бой ведёт его к подбору)
        if (!gone) w->loose[i] = 1;
    } else if (k >= 0 && !lost) {
        // Поднял своё; если его уже унесли — рука пуста
        if (ground_live(g, k) && g->owner[k] == src + i + 1) {
            ItemPlace from = place_ground(k), hand = hand_of(w, i, sh);
            item_move(g, p, &from, &hand);
        }
        w->gitem[i] = COMBAT_IN_HAND;
    }
    // Мёртвый и безрукий уже не поднимут: на поле оружие становится ничьим (в руке его нет с тех пор, как выронил)
    if (sh && gone && w->gitem[i] >= 0) {
        k = w->gitem[i];
        if (ground_live(g, k) && g->owner[k] == src + i + 1) g->owner[k] = 0;
        w->gitem[i] = COMBAT_GONE;
    }
}

void loot_field(Ground *g, Phys *p, Field *f, GroundFn ground, u64 tick)
{
    CombatWorld *w = &f->w;
    for (u32 i = 0; i < w->count; i++) {
        if (w->idle[i]) {
            // Бой стих, а оружие лежит: живой подбирает его, уходя
            i32 k = w->gitem[i];
            if (k >= 0 && ground_live(g, k) && g->owner[k] == GROUND_OWN_FIELD + i + 1) {
                ItemPlace from = place_ground(k), hand = place_hand(f->sheet[i], i);
                item_move(g, p, &from, &hand);
            }
            if (k >= 0) w->gitem[i] = COMBAT_IN_HAND;
            continue;
        }
        combat_hand(g, p, w, i, f->sheet[i], GROUND_OWN_FIELD, v3(0, 0, 0), ground(w->x[i], w->z[i]), tick);
    }
    // Хозяина слота сменили (field_reuse) — его выроненное теперь ничьё
    for (u32 k = 0; k < g->count; k++) {
        u16 o = g->owner[k];
        if (!item_live(g->item[k]) || !o || (o & GROUND_OWN_SRC) != GROUND_OWN_FIELD) continue;
        u32 s = (o & ~GROUND_OWN_SRC) - 1u;
        if (s >= w->count || w->gitem[s] != (i32)k) g->owner[k] = 0;
    }
}

void loot_arena(Ground *g, Phys *p, ArenaSession *s, bool in_bout, V3 center, f32 floor_y, u64 tick)
{
    CombatWorld *w = &s->w;
    if (in_bout)
        for (u32 i = 0; i < w->count; i++) combat_hand(g, p, w, i, NULL, GROUND_OWN_ARENA, center, floor_y, tick);
    // Схватка кончилась или в слотах уже другие: выроненное уходит вместе с бойцами
    for (u32 k = 0; k < g->count; k++) {
        u16 o = g->owner[k];
        if (!item_live(g->item[k]) || (o & GROUND_OWN_SRC) != GROUND_OWN_ARENA) continue;
        u32 i = (o & ~GROUND_OWN_SRC) - 1u;
        if (!in_bout || i >= w->count || w->gitem[i] != (i32)k) {
            ItemPlace from = place_ground((i32)k), hand = place_hand(NULL, ITEM_NOBODY);
            item_move(g, p, &from, &hand);
        }
    }
}

// ---------------------------------------------------------------- мешок ↔ земля

void loot_asks(Ground *g, Phys *p, GroundAsks *a, Squad *sq, u64 tick)
{
    for (u32 n = 0; n < a->n; n++) {
        Rng r;
        rng_seed(&r, tick, 0x51u + n);
        f32 h = a->yaw[n];
        V3 fwd = v3(sinf(h), 0, cosf(h));
        M4 xf = m4_mul(m4_translate(a->at[n]), m4_rot_y(h + rng_range(&r, -1.0f, 1.0f)));
        V3 v = v3_add(v3_mul(fwd, rng_range(&r, 0.8f, 1.4f)), v3(0, 1.0f, 0));
        V3 w = v3(rng_range(&r, -2, 2), rng_range(&r, -2, 2), rng_range(&r, -2, 2));
        // Вещь уже вне мешка (её вынуло окно мешка); некуда класть — обратно в мешок
        ItemPlace out = place_none(a->it[n]), drop = place_drop(xf, v, w, 0);
        if (a->who[n] < sq->count) out.who = sq->member[a->who[n]];
        if (item_move(g, p, &out, &drop) < 0 && a->who[n] < sq->count) {
            ItemPlace bag = place_bag(squad_sheet(sq, a->who[n]), out.who, -1);
            item_move(g, p, &out, &bag);
        }
    }
    a->n = 0;
}

void loot_pickups(Ground *g, Phys *p, Squad *sq, Field *f)
{
    for (u32 i = 0; i < sq->count; i++) {
        i32 k = sq->pick[i];
        if (k < 0) continue;
        Player *b = squad_body(sq, i);
        // Вещь унесли, или его втянули в бой
        if (!ground_live(g, k) || (b->slot >= 0 && field_awake(f, (u32)b->slot))) {
            sq->pick[i] = -1;
            continue;
        }
        V3 x = ground_pos(g, p, (u32)k);
        f32 dx = x.x - b->pos.x, dz = x.z - b->pos.z, d = sqrtf(dx * dx + dz * dz);
        if (d > LOOT_PICK_R) {
            // Дошёл, а она откатилась — идёт следом; далеко укатилась — бросает
            if (!b->moving) {
                if (d < 6.0f) player_order_move(b, x);
                else sq->pick[i] = -1;
            }
            continue;
        }
        sq->pick[i] = -1;
        u16 o = g->owner[k];
        CharSheet *c = squad_sheet(sq, i);
        ItemPlace from = place_ground(k), bag = place_bag(c, sq->member[i], -1);
        if (item_move(g, p, &from, &bag) < 0) {
            log_info("%s: МЕШОК ПОЛОН. ВЕЩЬ ОСТАЛАСЬ НА ЗЕМЛЕ.", c->name);
            continue;
        }
        if (o) {
            // Чужое выроненное: хозяин его уже не поднимет — встанет безоружным
            u32 s = (o & ~GROUND_OWN_SRC) - 1u;
            CombatWorld *w = &f->w;
            if (s < w->count && w->gitem[s] == k) {
                w->gitem[s] = COMBAT_GONE;
                w->disarmed[s] = 1;
                w->loose[s] = 0;
            }
        }
        b->moving = false;
        b->target = b->pos;
    }
}

void loot_drop_test(Ground *g, Phys *p, V3 at, u32 n, u64 seed)
{
    Rng r;
    rng_seed(&r, seed, 23);
    // Простые вещи: без оружия, брони и света
    u16 goods[ITEM_CAP];
    u32 ng = 0;
    for (u32 d = 1; d < g_item_count; d++)
        if (g_items[d].weapon == COMP_NONE && g_items[d].armor == COMP_NONE && g_items[d].light == COMP_NONE) goods[ng++] = (u16)d;
    if (!g_weapon_pool_count && !ng) return;
    for (u32 i = 0; i < n; i++) {
        ItemStack it = ng && (!g_weapon_pool_count || rng_below(&r, 3) == 0)
                      ? item_make(goods[rng_below(&r, ng)], Q_WORN, 1)
                      : item_of_weapon(g_weapon_pool[rng_below(&r, g_weapon_pool_count)], (u8)rng_below(&r, 3));
        V3 c = v3_add(at, v3(rng_range(&r, -1.5f, 1.5f), 1.2f + 0.3f * (f32)i, rng_range(&r, -1.5f, 1.5f)));
        M4 xf = m4_trs(c, v3(1, 1, 1), rng_range(&r, 0, 2 * PI_F), rng_range(&r, 0, 2 * PI_F), rng_range(&r, 0, 2 * PI_F));
        V3 w = v3(rng_range(&r, -3, 3), rng_range(&r, -3, 3), rng_range(&r, -3, 3));
        ItemPlace out = place_none(it), drop = place_drop(xf, v3(0, -0.5f, 0), w, 0);
        item_move(g, p, &out, &drop);
    }
}
