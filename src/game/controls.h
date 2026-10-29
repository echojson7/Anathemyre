// Управление: меню НАСТРОЙКИ слева вверху (бывшие F1–F10) и приказы отряду мышью и клавишами
#ifndef ANA_CONTROLS_H
#define ANA_CONTROLS_H

#include "../render/render.h"
#include "bench.h"
#include "camera.h"
#include "hud.h"
#include "world.h"

typedef struct Settings {
    RenderSettings rs;
    bool vsync, fullscreen;
    bool swap_late;          // показ кадра после логики следующего (без vsync)
    bool follow;             // камера следует за своим персонажем
    f32  ui_user, ui_user_text;  // ползунки размера интерфейса
    bool sound_ok, sound_on, music_on;
    f32  sound_vol, music_vol;
    // Просьбы меню на этот кадр
    bool travel, humans, bench;
} Settings;

// Меню НАСТРОЙКИ. *in — ввод для мира и интерфейса: если мышь занята меню, клики из него убраны
void menu_input(Hud *h, Input *in, Settings *s, World *w, RtsCamera *cam, bool humans_on, const FrameStats *st);
// ПКМ по чужому: выбранные атакуют его. ПКМ по земле: идут туда, бросив бой и погоню.
// Z шаг, X трусца, C бег — выбранным; G — держать строй; H — стоять: бросить приказ, но отбиваться.
// in — сырой ввод (курсор), hin — после меню
void orders_input(const Hud *h, const Input *in, const Input *hin, const HudWorld *hw, bool ui_busy, V3 eye, i32 ww, i32 wh,
                  World *w);

#endif
