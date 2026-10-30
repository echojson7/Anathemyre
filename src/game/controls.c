#include "controls.h"

#include <stdio.h>
#include <string.h>

#include "../audio/audio.h"
#include "../core/log.h"
#include "../render/ui.h"
#include "../world/map.h"
#include "figures.h"
#include "ostov.h"
#include "showhall.h"

const char *const g_stage_name[STAGE_COUNT] = {
    "ПУСТО", "ТЕНИ", "НЕБО И ТУМАН", "ЛУЧИ", "AO", "ПИКСЕЛЬНОСТЬ", "ФИЛЬТР", "РАСТЕНИЯ", "ШАГ МИРА", "ЛЮДИ", "СУТКИ И ОГНИ", "ПОЛНАЯ",
};

const char *const g_light_preset_name[LIGHT_PRESET_COUNT] = { "ПРЕЖНИЙ", "БЫСТРЫЙ", "РЕАЛИСТИЧНЫЙ" };

// Части СВЕТА в наборе: материалы, кривая, автоэкспозиция, свечение
static const u8 light_preset_parts[LIGHT_PRESET_COUNT][4] = {
    { LIGHT_MAT_OLD, LIGHT_CURVE_ACES, 0, 0 },
    { LIGHT_MAT_GGX, LIGHT_CURVE_NEUTRAL, 0, 0 },
    { LIGHT_MAT_GGX, LIGHT_CURVE_AGX, 1, 1 },
};

void light_preset_apply(RenderSettings *rs, LightPreset p)
{
    const u8 *v = light_preset_parts[p];
    rs->light_mat = v[0];
    rs->light_curve = v[1];
    rs->light_expo = v[2];
    rs->light_bloom = v[3];
}

LightPreset light_preset_of(const RenderSettings *rs)
{
    for (u32 p = 0; p < LIGHT_PRESET_COUNT; p++) {
        const u8 *v = light_preset_parts[p];
        if (rs->light_mat == v[0] && rs->light_curve == v[1] && rs->light_expo == v[2] && rs->light_bloom == v[3])
            return (LightPreset)p;
    }
    return LIGHT_PRESET_COUNT;
}

// Раздел СВЕТ: набор целиком или каждая часть отдельно; рядом то, что нужно для сравнения кадров
static void menu_light(Hud *h, Settings *s, World *w, RtsCamera *cam, const FrameStats *st)
{
    static const char *mat_name[LIGHT_MAT_COUNT] = { "СТАРЫЕ", "GGX" };
    static const char *curve_name[LIGHT_CURVE_COUNT] = { "ACES", "AGX", "PBR NEUTRAL" };
    RenderSettings *rs = &s->rs;
    HudForm *f = &h->dbg.f;
    char val[32];
    LightPreset p = light_preset_of(rs);
    i32 d = hud_form_step(f, "НАБОР", p < LIGHT_PRESET_COUNT ? g_light_preset_name[p] : "СВОЙ");
    if (d) {
        // Из своего набора шаг ведёт к ближайшему краю списка
        u32 cur = p < LIGHT_PRESET_COUNT ? p : (d > 0 ? LIGHT_PRESET_COUNT - 1 : 0);
        p = (LightPreset)((cur + LIGHT_PRESET_COUNT + (u32)d) % LIGHT_PRESET_COUNT);
        light_preset_apply(rs, p);
        log_info("СВЕТ: набор %s", g_light_preset_name[p]);
    }
    hud_form_head(f, "ПОВЕРХНОСТИ");
    if ((d = hud_form_step(f, "МАТЕРИАЛЫ", mat_name[rs->light_mat])))
        rs->light_mat = (u8)((rs->light_mat + LIGHT_MAT_COUNT + (u32)d) % LIGHT_MAT_COUNT);
    hud_form_head(f, "КАДР");
    if ((d = hud_form_step(f, "КРИВАЯ", curve_name[rs->light_curve])))
        rs->light_curve = (u8)((rs->light_curve + LIGHT_CURVE_COUNT + (u32)d) % LIGHT_CURVE_COUNT);
    hud_form_check(f, "ЭКСПОЗИЦИЯ АВТО", &rs->light_expo);
    hud_form_check(f, "СВЕЧЕНИЕ (BLOOM)", &rs->light_bloom);
    hud_form_head(f, "СРАВНЕНИЕ");
    if (hud_form_check(f, "СУТКИ ИДУТ", &w->day_run)) log_info("СУТКИ: %s", w->day_run ? "идут" : "остановлены");
    u32 mins = (u32)(w->tod * 24.0f * 60.0f);
    snprintf(val, sizeof val, "%02u:%02u", mins / 60 % 24, mins % 60);
    hud_form_slider(f, "ВРЕМЯ СУТОК", &w->tod, 0.0f, 0.999f, val);
    hud_form_check(f, "ПИКСЕЛЬНОСТЬ", &rs->pixel);
    if (hud_form_button(f, "КАМЕРА: В ОСТОВ")) *cam = OSTOV_CAM;
    if (hud_form_button(f, "КАМЕРА: В МЕРИЛЬНЮ")) *cam = SHOWHALL_CAM;
    snprintf(val, sizeof val, "%.2f МС  %.0f К/С", (f64)st->shown_ms, (f64)st->shown_fps);
    hud_form_text(f, "КАДР", val);
    if (hud_form_button(f, "ЗАМЕР ПО ЭТАПАМ")) s->bench_stages = true;
}

RenderSettings stage_render(const Settings *s)
{
    RenderSettings rs = s->rs;
    rs.shadows = s->stage >= STAGE_SHADOWS;
    rs.atmos = s->stage >= STAGE_SKY;
    rs.rays = s->stage >= STAGE_RAYS;
    rs.ao = s->stage >= STAGE_AO;
    rs.pixel &= s->stage >= STAGE_PIXEL;
    rs.post &= s->stage >= STAGE_POST;
    rs.flora = s->stage >= STAGE_FLORA;
    return rs;
}

// Раздел ОТЛАДКА: этапы сцены, проходы, окно, мир, звук
static void menu_debug(Hud *h, Settings *s, World *w, RtsCamera *cam, bool humans_on, const FrameStats *st)
{
    char val[32];
    RenderSettings *rs = &s->rs;
    // Этап сцены: < назад, > вперёд, от ПУСТО до ПОЛНОЙ (docs/performance-stages.md)
    snprintf(val, sizeof val, "%u %s", s->stage, g_stage_name[s->stage]);
    i32 st_step = hud_form_step(&h->dbg.f, "ЭТАП СЦЕНЫ", val);
    if (st_step) {
        s->stage = (s->stage + STAGE_COUNT + (u32)st_step) % STAGE_COUNT;
        log_info("ЭТАП СЦЕНЫ: %u %s", s->stage, g_stage_name[s->stage]);
    }
    if (hud_debug_button(h, "ЗАМЕР ПО ЭТАПАМ")) s->bench_stages = true;
    hud_debug_check(h, "ФИЛЬТР КАДРА", &rs->post);
    hud_debug_check(h, "ПИКСЕЛЬНОСТЬ", &rs->pixel);
    hud_debug_check(h, "ШЕЙДИНГ БЛОКАМИ", &rs->vrs);
    hud_debug_check(h, "AO ВПОЛОВИНУ", &rs->ao_half);
    hud_debug_check(h, "ШУМ ИЗ ТЕКСТУРЫ", &rs->noise_tex);
    if (hud_debug_check(h, "ЛЮДИ ОДНИМ ВЫЗОВОМ", &rs->mdi)) log_info("ЛЮДИ ОДНИМ ВЫЗОВОМ: %s", rs->mdi ? "вкл" : "выкл");
    if (hud_debug_check(h, "ПОЗЫ РАЗ ЗА КАДР", &rs->skin_cache)) log_info("ПОЗЫ РАЗ ЗА КАДР: %s", rs->skin_cache ? "вкл" : "выкл");
    if (hud_debug_check(h, "VSYNC", &s->vsync)) {
        platform_set_vsync(s->vsync);
        log_info("VSYNC: %s", s->vsync ? "вкл" : "выкл");
    }
    if (hud_debug_check(h, "ВО ВЕСЬ ЭКРАН", &s->fullscreen)) platform_set_fullscreen(s->fullscreen);
    hud_debug_check(h, "SWAP ПОСЛЕ ЛОГИКИ", &s->swap_late);
    if (hud_debug_check(h, "СУТКИ ИДУТ", &w->day_run)) log_info("СУТКИ: %s", w->day_run ? "идут" : "остановлены");
    if (hud_debug_check(h, "КАМЕРА СЛЕДУЕТ", &s->follow) && s->follow) cam->dist = MIN(cam->dist, 8.0f);
    bool humans_want = humans_on;
    if (hud_debug_button(h, world_squad_in_valley(w) ? "ПЕРЕХОД: В ЛАГЕРЬ" : "ПЕРЕХОД: В ДОЛИНУ")) s->travel = true;
    if (hud_debug_button(h, "КАМЕРА: В ОСТОВ")) *cam = OSTOV_CAM;
    if (hud_debug_button(h, "КАМЕРА: В МЕРИЛЬНЮ")) *cam = SHOWHALL_CAM;
    if (hud_debug_button(h, "ОТРЯДЫ ВДАЛИ: ПУСТИТЬ ПРОБНЫЕ")) s->bands_test = true;
    if (hud_debug_button(h, "МИР: СОХРАНИТЬ")) s->save = true;
    if (hud_debug_button(h, "МИР: ЗАГРУЗИТЬ")) s->load = true;
    snprintf(val, sizeof val, "%llu", (unsigned long long)w->chron->total);
    hud_debug_text(h, "СОБЫТИЙ В ЖУРНАЛЕ МИРА", val);
    if (hud_debug_check(h, "ЛЮДИ ДЛЯ СРАВНЕНИЯ", &humans_want)) {
        s->humans = true;
        if (humans_want) *cam = (RtsCamera){ .target = { 3.4f, 0, 22 }, .yaw = 0.0f, .pitch = 0.15f, .dist = 6.5f };
    }
    s->stand_moved |= hud_debug_check(h, "СТЕНД ФИГУР", &s->stand);
    static const char *stand_row[] = { "СТЕНД: МУЖЧИНА", "СТЕНД: ЖЕНЩИНА", "СТЕНД: ХУДОЙ", "СТЕНД: РОСТ", "СТЕНД: ПРИЧЁСКИ" };
    if (s->stand && hud_debug_button(h, stand_row[s->stand_row % 5])) {
        s->stand_row = (s->stand_row + 1) % 5;
        s->stand_moved = true;
    }
    u32 mins = (u32)(w->tod * 24.0f * 60.0f);
    snprintf(val, sizeof val, "%02u:%02u", mins / 60 % 24, mins % 60);
    hud_debug_slider(h, "ВРЕМЯ СУТОК", &w->tod, 0.0f, 0.999f, val);
    static const char *speed_name[] = { "ПАУЗА", "x1", "x2", "x5" };
    f32 sp = (f32)h->speed;
    if (hud_debug_slider(h, "СКОРОСТЬ", &sp, 0.0f, 3.0f, speed_name[h->speed])) {
        h->speed = (u8)(sp + 0.5f);
        if (h->speed) h->speed_before = h->speed;
    }
    snprintf(val, sizeof val, "X%.2f", (f64)ui_scale());
    hud_debug_slider(h, "МАСШТАБ ИНТЕРФЕЙСА", &s->ui_user, 0.5f, 1.5f, val);
    snprintf(val, sizeof val, "X%.2f", (f64)ui_text_scale());
    hud_debug_slider(h, "РАЗМЕР ТЕКСТА", &s->ui_user_text, 0.5f, 1.5f, val);
    snprintf(val, sizeof val, "%.2f МС  %.0f К/С", (f64)st->shown_ms, (f64)st->shown_fps);
    hud_debug_text(h, "КАДР", val);
    hud_debug_check(h, "СЧЁТЧИК КАДРОВ", &h->dbg.fps_on);
    if (hud_debug_button(h, "ПЕРЕЗАГРУЗИТЬ ШЕЙДЕРЫ")) {
        render_reload_shaders();
        figures_portraits_reset();
    }
    if (hud_debug_button(h, "ЗАМЕР КАДРА")) s->bench = true;
    hud_debug_check(h, "ЖУРНАЛ", &h->dbg.journal);
    bool snd = s->sound_on;
    if (hud_debug_check(h, "ЗВУК", &snd) && s->sound_ok) {
        if (snd) audio_start();
        s->sound_on = snd;
        audio_volume(s->sound_vol, s->sound_on);
    }
    snprintf(val, sizeof val, "%.0f%%", (f64)(s->sound_vol * 100.0f));
    if (hud_debug_slider(h, "ГРОМКОСТЬ", &s->sound_vol, 0.0f, 1.0f, val)) audio_volume(s->sound_vol, s->sound_on);
    if (hud_debug_check(h, "МУЗЫКА", &s->music_on)) audio_music(s->music_vol, s->music_on);
    snprintf(val, sizeof val, "%.0f%%", (f64)(s->music_vol * 100.0f));
    if (hud_debug_slider(h, "ГРОМКОСТЬ МУЗЫКИ", &s->music_vol, 0.0f, 1.0f, val)) audio_music(s->music_vol, s->music_on);
}

void menu_input(Hud *h, Input *in, Settings *s, World *w, RtsCamera *cam, bool humans_on, const FrameStats *st)
{
    Input raw = *in;
    if (hud_debug_begin(h, &raw)) {
        // Разделы: отладка и группа СВЕТ
        static const char *tab_name[] = { "ОТЛАДКА", "СВЕТ" };
        i32 tab_step = hud_form_step(&h->dbg.f, "РАЗДЕЛ", tab_name[h->dbg.tab]);
        if (tab_step) h->dbg.tab = (u8)((h->dbg.tab + 2u + (u32)tab_step) % 2u);
        if (h->dbg.tab == 1) menu_light(h, s, w, cam, st);
        else menu_debug(h, s, w, cam, humans_on, st);
    }
    if (hud_debug_end(h)) {
        memset(in->mouse_pressed, 0, sizeof in->mouse_pressed);
        memset(in->mouse_down, 0, sizeof in->mouse_down);
    }
}

void orders_input(const Hud *h, const Input *in, const Input *hin, const HudWorld *hw, bool ui_busy, V3 eye, i32 ww, i32 wh,
                  World *w)
{
    Squad *sq = &w->squad;
    Field *fd = w->field;
    if (hin->mouse_pressed[MOUSE_RIGHT] && !ui_busy && ww > 0 && wh > 0) {
        u8 pk;
        u32 pi;
        bool on_body = hud_pick(h, hin, hw, &pk, &pi);
        bool any = false;
        for (u32 i = 0; i < sq->count; i++) any |= sq->selected[i];
        if (on_body && pk == LOOK_FIELD) {
            for (u32 i = 0; i < sq->count; i++)
                if (sq->selected[i]) field_attack(fd, (u32)squad_body(sq, i)->slot, pi);
            if (any) audio_play_ui(SFX_UI_ORDER, 1.0f);
        } else if (!on_body || pk != LOOK_SQUAD) {
            f32 nx = 2.0f * ((f32)in->mouse_x + 0.5f) / (f32)ww - 1.0f;
            f32 ny = 1.0f - 2.0f * ((f32)in->mouse_y + 0.5f) / (f32)wh;
            V3 hit;
            if (camera_pick_ground(hw->view_proj, eye, nx, ny, world_ground, &hit)) {
                for (u32 i = 0; i < sq->count; i++)
                    if (sq->selected[i]) field_cancel(fd, (u32)squad_body(sq, i)->slot, true);
                // За край карты не уйти
                hit.x = clampf(hit.x, -MAP_HALF + 20.0f, MAP_HALF - 20.0f);
                hit.z = clampf(hit.z, -MAP_HALF + 20.0f, MAP_HALF - 20.0f);
                squad_order_move(sq, hit);
                if (any) audio_play_ui(SFX_UI_TICK, 1.0f);
            }
        }
    }
    if (hud_window_open(h)) return;
    if (in->key_pressed[KEY_H])
        for (u32 i = 0; i < sq->count; i++) {
            Player *p = squad_body(sq, i);
            if (!sq->selected[i]) continue;
            field_cancel(fd, (u32)p->slot, false);
            if (p->ctrl == CTRL_FREE) {
                p->target = p->pos;
                p->moving = false;
            }
        }
    if (in->key_pressed[KEY_Z]) squad_set_gait(sq, GAIT_WALK);
    if (in->key_pressed[KEY_X]) squad_set_gait(sq, GAIT_JOG);
    if (in->key_pressed[KEY_C]) squad_set_gait(sq, GAIT_RUN);
    if (in->key_pressed[KEY_G]) sq->formation = !sq->formation;
}
