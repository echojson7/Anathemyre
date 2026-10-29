#include "controls.h"

#include <stdio.h>
#include <string.h>

#include "../audio/audio.h"
#include "../core/log.h"
#include "../render/ui.h"
#include "figures.h"

void menu_input(Hud *h, Input *in, Settings *s, World *w, RtsCamera *cam, bool humans_on, const FrameStats *st)
{
    Input raw = *in;
    if (hud_debug_begin(h, &raw)) {
        char val[32];
        RenderSettings *rs = &s->rs;
        hud_debug_check(h, "ФИЛЬТР КАДРА", &rs->post);
        hud_debug_check(h, "ПИКСЕЛЬНОСТЬ", &rs->pixel);
        // Способ пиксельности: по глубине (дробные шаги или целые 1-2-4-8) или весь кадр в низком разрешении
        static const char *px_mode[] = { "ПИКСЕЛИ: ПО ГЛУБИНЕ", "ПИКСЕЛИ: ПО ГЛУБИНЕ 1-2-4-8", "ПИКСЕЛИ: КАДР 1/2",
                                         "ПИКСЕЛИ: КАДР 1/3" };
        u32 px_m = rs->pixel_div > 1 ? (u32)rs->pixel_div : rs->pixel_int ? 1u : 0u;
        if (hud_debug_button(h, px_mode[px_m])) {
            px_m = (px_m + 1) % 4;
            rs->pixel_int = px_m == 1;
            rs->pixel_div = px_m < 2 ? 1 : (i32)px_m;
        }
        hud_debug_check(h, "ШЕЙДИНГ БЛОКАМИ", &rs->vrs);
        hud_debug_check(h, "AO ВПОЛОВИНУ", &rs->ao_half);
        hud_debug_check(h, "ШУМ ИЗ ТЕКСТУРЫ", &rs->noise_tex);
        if (hud_debug_check(h, "VSYNC", &s->vsync)) {
            platform_set_vsync(s->vsync);
            log_info("VSYNC: %s", s->vsync ? "вкл" : "выкл");
        }
        if (hud_debug_check(h, "ВО ВЕСЬ ЭКРАН", &s->fullscreen)) platform_set_fullscreen(s->fullscreen);
        hud_debug_check(h, "SWAP ПОСЛЕ ЛОГИКИ", &s->swap_late);
        if (hud_debug_check(h, "СУТКИ ИДУТ", &w->day_run)) log_info("СУТКИ: %s", w->day_run ? "идут" : "остановлены");
        if (hud_debug_check(h, "КАМЕРА СЛЕДУЕТ", &s->follow) && s->follow) cam->dist = MIN(cam->dist, 8.0f);
        bool humans_want = humans_on;
        if (hud_debug_button(h, world_in_valley() ? "ПЕРЕХОД: В ПУСТОШЬ" : "ПЕРЕХОД: В ДОЛИНУ")) s->travel = true;
        if (hud_debug_check(h, "ЛЮДИ ДЛЯ СРАВНЕНИЯ", &humans_want)) {
            s->humans = true;
            if (humans_want) *cam = (RtsCamera){ .target = { 3.4f, 0, 22 }, .yaw = 0.0f, .pitch = 0.15f, .dist = 6.5f };
        }
        s->stand_moved |= hud_debug_check(h, "СТЕНД ФИГУР", &s->stand);
        static const char *stand_row[] = { "СТЕНД: МУЖЧИНА", "СТЕНД: ЖЕНЩИНА", "СТЕНД: ХУДОЙ", "СТЕНД: РОСТ" };
        if (s->stand && hud_debug_button(h, stand_row[s->stand_row % 4])) {
            s->stand_row = (s->stand_row + 1) % 4;
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
