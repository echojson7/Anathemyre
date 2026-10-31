#include "editor.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"
#include "../render/ui.h"

static const char *const g_body_name[BODY_KIND_COUNT] = { "МУЖЧИНА", "ЖЕНЩИНА", "ХУДОЙ" };

// Причёски по файлам сеток (g_hair_file)
static const char *cut_name(u32 body, u32 cut)
{
    if (!cut) return body == BODY_FEMALE ? "НЕТ" : "БРИТЫЙ";
    static const char *const file[] = { "crop", "shag", "tied", "tail", "bun", "braid", "buzz", "slick", "long", "bob", "loose", "side" };
    static const char *const name[] = { "СТРИЖКА", "ЛОХМЫ", "УЗЕЛ", "ХВОСТ", "ПУЧОК", "КОСА", "ЁЖИК", "ЗАЧЁС", "ДО ПЛЕЧ", "КАРЕ", "РАСПУЩЕННЫЕ", "ВИСКИ" };
    for (u32 k = 0; k < ARRAY_COUNT(file); k++)
        if (!strcmp(g_hair_file[body][cut - 1], file[k])) return name[k];
    return g_hair_file[body][cut - 1];
}

// Морфы: подпись и пределы ползунка (у генератора те же или уже)
static const struct {
    const char *name;
    f32 lo, hi;
} g_morph[LOOK_MORPHS] = {
    [LOOK_FAT] = { "ПОЛНОТА", 0.0f, 1.0f },     [LOOK_LEAN] = { "ХУДОБА", 0.0f, 1.0f },
    [LOOK_STRONG] = { "МЫШЦЫ", 0.0f, 1.0f },
    [LOOK_OLD] = { "ВОЗРАСТ", 0.0f, 1.0f },     [LOOK_NOSE] = { "НОС", -1.0f, 1.0f },
    [LOOK_EYES] = { "РАЗРЕЗ ГЛАЗ", -1.0f, 1.0f }, [LOOK_JAW] = { "ЧЕЛЮСТЬ", -0.6f, 0.5f },  // выше +50 не даём (пользователь 2026-10-30)
    [LOOK_LIPS] = { "ГУБЫ", -0.6f, 1.0f },  // ниже −0.6 губы расходятся
};
static const u8 g_body_morphs[] = { LOOK_FAT, LOOK_LEAN, LOOK_STRONG, LOOK_OLD };
static const u8 g_face_morphs[] = { LOOK_NOSE, LOOK_LIPS, LOOK_EYES, LOOK_JAW };

void editor_init(Editor *e)
{
    memset(e, 0, sizeof *e);
    // Справа, под часами: окно персонажа слева
    e->f = (HudForm){ .x = 1e6f, .y = 48, .w = 236, .slider = -1 };
    e->who = -1;
}

// Кого правят: свой из отряда или прохожий на поле (слот поля — номер в таблице людей)
static i32 target(const Hud *h, const World *w)
{
    if (h->look_kind == LOOK_SQUAD && h->look < w->squad.count) return (i32)w->squad.member[h->look];
    if (h->look_kind == LOOK_FIELD && h->look < w->people->count) return (i32)h->look;
    return -1;
}

static void remember(Editor *e, const People *p, u32 i)
{
    e->lk = p->lk[i];
    e->lk_own = p->lk_own[i];
    e->variant = p->variant[i];
    e->height = p->height[i];
    e->width = p->width[i];
    e->sheet_h = p->sheet[i].d.height;
}

// Рост — настоящий: лист и поле боя тоже (досягаемость в бою)
static void set_height(World *w, u32 i, f32 hs, f32 sheet_h)
{
    People *p = w->people;
    p->height[i] = hs;
    p->sheet[i].d.height = sheet_h;
    i32 slot = p->body[i].slot;
    if (slot >= 0 && (u32)slot < w->field->w.count) w->field->w.height[slot] = sheet_h;
}

// Линейный цвет в образец интерфейса
static u32 swatch(V3 c)
{
    return UI_RGBA((u32)(255.0f * powf(clampf(c.x, 0, 1), 1.0f / 2.2f)), (u32)(255.0f * powf(clampf(c.y, 0, 1), 1.0f / 2.2f)),
                   (u32)(255.0f * powf(clampf(c.z, 0, 1), 1.0f / 2.2f)), 255);
}

static void widgets(Editor *e, World *w, u32 i)
{
    People *p = w->people;
    CharLook *l = &p->lk[i];
    HudForm *f = &e->f;
    char val[32];
    bool changed = false;

    hud_form_portrait(f, HUD_PORTRAIT_FOCUS, HUD_PORTRAIT_FOCUS_PX);

    hud_form_head(f, "ТЕЛО");
    i32 st = hud_form_step(f, "ТИП ТЕЛА", g_body_name[p->variant[i] % BODY_KIND_COUNT]);
    if (st) {
        p->variant[i] = (u8)((p->variant[i] + BODY_KIND_COUNT + (u32)st) % BODY_KIND_COUNT);
        l->cut = (u8)MIN((u32)l->cut, look_cuts(p->variant[i]));
        changed = true;
    }
    f32 hs = p->height[i] > 0.0f ? p->height[i] : 1.0f;
    snprintf(val, sizeof val, "%.0f СМ", (f64)(180.0f * hs));
    // Не выше 194 см (решение пользователя 2026-10-30)
    if (hud_form_slider(f, "РОСТ", &hs, 0.83f, 1.94f / 1.8f, val)) set_height(w, i, hs, 1.8f * hs);
    f32 ws = p->width[i] > 0.0f ? p->width[i] : 1.0f;
    snprintf(val, sizeof val, "%.0f%%", (f64)(100.0f * ws));
    // Шире ±7% тело уже не похоже на человека: плечи и таз раздувает вместе с головой
    if (hud_form_slider(f, "ШИРИНА", &ws, 0.93f, 1.07f, val)) p->width[i] = ws;
    for (u32 k = 0; k < ARRAY_COUNT(g_body_morphs); k++) {
        u32 m = g_body_morphs[k];
        snprintf(val, sizeof val, "%.0f%%", (f64)(100.0f * l->morph[m]));
        changed |= hud_form_slider(f, g_morph[m].name, &l->morph[m], g_morph[m].lo, g_morph[m].hi, val);
    }

    hud_form_head(f, "ЛИЦО");
    for (u32 k = 0; k < ARRAY_COUNT(g_face_morphs); k++) {
        u32 m = g_face_morphs[k];
        snprintf(val, sizeof val, "%+.0f", (f64)(100.0f * l->morph[m]));
        changed |= hud_form_slider(f, g_morph[m].name, &l->morph[m], g_morph[m].lo, g_morph[m].hi, val);
    }

    hud_form_head(f, "ЦВЕТ");
    changed |= hud_form_slider(f, "КОЖА", &l->skin_t, 0.0f, 1.0f, "");
    hud_form_swatch(f, swatch(l->skin));
    changed |= hud_form_slider(f, "ВОЛОСЫ", &l->hair_t, 0.0f, 1.0f, "");
    hud_form_swatch(f, swatch(l->hair));
    snprintf(val, sizeof val, "%.0f%%", (f64)(100.0f * l->gray));
    changed |= hud_form_slider(f, "СЕДИНА", &l->gray, 0.0f, 1.0f, val);

    hud_form_head(f, "ВОЛОСЫ");
    u32 nc = look_cuts(p->variant[i]);
    st = hud_form_step(f, "ПРИЧЁСКА", cut_name(p->variant[i], l->cut));
    if (st) {
        l->cut = (u8)((l->cut + nc + 1 + (u32)st) % (nc + 1));
        changed = true;
    }
    bool bucket = false;
    for (u32 g = 0; g < p->ngear[i]; g++) bucket |= p->gear[i][g] < GEAR_CAP && (g_gear_hides[p->gear[i][g]] & GEAR_HIDES_HAIR);
    if (bucket) hud_form_text(f, "ПОД КОЖУХОМ НЕ ВИДНО", "");

    f->row += 4;
    if (hud_form_button(f, "СЛУЧАЙНО")) {
        const CharSheet *cs = &p->sheet[i];
        bool sheet = cs->d.mass > 0.0f && cs->d.height > 0.0f;
        look_make(l, p->variant[i], p->look_seed[i] ^ (++e->rolls * 0x9E3779B9u), sheet ? cs->d.mass : 72.0f,
                  sheet ? cs->d.height : 1.8f, sheet ? cs->d.skill[SK_STRENGTH] : 30.0f);
        changed = true;
    }
    if (hud_form_button(f, "ВЕРНУТЬ КАК БЫЛО")) {
        *l = e->lk;
        p->variant[i] = e->variant;
        p->width[i] = e->width;
        set_height(w, i, e->height, e->sheet_h);
        people_relook(p, i);
        p->lk_own[i] = e->lk_own;
        log_info("ВНЕШНОСТЬ: %s. Возвращена прежняя.", p->sheet[i].name ? p->sheet[i].name : "безымянный");
        return;
    }
    if (!changed) return;
    look_paint(l);
    p->lk_own[i] = true;
    people_relook(p, i);
}

void editor_input(Editor *e, Hud *h, Input *in, World *w, RtsCamera *cam, bool humans_on)
{
    bool was = e->f.open;
    e->f.open = h->win_look;
    i32 who = target(h, w);
    if (who != e->who || (e->f.open && !was)) {
        e->who = who;
        if (who >= 0) remember(e, w->people, (u32)who);
    }
    // Камера вплотную, лицом к лицу
    if (e->f.open && !was) {
        camera_close(true);
        cam->dist = 3.0f;
        cam->pitch = 0.12f;
        if (who >= 0) cam->yaw = w->people->body[who].heading;
    }
    Input raw = *in;
    const char *name = who >= 0 && w->people->sheet[who].name ? w->people->sheet[who].name : "";
    if (hud_form_begin(&e->f, &raw, "ВНЕШНОСТЬ", name)) {
        if (who >= 0) widgets(e, w, (u32)who);
        else hud_form_text(&e->f, "ВЫБЕРИ СВОЕГО ИЛИ ПРОХОЖЕГО", "");
    }
    if (hud_form_end(&e->f)) {
        memset(in->mouse_pressed, 0, sizeof in->mouse_pressed);
        memset(in->mouse_down, 0, sizeof in->mouse_down);
    }
    if (was && !e->f.open) camera_close(humans_on);
    h->win_look = e->f.open;
}

i32 editor_follow(const Editor *e) { return e->f.open ? e->who : -1; }
