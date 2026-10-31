#include "debug.h"

#include "../core/log.h"
#include "../core/prof.h"
#include "../sim/loot.h"
#include "../sim/scrap.h"
#include "../view/loot_view.h"
#include "editor.h"
#include "humans_row.h"
#include "showhall.h"
#include "stand.h"

#define ARENA_SHOW_AFTER_BOUT_S 12.0f
#define DEBUG_CMDS 16
#define PHYS_DROP_N 24  // лома за нажатие
#define LOOT_DROP_N 8   // вещей за нажатие

const bool g_debug = true;

// Команды мира: копятся между шагами, исполняются в крючке шага
typedef enum DebugCmdKind { CMD_BANDS_TEST, CMD_PHYS_DROP, CMD_LOOT_DROP } DebugCmdKind;
typedef struct DebugCmd {
    u8  kind;
    u32 n;
} DebugCmd;

struct Debug {
    World       *w;
    Arena       *perm;
    MeshBuilder *mb;
    const char  *data_dir;
    DebugCmd     cmd[DEBUG_CMDS];
    u32          ncmd;
    // Арена: бои подряд, как в --bench arena с этим зерном, но в реальном времени. Выроненное оружие — в свои
    // вещи на земле и свою физику
    ArenaSession *arena;
    ArenaRing     ring;
    Ground       *ground;
    Phys         *phys;
    LootView      loot;
    ShowHall      hall;
    Stand         stand;
    HumansRow     humans;
    Editor        editor;
    // Просьбы меню на следующий кадр
    bool humans_flip, stand_on, stand_moved;
    u32  stand_row;
};

static void cmd_push(Debug *d, u8 kind, u32 n)
{
    if (d->ncmd < DEBUG_CMDS) d->cmd[d->ncmd++] = (DebugCmd){ kind, n };
}

// Пробные отряды вдали: по одному у Скудели и в долине, враждуют и идут навстречу по одной дороге
static void bands_test(World *w)
{
    Bands *b = w->bands;
    if (b->count) {
        log_info("ОТРЯДЫ: пробные уже идут");
        return;
    }
    // Места мира: 0 — зал, 1 — Скудель, 2 — долина (world_init)
    static const u16 men[BAND_TIERS] = { 3, 2, 1 };
    i32 a = bands_add(b, 0, 1, men), c = bands_add(b, 1, 2, men);
    if (a < 0 || c < 0) return;
    bands_hostile(b, 0, 1);
    field_enmity(w->field, TEAM_BANDS + 0, TEAM_BANDS + 1);
    bands_send(b, (u32)a, 2);
    bands_send(b, (u32)c, 1);
    log_info("ОТРЯДЫ: два пробных отряда, у Скудели и в долине, идут навстречу");
}

// Крючок шага мира: команды, затем бой арены и его вещи
static void debug_step(World *w, void *user)
{
    Debug *d = user;
    V3 at = v3(w->focus.x, w->floor(w->focus.x, w->focus.z), w->focus.z);
    for (u32 k = 0; k < d->ncmd; k++) {
        const DebugCmd *c = &d->cmd[k];
        if (c->kind == CMD_BANDS_TEST) bands_test(w);
        else if (c->kind == CMD_LOOT_DROP) loot_drop_test(w->ground, w->phys, at, c->n, w->seed ^ w->tick);
        else {
            Rng r;
            rng_seed(&r, w->seed ^ w->tick, 17);
            scrap_drop(w->phys, &r, at, c->n);
        }
    }
    d->ncmd = 0;
    prof_zone(PZ_ARENA);
    arena_ring_tick(&d->ring, d->arena);
    loot_arena(d->ground, d->phys, d->arena, d->ring.in_bout, d->ring.center, d->ring.floor_y, w->tick);
    d->phys->on = w->phys->on;
    phys_zone(d->phys, &d->ring.center, 1);
    phys_tick(d->phys, TICK_DT, w->floor);
}

Debug *debug_init(Arena *perm, MeshBuilder *mb, const char *data_dir, World *w, const DebugStart *st)
{
    Debug *d = ARENA_PUSH_ARRAY(perm, Debug, 1);
    *d = (Debug){ .w = w, .perm = perm, .mb = mb, .data_dir = data_dir };
    d->arena = ARENA_PUSH_ARRAY(perm, ArenaSession, 1);
    if (!arena_session_init(d->arena, perm, w->seed)) return NULL;
    d->arena->log_bouts = 0xFFFFFFFFu;
    d->arena->after_bout_s = ARENA_SHOW_AFTER_BOUT_S;
    arena_ring_init(&d->ring, ARENA_CENTER, RING_FLOOR_Y);
    d->ground = ARENA_PUSH_ARRAY(perm, Ground, 1);
    ground_init(d->ground, perm);
    d->phys = ARENA_PUSH_ARRAY(perm, Phys, 1);
    phys_init(d->phys, perm);
    loot_view_init(&d->loot, mb, perm);
    showhall_init(&d->hall, mb, perm);
    d->hall.t_off = st->hall_t;
    humans_row_init(&d->humans, perm, w->floor);
    stand_init(&d->stand, perm);
    editor_init(&d->editor);
    d->humans_flip = st->humans;
    d->stand_on = d->stand_moved = st->stand > 0;
    d->stand_row = st->stand > 0 ? st->stand - 1 : 0;
    if (st->bands_test) cmd_push(d, CMD_BANDS_TEST, 0);
    if (st->phys_drop) cmd_push(d, CMD_PHYS_DROP, st->phys_drop);
    if (st->loot_drop) cmd_push(d, CMD_LOOT_DROP, st->loot_drop);
    w->hook = debug_step;
    w->hook_user = d;
    return d;
}

ArenaSession *debug_arena(Debug *d) { return d->arena; }
ArenaRing *debug_ring(Debug *d) { return &d->ring; }

DebugStand debug_update(Debug *d, RtsCamera *cam)
{
    if (d->humans_flip) {
        d->humans_flip = false;
        humans_row_toggle(&d->humans, d->mb, d->perm, d->data_dir, d->w->floor);
    }
    if (!d->stand_moved) return DEBUG_STAND_SAME;
    d->stand_moved = false;
    stand_show(&d->stand, d->stand_on, d->stand_row, d->mb, d->perm, d->w->floor, cam);
    return d->stand_on ? DEBUG_STAND_SHOWN : DEBUG_STAND_HIDDEN;
}

void debug_menu(Debug *d, Hud *h, RtsCamera *cam)
{
    if (hud_debug_button(h, "КАМЕРА: В МЕРИЛЬНЮ")) *cam = SHOWHALL_CAM;
    if (hud_debug_button(h, "ОТРЯДЫ ВДАЛИ: ПУСТИТЬ ПРОБНЫЕ")) cmd_push(d, CMD_BANDS_TEST, 0);
    if (hud_debug_button(h, "ФИЗИКА: БРОСИТЬ ЛОМ")) cmd_push(d, CMD_PHYS_DROP, PHYS_DROP_N);
    if (hud_debug_button(h, "ФИЗИКА: БРОСИТЬ ВЕЩИ")) cmd_push(d, CMD_LOOT_DROP, LOOT_DROP_N);
    bool humans_want = d->humans.on;
    if (hud_debug_check(h, "ЛЮДИ ДЛЯ СРАВНЕНИЯ", &humans_want)) {
        d->humans_flip = true;
        if (humans_want) *cam = (RtsCamera){ .target = { 3.4f, 0, 22 }, .yaw = 0.0f, .pitch = 0.15f, .dist = 6.5f };
    }
    d->stand_moved |= hud_debug_check(h, "СТЕНД ФИГУР", &d->stand_on);
    static const char *stand_row[] = { "СТЕНД: МУЖЧИНА", "СТЕНД: ЖЕНЩИНА", "СТЕНД: ХУДОЙ", "СТЕНД: РОСТ", "СТЕНД: ПРИЧЁСКИ" };
    if (d->stand_on && hud_debug_button(h, stand_row[d->stand_row % 5])) {
        d->stand_row = (d->stand_row + 1) % 5;
        d->stand_moved = true;
    }
}

void debug_menu_light(HudForm *f, RtsCamera *cam)
{
    if (hud_form_button(f, "КАМЕРА: В МЕРИЛЬНЮ")) *cam = SHOWHALL_CAM;
}

void debug_input(Debug *d, Hud *h, Input *in, World *w, RtsCamera *cam)
{
    editor_input(&d->editor, h, in, w, cam, d->humans.on);
}

i32 debug_follow(const Debug *d) { return editor_follow(&d->editor); }

void debug_present(Debug *d, const World *w, f32 dt) { arena_ring_present(&d->ring, d->arena, w->alpha, dt); }

f32 debug_ground(f32 x, f32 z, f32 base) { return showhall_ground(x, z, base); }

V3 debug_clamp_eye(V3 target, V3 eye) { return showhall_clamp_eye(target, eye); }

void debug_room(RoomDark *r) { showhall_room(r); }

u32 debug_lights(f32 time, PointLight *out, u32 max) { return showhall_lights(time, out, max); }

u32 debug_draw(Debug *d, const CharMeshes *cm, const World *w, V3 eye, f32 time, f32 dt, bool people, DrawItem *items)
{
    if (!people) {
        items[0] = (DrawItem){ .mesh = &d->hall.mesh };
        return 1;
    }
    u32 n = showhall_draw(&d->hall, cm, eye, time, dt, items);
    n += loot_draw(&d->loot, d->ground, d->phys, cm->weapon, w->alpha, eye, items + n, LOOT_DRAW_MAX);
    n += humans_row_draw(&d->humans, cm, items + n);
    n += stand_draw(&d->stand, cm, items + n);
    return n;
}

void debug_labels(const Debug *d, M4 view_proj, V3 eye) { showhall_labels(&d->hall, view_proj, eye); }
