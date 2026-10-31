#include "loot.h"

#include "../core/log.h"
#include "../sim/anim.h"

// ---------------------------------------------------------------- вид

void loot_view_init(LootView *v, MeshBuilder *mb, Arena *perm)
{
    // Узел: мешковина, перетянутая двумя ремнями
    size_t mark = perm->used;
    mb_init(mb, perm, 1024, 3072);
    mb_set(mb, v3(0.30f, 0.26f, 0.19f), MAT_CLOTH, 0);
    mb_box(mb, m4_trs(v3(0, 0, 0), v3(0.30f, 0.12f, 0.22f), 0, 0, 0));
    mb_set(mb, v3(0.16f, 0.10f, 0.06f), MAT_LEATHER, 0);
    mb_box(mb, m4_trs(v3(-0.07f, 0, 0), v3(0.03f, 0.125f, 0.225f), 0, 0, 0));
    mb_box(mb, m4_trs(v3(0.07f, 0, 0), v3(0.03f, 0.125f, 0.225f), 0, 0, 0));
    gpu_mesh_upload(&v->bundle, mb);
    perm->used = mark;
    v->bones = ARENA_PUSH_ARRAY(perm, M4, (size_t)LOOT_DRAW_MAX * RENDER_MAX_BONES);
}

u32 loot_draw(LootView *v, const Ground *g, const Phys *p, const GpuMesh *weapon, f32 alpha, V3 eye, DrawItem *out,
              u32 cap)
{
    u32 n = 0;
    for (u32 k = 0; k < g->count && n < cap && n < LOOT_DRAW_MAX; k++) {
        Item it = g->item[k];
        if (!it.kind) continue;
        u32 h = (u32)g->body[k];
        V3 d = v3_sub(p->x[h], eye);
        if (v3_dot(d, d) > 200.0f * 200.0f) continue;
        M4 *b = &v->bones[(size_t)n * RENDER_MAX_BONES];
        M4 m = phys_pose(p, h, alpha);
        bool wpn = it.kind == IK_WEAPON && it.id < g_weapon_count;
        // Размер узла — по форме тела (клетки мешка)
        if (!wpn) {
            PhysDesc pd = ground_desc(it);
            V3 s = v3_mul(pd.part[0].b, 2.0f);
            m = m4_mul(m, m4_trs(v3(0, 0, 0), v3(s.x / 0.30f, s.y / 0.12f, s.z / 0.22f), 0, 0, 0));
        }
        b[wpn ? CHAR_BONE_WEAPON : 0] = m;
        out[n++] = (DrawItem){ .mesh = wpn ? &weapon[weapon_mesh(it.id, it.quality)] : &v->bundle, .bones = b };
    }
    return n;
}

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

static Item hand_item(const CombatWorld *w, u32 i, const CharSheet *sh)
{
    return sh ? sh->equip[SLOT_WEAPON] : item_make(IK_WEAPON, w->weapon[i], w->weapon_q[i], 1);
}

// Общее для поля и арены: что с оружием бойца i в этом шаге. sh — лист (поле) или NULL (арена: только возврат)
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
        Item it = hand_item(w, i, sh);
        if (it.kind != IK_WEAPON) return;  // нечего ронять
        Rng r;
        rng_seed(&r, tick, src + i);
        f32 x = origin.x + w->x[i], z = origin.z + w->z[i];
        V3 v, av;
        hand_vel(w->heading[i], &r, &v, &av);
        k = ground_drop(g, p, it, hand_xf(x, y, z, w->heading[i], &r), v, av, (u16)(src + i + 1));
        if (k < 0) return;  // места нет: остаётся в руке
        w->gitem[i] = k;
        // Встанет — поднимет (после подъёма бой ведёт его к подбору)
        if (!gone) w->loose[i] = 1;
    } else if (k >= 0 && !lost) {
        // Поднял своё; если его уже унесли — рука пуста
        if (ground_live(g, k) && g->owner[k] == src + i + 1) ground_take(g, p, (u32)k);
        w->gitem[i] = COMBAT_IN_HAND;
    }
    // Мёртвый и безрукий уже не поднимут: на поле оружие становится ничьим, лист его больше не держит
    if (sh && gone && w->gitem[i] >= 0) {
        k = w->gitem[i];
        if (ground_live(g, k) && g->owner[k] == src + i + 1) g->owner[k] = 0;
        w->gitem[i] = COMBAT_GONE;
        sh->equip[SLOT_WEAPON] = (Item){ 0 };
    }
}

void loot_field(Ground *g, Phys *p, Field *f, GroundFn ground, u64 tick)
{
    CombatWorld *w = &f->w;
    for (u32 i = 0; i < w->count; i++) {
        if (w->idle[i]) {
            // Бой стих, а оружие лежит: живой подбирает его, уходя
            i32 k = w->gitem[i];
            if (k >= 0 && ground_live(g, k) && g->owner[k] == GROUND_OWN_FIELD + i + 1) ground_take(g, p, (u32)k);
            if (k >= 0) w->gitem[i] = COMBAT_IN_HAND;
            continue;
        }
        combat_hand(g, p, w, i, f->sheet[i], GROUND_OWN_FIELD, v3(0, 0, 0), ground(w->x[i], w->z[i]), tick);
    }
    // Хозяина слота сменили (field_reuse) — его выроненное теперь ничьё
    for (u32 k = 0; k < g->count; k++) {
        u16 o = g->owner[k];
        if (!g->item[k].kind || !o || (o & GROUND_OWN_SRC) != GROUND_OWN_FIELD) continue;
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
        if (!g->item[k].kind || (o & GROUND_OWN_SRC) != GROUND_OWN_ARENA) continue;
        u32 i = (o & ~GROUND_OWN_SRC) - 1u;
        if (!in_bout || i >= w->count || w->gitem[i] != (i32)k) ground_take(g, p, k);
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
        if (ground_drop(g, p, a->it[n], xf, v, w, 0) < 0 && a->who[n] < sq->count)
            inv_add(&squad_sheet(sq, a->who[n])->inv, a->it[n]);  // некуда класть — обратно в мешок
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
        if ((o & GROUND_OWN_SRC) == GROUND_OWN_ARENA && o) continue;  // бойцов арены не грабят
        CharSheet *c = squad_sheet(sq, i);
        Inventory probe = c->inv;
        if (!inv_add(&probe, g->item[k])) {
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
                f->sheet[s]->equip[SLOT_WEAPON] = (Item){ 0 };
            }
        }
        inv_add(&c->inv, ground_take(g, p, (u32)k));
        b->moving = false;
        b->target = b->pos;
    }
}

void loot_drop_test(Ground *g, Phys *p, V3 at, u32 n, u64 seed)
{
    Rng r;
    rng_seed(&r, seed, 23);
    if (!g_weapon_pool_count && !g_goods_count) return;
    for (u32 i = 0; i < n; i++) {
        Item it = g_goods_count && (!g_weapon_pool_count || rng_below(&r, 3) == 0)
                      ? item_make(IK_GOODS, (u8)rng_below(&r, g_goods_count), Q_WORN, 1)
                      : item_make(IK_WEAPON, g_weapon_pool[rng_below(&r, g_weapon_pool_count)], (u8)rng_below(&r, 3), 1);
        V3 c = v3_add(at, v3(rng_range(&r, -1.5f, 1.5f), 1.2f + 0.3f * (f32)i, rng_range(&r, -1.5f, 1.5f)));
        M4 xf = m4_trs(c, v3(1, 1, 1), rng_range(&r, 0, 2 * PI_F), rng_range(&r, 0, 2 * PI_F), rng_range(&r, 0, 2 * PI_F));
        V3 w = v3(rng_range(&r, -3, 3), rng_range(&r, -3, 3), rng_range(&r, -3, 3));
        ground_drop(g, p, it, xf, v3(0, -0.5f, 0), w, 0);
    }
}
