#include "field.h"

#include <string.h>

#include "../core/log.h"
#include "squad.h"

#define ENGAGE_R 3.5f   // ближе к цели — вступает в бой: дальше самого длинного удара (багор 3 м)
#define CHASE_R  6.0f   // цель в бою отошла дальше — догоняет бегом
#define WAKE_R   3.5f   // враг в бою так близко — спящий просыпается и отбивается
#define CALM_R   9.0f   // враг дальше — тихо
#define CALM_S   2.5f   // столько тихо — бой для бойца окончен
#define ASSIST_R 14.0f  // свои дерутся так близко — идёт на помощь
#define AGGRO_R  9.0f   // так близко видит тех, с кем враждует
#define LEASH_R  22.0f  // за своей целью дальше этого не гонится
#define GRUDGE_S 40.0f  // столько помнит вражду после боя

void field_init(Field *f, Arena *a, Persons *ps, u64 seed)
{
    memset(f, 0, sizeof *f);
    f->ps = ps;
    combat_init(&f->w, a, FIELD_MAX, 4096);
    // Место, тело, навыки и действие бойца — столбцы реестра: копий нет, раны пишутся сразу человеку
    CombatBody b = { .x = ps->x, .z = ps->z, .heading = ps->heading, .blood = ps->blood, .mass = ps->mass,
                     .height = ps->stature, .morale_base = ps->morale_base, .fracture = ps->fracture,
                     .severed = ps->severed, .act = ps->act, .act_flags = ps->act_flags, .act_t = ps->act_t,
                     .act_t0 = ps->act_t0, .act_dur = ps->act_dur };
    for (u32 p = 0; p < PART_COUNT; p++) b.hp[p] = ps->hp[p];
    for (u32 s = 0; s < SK_COUNT; s++) b.skill[s] = ps->skill[s];
    combat_bind_body(&f->w, &b);
    combat_reset(&f->w, seed);
    fighter_ai_init(&f->ai, a, FIELD_MAX);
    // Лежачих не добивают, сдачу принимают. Вражды нет, пока её не начнут
    f->w.mercy = true;
    memset(f->w.foe, 0, sizeof f->w.foe);
}

void field_join(Field *f, u32 i, u8 team, u8 flags)
{
    CombatWorld *w = &f->w;
    if (i > w->count || i >= FIELD_MAX) return;
    FighterDesc d = person_fighter(f->ps, i, team);
    if (i == w->count) {
        combat_add(w, &d);
        w->idle[i] = 1;
        f->flags[i] = flags;
        return;
    }
    combat_load(w, i, &d);
    w->idle[i] = 1;
    w->order[i] = w->target[i] = -1;
    w->grid_dirty = true;
    f->flags[i] = flags;
    f->soft[i] = f->leave[i] = 0;
    f->calm[i] = 0.0f;
    f->ps->move[i].ctrl = CTRL_FREE;
}

void field_free(Field *f, u32 i)
{
    CombatWorld *w = &f->w;
    if (i >= w->count) return;
    w->idle[i] = 1;
    w->team[i] = FIELD_TEAM_NOBODY;
    w->order[i] = w->target[i] = -1;
    w->mvx[i] = w->mvz[i] = 0.0f;
    combat_set_state(w, i, FS_DEAD, 0.0f);
    w->grid_dirty = true;
    f->flags[i] = FF_ABSENT;
    f->soft[i] = f->leave[i] = 0;
}

void field_ser(Ser *s, Field *f)
{
    combat_ser(s, &f->w);
    u32 n = f->w.count;
    fighter_ai_ser(s, &f->ai, n);
    SER_N(s, f->flags, n);
    SER_N(s, f->soft, n);
    SER_N(s, f->leave, n);
    SER_N(s, f->calm, n);
    SER_N(s, f->px, n);
    SER_N(s, f->pz, n);
    SER_N(s, f->ph, n);
    SER(s, f->grudge);
    SER(s, f->enmity);
    SER(s, f->fights);
}

void field_view_restore(Field *f)
{
    for (u32 i = 0; i < f->w.count; i++)
        if (!f->w.idle[i]) fight_anim_settle(&f->anim[i], &f->w, i, (f32)i * 13.7f + 3.0f);
}

void field_enmity(Field *f, u8 a, u8 b)
{
    if (a >= COMBAT_MAX_TEAMS || b >= COMBAT_MAX_TEAMS || a == b) return;
    f->enmity[a] |= 1u << b;
    f->enmity[b] |= 1u << a;
    combat_set_foes(&f->w, a, b, true);
}

static f32 dist(const CombatWorld *w, u32 i, u32 j)
{
    f32 dx = w->x[j] - w->x[i], dz = w->z[j] - w->z[i];
    return sqrtf(dx * dx + dz * dz);
}

// Есть чем бить: в руке или своё выроненное лежит рядом (поднимет)
static bool armed(const Field *f, u32 i)
{
    return item_weapon(f->ps->sheet[i].equip[SLOT_WEAPON]) != COMP_NONE || (i < f->w.count && f->w.gitem[i] >= 0);
}

// Цель ещё в бою: жива, не без сознания, не сдалась
static bool target_ok(const Field *f, u32 i, u32 t)
{
    const CombatWorld *w = &f->w;
    return t < w->count && t != i && w->team[t] != w->team[i] && !fs_out(w->state[t]);
}

bool field_hostile(const Field *f, u32 a, u32 b)
{
    return a < f->w.count && b < f->w.count && combat_hostile(&f->w, a, b);
}

i32 field_target(const Field *f, u32 i) { return i < f->w.count ? f->w.order[i] : -1; }

void field_pos(const Field *f, u32 i, f32 a, f32 *x, f32 *z, f32 *heading)
{
    const CombatWorld *w = &f->w;
    *x = lerpf(f->px[i], w->x[i], a);
    *z = lerpf(f->pz[i], w->z[i], a);
    f32 dh = w->heading[i] - f->ph[i];
    while (dh > PI_F) dh -= 2.0f * PI_F;
    while (dh < -PI_F) dh += 2.0f * PI_F;
    *heading = f->ph[i] + dh * a;
}

bool field_attack(Field *f, u32 who, u32 target)
{
    CombatWorld *w = &f->w;
    if (who >= w->count || !target_ok(f, who, target) || w->state[who] == FS_DEAD) return false;
    if (!armed(f, who)) {
        log_info("%s: НЕЧЕМ БИТЬ", f->ps->sheet[who].name);
        return false;
    }
    u8 a = w->team[who], b = w->team[target];
    if (!combat_hostile(w, who, target)) log_info("%s ТЕПЕРЬ ВРАГ", f->ps->sheet[target].name);
    combat_set_foes(w, a, b, true);
    f->grudge[a] = f->grudge[b] = GRUDGE_S;
    if (w->order[who] != (i32)target) log_info("%s: ЦЕЛЬ — %s", f->ps->sheet[who].name, f->ps->sheet[target].name);
    w->order[who] = (i32)target;
    f->soft[who] = 0;
    f->leave[who] = 0;
    return true;
}

void field_cancel(Field *f, u32 who, bool leave)
{
    CombatWorld *w = &f->w;
    if (who >= w->count) return;
    w->order[who] = -1;
    f->soft[who] = 0;
    if (!w->idle[who]) f->leave[who] = leave;
    if (f->ps->move[who].ctrl == CTRL_CHASE) f->ps->move[who].ctrl = CTRL_FREE;
}

// Слот просыпается: тело и раны — свои столбцы реестра, оружие из листа
static void wake(Field *f, u32 i)
{
    CombatWorld *w = &f->w;
    if (!w->idle[i]) return;
    FighterDesc d = person_fighter(f->ps, i, w->team[i]);
    combat_load(w, i, &d);
    w->disarmed[i] = !armed(f, i);
    w->idle[i] = 0;
    w->target[i] = -1;
    w->grid_dirty = true;
    // Свою цель выбирал сам: в бою выберет заново
    if (f->soft[i]) {
        w->order[i] = -1;
        f->soft[i] = 0;
    }
    // Догнали — дерётся, приказ идти пропал; уйти можно новым приказом
    f->leave[i] = 0;
    f->px[i] = w->x[i];
    f->pz[i] = w->z[i];
    f->ph[i] = w->heading[i];
    f->calm[i] = 0.0f;
    fight_anim_reset(&f->anim[i], w, i, (f32)i * 13.7f + 3.0f);
    f->ps->move[i].ctrl = CTRL_FIGHT;
    f->ps->move[i].speed = 0.0f;
    f->fights++;  // до пересчёта в конце тика
}

// Слот засыпает: тело снова у ходьбы; раны и навыки уже у человека
static void sleep_slot(Field *f, u32 i, GroundFn ground)
{
    CombatWorld *w = &f->w;
    if (w->idle[i]) return;
    w->idle[i] = 1;
    w->target[i] = -1;
    w->mvx[i] = w->mvz[i] = 0.0f;
    combat_set_state(w, i, FS_READY, 0.0f);
    w->grid_dirty = true;
    Persons *ps = f->ps;
    Motion *p = &ps->move[i];
    ps->y[i] = ground(w->x[i], w->z[i]);
    p->move_yaw = w->heading[i];
    p->speed = p->turn_rate = p->look = 0.0f;
    p->ctrl = CTRL_FREE;
    if (!f->leave[i]) {
        p->target = person_pos(ps, i);
        p->moving = false;
    }
    f->leave[i] = 0;
    f->calm[i] = 0.0f;
}

// Ближайший враг слота i в радиусе r. any: и спящие тоже; иначе только те, кто в бою
static i32 nearest_foe(const Field *f, u32 i, f32 r, bool any)
{
    const CombatWorld *w = &f->w;
    u32 mask = combat_foe_tags(w, w->team[i]);
    if (!mask) return -1;
    if (any) mask |= TAG_BIT(TAG_DEAD);
    if (!combat_any_near(w, w->x[i], w->z[i], r, mask)) return -1;
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], r, mask, near);
    i32 best = -1;
    f32 best_d = r;
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (j == i || !combat_hostile(w, i, j) || fs_out(w->state[j]) || (!any && w->idle[j])) continue;
        f32 d = dist(w, i, j);
        if (d < best_d || (d == best_d && best >= 0 && combat_prefer(i, j, (u32)best))) {
            best_d = d;
            best = (i32)j;
        }
    }
    return best;
}

// Кто-то из своих дерётся в радиусе r
static bool ally_fighting(const Field *f, u32 i, f32 r)
{
    const CombatWorld *w = &f->w;
    u32 mask = TEAM_BIT(w->team[i]);
    if (!combat_any_near(w, w->x[i], w->z[i], r, mask)) return false;
    u32 near[COMBAT_NEAR_MAX];
    u32 nn = combat_near(w, w->x[i], w->z[i], r, mask, near);
    for (u32 q = 0; q < nn; q++) {
        u32 j = near[q];
        if (j != i && !w->idle[j] && w->team[j] == w->team[i] && dist(w, i, j) <= r) return true;
    }
    return false;
}

static void order_done(Field *f, u32 i, u32 t)
{
    if (!f->soft[i]) log_info("%s: ЦЕЛЬ ВЫБЫЛА — %s, %s", f->ps->sheet[i].name, f->ps->sheet[t].name, g_state_name[f->w.state[t]]);
    f->w.order[i] = -1;
    f->soft[i] = 0;
}

static void log_events(Field *f)
{
    CombatWorld *w = &f->w;
    for (u32 k = 0; k < w->ev_count; k++) {
        const CombatEvent *e = &w->ev[k];
        const char *a = f->ps->sheet[e->a].name;
        u8 kind = CH_KIND_COUNT;
        switch (e->kind) {
        case EV_KO:        log_info("%s БЕЗ СОЗНАНИЯ", a); kind = CH_KO; break;
        case EV_DEATH:     log_info("%s МЁРТВ", a); kind = CH_DEATH; break;
        case EV_SURRENDER: log_info("%s СДАЁТСЯ", a); kind = CH_SURRENDER; break;
        case EV_RETREAT:   log_info("%s ОТСТУПАЕТ", a); break;
        default: break;
        }
        if (kind < CH_KIND_COUNT)
            chron_add(f->chron, (ChronRec){ .kind = kind, .from = 255, .to = 255, .a = e->a, .b = CHRON_NOBODY,
                                            .x = w->x[e->a], .z = w->z[e->a] });
    }
    combat_events_done(w);
}

// Правила поля после тика боя: кто просыпается, кто засыпает, чей приказ выполнен.
// Сначала все читают, кто ещё спит и кто ещё в бою. Потом пробуждение и сон.
// Иначе младший номер будит или укладывает цель до того, как она сама решит.
static void rules_tick(Field *f, GroundFn ground)
{
    CombatWorld *w = &f->w;
    u8 do_wake[FIELD_MAX], do_sleep[FIELD_MAX], ord_mode[FIELD_MAX];
    i32 ord_arg[FIELD_MAX];
    u32 active = 0;
    u32 n = w->count;
    if (n > FIELD_MAX) n = FIELD_MAX;
    memset(do_wake, 0, n);
    memset(do_sleep, 0, n);
    memset(ord_mode, 0, n);
    for (u32 i = 0; i < n; i++) {
        const Motion *p = &f->ps->move[i];
        i32 o = w->order[i];
        if (o >= 0 && !target_ok(f, i, (u32)o)) {
            ord_mode[i] = 1;
            ord_arg[i] = o;
            o = -1;
        }
        if (o >= 0) active |= 1u << w->team[i] | 1u << w->team[o];
        if (w->idle[i]) {
            if (o >= 0) {
                f32 d = dist(w, i, (u32)o);
                if (d <= ENGAGE_R) {
                    do_wake[i] = 1;
                    do_wake[o] = 1;
                } else if (f->soft[i] && d > LEASH_R) {
                    ord_mode[i] = 3;
                }
                continue;
            }
            if (!combat_thinks(w, i)) continue;
            if ((f->flags[i] & FF_PLAYER) && p->moving) continue;
            if (nearest_foe(f, i, WAKE_R, false) >= 0) {
                do_wake[i] = 1;
                continue;
            }
            i32 j = -1;
            if ((f->flags[i] & FF_ASSIST) && ally_fighting(f, i, ASSIST_R)) j = nearest_foe(f, i, ASSIST_R, false);
            if (j < 0 && (f->flags[i] & FF_AGGRO)) j = nearest_foe(f, i, AGGRO_R, true);
            if (j >= 0) {
                ord_mode[i] = 2;
                ord_arg[i] = j;
                active |= 1u << w->team[i] | 1u << w->team[j];
            }
            continue;
        }

        u8 s = w->state[i];
        if (s == FS_DEAD) continue;
        bool free = s == FS_READY || s == FS_RETREAT;
        if (f->leave[i] && free) {
            do_sleep[i] = 1;
            continue;
        }
        if (o >= 0) {
            f32 d = dist(w, i, (u32)o);
            if (w->idle[o] && d <= ENGAGE_R) do_wake[o] = 1;
            else if (free && (w->idle[o] || d > CHASE_R)) {
                do_sleep[i] = 1;
                continue;
            }
        }
        bool foe_near = nearest_foe(f, i, CALM_R, false) >= 0;
        f->calm[i] = foe_near ? 0.0f : f->calm[i] + COMBAT_DT;
        if (foe_near && !fs_out(s)) active |= 1u << w->team[i];
        if (o < 0 && f->calm[i] >= CALM_S && (free || s == FS_SURRENDER)) do_sleep[i] = 1;
    }
    for (u32 i = 0; i < n; i++) {
        if (do_wake[i]) wake(f, i);
        else if (do_sleep[i]) sleep_slot(f, i, ground);
        if (ord_mode[i] == 1) order_done(f, i, (u32)ord_arg[i]);
        else if (ord_mode[i] == 2) {
            w->order[i] = ord_arg[i];
            f->soft[i] = 1;
        } else if (ord_mode[i] == 3) {
            w->order[i] = -1;
            f->soft[i] = 0;
        }
    }
    // Бою есть что считать, пока кто-то не спит и жив: без сознания тоже приходит в себя в бою
    f->fights = 0;
    for (u32 i = 0; i < w->count; i++) f->fights += !w->idle[i] && w->state[i] != FS_DEAD;
    // Вражда забывается, когда команда долго не дерётся
    for (u32 t = 0; t < COMBAT_MAX_TEAMS; t++) {
        if (active & (1u << t)) {
            f->grudge[t] = GRUDGE_S;
        } else if (f->grudge[t] > 0.0f && (f->grudge[t] -= COMBAT_DT) <= 0.0f) {
            f->grudge[t] = 0.0f;
            for (u32 u = 0; u < COMBAT_MAX_TEAMS; u++)
                if (f->grudge[u] <= 0.0f && !(f->enmity[t] >> u & 1u)) combat_set_foes(w, (u8)t, (u8)u, false);
        }
    }
}

void field_tick(Field *f, GroundFn ground)
{
    CombatWorld *w = &f->w;
    // Место спящих — там, где их довела ходьба: те же столбцы реестра
    for (u32 i = 0; i < w->count; i++) {
        f->px[i] = w->x[i];
        f->pz[i] = w->z[i];
        f->ph[i] = w->heading[i];
        if (!w->idle[i]) fight_anim_pre_tick(&f->anim[i], w, i);
    }
    w->grid_dirty = true;
    if (f->fights) {
        fighter_think_all(&f->ai, w);
        combat_tick(w);
    } else {
        // Никто не дерётся: бою считать нечего, нужна только сетка для оглядки
        combat_sync_grid(w);
        w->tick++;
    }
    rules_tick(f, ground);
    log_events(f);

    Persons *ps = f->ps;
    for (u32 i = 0; i < w->count; i++) {
        Motion *p = &ps->move[i];
        i32 o = w->order[i];
        if (!w->idle[i]) {
            // В бою тело стоит там, где его держит симуляция; вид боя (выпады, сглаживание) — у кадра
            ps->y[i] = ground(w->x[i], w->z[i]);
            p->move_yaw = w->heading[i];
            continue;
        }
        if (o >= 0) {
            // Бежит к цели своим шагом; чужие гонятся бегом
            p->target = person_pos(ps, (u32)o);
            p->moving = true;
            p->ctrl = CTRL_CHASE;
            p->marker = 0.0f;
            if (!(f->flags[i] & FF_PLAYER)) p->vmax = person_speed(ps, i, GAIT_RUN);
        } else if (p->ctrl == CTRL_CHASE) {
            // Погоня окончена: стоит, где догнал
            p->ctrl = CTRL_FREE;
            p->target = person_pos(ps, i);
            p->moving = false;
        }
    }
}

void field_present(Field *f, f32 alpha, f32 dt)
{
    CombatWorld *w = &f->w;
    f->alpha = alpha;
    for (u32 i = 0; i < w->count; i++)
        if (!w->idle[i]) fight_anim_update(&f->anim[i], w, i, alpha, dt);
}
