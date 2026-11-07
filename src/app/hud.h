// HUD игры (docs/ui.md): счётчик к/с сверху по центру и меню настроек (Esc). Только интерфейс: мир не трогает
#pragma once
#include "settings.h"

// Числа кадра за прошлую секунду
struct HudStats {
    f64 fps, frame_ms, cpu_ms, gpu_ms, step_ms;
    const char *unfair;  // включённые отладочные режимы (docs/budget.md); пусто — замер честный
};

enum HudTab : u32 { HUD_TAB_SCREEN, HUD_TAB_UI, HUD_TAB_CONTROLS, HUD_TAB_DEBUG, HUD_TABS };

struct HudMenu {
    bool open = false;
    u32 tab = HUD_TAB_SCREEN;
    bool quit = false;     // нажата «Выйти из игры»
    bool changed = false;  // настройки менялись, пока меню открыто (записать при закрытии)
};

void hud_fps(const Settings &s, const HudStats &st);
// Меню поверх затемнённого кадра; меняет только настройки
void hud_menu(HudMenu &m, Settings &s);
