#include "field.h"

#include <stdio.h>
#include <string.h>

#include "../core/clock.h"
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

void field_init(Field *f, Arena *a, u64 seed)
{
    memset(f, 0, sizeof *f);
    combat_init(&f->w, a, FIELD_MAX, 4096);
    combat_reset(&f->w, seed);
    fighter_ai_init(&f->ai, a, FIELD_MAX);
    // Лежачих не добивают, сдачу принимают. Вражды нет, пока её не начнут
    f->w.mercy = true;
    memset(f->w.foe, 0, sizeof f->w.foe);
}

u32 field_add(Field *f, Player *body, CharSheet *sheet, u8 team, u8 flags)
{
    CombatWorld *w = &f->w;
    if (w->count >= FIELD_MAX) return FIELD_MAX - 1;
    FighterDesc d = sheet->d;
    d.team = team;
    d.pos = body->pos;
    d.heading = body->heading;
    u32 i = combat_add(w, &d);
    w->idle[i] = 1;
    f->body[i] = body;
    f->sheet[i] = sheet;
    f->flags[i] = flags;
    body->slot = (i32)i;
    return i;
}

void field_reuse(Field *f, u32 i, u8 team, u8 flags)
{
    CombatWorld *w = &f->w;
    if (i >= w->count) return;
    Player *p = f->body[i];
    FighterDesc d = f->sheet[i]->d;
    d.team = team;
    d.pos = p->pos;
    d.heading = p->heading;
    combat_load(w, i, &d);
    w->idle[i] = 1;
    w->order[i] = w->target[i] = -1;
    w->grid_dirty = true;
    f->flags[i] = flags;
    f->soft[i] = f->leave[i] = 0;
    f->calm[i] = 0.0f;
    p->slot = (i32)i;
    p->ctrl = CTRL_FREE;
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

static bool armed(const Field *f, u32 i) { return f->sheet[i]->equip[SLOT_WEAPON].kind == IK_WEAPON; }

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
        log_info("%s: НЕЧЕМ БИТЬ", f->sheet[who]->name);
        return false;
    }
    u8 a = w->team[who], b = w->team[target];
    if (!combat_hostile(w, who, target)) log_info("%s ТЕПЕРЬ ВРАГ", f->sheet[target]->name);
    combat_set_foes(w, a, b, true);
    f->grudge[a] = f->grudge[b] = GRUDGE_S;
    if (w->order[who] != (i32)target) log_info("%s: ЦЕЛЬ — %s", f->sheet[who]->name, f->sheet[target]->name);
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
    if (f->body[who]->ctrl == CTRL_CHASE) f->body[who]->ctrl = CTRL_FREE;
}

// Слот просыпается: тело, раны и оружие из листа, место — где стоит тело
static void wake(Field *f, u32 i)
{
    CombatWorld *w = &f->w;
    if (!w->idle[i]) return;
    Player *p = f->body[i];
    FighterDesc d = f->sheet[i]->d;
    d.team = w->team[i];
    d.pos = p->pos;
    d.heading = p->heading;
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
    p->ctrl = CTRL_FIGHT;
    p->speed = 0.0f;
    f->fights++;  // до пересчёта в конце тика
}

// Слот засыпает: раны в лист, тело снова у ходьбы
static void sleep_slot(Field *f, u32 i, GroundFn ground)
{
    CombatWorld *w = &f->w;
    if (w->idle[i]) return;
    combat_store(w, i, &f->sheet[i]->d);
    w->idle[i] = 1;
    w->target[i] = -1;
    w->mvx[i] = w->mvz[i] = 0.0f;
    combat_set_state(w, i, FS_READY, 0.0f);
    w->grid_dirty = true;
    Player *p = f->body[i];
    p->pos = v3(w->x[i], ground(w->x[i], w->z[i]), w->z[i]);
    p->heading = p->move_yaw = w->heading[i];
    p->speed = p->turn_rate = p->look = 0.0f;
    p->ctrl = CTRL_FREE;
    if (!f->leave[i]) {
        p->target = p->pos;
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
        if (d < best_d || (d == best_d && (i32)j < best)) {
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
    if (!f->soft[i]) log_info("%s: ЦЕЛЬ ВЫБЫЛА — %s, %s", f->sheet[i]->name, f->sheet[t]->name, g_state_name[f->w.state[t]]);
    f->w.order[i] = -1;
    f->soft[i] = 0;
}

static void log_events(Field *f)
{
    CombatWorld *w = &f->w;
    for (u32 k = 0; k < w->ev_count; k++) {
        const CombatEvent *e = &w->ev[k];
        const char *a = f->sheet[e->a]->name;
        switch (e->kind) {
        case EV_KO:        log_info("%s БЕЗ СОЗНАНИЯ", a); break;
        case EV_DEATH:     log_info("%s МЁРТВ", a); break;
        case EV_SURRENDER: log_info("%s СДАЁТСЯ", a); break;
        case EV_RETREAT:   log_info("%s ОТСТУПАЕТ", a); break;
        default: break;
        }
    }
    combat_events_done(w);
}

// Правила поля после тика боя: кто просыпается, кто засыпает, чей приказ выполнен.
// Идут по номерам в одном потоке, поэтому исход не зависит от числа потоков
static void rules_tick(Field *f, GroundFn ground)
{
    CombatWorld *w = &f->w;
    u32 active = 0;  // команды, которые сейчас дерутся
    for (u32 i = 0; i < w->count; i++) {
        Player *p = f->body[i];
        i32 o = w->order[i];
        if (o >= 0 && !target_ok(f, i, (u32)o)) {
            order_done(f, i, (u32)o);
            o = -1;
        }
        // Пока есть приказ, вражда не забывается: погоня может быть долгой
        if (o >= 0) active |= 1u << w->team[i] | 1u << w->team[o];
        if (w->idle[i]) {
            if (o >= 0) {
                f32 d = dist(w, i, (u32)o);
                if (d <= ENGAGE_R) {
                    wake(f, i);
                    wake(f, (u32)o);
                } else if (f->soft[i] && d > LEASH_R) {
                    w->order[i] = -1;
                    f->soft[i] = 0;
                }
                continue;
            }
            // Остальным оглядываться раз в COMBAT_THINK_TICKS: на тысячах тел это заметно
            if ((i + w->tick) % COMBAT_THINK_TICKS) continue;
            // Идёт по приказу игрока: мимо боя проходит, не ввязываясь
            if ((f->flags[i] & FF_PLAYER) && p->moving) continue;
            if (nearest_foe(f, i, WAKE_R, false) >= 0) {
                wake(f, i);
                continue;
            }
            i32 j = -1;
            if ((f->flags[i] & FF_ASSIST) && ally_fighting(f, i, ASSIST_R)) j = nearest_foe(f, i, ASSIST_R, false);
            if (j < 0 && (f->flags[i] & FF_AGGRO)) j = nearest_foe(f, i, AGGRO_R, true);
            if (j >= 0) {
                w->order[i] = j;
                f->soft[i] = 1;
            }
            continue;
        }

        u8 s = w->state[i];
        if (s == FS_DEAD) continue;  // тело остаётся лежать
        combat_store(w, i, &f->sheet[i]->d);
        bool free = s == FS_READY || s == FS_RETREAT;
        if (f->leave[i] && free) {
            sleep_slot(f, i, ground);
            continue;
        }
        if (o >= 0) {
            f32 d = dist(w, i, (u32)o);
            // Цель отошла: спящую будит, если рядом, иначе догоняет бегом
            if (w->idle[o] && d <= ENGAGE_R) wake(f, (u32)o);
            else if (free && (w->idle[o] || d > CHASE_R)) {
                sleep_slot(f, i, ground);
                continue;
            }
        }
        bool foe_near = nearest_foe(f, i, CALM_R, false) >= 0;
        f->calm[i] = foe_near ? 0.0f : f->calm[i] + COMBAT_DT;
        if (foe_near && !fs_out(s)) active |= 1u << w->team[i];
        if (o < 0 && f->calm[i] >= CALM_S && (free || s == FS_SURRENDER)) sleep_slot(f, i, ground);
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
    // Спящие стоят там, где их довела ходьба
    for (u32 i = 0; i < w->count; i++) {
        if (w->idle[i]) {
            w->x[i] = f->body[i]->pos.x;
            w->z[i] = f->body[i]->pos.z;
            w->heading[i] = f->body[i]->heading;
        }
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

    for (u32 i = 0; i < w->count; i++) {
        Player *p = f->body[i];
        i32 o = w->order[i];
        if (!w->idle[i]) {
            // В бою тело стоит там, где его держит симуляция; вид боя (выпады, сглаживание) — у кадра
            p->pos = v3(w->x[i], ground(w->x[i], w->z[i]), w->z[i]);
            p->heading = p->move_yaw = w->heading[i];
            continue;
        }
        if (o >= 0) {
            // Бежит к цели своим шагом; чужие гонятся бегом
            p->target = f->body[o]->pos;
            p->moving = true;
            p->ctrl = CTRL_CHASE;
            p->marker = 0.0f;
            if (!(f->flags[i] & FF_PLAYER)) p->vmax = sheet_speed(f->sheet[i], GAIT_RUN);
        } else if (p->ctrl == CTRL_CHASE) {
            // Погоня окончена: стоит, где догнал
            p->ctrl = CTRL_FREE;
            p->target = p->pos;
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

// ---------------------------------------------------------------- проверка без окна

static f32 flat_ground(f32 x, f32 z) { (void)x; (void)z; return 0.0f; }

// Тела без отряда идут сами: бродящие держат свой шаг, гонящиеся — бег, заданный полем
static void walk_bodies(Player *b, u32 n, f32 dt)
{
    for (u32 i = 0; i < n; i++) {
        if (b[i].ctrl == CTRL_FREE) b[i].vmax = g_gait_speed[GAIT_WALK];
        player_update(&b[i], dt, flat_ground);
    }
}

static void print_slots(const Field *f, f32 t)
{
    printf("%6.1f с |", (f64)t);
    for (u32 i = 0; i < f->w.count; i++) {
        const char *s = !f->w.idle[i] ? g_state_name[f->w.state[i]] : (f->body[i]->ctrl == CTRL_CHASE ? "БЕЖИТ К ЦЕЛИ" : (f->body[i]->moving ? "ИДЁТ" : "-"));
        printf(" %s: %s |", f->sheet[i]->name, s);
    }
    printf("\n");
}

// Сценка: свой отряд из четырёх атакует одного из трёх чужих; рядом двое прохожих
static int bench_scene(Arena *mem, u64 seed)
{
    Rng rng;
    rng_seed(&rng, seed, 77);
    Field *f = ARENA_PUSH_ARRAY(mem, Field, 1);
    field_init(f, mem, seed);
    Squad *sq = ARENA_PUSH_ARRAY(mem, Squad, 1);
    People *people = ARENA_PUSH_ARRAY(mem, People, 1);
    squad_init(sq, people, &rng, v3(0, 0, 0), 0.0f, flat_ground);
    static const char *names[] = { "ЧУРИЛА", "ВЕЛИМИР", "ГОСТЯТА", "ФЕОДУЛ", "ПРОКЛ" };
    Player npc[5];
    CharSheet *sheet = ARENA_PUSH_ARRAY(mem, CharSheet, 5);
    for (u32 i = 0; i < sq->count; i++) {
        field_add(f, squad_body(sq, i), squad_sheet(sq, i), 0, FF_PLAYER | FF_ASSIST);
        sq->selected[i] = true;
    }
    for (u32 k = 0; k < 5; k++) {
        V3 p = k < 3 ? v3((f32)k * 1.5f - 1.5f, 0, 18.0f) : v3(7.0f + (f32)k, 0, 9.0f);
        player_init(&npc[k], p, PI_F);
        npc[k].gait = GAIT_WALK;
        sheet_random(&sheet[k], &rng, names[k], k == 0 ? 1 : 0, (u8)(k % W_COUNT));
        field_add(f, &npc[k], &sheet[k], k < 3 ? 1 : (u8)(k - 1), k < 3 ? FF_ASSIST : 0);
    }
    printf("СЦЕНКА: ОТРЯД (4) АТАКУЕТ %s; С НИМ ДВОЕ СВОИХ, РЯДОМ ДВОЕ ПРОХОЖИХ\n", names[0]);
    for (u32 i = 0; i < sq->count; i++) field_attack(f, i, sq->count);
    // Шагами мира, как в игре: ходьба, затем тик поля
    const f32 dt = COMBAT_DT;
    f32 t = 0.0f, next_print = 0.0f;
    bool done = false;
    for (u32 fr = 0; fr < COMBAT_HZ * 180 && !done; fr++, t += dt) {
        squad_update(sq, dt, flat_ground);
        walk_bodies(npc, 5, dt);
        field_tick(f, flat_ground);
        if (t >= next_print) {
            print_slots(f, t);
            next_print += 4.0f;
        }
        // Конец: все спят, приказов нет
        done = t > 5.0f;
        for (u32 i = 0; i < f->w.count; i++)
            if (f->w.order[i] >= 0 || (!f->w.idle[i] && !fs_out(f->w.state[i]))) done = false;
    }
    print_slots(f, t);
    u32 tgt = sq->count;
    bool ok = fs_out(f->w.state[tgt]) || f->sheet[tgt]->d.blood < 0.5f * 100.0f * f->sheet[tgt]->d.mass / 75.0f;
    printf("ИТОГ: %s, ЦЕЛЬ %s, БОЙ ЗАТИХ: %s\n\n", names[0], g_state_name[f->w.state[tgt]], done ? "ДА" : "НЕТ");
    (void)ok;
    return done ? 0 : 1;
}

// Нагрузка: n тел в двух толпах; своя сторона по приказу атакует, чужая нападает и помогает своим
static int bench_scale(Arena *mem, u32 n, u64 seed)
{
    Rng rng;
    rng_seed(&rng, seed, 77);
    Field *f = ARENA_PUSH_ARRAY(mem, Field, 1);
    field_init(f, mem, seed);
    Player *b = ARENA_PUSH_ARRAY(mem, Player, n);
    CharSheet *sh = ARENA_PUSH_ARRAY(mem, CharSheet, n);
    for (u32 i = 0; i < n; i++) {
        u8 side = (u8)(i & 1);
        u32 k = i / 2, row = k / 25;
        f32 x = ((f32)(k % 25) - 12.0f) * 1.6f + rng_range(&rng, -0.3f, 0.3f);
        f32 z = (side ? 25.0f : -25.0f) + (side ? 1.0f : -1.0f) * (f32)row * 1.8f;
        player_init(&b[i], v3(x, 0, z), side ? PI_F : 0.0f);
        b[i].gait = GAIT_RUN;
        f32 roll = rng_f32(&rng);
        sheet_random(&sh[i], &rng, side ? "ЧУЖОЙ" : "СВОЙ", roll < 0.5f ? 0 : (roll < 0.85f ? 1 : 2), (u8)rng_range(&rng, 0, W_COUNT - 0.01f));
        field_add(f, &b[i], &sh[i], side, side ? (FF_ASSIST | FF_AGGRO) : (FF_ASSIST | FF_PLAYER));
    }
    log_set_echo(false);
    // Своя сторона идёт в атаку: каждый на того, кто стоит напротив
    for (u32 i = 0; i + 1 < n; i += 2) field_attack(f, i, i + 1);
    const f32 dt = COMBAT_DT;
    f64 sum = 0.0, worst = 0.0;
    u32 frames = 0, peak = 0;
    for (; frames < COMBAT_HZ * 120; frames++) {
        f64 t0 = time_now();
        for (u32 i = 0; i < n; i++) {
            if (b[i].ctrl == CTRL_FREE) b[i].vmax = g_gait_speed[GAIT_RUN];
            player_update(&b[i], dt, flat_ground);
        }
        field_tick(f, flat_ground);
        f64 ms = (time_now() - t0) * 1000.0;
        sum += ms;
        worst = ms > worst ? ms : worst;
        peak = MAX(peak, f->fights);
    }
    u32 out[2] = { 0, 0 }, hist[2][FS_COUNT] = { { 0 } }, idle[2] = { 0, 0 };
    for (u32 i = 0; i < n; i++) {
        if (fs_out(f->w.state[i])) out[i & 1]++;
        if (f->w.idle[i]) idle[i & 1]++;
        else hist[i & 1][f->w.state[i]]++;
    }
    for (u32 sd = 0; sd < 2; sd++) {
        printf("сторона %u: спят %u;", sd, idle[sd]);
        for (u32 k = 0; k < FS_COUNT; k++) if (hist[sd][k]) printf(" %s %u;", g_state_name[k], hist[sd][k]);
        printf("\n");
    }
    printf("НАГРУЗКА: %u ТЕЛ, 120 С: ШАГ МИРА (ХОДЬБА И ТИК ПОЛЯ) СРЕДНИЙ %.3f МС, ХУДШИЙ %.3f МС; В БОЮ ДО %u; ВЫБЫЛО СВОИХ %u, ЧУЖИХ %u\n",
           n, sum / frames, worst, peak, out[0], out[1]);
    log_set_echo(true);
    return 0;
}

int field_bench(u32 bodies, u64 seed)
{
    Arena mem;
    if (!arena_init(&mem, MB(16) + (size_t)bodies * KB(4))) return 1;
    int rc = bench_scene(&mem, seed);
    arena_reset(&mem);
    if (bodies) rc |= bench_scale(&mem, bodies, seed);
    arena_free(&mem);
    return rc;
}
