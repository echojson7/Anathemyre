#include "hud.h"

#include <stdio.h>
#include <string.h>

#include "../render/ui.h"

// Палитра: тёмные панели, выцветшая охра, кость; бледно-зелёный только для своих выбранных
#define C_PANEL  UI_RGBA(14, 13, 11, 228)
#define C_CELL   UI_RGBA(34, 31, 26, 245)
#define C_EDGE   UI_RGBA(92, 84, 66, 255)
#define C_TEXT   UI_RGBA(208, 202, 182, 255)
#define C_DIM    UI_RGBA(124, 118, 102, 255)
#define C_ACCENT UI_RGBA(198, 150, 74, 255)
#define C_SEL    UI_RGBA(142, 214, 150, 255)
#define C_BAD    UI_RGBA(190, 52, 38, 255)
#define C_SHADE  UI_RGBA(0, 0, 0, 150)

#define CARD_W 96
#define CARD_H 34
#define CARD_GAP 3
#define WIN_W 568
#define WIN_H 268
#define SLOT_W 108
#define SLOT_H 20
#define CELL 17
#define DOUBLE_CLICK_S 0.35f
#define SELECT_RING_R 0.48f  // радиус кольца выбора под ногами, как в main.c

// Порядок слотов в окне: две колонки по пять
static const u8 g_slot_order[SLOT_COUNT + 1] = {
    SLOT_HEAD, SLOT_FACE, SLOT_TORSO_UNDER, SLOT_TORSO_ARMOR, SLOT_HANDS,
    SLOT_LEGS, SLOT_BELT, SLOT_FEET, SLOT_BACK, SLOT_WEAPON,
};

// Фигура тела в клетках 11x23: x, y, w, h для каждой части. Лицом к зрителю: левая рука справа
static const u8 g_figure[PART_COUNT][4] = {
    [PART_HEAD] = { 4, 0, 3, 3 }, [PART_CHEST] = { 3, 4, 5, 5 }, [PART_STOMACH] = { 3, 9, 5, 4 },
    [PART_ARM_L] = { 9, 4, 2, 9 }, [PART_ARM_R] = { 0, 4, 2, 9 },
    [PART_LEG_L] = { 6, 13, 2, 10 }, [PART_LEG_R] = { 3, 13, 2, 10 },
};

// Всё, что окно показывает о персонаже, откуда бы он ни был
typedef struct LookData {
    const char *name, *role, *state;
    f32 hp[PART_COUNT], blood, blood_max, height, mass;
    u8  fracture, severed;
    const f32 *skill;
    Item equip[SLOT_COUNT + 1];
    CharSheet *sheet;   // свои: вещи можно перекладывать
} LookData;

typedef struct Layout {
    f32 w, h, mx, my;
    f32 card_y;
    f32 wx, wy;       // окно осмотра
    f32 sx, sy;       // слоты
    f32 ix, iy;       // инвентарь
} Layout;

static Layout layout(const Input *in)
{
    Layout L;
    i32 s = ui_scale();
    L.w = ui_width();
    L.h = ui_height();
    L.mx = (f32)(in->mouse_x / s);
    L.my = (f32)(in->mouse_y / s);
    L.card_y = L.h - CARD_H - 6;
    L.wx = floorf((L.w - WIN_W) * 0.5f);
    L.wy = fmaxf(4.0f, floorf((L.card_y - 6 - WIN_H) * 0.5f));
    L.sx = L.wx + 338;
    L.sy = L.wy + 30;
    L.ix = L.sx;
    L.iy = L.sy + 5 * (SLOT_H + 3) + 14;
    return L;
}

static bool inside(f32 mx, f32 my, f32 x, f32 y, f32 w, f32 h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static f32 card_x(u32 i) { return 6.0f + (f32)i * (CARD_W + CARD_GAP); }

static bool slot_rect(const Layout *L, u32 k, f32 *x, f32 *y)
{
    *x = L->sx + (f32)(k % 2) * (SLOT_W + 4);
    *y = L->sy + (f32)(k / 2) * (SLOT_H + 3);
    return true;
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

static i32 arena_slot(const ArenaView *v, const ArenaSession *s, u32 roster)
{
    if (!v->in_bout) return -1;
    for (u32 i = 0; i < s->n && i < s->w.count; i++)
        if (s->idx[i] == roster) return (i32)i;
    return -1;
}

bool hud_arena_pos(const ArenaView *v, const ArenaSession *s, u32 roster, V3 *out)
{
    i32 i = arena_slot(v, s, roster);
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
    static const char *mode[GAIT_COUNT] = { "СТОИТ  ШАГ", "СТОИТ  ТРУСЦА", "СТОИТ  БЕГ" };
    return p->moving ? go[p->gait] : mode[p->gait];
}

static bool look_data(const Hud *h, const HudWorld *w, LookData *o)
{
    memset(o, 0, sizeof *o);
    if (h->look_kind == LOOK_SQUAD) {
        if (h->look >= w->squad->count) return false;
        CharSheet *c = &w->squad->sheet[h->look];
        o->name = c->name;
        o->role = "ОТРЯД";
        o->state = move_state(&w->squad->body[h->look]);
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
    if (h->look_kind != LOOK_ARENA || h->look >= ARENA_ROSTER) return false;
    const ArenaSession *s = w->arena;
    const ArenaFighter *f = &s->roster[h->look];
    i32 i = arena_slot(w->aview, s, h->look);
    o->role = "БОЕЦ АРЕНЫ";
    o->height = f->d.height;
    o->mass = f->d.mass;
    o->blood_max = 100.0f * f->d.mass / 75.0f;
    if (i >= 0) {
        // На ринге: живое состояние из боя
        const CombatWorld *cw = &s->w;
        o->name = arena_roster_name(s, h->look);
        o->state = g_state_name[cw->state[i]];
        for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = cw->hp[p][i];
        o->blood = cw->blood[i];
        o->fracture = cw->fracture[i];
        o->severed = cw->severed[i];
        o->skill = f->d.skill;
        if (!cw->disarmed[i]) o->equip[SLOT_WEAPON] = item_make(IK_WEAPON, cw->weapon[i], cw->weapon_q[i], 1);
        for (u32 sl = 0; sl < SLOT_COUNT; sl++)
            if (cw->armor[sl][i] != ARMOR_NONE) o->equip[sl] = item_make(IK_ARMOR, cw->armor[sl][i], Q_WORN, 1);
        return true;
    }
    // Между боями: из ростера; нули в hp и крови значат «здоров»
    o->name = arena_roster_name(s, h->look);
    o->state = f->gone ? "ВЫБЫЛ" : "ОТДЫХАЕТ";
    bool fresh = f->d.blood <= 0.0f;
    for (u32 p = 0; p < PART_COUNT; p++) o->hp[p] = fresh ? g_part_hp[p] : f->d.hp[p];
    o->blood = fresh ? o->blood_max : f->d.blood;
    o->fracture = f->d.fracture;
    o->severed = f->d.severed;
    o->skill = f->d.skill;
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
}

void hud_open_inspect(Hud *h, u8 kind, u32 index)
{
    h->inspect = true;
    h->look_kind = kind;
    h->look = index;
}

static void close_inspect(Hud *h, Squad *sq)
{
    if (h->held.kind) {
        // Вещь с курсора обратно в мешок; некуда — окно не закрывается
        if (h->look_kind != LOOK_SQUAD || !inv_add(&sq->sheet[h->look].inv, h->held)) return;
        h->held.kind = IK_NONE;
    }
    h->inspect = false;
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

    if (in->key_pressed[KEY_I]) {
        i32 p = squad_primary(sq);
        if (h->inspect) close_inspect(h, sq);
        else if (p >= 0) hud_open_inspect(h, LOOK_SQUAD, (u32)p);
    }
    if (h->inspect && in->key_pressed[KEY_ESCAPE]) close_inspect(h, sq);

    if (h->inspect && inside(L.mx, L.my, L.wx, L.wy, WIN_W, WIN_H)) {
        over = true;
        if (lmb && inside(L.mx, L.my, L.wx + WIN_W - 14, L.wy + 2, 12, 11)) close_inspect(h, sq);
        else if (h->look_kind == LOOK_SQUAD && h->look < sq->count)
            inventory_input(h, &L, &sq->sheet[h->look], lmb, rmb);
        lmb = rmb = false;
    }

    // Панель отряда: клик — выбрать, с Shift — добавить, двойной — камера к нему
    for (u32 i = 0; i < sq->count; i++) {
        if (!inside(L.mx, L.my, card_x(i), L.card_y, CARD_W, CARD_H)) continue;
        over = true;
        if (!lmb) break;
        if (h->last_click_card == (i32)i && w->time - h->last_click_t < DOUBLE_CLICK_S) h->focus = (i32)i;
        h->last_click_card = (i32)i;
        h->last_click_t = w->time;
        if (shift) sq->selected[i] = !sq->selected[i];
        else {
            for (u32 k = 0; k < sq->count; k++) sq->selected[k] = k == i;
        }
        if (h->inspect && h->look_kind == LOOK_SQUAD && !h->held.kind) h->look = i;
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
        } else {
            u8 kind;
            u32 idx;
            V3 head;
            if (!pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
                if (!shift) memset(sq->selected, 0, sizeof sq->selected);
            } else if (kind == LOOK_SQUAD) {
                if (shift) sq->selected[idx] = !sq->selected[idx];
                else {
                    for (u32 k = 0; k < sq->count; k++) sq->selected[k] = k == idx;
                }
                if (h->inspect && h->look_kind == LOOK_SQUAD && !h->held.kind) h->look = idx;
            } else if (!h->held.kind) {
                // Чужого можно только осмотреть
                hud_open_inspect(h, LOOK_ARENA, idx);
            }
        }
    }
    (void)rmb;
    return over || h->held.kind != IK_NONE || h->dragging;
}

// ---------------------------------------------------------------- отрисовка

static u32 health_color(f32 r)
{
    // Кость → охра → кровь → тёмная кровь
    f32 a[3] = { 200, 194, 170 }, b[3] = { 200, 140, 52 }, c[3] = { 180, 40, 30 };
    f32 o[3];
    if (r < 0.0f) return UI_RGBA(70, 14, 12, 255);
    for (u32 k = 0; k < 3; k++)
        o[k] = r >= 0.5f ? lerpf(b[k], a[k], (r - 0.5f) * 2.0f) : lerpf(c[k], b[k], r * 2.0f);
    return UI_RGBA((u32)o[0], (u32)o[1], (u32)o[2], 255);
}

static void bar(f32 x, f32 y, f32 w, f32 h, f32 r, u32 color)
{
    ui_rect(x, y, w, h, UI_RGBA(40, 36, 30, 255));
    ui_rect(x, y, floorf(w * clampf(r, 0.0f, 1.0f)), h, color);
}

static void text_right(f32 x, f32 y, u32 color, const char *s) { ui_text(x - ui_text_width(s), y, color, s); }

static void body_figure(f32 x, f32 y, f32 unit, const f32 *hp, u8 fracture, u8 severed)
{
    for (u32 p = 0; p < PART_COUNT; p++) {
        const u8 *r = g_figure[p];
        f32 px = x + r[0] * unit, py = y + r[1] * unit, pw = r[2] * unit, ph = r[3] * unit;
        if (severed & (1u << p)) {
            ui_frame(px, py, pw, ph, UI_RGBA(90, 30, 24, 255));
            continue;
        }
        ui_rect(px, py, pw, ph, health_color(hp[p] / g_part_hp[p]));
        // Перелом: тёмная поперечная полоса
        if (fracture & (1u << p)) ui_rect(px, py + floorf(ph * 0.5f), pw, fmaxf(1.0f, floorf(unit * 0.5f)), C_SHADE);
    }
}

static f32 vital_ratio(const f32 *hp)
{
    f32 r = 1.0f;
    for (u32 p = 0; p <= PART_STOMACH; p++) r = fminf(r, hp[p] / g_part_hp[p]);
    return r;
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

static void draw_item_cell(f32 x, f32 y, f32 w, f32 h, Item it, u32 edge)
{
    static const u32 kind_fill[4] = { 0, UI_RGBA(52, 44, 36, 245), UI_RGBA(44, 46, 42, 245), UI_RGBA(48, 42, 30, 245) };
    ui_rect(x, y, w, h, kind_fill[it.kind]);
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
        text_right(x + w - 1, y + h - 8, C_ACCENT, n);
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
    f32 bw = w + 8, bh = (f32)n * UI_LINE + 4;
    f32 x = fminf(L->mx + 10, L->w - bw - 2), y = fminf(L->my + 12, L->h - bh - 2);
    ui_rect(x, y, bw, bh, UI_RGBA(8, 8, 7, 245));
    ui_frame(x, y, bw, bh, C_ACCENT);
    for (u32 k = 0; k < n; k++) ui_text(x + 4, y + 3 + (f32)k * UI_LINE, k ? C_DIM : C_TEXT, lines[k]);
}

static void section(f32 x, f32 y, f32 w, const char *title)
{
    ui_text(x, y, C_ACCENT, title);
    ui_rect(x, y + 9, w, 1, C_EDGE);
}

static void draw_inspect(const Hud *h, const Layout *L, const HudWorld *w)
{
    LookData d;
    if (!look_data(h, w, &d)) return;
    char buf[96];
    f32 x = L->wx, y = L->wy;
    ui_rect(x, y, WIN_W, WIN_H, C_PANEL);
    ui_frame(x, y, WIN_W, WIN_H, C_EDGE);
    // Заголовок: знаки ушедших, имя, кто он
    ui_rect(x + 1, y + 1, WIN_W - 2, 13, UI_RGBA(30, 27, 22, 255));
    f32 tx = ui_text(x + 6, y + 4, C_DIM, "\xee\x80\x80\xee\x80\x82");
    tx = ui_text(tx + 6, y + 4, C_TEXT, d.name);
    ui_text(tx + 12, y + 4, C_DIM, d.role);
    bool hot = inside(L->mx, L->my, x + WIN_W - 14, y + 2, 12, 11);
    ui_text(x + WIN_W - 11, y + 4, hot ? C_TEXT : C_DIM, "×");

    // Тело
    f32 bx = x + 10, by = y + 20;
    section(bx, by, 160, "ТЕЛО");
    body_figure(bx + 2, by + 16, 3, d.hp, d.fracture, d.severed);
    f32 cx = bx + 46, cy = by + 16;
    ui_text(cx, cy, C_TEXT, d.state);
    snprintf(buf, sizeof buf, "КРОВЬ %.0f/%.0f", (f64)d.blood, (f64)d.blood_max);
    ui_text(cx, cy + 14, C_DIM, buf);
    bar(cx, cy + 23, 112, 2, d.blood / d.blood_max, d.blood < 0.6f * d.blood_max ? C_BAD : UI_RGBA(150, 44, 34, 255));
    snprintf(buf, sizeof buf, "РОСТ %.2f М", (f64)d.height);
    ui_text(cx, cy + 32, C_DIM, buf);
    snprintf(buf, sizeof buf, "ВЕС %.0f КГ", (f64)d.mass);
    ui_text(cx, cy + 42, C_DIM, buf);
    f32 ry = by + 90;
    for (u32 p = 0; p < PART_COUNT; p++, ry += 12) {
        f32 r = d.hp[p] / g_part_hp[p];
        bool cut = d.severed & (1u << p);
        ui_text(bx, ry, cut ? C_BAD : C_TEXT, g_part_name[p]);
        if (cut) snprintf(buf, sizeof buf, "ОТСЕЧЕНА");
        else snprintf(buf, sizeof buf, "%s%.0f/%.0f", (d.fracture & (1u << p)) ? "ПЕРЕЛОМ " : "", (f64)d.hp[p], (f64)g_part_hp[p]);
        text_right(bx + 160, ry, (d.fracture & (1u << p)) || cut ? C_BAD : C_DIM, buf);
        bar(bx, ry + 8, 160, 2, r, health_color(r));
    }

    // Навыки
    f32 kx = x + 186, ky = y + 20;
    section(kx, ky, 136, "НАВЫКИ");
    for (u32 s = 0; s < SK_COUNT; s++) {
        f32 yy = ky + 16 + (f32)s * 13;
        if (s == SK_WEAPON) section(kx, yy - 2, 136, "ОРУЖИЕ");
        if (s >= SK_WEAPON) yy += 12;
        ui_text(kx, yy, C_TEXT, g_skill_name[s]);
        snprintf(buf, sizeof buf, "%.0f", (f64)d.skill[s]);
        text_right(kx + 136, yy, C_TEXT, buf);
        bar(kx, yy + 8, 136, 2, d.skill[s] / 100.0f, C_EDGE);
    }

    // Снаряжение: 9 слотов и оружие
    section(L->sx, y + 20, 220, "СНАРЯЖЕНИЕ");
    i32 hover_slot = slot_under(L);
    for (u32 k = 0; k <= SLOT_COUNT; k++) {
        u8 sl = g_slot_order[k];
        f32 sx, sy;
        slot_rect(L, k, &sx, &sy);
        bool fits = h->held.kind && item_slot(h->held) == sl;
        u32 edge = fits ? C_SEL : (hover_slot == sl ? C_TEXT : C_EDGE);
        if (d.equip[sl].kind) {
            draw_item_cell(sx, sy, SLOT_W, SLOT_H, d.equip[sl], edge);
            ui_text(sx + 2, sy + SLOT_H - 8, C_DIM, g_slot_name[sl]);
        } else {
            ui_rect(sx, sy, SLOT_W, SLOT_H, C_CELL);
            ui_frame(sx, sy, SLOT_W, SLOT_H, edge);
            ui_text(sx + 2, sy + 7, UI_RGBA(80, 76, 66, 255), g_slot_name[sl]);
        }
    }

    // Вещи
    section(L->ix, L->iy - 12, 220, "ВЕЩИ");
    if (!d.sheet) {
        ui_text(L->ix, L->iy + 4, C_DIM, "ЧУЖОЕ. НЕ ТРОГАТЬ.");
    } else {
        const Inventory *inv = &d.sheet->inv;
        for (i32 gy = 0; gy < INV_H; gy++)
            for (i32 gx = 0; gx < INV_W; gx++) {
                ui_rect(L->ix + gx * CELL, L->iy + gy * CELL, CELL - 1, CELL - 1, C_CELL);
            }
        i32 hx, hy;
        bool in_cell = cell_under(L, &hx, &hy);
        if (h->held.kind && in_cell) {
            // Куда ляжет вещь с курсора
            u8 iw, ih;
            item_size(h->held, &iw, &ih);
            bool ok = inv_fits(inv, h->held, hx, hy);
            ui_rect(L->ix + hx * CELL, L->iy + hy * CELL, iw * CELL - 1, ih * CELL - 1,
                    ok ? UI_RGBA(60, 110, 66, 160) : UI_RGBA(120, 40, 30, 160));
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
        ui_text(nx, ny, C_DIM, "НОША");
        snprintf(buf, sizeof buf, "%.1f/%.0f КГ", (f64)load, (f64)cap);
        ui_text(nx, ny + 10, load > cap ? C_BAD : C_TEXT, buf);
        bar(nx, ny + 19, 60, 2, load / cap, load > cap ? C_BAD : C_ACCENT);
        if (load > cap) ui_text(nx, ny + 25, C_BAD, "ПЕРЕГРУЗ");
        ui_text(nx, ny + 52, C_DIM, "ЛКМ ВЗЯТЬ");
        ui_text(nx, ny + 62, C_DIM, "ПКМ НАДЕТЬ");
        ui_text(nx, ny + 72, C_DIM, "    СНЯТЬ");
    }

    // Подсказка о вещи под курсором
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

static void draw_card(const HudWorld *w, const Layout *L, u32 i)
{
    const Squad *sq = w->squad;
    const CharSheet *c = &sq->sheet[i];
    f32 x = card_x(i), y = L->card_y;
    bool hot = inside(L->mx, L->my, x, y, CARD_W, CARD_H);
    ui_rect(x, y, CARD_W, CARD_H, C_PANEL);
    ui_frame(x, y, CARD_W, CARD_H, sq->selected[i] ? C_SEL : (hot ? C_TEXT : C_EDGE));
    // Вместо портрета — фигура тела с ранами
    body_figure(x + 4, y + 5, 1, c->d.hp, c->d.fracture, c->d.severed);
    ui_text(x + 20, y + 5, sq->selected[i] ? C_SEL : C_TEXT, c->name);
    ui_text(x + 20, y + 15, C_DIM, move_state(&sq->body[i]));
    f32 r = vital_ratio(c->d.hp);
    f32 blood = c->d.blood / (100.0f * c->d.mass / 75.0f);
    bar(x + 20, y + 25, CARD_W - 26, 2, r, health_color(r));
    bar(x + 20, y + 28, CARD_W - 26, 1, blood, UI_RGBA(150, 44, 34, 255));
    char n[4];
    snprintf(n, sizeof n, "%u", i + 1);
    text_right(x + CARD_W - 4, y + 5, C_DIM, n);
}

void hud_draw(const Hud *h, const Input *in, const HudWorld *w)
{
    Layout L = layout(in);
    const Squad *sq = w->squad;

    // Имя над тем, кто под курсором
    u8 kind;
    u32 idx;
    V3 head;
    bool over_ui = (h->inspect && inside(L.mx, L.my, L.wx, L.wy, WIN_W, WIN_H)) || L.my >= L.card_y;
    if (!over_ui && !h->drag_live && pick_at(w, L.mx, L.my, &kind, &idx, &head)) {
        f32 sx, sy;
        if (project(w->view_proj, v3_add(head, v3(0, 0.3f, 0)), &sx, &sy)) {
            LookData d;
            Hud tmp = *h;
            tmp.look_kind = kind;
            tmp.look = idx;
            if (look_data(&tmp, w, &d)) {
                f32 tw = ui_text_width(d.name);
                f32 x = floorf(sx - tw * 0.5f), y = floorf(sy - 10);
                ui_rect(x - 3, y - 2, tw + 6, 11, C_SHADE);
                ui_text(x, y, kind == LOOK_SQUAD ? C_SEL : C_TEXT, d.name);
            }
        }
    }

    if (h->drag_live) {
        f32 x0 = fminf(h->drag_x, L.mx), y0 = fminf(h->drag_y, L.my);
        f32 bw = fabsf(L.mx - h->drag_x), bh = fabsf(L.my - h->drag_y);
        ui_rect(x0, y0, bw, bh, UI_RGBA(142, 214, 150, 28));
        ui_frame(x0, y0, bw, bh, C_SEL);
    }

    const char *hint = sq->formation ? "ЛКМ ВЫБОР, РАМКА  ПКМ ИДТИ  Z X C ШАГ ТРУСЦА БЕГ  G СТРОЙ: ДА  I ОСМОТР  F СЛЕДОВАТЬ"
                                      : "ЛКМ ВЫБОР, РАМКА  ПКМ ИДТИ  Z X C ШАГ ТРУСЦА БЕГ  G СТРОЙ: НЕТ  I ОСМОТР  F СЛЕДОВАТЬ";
    ui_rect(3, 3, ui_text_width(hint) + 6, 12, C_SHADE);
    ui_text(6, 6, C_DIM, hint);
    for (u32 i = 0; i < sq->count; i++) draw_card(w, &L, i);
    if (h->inspect) draw_inspect(h, &L, w);

    if (h->held.kind) {
        u8 iw, ih;
        item_size(h->held, &iw, &ih);
        draw_item_cell(L.mx - 4, L.my - 4, iw * CELL - 1, ih * CELL - 1, h->held, C_ACCENT);
    }
}
