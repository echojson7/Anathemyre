#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "../render/render.h"
#include "../render/ui.h"

// Палитра старой машины: воронёный металл, кость, выцветшая охра; бледно-зелёный — только свои выбранные
#define C_METAL_T UI_RGBA(38, 36, 32, 236)
#define C_METAL_B UI_RGBA(17, 16, 14, 242)
#define C_HEAD_T  UI_RGBA(52, 47, 38, 255)
#define C_HEAD_B  UI_RGBA(30, 28, 23, 255)
#define C_HI      UI_RGBA(92, 86, 72, 255)
#define C_LO      UI_RGBA(4, 4, 3, 255)
#define C_INSET   UI_RGBA(9, 9, 8, 235)
#define C_CELL    UI_RGBA(26, 24, 21, 250)
#define C_EDGE    UI_RGBA(74, 68, 56, 255)
#define C_TEXT    UI_RGBA(212, 204, 182, 255)
#define C_DIM     UI_RGBA(126, 119, 102, 255)
#define C_FAINT   UI_RGBA(78, 74, 64, 255)
#define C_ACCENT  UI_RGBA(206, 156, 78, 255)
#define C_BRACKET UI_RGBA(150, 110, 56, 255)
#define C_SEL     UI_RGBA(142, 214, 150, 255)
#define C_BAD     UI_RGBA(196, 54, 40, 255)
#define C_BLOOD   UI_RGBA(140, 34, 26, 255)
#define C_SHADE   UI_RGBA(0, 0, 0, 120)

// Раскладка, логические пиксели
#define BOTTOM_H   104
#define SEL_W      298
#define CARD_W     62
#define CARD_GAP   4
#define BTN_W      100
#define BTN_H      16
#define INV_WIN_W  262
#define INV_WIN_H  282
#define CHAR_WIN_W 286
#define CHAR_WIN_H 338
#define WIN_TOP    48
#define SLOT_W     121
#define SLOT_H     20
#define CELL       20
#define CLOCK_W    150
#define CLOCK_H    32
#define DOUBLE_CLICK_S 0.35f
#define SELECT_RING_R 0.48f  // радиус кольца выбора под ногами, как в main.c

// Порядок слотов в окне инвентаря: две колонки по пять
static const u8 g_slot_order[SLOT_COUNT + 1] = {
    SLOT_HEAD, SLOT_FACE, SLOT_TORSO_UNDER, SLOT_TORSO_ARMOR, SLOT_HANDS,
    SLOT_LEGS, SLOT_BELT, SLOT_FEET, SLOT_BACK, SLOT_WEAPON,
};

// Схема тела: силуэт человека, руки вдоль тела. Лицом к зрителю: правая рука слева.
// H голова, C грудь, S живот, A/B правая/левая рука, R/L правая/левая нога
#define TPOSE_W 32
#define TPOSE_H 46
static const char *g_tpose[TPOSE_H] = {
    "..............HHHH..............",
    "............HHHHHHHH............",
    "...........HHHHHHHHHH...........",
    "...........HHHHHHHHHH...........",
    "...........HHHHHHHHHH...........",
    "............HHHHHHHH............",
    ".............HHHHHH.............",
    "..............HHHH..............",
    "..........CCCCCCCCCCCC..........",
    "........A.CCCCCCCCCCCC.B........",
    "......AAA.CCCCCCCCCCCC.BBB......",
    "......AAA.CCCCCCCCCCCC.BBB......",
    "......AAA.CCCCCCCCCCCC.BBB......",
    "......AAA..CCCCCCCCCC..BBB......",
    ".....AAA...CCCCCCCCCC...BBB.....",
    ".....AAA...CCCCCCCCCC...BBB.....",
    ".....AAA...CCCCCCCCCC...BBB.....",
    ".....AAA...CCCCCCCCCC...BBB.....",
    ".....AAA...SSSSSSSSSS...BBB.....",
    ".....AA....SSSSSSSSSS....BB.....",
    "....AAA....SSSSSSSSSS....BBB....",
    "....AAA....SSSSSSSSSS....BBB....",
    "....AAA...SSSSSSSSSSSS...BBB....",
    "....AAA...SSSSSSSSSSSS...BBB....",
    "....AA....SSSSSSSSSSSS....BB....",
    "....AAA...SSSSSSSSSSSS...BBB....",
    "...AAAA...RRRRR..LLLLL...BBBB...",
    "...AAAA...RRRRR..LLLLL...BBBB...",
    "....AA....RRRRR..LLLLL....BB....",
    "..........RRRRR..LLLLL..........",
    "..........RRRRR..LLLLL..........",
    "...........RRRR..LLLL...........",
    "...........RRRR..LLLL...........",
    "...........RRRR..LLLL...........",
    "...........RRRR..LLLL...........",
    "...........RRRR..LLLL...........",
    "...........RRR....LLL...........",
    "..........RRRR....LLLL..........",
    "..........RRRR....LLLL..........",
    "...........RRR....LLL...........",
    "...........RRR....LLL...........",
    "...........RRR....LLL...........",
    "...........RRR....LLL...........",
    "...........RRR....LLL...........",
    "..........RRRR....LLLL..........",
    ".........RRRRR....LLLLL.........",
};

static i32 tpose_part(i32 x, i32 y)
{
    if (x < 0 || y < 0 || x >= TPOSE_W || y >= TPOSE_H) return -1;
    switch (g_tpose[y][x]) {
    case 'H': return PART_HEAD;
    case 'C': return PART_CHEST;
    case 'S': return PART_STOMACH;
    case 'A': return PART_ARM_R;
    case 'B': return PART_ARM_L;
    case 'R': return PART_LEG_R;
    case 'L': return PART_LEG_L;
    default: return -1;
    }
}

static const char *g_gait_btn[GAIT_COUNT] = { "ШАГ", "ТРУСЦА", "БЕГ" };
static const char *g_gait_key[GAIT_COUNT] = { "Z", "X", "C" };

// Всё, что панели и окна показывают о персонаже, откуда бы он ни был
typedef struct LookData {
    const char *name, *role, *state;
    f32 hp[PART_COUNT], blood, blood_max, bleed, height, mass;
    u8  fracture, severed;
    const f32 *skill;
    Item equip[SLOT_COUNT + 1];
    CharSheet *sheet;   // свои: вещи можно перекладывать
    const Player *body; // свои: режим шага
} LookData;

typedef struct Layout {
    f32 w, h, mx, my;
    f32 bottom;       // верх нижней полосы
    f32 sel_x;        // панель выбранного
    f32 cards_x;
    f32 btn_x;
    f32 clock_x;
    f32 iwx, iwy;     // окно инвентаря
    f32 sx, sy;       // его слоты
    f32 ix, iy;       // его сетка
    f32 cwx, cwy;     // окно персонажа
} Layout;

static Layout layout(const Input *in)
{
    Layout L;
    f32 s = ui_scale();
    L.w = ui_width();
    L.h = ui_height();
    L.mx = floorf((f32)in->mouse_x / s);
    L.my = floorf((f32)in->mouse_y / s);
    L.bottom = L.h - BOTTOM_H - 8;
    L.sel_x = 8;
    L.cards_x = L.sel_x + SEL_W + 8;
    L.btn_x = L.w - BTN_W - 8;
    L.clock_x = L.w - CLOCK_W - 8;
    L.iwx = L.w - INV_WIN_W - 8;
    L.iwy = WIN_TOP;
    L.sx = L.iwx + 8;
    L.sy = L.iwy + 20;
    L.ix = L.sx;
    L.iy = L.sy + 5 * (SLOT_H + 3) + 14;
    L.cwx = 8;
    L.cwy = WIN_TOP;
    return L;
}

static bool inside(f32 mx, f32 my, f32 x, f32 y, f32 w, f32 h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static f32 card_x(const Layout *L, u32 i) { return L->cards_x + (f32)i * (CARD_W + CARD_GAP); }

static void slot_rect(const Layout *L, u32 k, f32 *x, f32 *y)
{
    *x = L->sx + (f32)(k % 2) * (SLOT_W + 4);
    *y = L->sy + (f32)(k / 2) * (SLOT_H + 3);
}

static i32 slot_under(const Layout *L)
{
    for (u32 k = 0; k <= SLOT_COUNT; k++) {
        f32 x, y;
        slot_rect(L, k, &x, &y);
        if (inside(L->mx, L->my, x, y, SLOT_W, SLOT_H)) return (i32)g_slot_order[k];
    }
    return -1;
}

static bool cell_under(const Layout *L, i32 *cx, i32 *cy)
{
    if (!inside(L->mx, L->my, L->ix, L->iy, INV_W * CELL, INV_H * CELL)) return false;
    *cx = (i32)((L->mx - L->ix) / CELL);
    *cy = (i32)((L->my - L->iy) / CELL);
    return true;
}

// Кнопки: режимы шага и строй в панели выбранного, окна внизу справа, скорость в часах
static void gait_btn_rect(const Layout *L, u32 g, f32 *x, f32 *y, f32 *w)
{
    static const f32 off[GAIT_COUNT] = { 0, 40, 92 }, wid[GAIT_COUNT] = { 38, 50, 36 };
    *x = L->sel_x + 92 + off[g];
    *y = L->bottom + 70;
    *w = wid[g];
}
static void formation_btn_rect(const Layout *L, f32 *x, f32 *y) { *x = L->sel_x + 92; *y = L->bottom + 86; }
static f32 win_btn_y(const Layout *L, u32 k) { return L->bottom + (f32)k * (BTN_H + 4); }
static f32 speed_btn_x(const Layout *L, u32 k) { return L->clock_x + 62 + (f32)k * 21; }

// Экранная точка в логических координатах; false — за камерой
static bool project(M4 vp, V3 p, f32 *sx, f32 *sy)
{
    const f32 *m = vp.m;
    f32 x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    f32 y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    f32 w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w < 0.1f) return false;
    *sx = (x / w * 0.5f + 0.5f) * ui_width();
    *sy = (0.5f - y / w * 0.5f) * ui_height();
    return true;
}

i32 hud_arena_fighter(const ArenaView *v, const ArenaSession *s, u32 roster)
{
    if (!v->in_bout) return -1;
    for (u32 i = 0; i < s->n && i < s->w.count; i++)
        if (s->idx[i] == roster) return (i32)i;
    return -1;
}

bool hud_arena_pos(const ArenaView *v, const ArenaSession *s, u32 roster, V3 *out)
{
    i32 i = hud_arena_fighter(v, s, roster);
    if (i < 0) return false;
    f32 a = v->acc / COMBAT_DT;
    *out = v3(v->center.x + lerpf(v->prev_x[i], s->w.x[i], a), v->floor_y,
              v->center.z + lerpf(v->prev_z[i], s->w.z[i], a));
    return true;
}

// Кто под курсором: свои и бойцы на ринге
static bool pick_at(const HudWorld *w, f32 mx, f32 my, u8 *kind, u32 *index, V3 *head)
{
    f32 best = 1e9f;
    u32 total = w->squad->count + ARENA_MAX_BOUT;
    for (u32 k = 0; k < total; k++) {
        V3 pos;
        f32 tall = 1.75f;
        u8 kd = LOOK_SQUAD;
        u32 idx = k;
        if (k < w->squad->count) {
            pos = w->squad->body[k].pos;
        } else {
            u32 i = k - w->squad->count;
            if (!w->aview->in_bout || i >= w->arena->n) continue;
            kd = LOOK_ARENA;
            idx = w->arena->idx[i];
            hud_arena_pos(w->aview, w->arena, idx, &pos);
            if (fs_lying(w->arena->w.state[i])) tall = 0.4f;
        }
        f32 fx, fy, hx, hy;
        V3 top = v3_add(pos, v3(0, tall, 0));
        if (!project(w->view_proj, pos, &fx, &fy) || !project(w->view_proj, top, &hx, &hy)) continue;
        f32 half = fmaxf(5.0f, 0.22f * (fy - hy));
        if (tall < 1.0f) half = fmaxf(half, 0.6f * (fy - hy) + 6.0f);
        if (my < hy - 3 || my > fy + 3 || fabsf(mx - fx) > half) continue;
        f32 d = fabsf(mx - fx) + 0.3f * fabsf(my - 0.5f * (fy + hy));
        if (d < best) {
            best = d;
            *kind = kd;
            *index = idx;
            *head = top;
        }
    }
    return best < 1e9f;
}

// Режим передвижения видно и когда стоит: «БЕГ» — побежит по приказу
static const char *move_state(const Player *p)
{
    static const char *go[GAIT_COUNT] = { "ИДЁТ ШАГОМ", "ТРУСИТ", "БЕЖИТ" };
    return p->moving ? go[p->gait] : "СТОИТ";
}

static bool look_data(u8 kind, u32 look, const HudWorld *w, LookData *o)
{
    memset(o, 0, sizeof *o);
    if (kind == LOOK_SQUAD) {
        if (look >= w->squad->count) return false;
        CharSheet *c = &w->squad->sheet[look];
        o->name = c->name;
        o->role = "ОТРЯД";
        o->body = &w->squad->body[look];
        o->state = move_state(o->body);
        memcpy(o->hp, c->d.hp, sizeof o->hp);
        o->blood = c->d.blood;
        o->blood_max = 100.0f * c->d.mass / 75.0f;
        o->height = c->d.height;
        o->mass = c->d.mass;
        o->fracture = c->d.fracture;
        o->severed = c->d.severed;
        o->skill = c->d.skill;
        memcpy(o->equip, c->equip, sizeof o->equip);
        o->sheet = c;
        return true;
    }
    if (kind != LOOK_ARENA || look >= ARENA_ROSTER) return false;
    const ArenaSession *s = w->arena;
    const ArenaFighter *f = &s->roster[look];
    i32 i = hud_arena_fighter(w->aview, s, look);
    o->name = arena_roster_name(s, look);
    o->role = "БОЕЦ АРЕНЫ";
    o->height = f->d.height;
    o->mass = f->d.mass;
    o->blood_max = 100.0f * f->d.mass / 75.0f;
    o->skill = f->d.skill;
    if (i >= 0) {
        // На ринге: живое состояние из боя
        const CombatWorld *cw = &s->w;
        o->state = g_state_name[cw->state[i]];
        for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = cw->hp[p][i];
        o->blood = cw->blood[i];
        o->bleed = cw->bleed[i];
        o->fracture = cw->fracture[i];
        o->severed = cw->severed[i];
        if (!cw->disarmed[i]) o->equip[SLOT_WEAPON] = item_make(IK_WEAPON, cw->weapon[i], cw->weapon_q[i], 1);
        for (u32 sl = 0; sl < SLOT_COUNT; sl++)
            if (cw->armor[sl][i] != ARMOR_NONE) o->equip[sl] = item_make(IK_ARMOR, cw->armor[sl][i], Q_WORN, 1);
        return true;
    }
    // Между боями: из ростера; нули в hp и крови значат «здоров»
    o->state = f->gone ? "ВЫБЫЛ" : "ОТДЫХАЕТ";
    bool fresh = f->d.blood <= 0.0f;
    for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = fresh ? g_part_hp[p] : f->d.hp[p];
    o->blood = fresh ? o->blood_max : f->d.blood;
    o->fracture = f->d.fracture;
    o->severed = f->d.severed;
    o->equip[SLOT_WEAPON] = item_make(IK_WEAPON, f->d.weapon, f->d.weapon_q, 1);
    for (u32 sl = 0; sl < SLOT_COUNT; sl++)
        if (f->d.armor[sl] != ARMOR_NONE) o->equip[sl] = item_make(IK_ARMOR, f->d.armor[sl], Q_WORN, 1);
    return true;
}

void hud_init(Hud *h)
{
    memset(h, 0, sizeof *h);
    h->last_click_card = -1;
    h->focus = -1;
    h->speed = h->speed_before = 1;
}

void hud_look(Hud *h, u8 kind, u32 index)
{
    h->look_kind = kind;
    h->look = index;
}

bool hud_window_open(const Hud *h) { return h->win_inv || h->win_char; }

// Вещь с курсора обратно в мешок; некуда — false, окно не закрывается
static bool drop_held(Hud *h, Squad *sq)
{
    if (!h->held.kind) return true;
    if (h->look_kind != LOOK_SQUAD || !inv_add(&sq->sheet[h->look].inv, h->held)) return false;
    h->held.kind = IK_NONE;
    return true;
}

static void look_at(Hud *h, Squad *sq, u8 kind, u32 index)
{
    if (h->look_kind == kind && h->look == index) return;
    if (!drop_held(h, sq)) return;
    hud_look(h, kind, index);
}

// Перекладывание вещей в окне своего персонажа
static void inventory_input(Hud *h, const Layout *L, CharSheet *c, bool lmb, bool rmb)
{
    i32 gx, gy, slot = slot_under(L);
    bool in_cell = cell_under(L, &gx, &gy);
    Item old;
    if (lmb && !h->held.kind) {
        i32 at = in_cell ? inv_at(&c->inv, gx, gy) : -1;
        if (at >= 0) h->held = inv_take(&c->inv, at);
        else if (slot >= 0 && c->equip[slot].kind) {
            h->held = c->equip[slot];
            sheet_equip(c, (u8)slot, (Item){ 0 }, &old);
        }
    } else if (lmb) {
        if (in_cell && inv_put(&c->inv, h->held, gx, gy)) h->held.kind = IK_NONE;
        else if (slot >= 0 && sheet_equip(c, (u8)slot, h->held, &old)) h->held = old;
    } else if (rmb && !h->held.kind) {
        // ПКМ: надеть из мешка или снять в мешок
        i32 at = in_cell ? inv_at(&c->inv, gx, gy) : -1;
        if (at >= 0 && item_slot(c->inv.item[at]) != SLOT_NONE) {
            Item it = inv_take(&c->inv, at);
            u8 sl = item_slot(it);
            sheet_equip(c, sl, it, &old);
            if (old.kind && !inv_add(&c->inv, old)) {
                sheet_equip(c, sl, old, &it);
                inv_add(&c->inv, it);
            }
        } else if (slot >= 0 && c->equip[slot].kind && inv_add(&c->inv, c->equip[slot])) {
            sheet_equip(c, (u8)slot, (Item){ 0 }, &old);
        }
    }
}

static void select_only(Squad *sq, u32 i)
{
    for (u32 k = 0; k < sq->count; k++) sq->selected[k] = k == i;
}

static bool close_hot(const Layout *L, f32 x, f32 y, f32 w) { return inside(L->mx, L->my, x + w - 13, y + 3, 10, 10); }

bool hud_input(Hud *h, const Input *in, HudWorld *w)
{
    Squad *sq = w->squad;
    Layout L = layout(in);
    bool lmb = in->mouse_pressed[MOUSE_LEFT], rmb = in->mouse_pressed[MOUSE_RIGHT];
    bool lmb_up = h->lmb_was && !in->mouse_down[MOUSE_LEFT];
    bool shift = in->key_down[KEY_SHIFT];
    bool over = false;
    h->lmb_was = in->mouse_down[MOUSE_LEFT];
    h->focus = -1;

    // Панель показывает первого выбранного, пока не щёлкнули по другому
    if (h->look_kind == LOOK_SQUAD && (h->look >= sq->count || !sq->selected[h->look]) && !h->held.kind) {
        i32 p = squad_primary(sq);
        if (p >= 0) hud_look(h, LOOK_SQUAD, (u32)p);
        else h->look_kind = LOOK_NONE;
    }
    if (h->look_kind == LOOK_NONE) {
        i32 p = squad_primary(sq);
        if (p >= 0) hud_look(h, LOOK_SQUAD, (u32)p);
    }

    bool has_look = h->look_kind != LOOK_NONE;
    if (in->key_pressed[KEY_I] && (h->win_inv || has_look) && drop_held(h, sq)) h->win_inv = !h->win_inv;
    if (in->key_pressed[KEY_P] && (h->win_char || has_look)) h->win_char = !h->win_char;
    if (in->key_pressed[KEY_ESCAPE] && drop_held(h, sq)) h->win_inv = h->win_char = false;
    if (in->key_pressed[KEY_SPACE]) {
        if (h->speed) {
            h->speed_before = h->speed;
            h->speed = 0;
        } else {
            h->speed = h->speed_before;
        }
    }

    // Окна
    if (h->win_inv && inside(L.mx, L.my, L.iwx, L.iwy, INV_WIN_W, INV_WIN_H)) {
        over = true;
        if (lmb && close_hot(&L, L.iwx, L.iwy, INV_WIN_W)) {
            if (drop_held(h, sq)) h->win_inv = false;
        } else if (h->look_kind == LOOK_SQUAD && h->look < sq->count) {
            inventory_input(h, &L, &sq->sheet[h->look], lmb, rmb);
        }
        lmb = rmb = false;
    }
    if (h->win_char && inside(L.mx, L.my, L.cwx, L.cwy, CHAR_WIN_W, CHAR_WIN_H)) {
        over = true;
        if (lmb && close_hot(&L, L.cwx, L.cwy, CHAR_WIN_W)) h->win_char = false;
        lmb = rmb = false;
    }

    // Часы: скорость времени
    if (inside(L.mx, L.my, L.clock_x, 8, CLOCK_W, CLOCK_H)) {
        over = true;
        for (u32 k = 0; k < 4 && lmb; k++)
            if (inside(L.mx, L.my, speed_btn_x(&L, k), 24, 19, 11)) {
                if (k == 0 && h->speed) h->speed_before = h->speed;
                h->speed = (u8)k;
                if (k) h->speed_before = (u8)k;
            }
        lmb = false;
    }

    // Панель выбранного: режим шага и строй
    if (has_look && inside(L.mx, L.my, L.sel_x, L.bottom, SEL_W, BOTTOM_H)) {
        over = true;
        if (lmb && h->look_kind == LOOK_SQUAD) {
            for (u32 g = 0; g < GAIT_COUNT; g++) {
                f32 x, y, bw;
                gait_btn_rect(&L, g, &x, &y, &bw);
                if (inside(L.mx, L.my, x, y, bw, 13)) squad_set_gait(sq, (Gait)g);
            }
            f32 fx, fy;
            formation_btn_rect(&L, &fx, &fy);
            if (inside(L.mx, L.my, fx, fy, 70, 13)) sq->formation = !sq->formation;
        }
        lmb = false;
    }

    // Кнопки окон
    if (inside(L.mx, L.my, L.btn_x, L.bottom, BTN_W, 2 * BTN_H + 4)) {
        over = true;
        if (lmb && has_look) {
            if (inside(L.mx, L.my, L.btn_x, win_btn_y(&L, 0), BTN_W, BTN_H) && drop_held(h, sq)) h->win_inv = !h->win_inv;
            if (inside(L.mx, L.my, L.btn_x, win_btn_y(&L, 1), BTN_W, BTN_H)) h->win_char = !h->win_char;
        }
        lmb = false;
    }

    // Отряд: клик — выбрать, с Shift — добавить, двойной — камера к нему
    for (u32 i = 0; i < sq->count; i++) {
        if (!inside(L.mx, L.my, card_x(&L, i), L.bottom, CARD_W, BOTTOM_H)) continue;
        over = true;
        if (!lmb) break;
        if (h->last_click_card == (i32)i && w->time - h->last_click_t < DOUBLE_CLICK_S) h->focus = (i32)i;
        h->last_click_card = (i32)i;
        h->last_click_t = w->time;
        if (shift) sq->selected[i] = !sq->selected[i];
        else select_only(sq, i);
        if (sq->selected[i]) look_at(h, sq, LOOK_SQUAD, i);
        lmb = false;
        break;
    }

    // Мир: щелчок выбирает, протяжка тянет рамку
    if (lmb && !over && !h->held.kind) {
        h->dragging = true;
        h->drag_live = false;
        h->drag_x = L.mx;
        h->drag_y = L.my;
    }
    if (h->dragging && fabsf(L.mx - h->drag_x) + fabsf(L.my - h->drag_y) > 4.0f) h->drag_live = true;
    if (h->dragging && lmb_up) {
        bool box = h->drag_live;
        h->dragging = h->drag_live = false;
        if (box) {
            f32 x0 = fminf(h->drag_x, L.mx), x1 = fmaxf(h->drag_x, L.mx);
            f32 y0 = fminf(h->drag_y, L.my), y1 = fmaxf(h->drag_y, L.my);
            for (u32 i = 0; i < sq->count; i++) {
                // Рамка ловит круг у ног (тот, что подсвечивается выбором): центр или любую точку кольца
                bool in_box = false;
                for (u32 k = 0; k <= 12 && !in_box; k++) {
                    f32 a = (f32)k * (2.0f * PI_F / 12.0f), r = k ? SELECT_RING_R : 0.0f, px, py;
                    V3 p = v3_add(sq->body[i].pos, v3(sinf(a) * r, 0.03f, cosf(a) * r));
                    in_box = project(w->view_proj, p, &px, &py) && px >= x0 && px <= x1 && py >= y0 && py <= y1;
                }
                if (in_box) sq->selected[i] = true;
                else if (!shift) sq->selected[i] = false;
            }
            i32 p = squad_primary(sq);
            if (p >= 0 && (h->look_kind != LOOK_SQUAD || !sq->selected[h->look])) look_at(h, sq, LOOK_SQUAD, (u32)p);
        } else {
            u8 kind;
            u32 idx;
            V3 head;
            if (!pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
                if (!shift) {
                    memset(sq->selected, 0, sizeof sq->selected);
                    if (drop_held(h, sq)) {
                        h->look_kind = LOOK_NONE;
                        h->win_inv = h->win_char = false;
                    }
                }
            } else if (kind == LOOK_SQUAD) {
                if (shift) sq->selected[idx] = !sq->selected[idx];
                else select_only(sq, idx);
                if (sq->selected[idx]) look_at(h, sq, LOOK_SQUAD, idx);
            } else {
                // Чужого можно только осмотреть
                look_at(h, sq, LOOK_ARENA, idx);
            }
        }
    }
    return over || h->held.kind != IK_NONE || h->dragging;
}

// ---------------------------------------------------------------- оформление

static u32 health_color(f32 r)
{
    // Кость → охра → кровь → тёмная кровь
    f32 a[3] = { 186, 180, 156 }, b[3] = { 196, 132, 50 }, c[3] = { 170, 38, 28 };
    f32 o[3];
    if (r < 0.0f) return UI_RGBA(64, 14, 12, 255);
    for (u32 k = 0; k < 3; k++)
        o[k] = r >= 0.5f ? lerpf(b[k], a[k], (r - 0.5f) * 2.0f) : lerpf(c[k], b[k], r * 2.0f);
    return UI_RGBA((u32)o[0], (u32)o[1], (u32)o[2], 255);
}

static u32 darker(u32 c, f32 k)
{
    u32 r = (u32)((f32)(c & 255) * k), g = (u32)((f32)((c >> 8) & 255) * k), b = (u32)((f32)((c >> 16) & 255) * k);
    return UI_RGBA(r, g, b, c >> 24);
}

static void text_right(f32 x, f32 y, u32 color, const char *s) { ui_text(x - ui_text_width(s), y, color, s); }
static void num_right(f32 x, f32 y, u32 color, const char *s) { ui_num(x - ui_num_width(s), y, color, s); }

// Утопленное поле: тёмная заливка, тень сверху, блик снизу
static void inset(f32 x, f32 y, f32 w, f32 h)
{
    ui_rect(x, y, w, h, C_INSET);
    ui_rect(x, y, w, 1, C_LO);
    ui_rect(x, y, 1, h, C_LO);
    ui_rect(x, y + h - 1, w, 1, UI_RGBA(60, 56, 48, 255));
    ui_rect(x + w - 1, y, 1, h, UI_RGBA(60, 56, 48, 255));
}

// Скобы по углам
static void brackets(f32 x, f32 y, f32 w, f32 h, f32 len, u32 c)
{
    ui_rect(x, y, len, 1, c);
    ui_rect(x, y, 1, len, c);
    ui_rect(x + w - len, y, len, 1, c);
    ui_rect(x + w - 1, y, 1, len, c);
    ui_rect(x, y + h - 1, len, 1, c);
    ui_rect(x, y + h - len, 1, len, c);
    ui_rect(x + w - len, y + h - 1, len, 1, c);
    ui_rect(x + w - 1, y + h - len, 1, len, c);
}

// Панель старой машины: металл со скосом, скобы, заклёпки, шапка со знаками ушедших
static void panel(f32 x, f32 y, f32 w, f32 h, const char *title, const char *right)
{
    ui_rect(x + 2, y + 2, w, h, C_SHADE);
    ui_panel_fill(x, y, w, h, C_METAL_T, C_METAL_B);
    ui_rect(x, y, w, 1, C_HI);
    ui_rect(x, y, 1, h, C_HI);
    ui_rect(x, y + h - 1, w, 1, C_LO);
    ui_rect(x + w - 1, y, 1, h, C_LO);
    ui_frame(x + 2, y + 2, w - 4, h - 4, UI_RGBA(0, 0, 0, 80));
    brackets(x + 1, y + 1, w - 2, h - 2, 5, C_BRACKET);
    ui_rect(x + 4, y + h - 5, 1, 1, C_HI);
    ui_rect(x + w - 5, y + h - 5, 1, 1, C_HI);
    if (!title) return;
    ui_rect_grad(x + 3, y + 3, w - 6, 11, C_HEAD_T, C_HEAD_B);
    ui_rect(x + 3, y + 14, w - 6, 1, darker(C_BRACKET, 0.7f));
    f32 tx = ui_text(x + 7, y + 5, C_FAINT, "\xee\x80\x81");
    ui_text(tx + 4, y + 5, C_ACCENT, title);
    if (right) text_right(x + w - 18, y + 5, C_DIM, right);
}

static void close_btn(const Layout *L, f32 x, f32 y, f32 w)
{
    ui_text(x + w - 12, y + 5, close_hot(L, x, y, w) ? C_TEXT : C_DIM, "×");
}

static void button(f32 x, f32 y, f32 w, f32 h, const char *key, const char *label, bool active, bool hot)
{
    u32 top = active ? UI_RGBA(84, 62, 30, 255) : UI_RGBA(44, 41, 35, 255);
    u32 bot = active ? UI_RGBA(52, 38, 18, 255) : UI_RGBA(24, 23, 20, 255);
    ui_rect_grad(x, y, w, h, top, bot);
    ui_rect(x, y, w, 1, hot ? C_TEXT : C_HI);
    ui_rect(x, y + h - 1, w, 1, C_LO);
    ui_rect(x, y, 1, h, hot ? C_TEXT : C_HI);
    ui_rect(x + w - 1, y, 1, h, C_LO);
    f32 tx = x + 3;
    if (key) {
        ui_rect(x + 2, y + 2, 9, h - 4, UI_RGBA(12, 11, 10, 255));
        ui_text(x + 4, y + (h - 7) * 0.5f, active ? C_ACCENT : C_DIM, key);
        tx = x + 14;
    }
    ui_text(tx, y + floorf((h - 7) * 0.5f), active ? C_ACCENT : (hot ? C_TEXT : C_DIM), label);
}

static void bar(f32 x, f32 y, f32 w, f32 h, f32 r, u32 color)
{
    ui_rect(x, y, w, h, UI_RGBA(8, 8, 7, 255));
    ui_rect(x, y, floorf(w * clampf(r, 0.0f, 1.0f)), h, color);
    // Деления каждые 10%
    for (u32 k = 1; k < 10 && w >= 60; k++) ui_rect(x + floorf(w * (f32)k * 0.1f), y, 1, h, UI_RGBA(0, 0, 0, 110));
}

// Схема тела клетками маски: цвет по ранам, тёмный контур частей, мелкие цифры ХП
static void tpose(f32 x, f32 y, f32 unit, const f32 *hp, u8 fracture, u8 severed, bool numbers)
{
    for (i32 cy = 0; cy < TPOSE_H; cy++) {
        for (i32 cx = 0; cx < TPOSE_W; cx++) {
            i32 p = tpose_part(cx, cy);
            if (p < 0) continue;
            bool edge = tpose_part(cx - 1, cy) != p || tpose_part(cx + 1, cy) != p ||
                        tpose_part(cx, cy - 1) != p || tpose_part(cx, cy + 1) != p;
            f32 px = x + (f32)cx * unit, py = y + (f32)cy * unit;
            if (severed & (1u << p)) {
                // Отсечена: пунктир по контуру
                if (edge && (cx + cy) % 2 == 0) ui_rect(px, py, unit, unit, C_BAD);
                continue;
            }
            u32 c = health_color(hp[p] / g_part_hp[p]);
            // Сверху светлее, снизу темнее: объём без текстур
            c = darker(c, 0.95f - 0.15f * (f32)cy / TPOSE_H);
            if (edge) c = darker(c, 0.55f);
            else if ((fracture & (1u << p)) && (cx + cy) % 3 == 0) c = UI_RGBA(60, 10, 6, 255);
            ui_rect(px, py, unit, unit, c);
        }
    }
    if (!numbers) return;
    // Цифра по центру части, если влезает в её ширину на этой высоте
    char buf[8];
    for (u32 p = 0; p < PART_COUNT; p++) {
        if (severed & (1u << p)) continue;
        i32 y0 = TPOSE_H, y1 = -1;
        for (i32 cy = 0; cy < TPOSE_H; cy++)
            for (i32 cx = 0; cx < TPOSE_W; cx++)
                if (tpose_part(cx, cy) == (i32)p) {
                    y0 = MIN(y0, cy);
                    y1 = MAX(y1, cy);
                }
        if (y1 < 0) continue;
        i32 cy = (y0 + y1) / 2, x0 = -1, x1 = -1;
        for (i32 cx = 0; cx < TPOSE_W; cx++)
            if (tpose_part(cx, cy) == (i32)p) {
                if (x0 < 0) x0 = cx;
                x1 = cx;
            }
        snprintf(buf, sizeof buf, "%d", (int)ceilf(hp[p]));
        f32 nw = ui_num_width(buf), pw = (f32)(x1 - x0 - 1) * unit;
        if (nw > pw) continue;
        ui_num(floorf(x + (f32)(x0 + 1) * unit + (pw - nw) * 0.5f), floorf(y + ((f32)cy + 0.5f) * unit - UI_SMALL_H * 0.5f),
               UI_RGBA(14, 12, 10, 255), buf);
    }
}

static f32 vital_ratio(const f32 *hp)
{
    f32 r = 1.0f;
    for (u32 p = 0; p <= PART_STOMACH; p++) r = fminf(r, hp[p] / g_part_hp[p]);
    return r;
}

// Общее состояние одним словом, как в Kenshi
static const char *condition(const LookData *d)
{
    f32 v = vital_ratio(d->hp);
    if (v <= 0.0f) return "ПРИ СМЕРТИ";
    if (v < 0.35f || d->blood < 0.55f * d->blood_max) return "ТЯЖЕЛО РАНЕН";
    if (v < 0.99f || d->fracture || d->severed) return "РАНЕН";
    return "ЦЕЛ";
}

// Первые n букв UTF-8 в out
static void utf8_head(char *out, size_t size, const char *s, u32 n)
{
    size_t i = 0;
    for (u32 k = 0; s[i] && k < n; k++) {
        i++;
        while (s[i] && ((u8)s[i] & 0xC0) == 0x80) i++;
    }
    if (i >= size) i = size - 1;
    memcpy(out, s, i);
    out[i] = 0;
}

// Текст, обрезанный по ширине
static void text_fit(f32 x, f32 y, f32 w, u32 color, const char *s)
{
    char buf[64];
    utf8_head(buf, sizeof buf, s, (u32)((w + 1) / UI_ADVANCE));
    ui_text(x, y, color, buf);
}

static void portrait(f32 x, f32 y, f32 size, u32 slot, i32 px)
{
    const f32 tw = (f32)(RENDER_PORTRAIT_PX * RENDER_PORTRAIT_SLOTS), th = (f32)RENDER_PORTRAIT_PX;
    f32 u0 = (f32)(slot * RENDER_PORTRAIT_PX) / tw, u1 = u0 + (f32)px / tw;
    inset(x - 1, y - 1, size + 2, size + 2);
    ui_rect_grad(x, y, size, size, UI_RGBA(40, 38, 33, 255), UI_RGBA(14, 13, 11, 255));
    ui_image(x, y, size, size, u0, (f32)px / th, u1, 0.0f, UI_RGBA(255, 255, 255, 255));
}

static void draw_item_cell(f32 x, f32 y, f32 w, f32 h, Item it, u32 edge)
{
    static const u32 fill_t[4] = { 0, UI_RGBA(66, 54, 42, 250), UI_RGBA(52, 56, 50, 250), UI_RGBA(60, 52, 36, 250) };
    ui_rect_grad(x, y, w, h, fill_t[it.kind], darker(fill_t[it.kind], 0.6f));
    ui_frame(x, y, w, h, edge);
    // В клетку влезает несколько букв; полное имя во всплывающей подсказке
    char ab[32];
    u32 chars = (u32)((w - 3) / UI_ADVANCE);
    const char *name = item_name(it), *sp = strchr(name, ' ');
    if (sp && chars <= 3) {
        char first[8], second[8];
        utf8_head(first, sizeof first, name, 1);
        utf8_head(second, sizeof second, sp + 1, 1);
        snprintf(ab, sizeof ab, "%s%s", first, second);
    } else {
        utf8_head(ab, sizeof ab, name, chars);
    }
    ui_text(x + 2, y + 2, C_TEXT, ab);
    if (it.kind == IK_GOODS && it.count > 1) {
        char n[8];
        snprintf(n, sizeof n, "%u", it.count);
        num_right(x + w - 2, y + h - 7, C_ACCENT, n);
    }
}

static void tooltip(const Layout *L, Item it)
{
    char lines[4][64];
    u32 n = 0;
    snprintf(lines[n++], 64, "%s", item_name(it));
    if (it.kind == IK_WEAPON) {
        const WeaponDef *d = &g_weapons[it.id];
        snprintf(lines[n++], 64, "%s", g_quality_name[it.quality]);
        snprintf(lines[n++], 64, "РЕЗ %.0f  ДРОБ %.0f  УКОЛ %.0f", (f64)d->cut, (f64)d->blunt, (f64)d->pierce);
        snprintf(lines[n++], 64, "ДЛИНА %.1f М  ВЕС %.0f КГ", (f64)d->length, (f64)d->weight);
    } else if (it.kind == IK_ARMOR) {
        const ArmorDef *d = &g_armors[it.id];
        snprintf(lines[n++], 64, "%s, %s", g_slot_name[d->slot], g_quality_name[it.quality]);
        snprintf(lines[n++], 64, "ЗАЩИТА: РЕЗ %.0f%% ДРОБ %.0f%% УКОЛ %.0f%%", (f64)(d->res_cut * 100.0f),
                 (f64)(d->res_blunt * 100.0f), (f64)(d->res_pierce * 100.0f));
        snprintf(lines[n++], 64, "ВЕС %.0f КГ", (f64)d->weight);
    } else {
        const GoodsDef *d = &g_goods[it.id];
        snprintf(lines[n++], 64, "%s", d->desc);
        if (d->slot != SLOT_NONE) snprintf(lines[n++], 64, "НАДЕВАЕТСЯ: %s", g_slot_name[d->slot]);
        snprintf(lines[n++], 64, "ВЕС %.1f КГ", (f64)item_weight(it));
    }
    f32 w = 0;
    for (u32 k = 0; k < n; k++) w = fmaxf(w, ui_text_width(lines[k]));
    f32 bw = w + 12, bh = (f32)n * UI_LINE + 6;
    f32 x = fminf(L->mx + 12, L->w - bw - 2), y = fminf(L->my + 14, L->h - bh - 2);
    ui_rect(x + 2, y + 2, bw, bh, C_SHADE);
    ui_panel_fill(x, y, bw, bh, UI_RGBA(20, 19, 16, 250), UI_RGBA(10, 10, 9, 250));
    ui_frame(x, y, bw, bh, C_EDGE);
    brackets(x, y, bw, bh, 4, C_ACCENT);
    for (u32 k = 0; k < n; k++) ui_text(x + 6, y + 4 + (f32)k * UI_LINE, k ? C_DIM : C_ACCENT, lines[k]);
}

static void small_label(f32 x, f32 y, const char *s) { ui_text(x, y, C_FAINT, s); }

// ---------------------------------------------------------------- панели

static void draw_selected(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h->look_kind, h->look, w, &d)) return;
    char buf[64];
    f32 x = L->sel_x, y = L->bottom;
    panel(x, y, SEL_W, BOTTOM_H, d.name, d.role);
    portrait(x + 7, y + 19, HUD_PORTRAIT_FOCUS_PX, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX);

    f32 tx = x + 94, ty = y + 20;
    ui_text(tx, ty, C_TEXT, d.state);
    const char *cond = condition(&d);
    ui_text(tx, ty + 10, !strcmp(cond, "ЦЕЛ") ? C_DIM : C_BAD, cond);
    if (d.bleed > 0.01f) ui_text(tx + ui_text_width(cond) + 6, ty + 10, C_BAD, "КРОВЬ ТЕЧЁТ");
    Item wp = d.equip[SLOT_WEAPON];
    snprintf(buf, sizeof buf, "%s", wp.kind ? item_name(wp) : "БЕЗ ОРУЖИЯ");
    text_fit(tx, ty + 22, 124, C_DIM, buf);
    small_label(tx, ty + 34, "КРОВЬ");
    snprintf(buf, sizeof buf, "%.0f/%.0f", (f64)d.blood, (f64)d.blood_max);
    num_right(tx + 124, ty + 35, C_DIM, buf);
    bar(tx, ty + 43, 124, 3, d.blood / d.blood_max, d.blood < 0.6f * d.blood_max ? C_BAD : C_BLOOD);

    if (d.body) {
        for (u32 g = 0; g < GAIT_COUNT; g++) {
            f32 bx, by, bw;
            gait_btn_rect(L, g, &bx, &by, &bw);
            button(bx, by, bw, 13, g_gait_key[g], g_gait_btn[g], d.body->gait == g, inside(L->mx, L->my, bx, by, bw, 13));
        }
        f32 fx, fy;
        formation_btn_rect(L, &fx, &fy);
        button(fx, fy, 70, 13, "G", "СТРОЙ", w->squad->formation, inside(L->mx, L->my, fx, fy, 70, 13));
    } else {
        ui_text(tx, y + 74, C_FAINT, "ЧУЖОЙ. ПРИКАЗАМ");
        ui_text(tx, y + 84, C_FAINT, "НЕ ПОДЧИНЯЕТСЯ.");
    }

    // Т-поза справа
    f32 fx = x + SEL_W - TPOSE_W - 14, fy = y + 22;
    inset(fx - 6, fy - 4, TPOSE_W + 12, TPOSE_H + 8);
    tpose(fx, fy, 1, d.hp, d.fracture, d.severed, false);
    snprintf(buf, sizeof buf, "%.0f%%", (f64)(fmaxf(0.0f, vital_ratio(d.hp)) * 100.0f));
    ui_num(floorf(fx + (TPOSE_W - ui_num_width(buf)) * 0.5f), fy + TPOSE_H + 9, health_color(vital_ratio(d.hp)), buf);
}

static void draw_card(const Hud *h, const HudWorld *w, const Layout *L, u32 i)
{
    const Squad *sq = w->squad;
    const CharSheet *c = &sq->sheet[i];
    const Player *pl = &sq->body[i];
    f32 x = card_x(L, i), y = L->bottom;
    bool hot = inside(L->mx, L->my, x, y, CARD_W, BOTTOM_H);
    bool look = h->look_kind == LOOK_SQUAD && h->look == i;
    panel(x, y, CARD_W, BOTTOM_H, NULL, NULL);
    char n[4];
    snprintf(n, sizeof n, "%u", i + 1);
    ui_num(x + 5, y + 5, look ? C_ACCENT : C_FAINT, n);
    // Режим шага: одно, два или три деления; горят, пока идёт
    for (u32 g = 0; g <= pl->gait; g++)
        ui_rect(x + CARD_W - 7 - (f32)g * 4, y + 5, 3, 5, pl->moving ? C_ACCENT : C_FAINT);
    portrait(x + 4, y + 13, HUD_PORTRAIT_CARD_PX, i, HUD_PORTRAIT_CARD_PX);
    text_fit(x + 4, y + 71, CARD_W - 8, sq->selected[i] ? C_SEL : C_TEXT, c->name);
    text_fit(x + 4, y + 80, CARD_W - 8, C_DIM, pl->moving ? (pl->gait == GAIT_RUN ? "БЕЖИТ" : "ИДЁТ") : "СТОИТ");
    f32 r = vital_ratio(c->d.hp);
    bar(x + 4, y + 91, CARD_W - 8, 3, r, health_color(r));
    bar(x + 4, y + 96, CARD_W - 8, 2, c->d.blood / (100.0f * c->d.mass / 75.0f), C_BLOOD);
    if (sq->selected[i]) brackets(x - 1, y - 1, CARD_W + 2, BOTTOM_H + 2, 8, C_SEL);
    else if (hot) brackets(x - 1, y - 1, CARD_W + 2, BOTTOM_H + 2, 8, C_TEXT);
}

static void draw_buttons(const Hud *h, const Layout *L)
{
    bool can = h->look_kind != LOOK_NONE;
    f32 x = L->btn_x;
    f32 y0 = win_btn_y(L, 0), y1 = win_btn_y(L, 1);
    button(x, y0, BTN_W, BTN_H, "I", "ИНВЕНТАРЬ", h->win_inv, can && inside(L->mx, L->my, x, y0, BTN_W, BTN_H));
    button(x, y1, BTN_W, BTN_H, "P", "ПЕРСОНАЖ", h->win_char, can && inside(L->mx, L->my, x, y1, BTN_W, BTN_H));
}

static void draw_clock(const Hud *h, const Layout *L, const HudWorld *w)
{
    char buf[32];
    f32 x = L->clock_x, y = 8;
    panel(x, y, CLOCK_W, CLOCK_H, NULL, NULL);
    u32 minutes = (u32)(w->tod * 24.0f * 60.0f) % (24 * 60);
    snprintf(buf, sizeof buf, "ДЕНЬ %u", w->day);
    ui_text(x + 7, y + 6, C_DIM, buf);
    snprintf(buf, sizeof buf, "%02u:%02u", minutes / 60, minutes % 60);
    ui_text(x + 7, y + 17, C_ACCENT, buf);
    static const char *labels[4] = { "II", ">", ">>", ">>>" };
    ui_text(x + 62, y + 6, C_FAINT, h->speed ? "ВРЕМЯ" : "ПАУЗА");
    for (u32 k = 0; k < 4; k++) {
        f32 bx = speed_btn_x(L, k);
        bool on = h->speed == k, hot = inside(L->mx, L->my, bx, 24, 19, 11);
        ui_rect_grad(bx, 24, 19, 11, on ? UI_RGBA(84, 62, 30, 255) : UI_RGBA(40, 37, 32, 255), on ? UI_RGBA(52, 38, 18, 255) : UI_RGBA(20, 19, 17, 255));
        ui_frame(bx, 24, 19, 11, hot ? C_TEXT : C_EDGE);
        f32 tw = ui_text_width(labels[k]);
        ui_text(bx + floorf((19 - tw) * 0.5f), 26, on ? C_ACCENT : C_DIM, labels[k]);
    }
}

static void draw_inventory(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h->look_kind, h->look, w, &d)) return;
    char buf[64];
    f32 x = L->iwx, y = L->iwy;
    panel(x, y, INV_WIN_W, INV_WIN_H, "ИНВЕНТАРЬ", d.name);
    close_btn(L, x, y, INV_WIN_W);

    i32 hover_slot = slot_under(L);
    for (u32 k = 0; k <= SLOT_COUNT; k++) {
        u8 sl = g_slot_order[k];
        f32 sx, sy;
        slot_rect(L, k, &sx, &sy);
        bool fits = h->held.kind && item_slot(h->held) == sl;
        u32 edge = fits ? C_SEL : (hover_slot == sl ? C_TEXT : C_EDGE);
        if (d.equip[sl].kind) {
            draw_item_cell(sx, sy, SLOT_W, SLOT_H, d.equip[sl], edge);
            ui_text(sx + 2, sy + SLOT_H - 9, C_DIM, g_slot_name[sl]);
        } else {
            inset(sx, sy, SLOT_W, SLOT_H);
            if (edge != C_EDGE) ui_frame(sx, sy, SLOT_W, SLOT_H, edge);
            ui_text(sx + 3, sy + 7, C_FAINT, g_slot_name[sl]);
        }
    }

    ui_text(L->ix, L->iy - 11, C_ACCENT, "ВЕЩИ");
    ui_rect(L->ix + 28, L->iy - 7, INV_WIN_W - 44, 1, darker(C_BRACKET, 0.7f));
    if (!d.sheet) {
        ui_text(L->ix, L->iy + 4, C_DIM, "ЧУЖОЕ. НЕ ТРОГАТЬ.");
    } else {
        const Inventory *inv = &d.sheet->inv;
        inset(L->ix - 1, L->iy - 1, INV_W * CELL + 1, INV_H * CELL + 1);
        for (i32 gy = 0; gy < INV_H; gy++)
            for (i32 gx = 0; gx < INV_W; gx++) ui_rect(L->ix + gx * CELL, L->iy + gy * CELL, CELL - 1, CELL - 1, C_CELL);
        i32 hx, hy;
        bool in_cell = cell_under(L, &hx, &hy);
        if (h->held.kind && in_cell) {
            // Куда ляжет вещь с курсора
            u8 iw, ih;
            item_size(h->held, &iw, &ih);
            bool ok = inv_fits(inv, h->held, hx, hy);
            ui_rect(L->ix + hx * CELL, L->iy + hy * CELL, iw * CELL - 1, ih * CELL - 1,
                    ok ? UI_RGBA(60, 110, 66, 150) : UI_RGBA(120, 40, 30, 150));
        }
        i32 hover = in_cell ? inv_at(inv, hx, hy) : -1;
        for (u32 i = 0; i < inv->n; i++) {
            u8 iw, ih;
            item_size(inv->item[i], &iw, &ih);
            draw_item_cell(L->ix + inv->x[i] * CELL, L->iy + inv->y[i] * CELL, iw * CELL - 1, ih * CELL - 1,
                           inv->item[i], (i32)i == hover ? C_TEXT : C_EDGE);
        }
        f32 load = sheet_load(d.sheet), cap = sheet_carry_max(d.sheet);
        f32 nx = L->ix + INV_W * CELL + 6, ny = L->iy;
        small_label(nx, ny, "НОША");
        snprintf(buf, sizeof buf, "%.1f/%.0f", (f64)load, (f64)cap);
        ui_num(nx, ny + 11, load > cap ? C_BAD : C_TEXT, buf);
        bar(nx, ny + 19, 54, 3, load / cap, load > cap ? C_BAD : C_ACCENT);
        if (load > cap) ui_text(nx, ny + 26, C_BAD, "ПЕРЕГРУЗ");
        small_label(nx, ny + 88, "ЛКМ ВЗЯТЬ");
        small_label(nx, ny + 98, "ПКМ НАДЕТЬ");
        small_label(nx, ny + 108, "    СНЯТЬ");
    }

    if (!h->held.kind) {
        Item tip = { 0 };
        i32 gx, gy;
        if (hover_slot >= 0) tip = d.equip[hover_slot];
        else if (d.sheet && cell_under(L, &gx, &gy)) {
            i32 at = inv_at(&d.sheet->inv, gx, gy);
            if (at >= 0) tip = d.sheet->inv.item[at];
        }
        if (tip.kind) tooltip(L, tip);
    }
}

static void draw_character(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h->look_kind, h->look, w, &d)) return;
    char buf[64];
    f32 x = L->cwx, y = L->cwy;
    panel(x, y, CHAR_WIN_W, CHAR_WIN_H, "ПЕРСОНАЖ", d.name);
    close_btn(L, x, y, CHAR_WIN_W);

    // Портрет и сведения
    portrait(x + 9, y + 21, HUD_PORTRAIT_FOCUS_PX, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX);
    f32 tx = x + 98, ty = y + 22;
    ui_text(tx, ty, C_TEXT, d.role);
    ui_text(tx, ty + 11, C_DIM, d.state);
    const char *cond = condition(&d);
    ui_text(tx, ty + 22, !strcmp(cond, "ЦЕЛ") ? C_DIM : C_BAD, cond);
    small_label(tx, ty + 36, "РОСТ");
    snprintf(buf, sizeof buf, "%.2f", (f64)d.height);
    ui_num(tx + 34, ty + 37, C_TEXT, buf);
    small_label(tx + 74, ty + 36, "ВЕС");
    snprintf(buf, sizeof buf, "%.0f", (f64)d.mass);
    ui_num(tx + 98, ty + 37, C_TEXT, buf);
    small_label(tx, ty + 50, "КРОВЬ");
    snprintf(buf, sizeof buf, "%.0f/%.0f", (f64)d.blood, (f64)d.blood_max);
    num_right(x + CHAR_WIN_W - 10, ty + 51, C_DIM, buf);
    bar(tx, ty + 59, x + CHAR_WIN_W - 10 - tx, 3, d.blood / d.blood_max, d.blood < 0.6f * d.blood_max ? C_BAD : C_BLOOD);
    if (d.bleed > 0.01f) ui_text(tx, ty + 66, C_BAD, "КРОВЬ ТЕЧЁТ");

    // Тело: крупная Т-поза и список частей
    f32 by = y + 110;
    ui_text(x + 9, by, C_ACCENT, "ТЕЛО");
    ui_rect(x + 37, by + 4, CHAR_WIN_W - 46, 1, darker(C_BRACKET, 0.7f));
    inset(x + 9, by + 12, TPOSE_W * 2 + 20, TPOSE_H * 2 + 10);
    tpose(x + 19, by + 17, 2, d.hp, d.fracture, d.severed, true);
    f32 lx = x + 120, ly = by + 13;
    for (u32 p = 0; p < PART_COUNT; p++, ly += 14) {
        f32 r = d.hp[p] / g_part_hp[p];
        bool cut = d.severed & (1u << p), broken = d.fracture & (1u << p);
        ui_text(lx, ly, cut ? C_BAD : C_TEXT, g_part_name[p]);
        if (cut) text_right(x + CHAR_WIN_W - 10, ly, C_BAD, "ОТСЕЧЕНА");
        else {
            snprintf(buf, sizeof buf, "%.0f/%.0f", (f64)d.hp[p], (f64)g_part_hp[p]);
            num_right(x + CHAR_WIN_W - 10, ly + 1, broken ? C_BAD : C_DIM, buf);
            if (broken) text_right(x + CHAR_WIN_W - 12 - ui_num_width(buf) - 4, ly, C_BAD, "ПЕРЕЛОМ");
        }
        bar(lx, ly + 9, x + CHAR_WIN_W - 10 - lx, 2, r, health_color(r));
    }

    // Навыки в две колонки
    f32 ky = by + 118;
    ui_text(x + 9, ky, C_ACCENT, "НАВЫКИ");
    ui_rect(x + 49, ky + 4, CHAR_WIN_W - 58, 1, darker(C_BRACKET, 0.7f));
    f32 colw = (CHAR_WIN_W - 28) * 0.5f;
    for (u32 s = 0; s < SK_COUNT; s++) {
        f32 cx = x + 9 + (s % 2) * (colw + 10), cy = ky + 13 + (f32)(s / 2) * 14;
        ui_text(cx, cy, s >= SK_WEAPON ? C_DIM : C_TEXT, g_skill_name[s]);
        snprintf(buf, sizeof buf, "%.0f", (f64)d.skill[s]);
        num_right(cx + colw, cy + 1, C_TEXT, buf);
        bar(cx, cy + 9, colw, 2, d.skill[s] / 100.0f, C_EDGE);
    }
}

void hud_draw(const Hud *h, const Input *in, const HudWorld *w)
{
    Layout L = layout(in);
    const Squad *sq = w->squad;

    // Имя над тем, кто под курсором
    u8 kind;
    u32 idx;
    V3 head;
    if (!h->drag_live && L.my < L.bottom && pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
        f32 sx, sy;
        LookData d;
        if (project(w->view_proj, v3_add(head, v3(0, 0.3f, 0)), &sx, &sy) && look_data(kind, idx, w, &d)) {
            f32 tw = ui_text_width(d.name);
            f32 x = floorf(sx - tw * 0.5f), y = floorf(sy - 10);
            ui_rect(x - 4, y - 3, tw + 8, 13, UI_RGBA(10, 10, 9, 200));
            brackets(x - 4, y - 3, tw + 8, 13, 3, kind == LOOK_SQUAD ? C_SEL : C_BRACKET);
            ui_text(x, y, kind == LOOK_SQUAD ? C_SEL : C_TEXT, d.name);
        }
    }

    if (h->drag_live) {
        f32 x0 = fminf(h->drag_x, L.mx), y0 = fminf(h->drag_y, L.my);
        f32 bw = fabsf(L.mx - h->drag_x), bh = fabsf(L.my - h->drag_y);
        ui_rect(x0, y0, bw, bh, UI_RGBA(142, 214, 150, 22));
        ui_frame(x0, y0, bw, bh, UI_RGBA(142, 214, 150, 160));
        brackets(x0, y0, bw, bh, 6, C_SEL);
    }

    draw_clock(h, &L, w);
    if (h->look_kind != LOOK_NONE) draw_selected(h, &L, w);
    for (u32 i = 0; i < sq->count; i++) draw_card(h, w, &L, i);
    draw_buttons(h, &L);
    if (h->win_char) draw_character(h, &L, w);
    if (h->win_inv) draw_inventory(h, &L, w);

    if (h->held.kind) {
        u8 iw, ih;
        item_size(h->held, &iw, &ih);
        draw_item_cell(L.mx - 4, L.my - 4, iw * CELL - 1, ih * CELL - 1, h->held, C_ACCENT);
    }
}
