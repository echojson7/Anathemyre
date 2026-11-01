#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"
#include "../render/render.h"
#include "../render/ui.h"

// Палитра: тёмные полупрозрачные окна без рамок, светло-серые линии и сетка, белый текст.
// Цвет — только у важного: бледно-зелёный — свои выбранные, красный — раны и враги
#define C_WIN     UI_RGBA(9, 9, 9, 196)
#define C_CELL    UI_RGBA(0, 0, 0, 96)
#define C_CELL_HOT UI_RGBA(255, 255, 255, 18)
#define C_GRID    UI_RGBA(112, 112, 108, 120)
#define C_SEP     UI_RGBA(170, 170, 164, 56)
#define C_ITEM    UI_RGBA(62, 62, 60, 236)
#define C_ITEM_HOT UI_RGBA(84, 84, 81, 245)
#define C_ITEM_EDGE UI_RGBA(104, 104, 100, 255)
#define C_TEXT    UI_RGBA(226, 226, 222, 255)
#define C_DIM     UI_RGBA(150, 150, 146, 255)
#define C_FAINT   UI_RGBA(104, 104, 100, 255)
#define C_BAR_BG  UI_RGBA(255, 255, 255, 34)
#define C_BAR     UI_RGBA(214, 214, 210, 255)
#define C_BTN     UI_RGBA(0, 0, 0, 110)
#define C_BTN_HOT UI_RGBA(255, 255, 255, 26)
#define C_BTN_ON  UI_RGBA(255, 255, 255, 44)
#define C_SEL     UI_RGBA(142, 214, 150, 255)
#define C_BAD     UI_RGBA(200, 70, 56, 255)
#define C_BLOOD   UI_RGBA(150, 42, 34, 255)
#define FEATHER   10.0f   // растушёвка края окна
#define MARGIN    16.0f   // фон окна выходит за его прямоугольник: сплошной на 6, дальше растушёвка
#define EDGE      20      // от края экрана до окна

// Раскладка, логические пиксели
#define BOTTOM_H   106
#define SEL_W      400
#define CARD_W     80
#define CARD_GAP   22
#define BTN_W      122
#define BTN_H      18
#define WIN_TOP    (EDGE + CLOCK_H + 28)
#define WIN_HEAD_H 20     // шапка окна
#define CLOCK_W    164
#define CLOCK_H    40
#define DOUBLE_CLICK_S 0.35f
#define SELECT_RING_R 0.48f  // радиус кольца выбора под ногами, как в main.c
#define DBG_W      280  // окно НАСТРОЙКИ
#define DBG_ROW    16   // строка виджета
#define DBG_LABEL  128  // ширина подписи у ползунка и выбора
#define DBG_BTN_W  96

// Окно ИНВЕНТАРЬ: слева снаряжение и сумка на одной сетке клеток, справа состояние и навыки
#define CELL       22   // клетка
#define PAD        18   // между снаряжением и правой колонкой
#define COL_GAP    12   // между колонками слотов
#define ROW_GAP    6
#define LABEL_H    14   // подпись над слотом
#define EQUIP_W    (INV_W * CELL)
#define RCOL_W     160
#define INV_WIN_W  (EQUIP_W + 2 * PAD + 1 + RCOL_W)
#define COL_X(c)   ((c) * (2 * CELL + COL_GAP))
#define ROW0       LABEL_H
#define ROW1       (ROW0 + 2 * CELL + ROW_GAP + LABEL_H)
#define ROW2       (ROW1 + 3 * CELL + ROW_GAP + LABEL_H)
#define EQUIP_H    (ROW2 + 3 * CELL)
#define BAG_HEAD   20   // строка ВЕЩИ с ношей
#define INV_WIN_H  (WIN_HEAD_H + 8 + EQUIP_H + 10 + BAG_HEAD + INV_H * CELL)

// Слоты: где лежат на сетке (x, y в логических от угла снаряжения) и сколько клеток
typedef struct SlotCell {
    u8 slot;
    i16 x, y;
    u8 w, h;
} SlotCell;

static const SlotCell g_slots[SLOT_COUNT + 1] = {
    { SLOT_HEAD, COL_X(0), ROW0, 2, 2 },        { SLOT_FACE, COL_X(1), ROW0, 2, 1 },
    { SLOT_WEAPON, COL_X(2), ROW0, 2, 5 },      { SLOT_HANDS, COL_X(3), ROW0, 2, 3 },
    { SLOT_FEET, COL_X(4), ROW0, 2, 2 },        { SLOT_TORSO_UNDER, COL_X(0), ROW1, 2, 3 },
    { SLOT_TORSO_ARMOR, COL_X(1), ROW1, 2, 3 }, { SLOT_PANTS, COL_X(0), ROW2, 2, 2 },
    { SLOT_LEGS, COL_X(1), ROW2, 2, 3 },        { SLOT_BELT, COL_X(2), ROW2, 2, 1 },
    { SLOT_BACK, EQUIP_W - 4 * CELL, ROW2, 4, 3 },
};

// Схема тела пластинами, руки вдоль тела. Клетки 21x30: x, y, w, h. Лицом к зрителю: правая рука слева
static const u8 g_tpose[PART_COUNT][4] = {
    [PART_HEAD] = { 8, 0, 5, 5 }, [PART_CHEST] = { 6, 6, 9, 7 }, [PART_STOMACH] = { 6, 14, 9, 5 },
    [PART_ARM_R] = { 2, 6, 3, 13 }, [PART_ARM_L] = { 16, 6, 3, 13 },
    [PART_LEG_R] = { 6, 20, 4, 10 }, [PART_LEG_L] = { 11, 20, 4, 10 },
};
#define TPOSE_W 21
#define TPOSE_H 30

static const char *g_gait_btn[GAIT_COUNT] = { "ШАГ", "ТРУСЦА", "БЕГ" };
static const char *g_gait_key[GAIT_COUNT] = { "Z", "X", "C" };

// Всё, что панели и окна показывают о персонаже, откуда бы он ни был
typedef struct LookData {
    const char *name, *role, *state;
    f32 hp[PART_COUNT], blood, blood_max, bleed, height, mass;
    u8  fracture, severed;
    f32 skill[SK_COUNT];
    ItemStack equip[SLOT_COUNT + 1];
    CharSheet *sheet;   // свои: вещи можно перекладывать
    const Motion *body; // свои: режим шага
} LookData;

// Два слоя: нижняя полоса (отряд, выбранный) и верхний — окна, часы, кнопки, подписи в мире
typedef struct Layout {
    f32 w, h, mx, my;     // верхний слой
    f32 bmx, bmy;         // мышь в нижнем слое
    f32 bottom;           // верх нижней полосы, нижний слой
    f32 bottom_top;       // он же в координатах верхнего слоя
    f32 btn_y;
    f32 sel_x;        // панель выбранного
    f32 cards_x;
    f32 btn_x;
    f32 clock_x;
    f32 iwx, iwy;     // окно инвентаря
    f32 sx, sy;       // угол снаряжения
    f32 ix, iy;       // сетка сумки
    f32 rx;           // правая колонка
} Layout;

// Окно не уходит за край
static void win_place(f32 *x, f32 *y, f32 w, f32 h, f32 sw, f32 sh)
{
    *x = floorf(clampf(*x, 0.0f, fmaxf(0.0f, sw - w)));
    *y = floorf(clampf(*y, 0.0f, fmaxf(0.0f, sh - h)));
}

// Оставляет текущим верхний слой: в нём же считается проекция мира для выбора
static Layout layout(const Hud *h, const Input *in)
{
    (void)h;
    Layout L;
    ui_layer(UI_BOTTOM);
    f32 sb = ui_scale();
    L.bmx = floorf((f32)in->mouse_x / sb);
    L.bmy = floorf((f32)in->mouse_y / sb);
    L.bottom = ui_height() - BOTTOM_H - EDGE;
    ui_layer(UI_TOP);
    f32 s = ui_scale();
    L.bottom_top = floorf(L.bottom * sb / s);
    L.w = ui_width();
    L.h = ui_height();
    L.mx = floorf((f32)in->mouse_x / s);
    L.my = floorf((f32)in->mouse_y / s);
    L.sel_x = EDGE;
    L.cards_x = L.sel_x + SEL_W + CARD_GAP + 6;
    L.btn_x = L.w - BTN_W - EDGE;
    L.btn_y = L.h - 3 * BTN_H - 2 * 4 - EDGE;
    L.clock_x = L.w - CLOCK_W - EDGE;
    // Инвентарь прижат к правому краю под часами
    L.iwx = L.w - INV_WIN_W - EDGE;
    L.iwy = WIN_TOP;
    win_place(&L.iwx, &L.iwy, INV_WIN_W, INV_WIN_H, L.w, L.h);
    L.sx = L.iwx;
    L.sy = L.iwy + WIN_HEAD_H + 8;
    L.ix = L.sx;
    L.iy = L.sy + EQUIP_H + 10 + BAG_HEAD;
    L.rx = L.sx + EQUIP_W + PAD;
    return L;
}

static bool inside(f32 mx, f32 my, f32 x, f32 y, f32 w, f32 h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static f32 card_x(const Layout *L, u32 i) { return L->cards_x + (f32)i * (CARD_W + CARD_GAP); }

static void slot_rect(const Layout *L, u32 k, f32 *x, f32 *y, f32 *w, f32 *h)
{
    *x = L->sx + g_slots[k].x;
    *y = L->sy + g_slots[k].y;
    *w = g_slots[k].w * CELL;
    *h = g_slots[k].h * CELL;
}

static i32 slot_under(const Layout *L)
{
    for (u32 k = 0; k <= SLOT_COUNT; k++) {
        f32 x, y, w, h;
        slot_rect(L, k, &x, &y, &w, &h);
        if (inside(L->mx, L->my, x, y, w, h)) return (i32)g_slots[k].slot;
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
#define SEL_TX 92   // колонка текста в панели выбранного
#define SEL_PORTRAIT 72
#define CARD_PORTRAIT 48
#define SEL_BTN_Y 84
// Ширина кнопки по надписи; считается в слое нижней полосы
static f32 btn_width(const char *key, const char *label)
{
    u32 was = ui_current_layer();
    ui_layer(UI_BOTTOM);
    f32 w = ui_text_width(key) + ui_text_width(label) + 18;
    ui_layer(was);
    return ceilf(w);
}
// i < GAIT_COUNT — режимы шага, GAIT_COUNT — строй; в ряд слева направо
static void sel_btn_rect(const Layout *L, u32 i, f32 *x, f32 *y, f32 *w)
{
    f32 bx = L->sel_x + SEL_TX;
    for (u32 k = 0; k <= i; k++) {
        f32 bw = k < GAIT_COUNT ? btn_width(g_gait_key[k], g_gait_btn[k]) : btn_width("G", "СТРОЙ");
        if (k == i) *w = bw;
        else bx += bw + 4;
    }
    *x = bx;
    *y = L->bottom + SEL_BTN_Y;
}
static f32 win_btn_y(const Layout *L, u32 k) { return L->btn_y + (f32)k * (BTN_H + 4); }
const u8 g_speed_scale[4] = { 0, 1, 2, 5 };

#define SPEED_W 24
static f32 speed_btn_x(const Layout *L, u32 k) { return L->clock_x + CLOCK_W - 2 - (f32)(4 - k) * (SPEED_W + 2); }
#define SPEED_Y (EDGE + 18)

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

i32 hud_arena_fighter(const ArenaRing *v, const ArenaSession *s, u32 roster)
{
    if (!v || !v->in_bout) return -1;
    for (u32 i = 0; i < s->n && i < s->w.count; i++)
        if (s->idx[i] == roster) return (i32)i;
    return -1;
}

bool hud_arena_pos(const ArenaRing *v, const ArenaSession *s, u32 roster, V3 *out)
{
    i32 i = hud_arena_fighter(v, s, roster);
    if (i < 0) return false;
    *out = v3(v->center.x + v->fa[i].disp_x, v->floor_y, v->center.z + v->fa[i].disp_z);
    return true;
}

// Кто под курсором: свои, бойцы на ринге, чужие на поле
static bool pick_at(const HudWorld *w, f32 mx, f32 my, u8 *kind, u32 *index, V3 *head)
{
    f32 best = 1e9f;
    const Field *fd = w->field;
    u32 total = w->squad->count + ARENA_MAX_BOUT + fd->w.count;
    for (u32 k = 0; k < total; k++) {
        V3 pos;
        f32 tall = 1.75f;
        u8 kd = LOOK_SQUAD;
        u32 idx = k;
        if (k < w->squad->count) {
            u32 s = squad_at(w->squad, k);
            pos = w->view[s].pos;
            if (field_awake(fd, s) && fs_lying(fd->w.state[s])) tall = 0.4f;
        } else if (k >= w->squad->count + ARENA_MAX_BOUT) {
            u32 s = k - w->squad->count - ARENA_MAX_BOUT;
            if (fd->flags[s] & (FF_PLAYER | FF_ABSENT)) continue;
            kd = LOOK_FIELD;
            idx = s;
            pos = w->view[s].pos;  // слот поля = место в реестре
            if (field_awake(fd, s) && fs_lying(fd->w.state[s])) tall = 0.4f;
        } else {
            u32 i = k - w->squad->count;
            if (!w->arena || !w->ring->in_bout || i >= w->arena->n) continue;
            kd = LOOK_ARENA;
            idx = w->arena->idx[i];
            hud_arena_pos(w->ring, w->arena, idx, &pos);
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

// Режим передвижения видно и когда стоит: «БЕГ» — побежит по приказу.
// В бою — состояние боя, в погоне — к цели
static const char *move_state(const Field *f, u32 i)
{
    static const char *go[GAIT_COUNT] = { "ИДЁТ ШАГОМ", "ТРУСИТ", "БЕЖИТ" };
    const Motion *p = &f->ps->move[i];
    if (field_awake(f, i)) return g_state_name[f->w.state[i]];
    if (p->ctrl == CTRL_CHASE) return "К ЦЕЛИ";
    if (p->rest != REST_NONE && p->rest < REST_COUNT) {
        // Руки на груди и взгляд вверх — без «садится» и «встаёт»
        if (p->rest_ph == RP_HOLD || p->rest >= REST_ARMS) return g_rest[p->rest].name;
        if (p->rest_ph != RP_ENTER) return "ВСТАЁТ";
        return p->rest == REST_KNEEL ? "ОПУСКАЕТСЯ НА КОЛЕНО" : "САДИТСЯ";
    }
    return p->moving ? go[p->gait] : "СТОИТ";
}

// Враждует ли чужой слот с отрядом
static bool foe_of_squad(const HudWorld *w, u32 slot)
{
    return w->squad->count && field_hostile(w->field, squad_at(w->squad, 0), slot);
}

// Тело из реестра: раны, кровь, сложение, навыки
static void look_person(LookData *o, const Persons *ps, u32 i)
{
    for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = ps->hp[p][i];
    o->blood = ps->blood[i];
    o->blood_max = 100.0f * ps->mass[i] / 75.0f;
    o->height = ps->stature[i];
    o->mass = ps->mass[i];
    o->fracture = ps->fracture[i];
    o->severed = ps->severed[i];
    for (u32 k = 0; k < SK_COUNT; k++) o->skill[k] = ps->skill[k][i];
}

static bool look_data(u8 kind, u32 look, const HudWorld *w, LookData *o)
{
    memset(o, 0, sizeof *o);
    if (kind == LOOK_SQUAD) {
        if (look >= w->squad->count) return false;
        CharSheet *c = squad_sheet(w->squad, look);
        o->name = c->name;
        o->role = "ОТРЯД";
        u32 at = squad_at(w->squad, look);
        o->body = squad_move(w->squad, look);
        o->state = move_state(w->field, at);
        if (field_awake(w->field, at)) o->bleed = w->field->w.bleed[at];
        look_person(o, w->field->ps, at);
        memcpy(o->equip, c->equip, sizeof o->equip);
        o->sheet = c;
        return true;
    }
    if (kind == LOOK_FIELD) {
        // Чужой на поле: лист читается, но не меняется
        const Field *fd = w->field;
        if (look >= fd->w.count) return false;
        const CharSheet *c = &fd->ps->sheet[look];
        o->name = c->name;
        o->role = foe_of_squad(w, look) ? "ВРАГ" : c->role ? c->role : "ПРОХОЖИЙ";
        o->state = move_state(fd, look);
        if (field_awake(fd, look)) o->bleed = fd->w.bleed[look];
        look_person(o, fd->ps, look);
        memcpy(o->equip, c->equip, sizeof o->equip);
        return true;
    }
    if (kind != LOOK_ARENA || look >= ARENA_ROSTER || !w->arena) return false;
    const ArenaSession *s = w->arena;
    const ArenaFighter *f = &s->roster[look];
    i32 i = hud_arena_fighter(w->ring, s, look);
    o->name = arena_roster_name(s, look);
    o->role = "БОЕЦ АРЕНЫ";
    o->height = f->d.height;
    o->mass = f->d.mass;
    o->blood_max = 100.0f * f->d.mass / 75.0f;
    memcpy(o->skill, f->d.skill, sizeof o->skill);
    if (i >= 0) {
        // На ринге: живое состояние из боя
        const CombatWorld *cw = &s->w;
        o->state = g_state_name[cw->state[i]];
        for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = cw->hp[p][i];
        o->blood = cw->blood[i];
        o->bleed = cw->bleed[i];
        o->fracture = cw->fracture[i];
        o->severed = cw->severed[i];
        if (!cw->disarmed[i]) o->equip[SLOT_WEAPON] = item_of_weapon(cw->weapon[i], cw->weapon_q[i]);
        for (u32 sl = 0; sl < SLOT_COUNT; sl++)
            if (cw->armor[sl][i] != ARMOR_NONE) o->equip[sl] = item_of_armor(cw->armor[sl][i], Q_WORN);
        return true;
    }
    // Между боями: из ростера; нули в hp и крови значат «здоров»
    o->state = f->gone ? "ВЫБЫЛ" : "ОТДЫХАЕТ";
    bool fresh = f->d.blood <= 0.0f;
    for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = fresh ? g_part_hp[p] : f->d.hp[p];
    o->blood = fresh ? o->blood_max : f->d.blood;
    o->fracture = f->d.fracture;
    o->severed = f->d.severed;
    o->equip[SLOT_WEAPON] = item_of_weapon(f->d.weapon, f->d.weapon_q);
    for (u32 sl = 0; sl < SLOT_COUNT; sl++)
        if (f->d.armor[sl] != ARMOR_NONE) o->equip[sl] = item_of_armor(f->d.armor[sl], Q_WORN);
    return true;
}

void hud_init(Hud *h)
{
    memset(h, 0, sizeof *h);
    h->last_click_card = -1;
    h->focus = -1;
    h->speed = h->speed_before = 1;
    h->dbg.f = (HudForm){ .x = EDGE, .y = EDGE + 36, .w = DBG_W, .slider = -1 };
    h->dbg.journal = true;
    h->dbg.fps_on = true;
}

void hud_look(Hud *h, u8 kind, u32 index)
{
    h->look_kind = kind;
    h->look = index;
}

bool hud_window_open(const Hud *h) { return h->win_inv || h->win_char; }

bool hud_pick(const Hud *h, const Input *in, const HudWorld *w, u8 *kind, u32 *index)
{
    Layout L = layout(h, in);
    V3 head;
    return L.my < L.bottom_top && pick_at(w, L.mx, L.my, kind, index, &head);
}

// Вещь на земле под курсором: ближайшая к курсору на экране в пределах ITEM_PICK_PX
#define ITEM_PICK_PX 12.0f
static i32 ground_item_under(const HudWorld *w, f32 mx, f32 my)
{
    if (!w->ground || !w->phys) return -1;
    i32 best = -1;
    f32 bd = ITEM_PICK_PX * ITEM_PICK_PX;
    for (u32 k = 0; k < w->ground->count; k++) {
        if (!w->ground->item[k].def) continue;
        u32 b = (u32)w->ground->body[k];
        f32 sx, sy;
        if (!project(w->view_proj, v3_lerp(w->phys->x_tick[b], w->phys->x[b], w->alpha), &sx, &sy)) continue;
        f32 d = (sx - mx) * (sx - mx) + (sy - my) * (sy - my);
        if (d < bd) {
            bd = d;
            best = (i32)k;
        }
    }
    return best;
}

i32 hud_pick_item(const Hud *h, const Input *in, const HudWorld *w)
{
    Layout L = layout(h, in);
    return L.my < L.bottom_top ? ground_item_under(w, L.mx, L.my) : -1;
}

// Вещь с курсора отпущена: она и не уходила со своего места
static void drop_held(Hud *h) { h->held = (ItemStack){ 0 }; }

// Откуда взята вещь на курсоре; на месте уже не она (её сняли в шаге мира) — IP_NONE
static ItemPlace held_from(const Hud *h, CharSheet *c, u32 who)
{
    ItemPlace from = h->held_slot == ASK_BAG ? place_bag(c, who, inv_at(&c->inv, h->held_x, h->held_y)) : place_body(c, who, h->held_slot);
    bool ok = from.kind == IP_BAG ? from.at >= 0 && c->inv.x[from.at] == h->held_x && c->inv.y[from.at] == h->held_y
                                  : from.slot < SLOT_COUNT;
    if (!ok || !item_same(item_at(NULL, &from), h->held)) from.kind = IP_NONE;
    return from;
}

static void hold(Hud *h, ItemStack it, u8 slot, u8 x, u8 y)
{
    h->held = it;
    h->held_slot = slot;
    h->held_x = x;
    h->held_y = y;
}

// Надеть в слот с заменой: прежнее — в мешок (первое свободное место); не вышло — всё как было
static bool equip_swap(CharSheet *c, u32 who, const ItemPlace *from, u8 slot)
{
    ItemPlace body = place_body(c, who, slot), bag = place_bag(c, who, -1);
    if (from->kind == IP_BODY && from->slot == slot) return false;
    if (!c->equip[slot].def) return item_move(NULL, NULL, from, &body) >= 0;
    ItemStack it = item_at(NULL, from);
    if (item_slot(it) != slot) return false;
    // Сначала освободить слот; вещь из мешка ищем заново — номера в мешке после переезда сдвигаются
    u8 fx = from->kind == IP_BAG ? c->inv.x[from->at] : 0, fy = from->kind == IP_BAG ? c->inv.y[from->at] : 0;
    if (item_move(NULL, NULL, &body, &bag) < 0) return false;
    ItemPlace src = *from;
    if (src.kind == IP_BAG) src.at = inv_at(&c->inv, fx, fy);
    // Слот свободен и вещь ему подходит: не выйти может только порченый лист — тогда обе вещи в мешке
    return item_move(NULL, NULL, &src, &body) >= 0;
}

// Персонаж и инвентарь открываются и закрываются вместе; закрыли — вещь с курсора остаётся на своём месте
static void windows_set(Hud *h, bool open)
{
    if (!open) drop_held(h);
    h->win_inv = h->win_char = open;
}

static void look_at(Hud *h, u8 kind, u32 index)
{
    if (h->look_kind == kind && h->look == index) return;
    drop_held(h);
    hud_look(h, kind, index);
}

// Перекладывание вещей в окне своего персонажа: взять — только запомнить откуда, отпустить — item_move.
// who — номер человека в реестре (для журнала вещей)
static void inventory_input(Hud *h, const Layout *L, CharSheet *c, u32 who, bool lmb, bool rmb)
{
    i32 gx, gy, slot = slot_under(L);
    bool in_cell = cell_under(L, &gx, &gy);
    if (lmb && !h->held.def) {
        i32 at = in_cell ? inv_at(&c->inv, gx, gy) : -1;
        if (at >= 0) hold(h, c->inv.item[at], ASK_BAG, c->inv.x[at], c->inv.y[at]);
        else if (slot >= 0 && c->equip[slot].def) hold(h, c->equip[slot], (u8)slot, 0, 0);
    } else if (lmb) {
        ItemPlace from = held_from(h, c, who);
        if (from.kind == IP_NONE) {
            h->held = (ItemStack){ 0 };
        } else if (in_cell) {
            ItemPlace to = place_bag(c, who, -1);
            to.x = (u8)gx, to.y = (u8)gy;
            if (item_move(NULL, NULL, &from, &to) >= 0) h->held = (ItemStack){ 0 };
        } else if (slot >= 0 && (equip_swap(c, who, &from, (u8)slot) || (from.kind == IP_BODY && from.slot == slot))) {
            h->held = (ItemStack){ 0 };
        }
    } else if (rmb && !h->held.def) {
        // ПКМ: надеть из мешка (прежнее — в мешок) или снять в мешок
        i32 at = in_cell ? inv_at(&c->inv, gx, gy) : -1;
        if (at >= 0 && item_slot(c->inv.item[at]) != SLOT_NONE) {
            ItemPlace from = place_bag(c, who, at);
            equip_swap(c, who, &from, item_slot(c->inv.item[at]));
        } else if (slot >= 0 && c->equip[slot].def) {
            ItemPlace from = place_body(c, who, (u8)slot), to = place_bag(c, who, -1);
            item_move(NULL, NULL, &from, &to);
        }
    }
}

static void select_only(Squad *sq, u32 i)
{
    for (u32 k = 0; k < sq->count; k++) sq->selected[k] = k == i;
}

static bool close_hot(const Layout *L, f32 x, f32 y, f32 w) { return inside(L->mx, L->my, x + w - 16, y + 2, 16, 16); }

bool hud_input(Hud *h, const Input *in, HudWorld *w)
{
    Squad *sq = w->squad;
    Layout L = layout(h, in);
    bool lmb = in->mouse_pressed[MOUSE_LEFT], rmb = in->mouse_pressed[MOUSE_RIGHT];
    bool lmb_up = h->lmb_was && !in->mouse_down[MOUSE_LEFT];
    bool shift = in->key_down[KEY_SHIFT];
    bool over = false;
    h->lmb_was = in->mouse_down[MOUSE_LEFT];
    h->focus = -1;

    // Панель показывает первого выбранного, пока не щёлкнули по другому
    if (h->look_kind == LOOK_SQUAD && (h->look >= sq->count || !sq->selected[h->look]) && !h->held.def) {
        i32 p = squad_primary(sq);
        if (p >= 0) hud_look(h, LOOK_SQUAD, (u32)p);
        else h->look_kind = LOOK_NONE;
    }
    if (h->look_kind == LOOK_NONE) {
        i32 p = squad_primary(sq);
        if (p >= 0) hud_look(h, LOOK_SQUAD, (u32)p);
    }

    bool has_look = h->look_kind != LOOK_NONE;
    if ((in->key_pressed[KEY_I] || in->key_pressed[KEY_P]) && (hud_window_open(h) || has_look)) windows_set(h, !hud_window_open(h));
    if (in->key_pressed[KEY_K] && h->can_look && (h->win_look || has_look)) h->win_look = !h->win_look;
    if (in->key_pressed[KEY_ESCAPE]) {
        windows_set(h, false);
        h->win_look = false;
    }
    if (in->key_pressed[KEY_SPACE]) {
        if (h->speed) {
            h->speed_before = h->speed;
            h->speed = 0;
        } else {
            h->speed = h->speed_before;
        }
    }

    // Окно ИНВЕНТАРЬ (оно же персонаж): закрыть крестом, перекладывать вещи своих
    if (h->win_inv && inside(L.mx, L.my, L.iwx, L.iwy, INV_WIN_W, INV_WIN_H)) {
        over = true;
        if (lmb && close_hot(&L, L.iwx, L.iwy, INV_WIN_W)) {
            windows_set(h, false);
        } else if (h->look_kind == LOOK_SQUAD && h->look < sq->count) {
            inventory_input(h, &L, squad_sheet(sq, h->look), squad_at(sq, h->look), lmb, rmb);
        }
        lmb = rmb = false;
    }

    // Часы: скорость времени
    if (inside(L.mx, L.my, L.clock_x, 8, CLOCK_W, CLOCK_H)) {
        over = true;
        for (u32 k = 0; k < 4 && lmb; k++)
            if (inside(L.mx, L.my, speed_btn_x(&L, k), SPEED_Y, SPEED_W, 14)) {
                if (k == 0 && h->speed) h->speed_before = h->speed;
                h->speed = (u8)k;
                if (k) h->speed_before = (u8)k;
            }
        lmb = false;
    }

    // Панель выбранного: режим шага и строй
    if (has_look && inside(L.bmx, L.bmy, L.sel_x, L.bottom, SEL_W, BOTTOM_H)) {
        over = true;
        if (lmb && h->look_kind == LOOK_SQUAD) {
            for (u32 g = 0; g <= GAIT_COUNT; g++) {
                f32 x, y, bw;
                sel_btn_rect(&L, g, &x, &y, &bw);
                if (!inside(L.bmx, L.bmy, x, y, bw, 16)) continue;
                if (g < GAIT_COUNT) squad_set_gait(sq, (Gait)g);
                else sq->formation = !sq->formation;
            }
        }
        lmb = false;
    }

    // Кнопки окон
    if (inside(L.mx, L.my, L.btn_x, L.btn_y, BTN_W, 3 * BTN_H + 2 * 4)) {
        over = true;
        if (lmb && has_look) {
            if (L.my < win_btn_y(&L, 2)) windows_set(h, !hud_window_open(h));
            else if (h->can_look) h->win_look = !h->win_look;
        }
        lmb = false;
    }

    // Отряд: клик — выбрать, с Shift — добавить, двойной — камера к нему
    for (u32 i = 0; i < sq->count; i++) {
        if (!inside(L.bmx, L.bmy, card_x(&L, i), L.bottom, CARD_W, BOTTOM_H)) continue;
        over = true;
        if (!lmb) break;
        if (h->last_click_card == (i32)i && w->time - h->last_click_t < DOUBLE_CLICK_S) h->focus = (i32)i;
        h->last_click_card = (i32)i;
        h->last_click_t = w->time;
        if (shift) sq->selected[i] = !sq->selected[i];
        else select_only(sq, i);
        if (sq->selected[i]) look_at(h, LOOK_SQUAD, i);
        lmb = false;
        break;
    }

    // Вещь с курсора — в мир: бросает тот, чей мешок открыт, себе под ноги (упадёт на следующем шаге мира)
    GroundAsks *ask = w->ground_ask;
    if (lmb && !over && h->held.def && h->look_kind == LOOK_SQUAD && h->look < sq->count && ask && ask->n < GROUND_ASKS &&
        L.my < L.bottom_top) {
        const Player *b = &w->view[squad_at(sq, h->look)];
        f32 hs = sinf(b->heading), hc = cosf(b->heading);
        ask->it[ask->n] = h->held;
        ask->slot[ask->n] = h->held_slot;
        ask->x[ask->n] = h->held_x;
        ask->y[ask->n] = h->held_y;
        ask->at[ask->n] = v3_add(b->pos, v3(hs * 0.35f, 1.1f, hc * 0.35f));
        ask->yaw[ask->n] = b->heading;
        ask->who[ask->n] = (u8)h->look;
        ask->n++;
        h->held = (ItemStack){ 0 };
        lmb = false;
    }

    // Мир: щелчок выбирает, протяжка тянет рамку
    if (lmb && !over && !h->held.def) {
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
                    V3 p = v3_add(w->view[squad_at(sq, i)].pos, v3(sinf(a) * r, 0.03f, cosf(a) * r));
                    in_box = project(w->view_proj, p, &px, &py) && px >= x0 && px <= x1 && py >= y0 && py <= y1;
                }
                if (in_box) sq->selected[i] = true;
                else if (!shift) sq->selected[i] = false;
            }
            i32 p = squad_primary(sq);
            if (p >= 0 && (h->look_kind != LOOK_SQUAD || !sq->selected[h->look])) look_at(h, LOOK_SQUAD, (u32)p);
        } else {
            u8 kind;
            u32 idx;
            V3 head;
            if (!pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
                if (!shift) {
                    memset(sq->selected, 0, sizeof sq->selected);
                    drop_held(h);
                    h->look_kind = LOOK_NONE;
                    h->win_inv = h->win_char = false;
                }
            } else if (kind == LOOK_SQUAD) {
                if (shift) sq->selected[idx] = !sq->selected[idx];
                else select_only(sq, idx);
                if (sq->selected[idx]) look_at(h, LOOK_SQUAD, idx);
            } else {
                // Чужого можно только осмотреть
                look_at(h, kind, idx);
            }
        }
    }
    return over || h->held.def != ITEM_NONE || h->dragging;
}

// ---------------------------------------------------------------- оформление

static u32 health_color(f32 r)
{
    // Светло-серый → охра → кровь
    f32 a[3] = { 206, 206, 200 }, b[3] = { 196, 150, 76 }, c[3] = { 176, 46, 34 };
    f32 o[3];
    if (r < 0.0f) return UI_RGBA(70, 16, 12, 255);
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
static void text_center(f32 x, f32 w, f32 y, u32 color, const char *s) { ui_text(floorf(x + (w - ui_text_width(s)) * 0.5f), y, color, s); }

// Значок раздела: маленький ромб из квадратов
static void mark(f32 x, f32 y, u32 c)
{
    f32 cy = y + floorf(ui_line() * 0.5f);
    ui_rect(x + 2, cy - 2, 1, 5, c);
    ui_rect(x + 1, cy - 1, 3, 3, c);
    ui_rect(x, cy, 5, 1, c);
}

// Заголовок раздела: «◆ НАВЫКИ»; возвращает y под ним
static f32 section(f32 x, f32 y, const char *title)
{
    mark(x, y, C_TEXT);
    ui_text(x + 9, y, C_TEXT, title);
    return y + ui_line() + 2;
}

// Строка «подпись … значение»
static void row(f32 x, f32 y, f32 w, const char *label, u32 lc, const char *value, u32 vc)
{
    ui_text(x, y, lc, label);
    text_right(x + w, y, vc, value);
}

// Окно в духе DD2: тёмный полупрозрачный фон без рамки, края растушёваны; шапка с линией под ней
static void panel(f32 x, f32 y, f32 w, f32 h, const char *title, const char *right)
{
    ui_soft(x - MARGIN, y - MARGIN, w + 2 * MARGIN, h + 2 * MARGIN, C_WIN, FEATHER);
    if (!title) return;
    f32 ty = y + floorf((WIN_HEAD_H - ui_line()) * 0.5f);
    mark(x, ty, C_TEXT);
    f32 tx = ui_text(x + 9, ty, C_TEXT, title);
    if (right) ui_text(tx + 8, ty, C_DIM, right);
    ui_hline(x, y + WIN_HEAD_H, w, C_SEP);
}

static void close_btn(const Layout *L, f32 x, f32 y, f32 w)
{
    bool hot = close_hot(L, x, y, w);
    if (hot) ui_rect(x + w - 16, y + 2, 16, 16, C_BTN_HOT);
    text_center(x + w - 16, 16, y + floorf((WIN_HEAD_H - ui_line()) * 0.5f), hot ? C_TEXT : C_DIM, "×");
}

// Кнопка без рамки: тёмная подложка, под курсором светлее, включённая — светлая с линией снизу
static void button(f32 x, f32 y, f32 w, f32 h, const char *key, const char *label, bool active, bool hot)
{
    ui_rect(x, y, w, h, active ? C_BTN_ON : (hot ? C_BTN_HOT : C_BTN));
    if (active) ui_hline(x, y + h - ui_hair(), w, C_TEXT);
    f32 ty = y + floorf((h - ui_line()) * 0.5f), tx = x + 6;
    if (key) tx = ui_text(tx, ty, C_FAINT, key) + 6;
    ui_text(tx, ty, active || hot ? C_TEXT : C_DIM, label);
}

static void bar(f32 x, f32 y, f32 w, f32 h, f32 r, u32 color)
{
    ui_rect(x, y, w, h, C_BAR_BG);
    ui_rect(x, y, floorf(w * clampf(r, 0.0f, 1.0f)), h, color);
}

// Схема тела: пластины по ранам, в каждой — оставшиеся ХП мелкими цифрами
static void tpose(f32 x, f32 y, f32 unit, const f32 *hp, u8 fracture, u8 severed, bool numbers)
{
    char buf[8];
    for (u32 p = 0; p < PART_COUNT; p++) {
        const u8 *r = g_tpose[p];
        f32 px = x + r[0] * unit, py = y + r[1] * unit, pw = r[2] * unit, ph = r[3] * unit;
        if (severed & (1u << p)) {
            ui_frame(px, py, pw, ph, C_BAD);
            continue;
        }
        f32 ratio = hp[p] / g_part_hp[p];
        u32 c = health_color(ratio);
        ui_rect(px, py, pw, ph, darker(c, 0.85f));
        // Перелом: косая штриховка
        if (fracture & (1u << p))
            for (f32 k = 2; k < pw + ph; k += 3)
                for (f32 t = 0; t < ph; t++) {
                    f32 xx = k - t;
                    if (xx >= 1 && xx < pw - 1 && t >= 1 && t < ph - 1 && (u32)t % 2 == 0)
                        ui_rect(px + xx, py + t, 1, 1, UI_RGBA(30, 6, 4, 200));
                }
        if (numbers) {
            ui_font(UI_FONT_SMALL);
            snprintf(buf, sizeof buf, "%d", (int)ceilf(hp[p]));
            f32 nw = ui_text_width(buf);
            if (nw <= pw - 2 && ph >= ui_cap() + 2)
                ui_text(floorf(px + (pw - nw) * 0.5f), floorf(py + (ph - ui_line()) * 0.5f), UI_RGBA(14, 14, 13, 255), buf);
            ui_font(UI_FONT_TEXT);
        }
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

// Текст, обрезанный по ширине
static void text_fit(f32 x, f32 y, f32 w, u32 color, const char *s)
{
    char buf[96];
    u32 n = MIN(ui_text_fit(s, w), (u32)sizeof buf - 1);
    memcpy(buf, s, n);
    buf[n] = 0;
    ui_text(x, y, color, buf);
}

static void portrait(f32 x, f32 y, f32 size, u32 slot, i32 px)
{
    const f32 tw = (f32)(RENDER_PORTRAIT_PX * RENDER_PORTRAIT_SLOTS), th = (f32)RENDER_PORTRAIT_PX;
    f32 u0 = (f32)(slot * RENDER_PORTRAIT_PX) / tw, u1 = u0 + (f32)px / tw;
    ui_rect(x, y, size, size, UI_RGBA(0, 0, 0, 120));
    ui_image(x, y, size, size, u0, (f32)px / th, u1, 0.0f, UI_RGBA(255, 255, 255, 255));
}

// Первая буква слова UTF-8: длина в байтах
static u32 utf8_first(const char *s)
{
    u32 n = 1;
    while (s[n] && ((u8)s[n] & 0xC0) == 0x80) n++;
    return n;
}

// Короткое имя вещи под ширину клетки: целиком, иначе последнее слово (существительное),
// иначе оно же с точкой, иначе первые буквы слов
static void short_name(char *out, size_t size, const char *name, f32 w)
{
    size_t len = strlen(name);
    if (ui_text_fit(name, w) >= len) {
        snprintf(out, size, "%s", name);
        return;
    }
    const char *last = strrchr(name, ' ');
    const char *word = last ? last + 1 : name;
    if (ui_text_fit(word, w) >= strlen(word)) {
        snprintf(out, size, "%s", word);
        return;
    }
    u32 n = ui_text_fit(word, w - ui_text_width("."));
    // Хотя бы три буквы, иначе сокращение не читается
    u32 letters = 0;
    for (u32 i = 0; i < n; i++) letters += ((u8)word[i] & 0xC0) != 0x80;
    if (letters >= 3) {
        snprintf(out, size, "%.*s.", (int)n, word);
        return;
    }
    size_t o = 0;
    if (last) {
        // Первые буквы слов: РАБОЧИЕ БОТИНКИ — РБ
        for (const char *p = name; *p && o + 4 < size;) {
            u32 k = utf8_first(p);
            memcpy(out + o, p, k);
            o += k;
            p = strchr(p, ' ');
            if (!p) break;
            p++;
        }
    } else {
        u32 k = utf8_first(word);
        k += word[k] ? utf8_first(word + k) : 0;
        memcpy(out, word, MIN((size_t)k, size - 1));
        o = MIN((size_t)k, size - 1);
    }
    out[o] = 0;
}

// Вещь: серая плитка со светлой кромкой, короткое имя сверху слева, число снизу справа
static void draw_item_cell(f32 x, f32 y, f32 w, f32 h, ItemStack it, bool hot)
{
    const f32 m = 2;  // отступ от линий сетки
    ui_rect(x + m, y + m, w - 2 * m, h - 2 * m, hot ? C_ITEM_HOT : C_ITEM);
    ui_frame(x + m, y + m, w - 2 * m, h - 2 * m, hot ? C_TEXT : C_ITEM_EDGE);
    if (!item_live(it)) return;
    char ab[48];
    short_name(ab, sizeof ab, item_name(it), w - 2 * m - 4);
    ui_text(x + m + 2, y + m, C_TEXT, ab);
    if (it.count > 1) {
        char n[8];
        snprintf(n, sizeof n, "%u", it.count);
        ui_font(UI_FONT_SMALL);
        text_right(x + w - m - 3, y + h - m - ui_line(), C_DIM, n);
        ui_font(UI_FONT_TEXT);
    }
}

// Сетка клеток: темнее окна, светло-серые линии
static void grid(f32 x, f32 y, u32 cw, u32 ch, bool hot)
{
    ui_rect(x, y, cw * CELL, ch * CELL, hot ? C_CELL_HOT : C_CELL);
    for (u32 i = 0; i <= cw; i++) ui_vline(x + i * CELL - (i == cw ? ui_hair() : 0), y, ch * CELL, C_GRID);
    for (u32 j = 0; j <= ch; j++) ui_hline(x, y + j * CELL - (j == ch ? ui_hair() : 0), cw * CELL, C_GRID);
}

// Подсказка вещи: имя, линия, строки «подпись … значение»
static void tooltip(const Layout *L, ItemStack it)
{
    char lab[6][32], val[6][48];
    u32 n = 0;
    const ItemDef *id = item_def(it);
    const char *kind = "";
    if (id->weapon != COMP_NONE) {
        const WeaponDef *d = &g_weapons[id->weapon];
        kind = "ОРУЖИЕ";
        snprintf(lab[n], 32, "КАЧЕСТВО"), snprintf(val[n++], 48, "%s", g_quality_name[it.quality]);
        snprintf(lab[n], 32, "РЕЗ / ДРОБ / УКОЛ"), snprintf(val[n++], 48, "%.0f / %.0f / %.0f", (f64)d->cut, (f64)d->blunt, (f64)d->pierce);
        snprintf(lab[n], 32, "ДЛИНА"), snprintf(val[n++], 48, "%.1f М", (f64)d->length);
        snprintf(lab[n], 32, "ВЕС"), snprintf(val[n++], 48, "%.1f КГ", (f64)d->weight);
    } else if (id->armor != COMP_NONE) {
        const ArmorDef *d = &g_armors[id->armor];
        kind = g_slot_name[d->slot];
        snprintf(lab[n], 32, "КАЧЕСТВО"), snprintf(val[n++], 48, "%s", g_quality_name[it.quality]);
        snprintf(lab[n], 32, "ЗАЩИТА ОТ РЕЗА"), snprintf(val[n++], 48, "%.0f%%", (f64)(d->res_cut * 100.0f));
        snprintf(lab[n], 32, "ОТ УДАРА"), snprintf(val[n++], 48, "%.0f%%", (f64)(d->res_blunt * 100.0f));
        snprintf(lab[n], 32, "ОТ УКОЛА"), snprintf(val[n++], 48, "%.0f%%", (f64)(d->res_pierce * 100.0f));
        snprintf(lab[n], 32, "ВЕС"), snprintf(val[n++], 48, "%.1f КГ", (f64)d->weight);
    } else {
        kind = id->desc;
        if (id->slot != SLOT_NONE) snprintf(lab[n], 32, "НАДЕВАЕТСЯ"), snprintf(val[n++], 48, "%s", g_slot_name[id->slot]);
        snprintf(lab[n], 32, "ВЕС"), snprintf(val[n++], 48, "%.1f КГ", (f64)item_weight(it));
    }
    const f32 lh = ui_line(), gap = 16;
    f32 w = fmaxf(ui_text_width(item_name(it)), ui_text_width(kind));
    for (u32 k = 0; k < n; k++) w = fmaxf(w, ui_text_width(lab[k]) + gap + ui_text_width(val[k]));
    f32 bw = w, bh = lh * (f32)(n + 2) + 4;
    f32 x = fminf(L->mx + 24, L->w - bw - MARGIN), y = fminf(L->my + 26, L->h - bh - MARGIN);
    ui_soft(x - MARGIN, y - MARGIN, bw + 2 * MARGIN, bh + 2 * MARGIN, UI_RGBA(6, 6, 6, 236), FEATHER);
    f32 tx = x, ty = y;
    ui_text(tx, ty, C_TEXT, item_name(it));
    ui_hline(tx, ty + lh + 1, w, C_SEP);
    ty += lh + 4;
    ui_text(tx, ty, C_DIM, kind);
    for (u32 k = 0; k < n; k++) row(tx, ty + lh * (f32)(k + 1), w, lab[k], C_DIM, val[k], C_TEXT);
}

// ---------------------------------------------------------------- панели

static void draw_selected(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h->look_kind, h->look, w, &d)) return;
    char buf[64];
    f32 x = L->sel_x, y = L->bottom;
    panel(x, y, SEL_W, BOTTOM_H, d.name, d.role);
    portrait(x, y + WIN_HEAD_H + 6, SEL_PORTRAIT, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX);

    const f32 lh = ui_line(), tx = x + SEL_TX, tw = SEL_W - SEL_TX - 60;
    f32 ty = y + WIN_HEAD_H + 4;
    const char *cond = condition(&d);
    ui_text(tx, ty, C_TEXT, d.state);
    f32 cx = ui_text(tx, ty + lh, !strcmp(cond, "ЦЕЛ") ? C_DIM : C_BAD, cond);
    if (d.bleed > 0.01f) ui_text(cx + 8, ty + lh, C_BAD, "КРОВЬ ТЕЧЁТ");
    ItemStack wp = d.equip[SLOT_WEAPON];
    text_fit(tx, ty + 2 * lh, tw, C_DIM, wp.def ? item_name(wp) : "БЕЗ ОРУЖИЯ");
    snprintf(buf, sizeof buf, "%.0f/%.0f", (f64)d.blood, (f64)d.blood_max);
    bar(tx, ty + 3 * lh + 2, tw, 2, d.blood / d.blood_max, d.blood < 0.6f * d.blood_max ? C_BAD : C_BLOOD);

    if (d.body) {
        for (u32 g = 0; g <= GAIT_COUNT; g++) {
            f32 bx, by, bw;
            sel_btn_rect(L, g, &bx, &by, &bw);
            bool on = g < GAIT_COUNT ? d.body->gait == g : w->squad->formation;
            button(bx, by, bw, 16, g < GAIT_COUNT ? g_gait_key[g] : "G", g < GAIT_COUNT ? g_gait_btn[g] : "СТРОЙ", on,
                   inside(L->bmx, L->bmy, bx, by, bw, 16));
        }
    } else {
        ui_text(tx, y + SEL_BTN_Y, C_FAINT, "ЧУЖОЙ. ПРИКАЗАМ НЕ ПОДЧИНЯЕТСЯ.");
    }

    // Схема тела справа
    f32 fx = x + SEL_W - TPOSE_W * 2, fy = y + WIN_HEAD_H + 6;
    tpose(fx, fy, 2, d.hp, d.fracture, d.severed, true);
    snprintf(buf, sizeof buf, "%.0f%%", (f64)(fmaxf(0.0f, vital_ratio(d.hp)) * 100.0f));
    ui_font(UI_FONT_SMALL);
    text_center(fx, TPOSE_W * 2, fy + TPOSE_H * 2 + 2, health_color(vital_ratio(d.hp)), buf);
    ui_font(UI_FONT_TEXT);
}

static void draw_card(const Hud *h, const HudWorld *w, const Layout *L, u32 i)
{
    const Squad *sq = w->squad;
    const CharSheet *c = squad_sheet(sq, i);
    const Motion *pl = squad_move(sq, i);
    u32 at = squad_at(sq, i);
    f32 x = card_x(L, i), y = L->bottom;
    bool hot = inside(L->bmx, L->bmy, x, y, CARD_W, BOTTOM_H);
    bool look = h->look_kind == LOOK_SQUAD && h->look == i;
    panel(x, y, CARD_W, BOTTOM_H, NULL, NULL);
    if (hot) ui_soft(x - 6, y - 6, CARD_W + 12, BOTTOM_H + 12, C_BTN_HOT, 6);
    char n[4];
    snprintf(n, sizeof n, "%u", i + 1);
    ui_font(UI_FONT_SMALL);
    ui_text(x, y - 2, look ? C_TEXT : C_FAINT, n);
    ui_font(UI_FONT_TEXT);
    // Режим шага: одно, два или три деления; горят, пока идёт
    for (u32 g = 0; g <= pl->gait; g++)
        ui_rect(x + CARD_W - 2 - (f32)g * 4, y + 2, 2, 6, pl->moving ? C_TEXT : C_FAINT);
    const f32 px = floorf(x + (CARD_W - CARD_PORTRAIT) * 0.5f);
    portrait(px, y + 12, CARD_PORTRAIT, i, HUD_PORTRAIT_CARD_PX);
    const f32 lh = ui_line(), ty = y + 12 + CARD_PORTRAIT + 2;
    text_fit(x, ty, CARD_W, sq->selected[i] ? C_SEL : C_TEXT, c->name);
    // В бою — что делает в бою; выбыл — красным
    const Field *fd = w->field;
    ui_font(UI_FONT_SMALL);
    if (field_awake(fd, at)) {
        u8 st = fd->w.state[at];
        text_fit(x, ty + lh - 1, CARD_W, fs_out(st) ? C_BAD : C_TEXT, fs_out(st) ? g_state_name[st] : "В БОЮ");
    } else {
        text_fit(x, ty + lh - 1, CARD_W, C_DIM, pl->ctrl == CTRL_CHASE ? "К ЦЕЛИ" : (pl->moving ? (pl->gait == GAIT_RUN ? "БЕЖИТ" : "ИДЁТ") : "СТОИТ"));
    }
    ui_font(UI_FONT_TEXT);
    f32 hp[PART_COUNT];
    for (u32 p = 0; p < PART_COUNT; p++) hp[p] = fd->ps->hp[p][at];
    f32 r = vital_ratio(hp);
    bar(x, y + BOTTOM_H - 6, CARD_W, 2, r, health_color(r));
    bar(x, y + BOTTOM_H - 2, CARD_W, 2, fd->ps->blood[at] / (100.0f * fd->ps->mass[at] / 75.0f), C_BLOOD);
    // Выбранный: бледно-зелёная черта сверху
    if (sq->selected[i]) ui_rect(x, y - 8, CARD_W, 2, C_SEL);
}

static void draw_buttons(const Hud *h, const Layout *L)
{
    bool can = h->look_kind != LOOK_NONE;
    f32 x = L->btn_x;
    f32 y0 = win_btn_y(L, 0), y1 = win_btn_y(L, 1), y2 = win_btn_y(L, 2);
    button(x, y0, BTN_W, BTN_H, "I", "ИНВЕНТАРЬ", h->win_inv, can && inside(L->mx, L->my, x, y0, BTN_W, BTN_H));
    button(x, y1, BTN_W, BTN_H, "P", "ПЕРСОНАЖ", h->win_char, can && inside(L->mx, L->my, x, y1, BTN_W, BTN_H));
    if (h->can_look) button(x, y2, BTN_W, BTN_H, "K", "ВНЕШНОСТЬ", h->win_look, can && inside(L->mx, L->my, x, y2, BTN_W, BTN_H));
}

static void draw_clock(const Hud *h, const Layout *L, const HudWorld *w)
{
    char buf[32];
    f32 x = L->clock_x, y = EDGE;
    panel(x, y, CLOCK_W, CLOCK_H, NULL, NULL);
    u32 minutes = (u32)(w->tod * 24.0f * 60.0f) % (24 * 60);
    snprintf(buf, sizeof buf, "ДЕНЬ %u", w->day);
    ui_font(UI_FONT_SMALL);
    ui_text(x, y - 2, C_DIM, buf);
    text_right(x + CLOCK_W, y - 2, C_FAINT, h->speed ? "ВРЕМЯ" : "ПАУЗА");
    ui_font(UI_FONT_BIG);
    snprintf(buf, sizeof buf, "%02u:%02u", minutes / 60, minutes % 60);
    ui_text(x, y + CLOCK_H - ui_line() + 2, C_TEXT, buf);
    ui_font(UI_FONT_TEXT);
    static const char *labels[4] = { "II", ">", ">>", ">>>" };
    for (u32 k = 0; k < 4; k++) {
        f32 bx = speed_btn_x(L, k);
        bool on = h->speed == k, hot = inside(L->mx, L->my, bx, SPEED_Y, SPEED_W, 14);
        ui_rect(bx, SPEED_Y, SPEED_W, 14, on ? C_BTN_ON : (hot ? C_BTN_HOT : C_BTN));
        ui_font(UI_FONT_SMALL);
        text_center(bx, SPEED_W, SPEED_Y + floorf((14 - ui_line()) * 0.5f), on || hot ? C_TEXT : C_DIM, labels[k]);
        ui_font(UI_FONT_TEXT);
    }
}

// Окно ИНВЕНТАРЬ: слоты с подписями, сумка, справа состояние, навыки и тело
static void draw_inventory(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h->look_kind, h->look, w, &d)) return;
    char buf[64];
    f32 x = L->iwx, y = L->iwy;
    panel(x, y, INV_WIN_W, INV_WIN_H, "ИНВЕНТАРЬ", d.name);
    close_btn(L, x, y, INV_WIN_W);
    const f32 lh = ui_line();

    // Снаряжение
    i32 hover_slot = slot_under(L);
    for (u32 k = 0; k <= SLOT_COUNT; k++) {
        u8 sl = g_slots[k].slot;
        f32 sx, sy, sw, sh;
        slot_rect(L, k, &sx, &sy, &sw, &sh);
        text_center(sx, sw, sy - LABEL_H + floorf((LABEL_H - lh) * 0.5f) - 1, C_DIM, g_slot_name[sl]);
        bool fits = h->held.def && item_slot(h->held) == sl;
        grid(sx, sy, g_slots[k].w, g_slots[k].h, hover_slot == sl);
        if (fits) ui_frame(sx, sy, sw, sh, C_SEL);
        // Взятая на курсор вещь ещё на месте: место показано пустым
        bool taken = h->held.def && h->held_slot == sl;
        if (d.equip[sl].def && !taken) {
            u8 iw, ih;
            item_size(d.equip[sl], &iw, &ih);
            draw_item_cell(sx, sy, MIN(iw, g_slots[k].w) * CELL, MIN(ih, g_slots[k].h) * CELL, d.equip[sl], hover_slot == sl);
        }
    }

    // Строка ВЕЩИ: ноша полосой и числом
    f32 hy = L->iy - BAG_HEAD + floorf((BAG_HEAD - lh) * 0.5f) - 2;
    section(L->ix, hy, "ВЕЩИ");
    if (d.sheet) {
        f32 load = sheet_load(d.sheet), cap = sheet_carry_max(d.sheet, d.skill[SK_STRENGTH]);
        snprintf(buf, sizeof buf, "%.1f / %.0f КГ", (f64)load, (f64)cap);
        f32 vw = ui_text_width(buf), bx = L->ix + EQUIP_W - vw - 12 - 70;
        text_right(L->ix + EQUIP_W, hy, load > cap ? C_BAD : C_TEXT, buf);
        bar(bx, hy + floorf(lh * 0.5f) - 1, 70, 2, load / cap, load > cap ? C_BAD : C_BAR);
    }

    // Сумка
    grid(L->ix, L->iy, INV_W, INV_H, false);
    if (!d.sheet) {
        text_center(L->ix, EQUIP_W, L->iy + INV_H * CELL * 0.5f - lh * 0.5f, C_DIM, "ЧУЖОЕ. НЕ ТРОГАТЬ.");
    } else {
        const Inventory *inv = &d.sheet->inv;
        i32 hx, hy2;
        bool in_cell = cell_under(L, &hx, &hy2);
        if (h->held.def && in_cell) {
            // Куда ляжет вещь с курсора
            u8 iw, ih;
            item_size(h->held, &iw, &ih);
            // Своя прежняя клетка не мешает: проба на мешке без взятой вещи
            Inventory probe = *inv;
            i32 own = h->held_slot == ASK_BAG ? inv_at(&probe, h->held_x, h->held_y) : -1;
            if (own >= 0) inv_take(&probe, own);
            bool ok = inv_fits(&probe, h->held, hx, hy2);
            ui_rect(L->ix + hx * CELL, L->iy + hy2 * CELL, iw * CELL, ih * CELL,
                    ok ? UI_RGBA(142, 214, 150, 40) : UI_RGBA(200, 70, 56, 50));
        }
        i32 hover = in_cell ? inv_at(inv, hx, hy2) : -1;
        for (u32 i = 0; i < inv->n; i++) {
            if (h->held.def && h->held_slot == ASK_BAG && inv->x[i] == h->held_x && inv->y[i] == h->held_y) continue;
            u8 iw, ih;
            item_size(inv->item[i], &iw, &ih);
            draw_item_cell(L->ix + inv->x[i] * CELL, L->iy + inv->y[i] * CELL, iw * CELL, ih * CELL, inv->item[i], (i32)i == hover);
        }
    }

    // Правая колонка, отделена тонкой линией
    const f32 rx = L->rx + PAD + 1, rw = RCOL_W;
    ui_vline(L->rx, L->sy, INV_WIN_H - (L->sy - y), C_SEP);
    f32 ry = L->sy;
    ry = section(rx, ry, "СОСТОЯНИЕ");
    const char *cond = condition(&d);
    row(rx, ry, rw, "ОБЩЕЕ", C_DIM, cond, !strcmp(cond, "ЦЕЛ") ? C_TEXT : C_BAD);
    ry += lh;
    snprintf(buf, sizeof buf, "%.0f%%", (f64)(100.0f * d.blood / fmaxf(d.blood_max, 1.0f)));
    row(rx, ry, rw, d.bleed > 0.01f ? "КРОВЬ ТЕЧЁТ" : "КРОВЬ", d.bleed > 0.01f ? C_BAD : C_DIM, buf, d.blood < 0.6f * d.blood_max ? C_BAD : C_TEXT);
    ry += lh;
    row(rx, ry, rw, "СЕЙЧАС", C_DIM, d.state ? d.state : "", C_TEXT);
    ry += lh + 10;

    ry = section(rx, ry, "НАВЫКИ");
    // Тело, бой, оружие — три группы через тонкую линию
    static const u8 order[SK_COUNT] = {
        SK_STRENGTH, SK_DEXTERITY, SK_TOUGHNESS, SK_ATHLETICS, SK_ATTACK, SK_DEFENSE, SK_DODGE,
        SK_WEAPON + WC_BLADE, SK_WEAPON + WC_CLEAVER, SK_WEAPON + WC_BLUNT, SK_WEAPON + WC_POLEARM, SK_WEAPON + WC_HEAVY,
    };
    for (u32 k = 0; k < SK_COUNT; k++) {
        if (k == 4 || k == 7) {
            ui_hline(rx, ry + 3, rw, C_SEP);
            ry += 7;
        }
        snprintf(buf, sizeof buf, "%.0f", (f64)d.skill[order[k]]);
        row(rx, ry, rw, g_skill_name[order[k]], C_DIM, buf, C_TEXT);
        ry += lh;
    }
    ry += 10;

    ry = section(rx, ry, "ТЕЛО");
    for (u32 p = 0; p < PART_COUNT; p++, ry += lh) {
        bool cut = d.severed & (1u << p), broken = d.fracture & (1u << p);
        if (cut) snprintf(buf, sizeof buf, "ОТСЕЧЕНА");
        else if (broken) snprintf(buf, sizeof buf, "ПЕРЕЛОМ  %.0f/%.0f", (f64)d.hp[p], (f64)g_part_hp[p]);
        else snprintf(buf, sizeof buf, "%.0f/%.0f", (f64)d.hp[p], (f64)g_part_hp[p]);
        f32 r = d.hp[p] / g_part_hp[p];
        row(rx, ry, rw, g_part_name[p], cut ? C_BAD : C_DIM, buf, cut || broken || r < 0.5f ? C_BAD : (r < 0.99f ? health_color(r) : C_TEXT));
    }

    if (d.sheet) {
        ui_font(UI_FONT_SMALL);
        f32 by = y + INV_WIN_H - ui_line();
        ui_text(rx, by - ui_line(), C_FAINT, "ЛКМ — ВЗЯТЬ, ПОЛОЖИТЬ");
        ui_text(rx, by, C_FAINT, "ПКМ — НАДЕТЬ, СНЯТЬ");
        ui_font(UI_FONT_TEXT);
    }

    if (!h->held.def) {
        ItemStack tip = { 0 };
        i32 gx, gy;
        if (hover_slot >= 0) tip = d.equip[hover_slot];
        else if (d.sheet && cell_under(L, &gx, &gy)) {
            i32 at = inv_at(&d.sheet->inv, gx, gy);
            if (at >= 0) tip = d.sheet->inv.item[at];
        }
        if (tip.def) tooltip(L, tip);
    }
}

// Подпись в мире: тёмная плашка без рамки, черта снизу цветом стороны
static void world_tag(f32 x, f32 y, const char *s, u32 tc, u32 line)
{
    f32 tw = ui_text_width(s), lh = ui_line();
    ui_soft(x - 8, y - 2, tw + 16, lh + 4, UI_RGBA(0, 0, 0, 170), 4);
    if (line) ui_hline(x - 2, y + lh, tw + 4, line);
    ui_text(x, y, tc, s);
}

void hud_draw(const Hud *h, const Input *in, const HudWorld *w)
{
    Layout L = layout(h, in);
    const Squad *sq = w->squad;

    // Имя над тем, кто под курсором
    u8 kind;
    u32 idx;
    V3 head;
    if (!h->drag_live && L.my < L.bottom_top && pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
        f32 sx, sy;
        LookData d;
        if (project(w->view_proj, v3_add(head, v3(0, 0.3f, 0)), &sx, &sy) && look_data(kind, idx, w, &d)) {
            f32 tw = ui_text_width(d.name);
            // Свои — бледно-зелёные, враги на поле — красные
            bool foe = kind == LOOK_FIELD && foe_of_squad(w, idx);
            world_tag(floorf(sx - tw * 0.5f), floorf(sy - ui_line()), d.name, kind == LOOK_SQUAD ? C_SEL : (foe ? C_BAD : C_TEXT),
                      kind == LOOK_SQUAD ? C_SEL : (foe ? C_BAD : 0));
        }
    } else if (!h->drag_live && L.my < L.bottom_top) {
        // Вещь на земле: имя у курсора (ПКМ — подобрать)
        i32 k = ground_item_under(w, L.mx, L.my);
        if (k >= 0) world_tag(floorf(L.mx + 12), floorf(L.my - 16), item_name(w->ground->item[k]), C_TEXT, 0);
    }

    if (h->drag_live) {
        f32 x0 = fminf(h->drag_x, L.mx), y0 = fminf(h->drag_y, L.my);
        f32 bw = fabsf(L.mx - h->drag_x), bh = fabsf(L.my - h->drag_y);
        ui_rect(x0, y0, bw, bh, UI_RGBA(142, 214, 150, 22));
        ui_frame(x0, y0, bw, bh, UI_RGBA(142, 214, 150, 160));
    }

    draw_clock(h, &L, w);
    draw_buttons(h, &L);
    ui_layer(UI_BOTTOM);
    if (h->look_kind != LOOK_NONE) draw_selected(h, &L, w);
    for (u32 i = 0; i < sq->count; i++) draw_card(h, w, &L, i);
    ui_layer(UI_TOP);
    if (h->win_inv) draw_inventory(h, &L, w);

    if (h->held.def) {
        u8 iw, ih;
        item_size(h->held, &iw, &ih);
        draw_item_cell(L.mx - 6, L.my - 6, iw * CELL, ih * CELL, h->held, true);
    }
}

// ---------------------------------------------------------------- окна с виджетами

// Мышь в слое окон; сбросить строки
static void form_input(HudForm *f, const Input *in)
{
    ui_layer(UI_DEBUG);
    f32 s = ui_scale();
    f->mx = floorf((f32)in->mouse_x / s);
    f->my = floorf((f32)in->mouse_y / s);
    f->lmb = in->mouse_pressed[MOUSE_LEFT];
    f->down = in->mouse_down[MOUSE_LEFT];
    f->over = false;
    f->id = 0;
    if (!f->down) f->slider = -1;
}

// Открытое окно: тянут за шапку, закрывают крестом. false — закрыли
static bool form_window(HudForm *f, const char *title, const char *right)
{
    if (f->drag) {
        if (f->down) {
            f->x = f->mx - f->grab_x;
            f->y = f->my - f->grab_y;
        } else {
            f->drag = false;
        }
        f->over = true;
    }
    win_place(&f->x, &f->y, f->w, f->h, ui_width(), ui_height());
    f32 x = f->x, y = f->y;
    bool close = inside(f->mx, f->my, x + f->w - 16, y + 2, 16, 16);
    if (inside(f->mx, f->my, x, y, f->w, f->h)) {
        f->over = true;
        if (f->lmb && close) {
            f->open = false;
            f->lmb = false;
            return false;
        }
        if (f->lmb && f->my < y + WIN_HEAD_H) {
            f->drag = true;
            f->grab_x = f->mx - x;
            f->grab_y = f->my - y;
            f->lmb = false;
        }
    }
    panel(x, y, f->w, f->h, title, right);
    if (close) ui_rect(x + f->w - 16, y + 2, 16, 16, C_BTN_HOT);
    text_center(x + f->w - 16, 16, y + floorf((WIN_HEAD_H - ui_line()) * 0.5f), close ? C_TEXT : C_DIM, "×");
    f->row = y + WIN_HEAD_H + 6;
    return true;
}

bool hud_form_begin(HudForm *f, const Input *in, const char *title, const char *right)
{
    form_input(f, in);
    return f->open && form_window(f, title, right);
}

// Строка виджета: прямоугольник строки и горячая ли она
static bool form_row(HudForm *f, f32 *x, f32 *y)
{
    *x = f->x;
    *y = f->row;
    f->row += DBG_ROW;
    f->id++;
    bool hot = inside(f->mx, f->my, *x - 4, *y, f->w + 8, DBG_ROW);
    if (hot) ui_rect(*x - 4, *y, f->w + 8, DBG_ROW, C_CELL_HOT);
    return hot;
}

// Текст по середине строки виджета
static f32 row_ty(f32 y) { return y + floorf((DBG_ROW - ui_line()) * 0.5f); }

bool hud_form_check(HudForm *f, const char *label, bool *v)
{
    f32 x, y;
    bool hot = form_row(f, &x, &y);
    f32 by = y + floorf((DBG_ROW - 9) * 0.5f);
    ui_rect(x, by, 9, 9, C_CELL);
    ui_frame(x, by, 9, 9, hot ? C_TEXT : C_GRID);
    if (*v) ui_rect(x + 2, by + 2, 5, 5, C_TEXT);
    ui_text(x + 16, row_ty(y), hot || *v ? C_TEXT : C_DIM, label);
    if (hot && f->lmb) {
        *v = !*v;
        f->lmb = false;
        return true;
    }
    return false;
}

bool hud_form_button(HudForm *f, const char *label)
{
    f32 x, y;
    bool hot = form_row(f, &x, &y);
    button(x, y + 1, f->w, DBG_ROW - 2, NULL, label, false, hot);
    if (hot && f->lmb) {
        f->lmb = false;
        return true;
    }
    return false;
}

bool hud_form_slider(HudForm *f, const char *label, f32 *v, f32 lo, f32 hi, const char *value)
{
    f32 x, y;
    bool hot = form_row(f, &x, &y);
    const f32 tx = x + DBG_LABEL, tw = f->w - DBG_LABEL - 40;
    text_fit(x, row_ty(y), DBG_LABEL - 6, hot || f->slider == f->id ? C_TEXT : C_DIM, label);
    if (hot && f->lmb && inside(f->mx, f->my, tx - 3, y, tw + 6, DBG_ROW)) {
        f->slider = f->id;
        f->lmb = false;
    }
    bool changed = false;
    if (f->slider == f->id) {
        f32 nv = lo + clampf((f->mx - tx) / tw, 0.0f, 1.0f) * (hi - lo);
        changed = nv != *v;
        *v = nv;
    }
    f32 r = clampf((*v - lo) / (hi - lo), 0.0f, 1.0f), my = y + floorf(DBG_ROW * 0.5f);
    ui_rect(tx, my - 1, tw, 2, C_BAR_BG);
    ui_rect(tx, my - 1, floorf(tw * r), 2, C_BAR);
    ui_rect(floorf(tx + (tw - 3) * r), my - 4, 3, 8, f->slider == f->id || hot ? C_TEXT : C_DIM);
    text_right(x + f->w, row_ty(y), C_DIM, value);
    return changed;
}

void hud_form_text(HudForm *f, const char *label, const char *value)
{
    f32 x, y;
    form_row(f, &x, &y);
    ui_text(x, row_ty(y), C_FAINT, label);
    text_right(x + f->w, row_ty(y), C_DIM, value);
}

i32 hud_form_step(HudForm *f, const char *label, const char *value)
{
    f32 x, y;
    bool hot = form_row(f, &x, &y);
    const f32 tx = x + DBG_LABEL, tw = f->w - DBG_LABEL;
    bool hl = hot && inside(f->mx, f->my, tx - 2, y, 14, DBG_ROW);
    bool hr = hot && inside(f->mx, f->my, tx + tw - 12, y, 14, DBG_ROW);
    text_fit(x, row_ty(y), DBG_LABEL - 6, hot ? C_TEXT : C_DIM, label);
    ui_text(tx + 2, row_ty(y), hl ? C_TEXT : C_FAINT, "<");
    text_right(tx + tw - 2, row_ty(y), hr ? C_TEXT : C_FAINT, ">");
    text_center(tx, tw, row_ty(y), C_TEXT, value);
    if (!hot || !f->lmb || !inside(f->mx, f->my, tx - 2, y, tw + 4, DBG_ROW)) return 0;
    f->lmb = false;
    // Слева от середины — назад, справа — вперёд
    return f->mx < tx + tw * 0.5f ? -1 : 1;
}

bool hud_form_tabs(HudForm *f, const char *const *names, u32 n, u8 *cur)
{
    f32 x, y;
    bool hot = form_row(f, &x, &y);
    f32 w = floorf((f->w - (f32)(n - 1) * 3) / (f32)n);
    bool changed = false;
    for (u32 i = 0; i < n; i++) {
        f32 bx = x + (f32)i * (w + 3);
        bool h = hot && inside(f->mx, f->my, bx, y, w, DBG_ROW);
        button(bx, y + 1, w, DBG_ROW - 2, NULL, names[i], *cur == i, h);
        if (h && f->lmb && *cur != i) {
            *cur = (u8)i;
            changed = true;
        }
    }
    if (hot && f->lmb) f->lmb = false;
    return changed;
}

void hud_form_head(HudForm *f, const char *label)
{
    f32 x = f->x, y = f->row + 4;
    f->row += DBG_ROW + 6;
    section(x, y, label);
    ui_hline(x, y + ui_line() + 1, f->w, C_SEP);
}

void hud_form_swatch(HudForm *f, u32 rgba)
{
    f32 x = f->x + f->w - 24, y = f->row - DBG_ROW + 3;
    ui_rect(x, y, 24, DBG_ROW - 6, rgba);
}

void hud_form_portrait(HudForm *f, u32 slot, i32 px)
{
    f32 x = floorf(f->x + (f->w - (f32)px) * 0.5f), y = f->row;
    portrait(x, y, (f32)px, slot, px);
    f->row += (f32)px + 6;
}

bool hud_form_end(HudForm *f)
{
    if (f->open) f->h = f->row - f->y + 6;
    return f->over || f->slider >= 0;
}

// Отладочное меню: кнопка в углу, счётчик кадров, окно НАСТРОЙКИ
bool hud_debug_begin(Hud *h, const Input *in)
{
    HudForm *f = &h->dbg.f;
    form_input(f, in);
    bool hot = inside(f->mx, f->my, EDGE, EDGE - 6, DBG_BTN_W, 18);
    button(EDGE, EDGE - 6, DBG_BTN_W, 18, NULL, "НАСТРОЙКИ", f->open, hot);
    if (hot) {
        f->over = true;
        if (f->lmb) {
            f->open = !f->open;
            f->lmb = false;
        }
    }
    if (h->dbg.fps_on) {
        char buf[32];
        snprintf(buf, sizeof buf, "%.0f К/С  %.2f МС", (f64)h->dbg.fps, (f64)h->dbg.fps_ms);
        f32 w = ui_text_width(buf) + 16, x = floorf((ui_width() - w) * 0.5f);
        ui_soft(x, EDGE - 6, w, 18, UI_RGBA(0, 0, 0, 150), 4);
        text_center(x, w, EDGE - 6 + floorf((18 - ui_line()) * 0.5f), C_TEXT, buf);
    }
    return f->open && form_window(f, "НАСТРОЙКИ", "ОТЛАДКА");
}

bool hud_debug_check(Hud *h, const char *label, bool *v) { return hud_form_check(&h->dbg.f, label, v); }
bool hud_debug_button(Hud *h, const char *label) { return hud_form_button(&h->dbg.f, label); }
bool hud_debug_slider(Hud *h, const char *label, f32 *v, f32 lo, f32 hi, const char *value)
{
    return hud_form_slider(&h->dbg.f, label, v, lo, hi, value);
}
void hud_debug_text(Hud *h, const char *label, const char *value) { hud_form_text(&h->dbg.f, label, value); }

#define JOURNAL_W 420

// Журнал справа от меню: всё, что раньше шло в консоль, новые строки внизу, длинные переносятся по ширине
static void draw_journal(Hud *h)
{
    HudForm *f = &h->dbg.f;
    f32 x = f->x + f->w + 2 * MARGIN + 4, y = f->y, jh = fmaxf(f->h, 320);
    if (inside(f->mx, f->my, x, y, JOURNAL_W, jh)) f->over = true;
    panel(x, y, JOURNAL_W, jh, "ЖУРНАЛ", NULL);
    ui_font(UI_FONT_SMALL);
    const f32 lh = ui_line(), tw = JOURNAL_W;
    const u32 rows = (u32)((jh - WIN_HEAD_H - 10) / lh);
    f32 ty = y + jh - lh;
    u32 used = 0, n = log_count();
    char seg[LOG_LINE_MAX];
    for (u32 i = n; i-- > 0 && used < rows;) {
        const char *line = log_line(i);
        // Разбить на куски по ширине, рисовать снизу вверх
        u32 cut[32], parts = 0;
        for (u32 at = 0; line[at] && parts < 32;) {
            cut[parts++] = at;
            u32 k = ui_text_fit(line + at, tw);
            at += k ? k : (u32)utf8_first(line + at);
        }
        if (!parts) cut[parts++] = 0;
        u32 c = i + 1 == n ? C_TEXT : (line[0] == '[' ? C_FAINT : C_DIM);
        for (u32 k = parts; k-- > 0 && used < rows; used++, ty -= lh) {
            u32 end = k + 1 < parts ? cut[k + 1] : (u32)strlen(line);
            u32 len = MIN(end - cut[k], (u32)sizeof seg - 1);
            memcpy(seg, line + cut[k], len);
            seg[len] = 0;
            ui_text(x, ty, c, seg);
        }
    }
    ui_font(UI_FONT_TEXT);
}

bool hud_debug_end(Hud *h)
{
    if (h->dbg.f.open && h->dbg.journal) {
        h->dbg.f.h = h->dbg.f.row - h->dbg.f.y + 6;
        draw_journal(h);
    }
    return hud_form_end(&h->dbg.f);
}
