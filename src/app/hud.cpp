// HUD: счётчик к/с и меню настроек в стиле панелей без рамок (docs/ui.md)
#include "hud.h"
#include "../ui/ui.h"
#include <math.h>
#include <stdio.h>

void hud_fps(const Settings &s, const HudStats &st) {
    if (s.fps == FPS_OFF) return;
    char n[16], u[48];
    snprintf(n, sizeof n, "%.0f", st.fps);
    snprintf(u, sizeof u, "к/с  ·  %.1f мс", st.frame_ms);
    f32 gap = ui_px(8);
    f32 wn = ui_text_width(UI_TITLE, n), wu = ui_text_width(UI_SMALL, u);
    f32 lt = ui_line(UI_TITLE), ls = ui_line(UI_SMALL);
    char full[96] = "";
    if (s.fps == FPS_FULL)
        snprintf(full, sizeof full, "CPU %.2f  ·  GPU %.2f  ·  шаг %.2f мс", st.cpu_ms, st.gpu_ms, st.step_ms);
    f32 wf = s.fps == FPS_FULL ? ui_text_width(UI_SMALL, full) : 0;
    f32 w = wn + gap + wu;
    if (wf > w) w = wf;
    f32 cx = ui_width() * 0.5f, y = ui_px(14);
    // Отладочный режим — строкой под счётчиком: такие к/с с бюджетом не сравнивают (docs/budget.md)
    char warn[192] = "";
    if (st.unfair && st.unfair[0]) snprintf(warn, sizeof warn, "не для замера: %s", st.unfair);
    f32 ww = warn[0] ? ui_text_width(UI_SMALL, warn) : 0;
    if (ww > w) w = ww;
    f32 h = lt + (s.fps == FPS_FULL ? ls + ui_px(2) : 0) + (warn[0] ? ls + ui_px(2) : 0);
    f32 px = ui_px(22), py = ui_px(8);
    // Подложка едва видна: цифры читаются и на ярком небе
    ui_soft(cx - w * 0.5f - px, y - py, w + 2 * px, h + 2 * py, ui_rgba(12, 13, 14, 120), ui_px(10));
    f32 x = cx - (wn + gap + wu) * 0.5f;
    ui_text(x, y, UI_TITLE, ui_col::TEXT, n);
    // Подпись — по линии букв числа
    f32 base = y + ui_cap(UI_TITLE) + (lt - ui_cap(UI_TITLE)) * 0.5f;
    ui_text(x + wn + gap, base - ui_cap(UI_SMALL) - (ls - ui_cap(UI_SMALL)) * 0.5f, UI_SMALL, ui_col::DIM, u);
    f32 yl = y + lt + ui_px(2);
    if (s.fps == FPS_FULL) {
        ui_text_center(cx, yl, UI_SMALL, ui_col::DIM, full);
        yl += ls + ui_px(2);
    }
    if (warn[0]) ui_text_center(cx, yl, UI_SMALL, ui_col::WARN, warn);
}

static const char *const TABS[HUD_TABS] = {"ЭКРАН", "ИНТЕРФЕЙС", "УПРАВЛЕНИЕ", "ОТЛАДКА"};
static const char *const FPS_NAMES[FPS_MODES] = {"выкл", "к/с", "подробно"};
static const char *const QUALITY_NAMES[] = {"низко", "средне", "высоко", "ультра"};

static void tab_screen(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "ОКНО");
    ch |= ui_toggle(f, "Весь экран", s.fullscreen);
    ch |= ui_toggle(f, "Вертикальная синхронизация", s.vsync);
    ui_section(f, "КАМЕРА");
    ch |= ui_slider(f, "Поле зрения", s.fov_deg, 40.0f, 100.0f, 1.0f, "%.0f°");
    // Ступени света (docs/light.md, «Ступени»): каждая — тот же облик, ниже — дешевле. Шаги этапа 6 добавляют свои
    ui_section(f, "ГРАФИКА");
    ch |= ui_choice(f, "Качество света", s.light_quality, QUALITY_NAMES, ANA_COUNTOF(QUALITY_NAMES));
}

static void tab_ui(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "РАЗМЕР");
    ch |= ui_slider(f, "Масштаб интерфейса", s.ui_scale, 0.75f, 2.0f, 0.05f, "%.2f×");
    ch |= ui_slider(f, "Размер текста", s.text_scale, 0.75f, 1.5f, 0.05f, "%.2f×");
    ui_section(f, "НА ЭКРАНЕ");
    ch |= ui_choice(f, "Счётчик к/с", s.fps, FPS_NAMES, FPS_MODES);
}

static void keys_column(UiForm &f, const char *title, const char *const (*rows)[2], u32 n) {
    ui_section(f, title);
    for (u32 i = 0; i < n; ++i) ui_key_row(f, rows[i][0], rows[i][1]);
}

static void tab_controls(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "МЫШЬ");
    ch |= ui_slider(f, "Чувствительность", s.mouse_sens, 0.2f, 4.0f, 0.05f, "%.2f×");
    ch |= ui_toggle(f, "Инвертировать по вертикали", s.invert_y);
    // Справка по клавишам — два столбца
    static const char *const CAM[][2] = {
        {"ПКМ", "вращать"}, {"СКМ", "двигать"}, {"Колесо", "ближе, дальше"}, {"W A S D", "двигать"},
        {"Shift", "быстрее"}, {"F", "свободный полёт"}, {"Q  E", "вниз, вверх"}};
    static const char *const WORLD[][2] = {{"ЛКМ", "притянуть точки"}, {"Ctrl ЛКМ", "вспышка"}, {"R", "отпустить"}};
    static const char *const MAIN[][2] = {{"Esc", "настройки"}, {"F11", "весь экран"}, {"F3", "панель отладки"}};
    static const char *const DBG[][2] = {{"Space", "пауза"}, {".", "шаг на паузе"}, {"−  =", "скорость мира"},
                                          {"[  ]", "час назад, вперёд"}, {"T", "ход суток"}, {"L", "отрезки"},
                                          {"P", "пиксельное правило"}, {"B", "стенд: согнуть кость"}};
    f32 gap = ui_px(28), cw = (f.w - gap) * 0.5f;
    UiForm l = f, r = f;
    l.w = r.w = cw;
    r.x = f.x + cw + gap;
    keys_column(l, "КАМЕРА", CAM, ANA_COUNTOF(CAM));
    keys_column(l, "МИР", WORLD, ANA_COUNTOF(WORLD));
    keys_column(r, "ОБЩЕЕ", MAIN, ANA_COUNTOF(MAIN));
    if (s.debug) keys_column(r, "ОТЛАДКА", DBG, ANA_COUNTOF(DBG));
}

static void tab_debug(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "ОТЛАДКА");
    ch |= ui_toggle(f, "Отладочные клавиши и панель", s.debug);
    if (!s.debug) {
        ui_text(f.x, f.y + ui_px(6), UI_SMALL, ui_col::FAINT, "Выключено: клавиши отладки и F3 не работают");
        return;
    }
    ch |= ui_toggle(f, "Панель отладки  (F3)", s.debug_panel);
    ui_section(f, "ВИД");
    ch |= ui_toggle(f, "Отрезки и точки  (L)", s.lines);
    ch |= ui_toggle(f, "Пиксельное правило  (P)", s.pixel);
}

void hud_menu(HudMenu &m, Settings &s) {
    if (!m.open) return;
    f32 W = ui_width(), H = ui_height();
    ui_rect(0, 0, W, H, ui_col::SHADE);
    ui_block(0, 0, W, H);  // под меню мир мышь не получает
    f32 pw = ui_px(700), ph = ui_px(700), pad = ui_px(34);
    if (pw > W - ui_px(20)) pw = W - ui_px(20);
    f32 x = floorf((W - pw) * 0.5f), y = floorf((H - ph) * 0.5f);
    ui_soft(x, y, pw, ph, ui_col::PANEL, ui_px(14));

    f32 cx = x + pad, cw = pw - 2 * pad, cy = y + pad;
    ui_text(cx, cy, UI_TITLE, ui_col::TEXT, "НАСТРОЙКИ", ui_px(2));
    cy += ui_line(UI_TITLE) + ui_px(18);
    ui_tabs(cx, cy, cw, TABS, HUD_TABS, m.tab);
    cy += ui_line(UI_SMALL) + ui_px(10);
    ui_hline(cx, cy, cw, ui_col::LINE);
    cy += ui_px(8);

    UiForm f = ui_form(cx, cy, cw);
    bool ch = false;
    switch (m.tab) {
    case HUD_TAB_SCREEN: tab_screen(f, s, ch); break;
    case HUD_TAB_UI: tab_ui(f, s, ch); break;
    case HUD_TAB_CONTROLS: tab_controls(f, s, ch); break;
    default: tab_debug(f, s, ch); break;
    }
    m.changed |= ch;

    // Низ: выход слева, «Продолжить» справа
    f32 bh = ui_px(36), by = y + ph - pad - bh;
    ui_hline(cx, by - ui_px(18), cw, ui_col::LINE);
    f32 bw = ui_px(180);
    if (ui_button(cx, by, bw, bh, "Выйти из игры")) m.quit = true;
    if (ui_button(cx + cw - bw, by, bw, bh, "Продолжить")) m.open = false;
    ui_text_right(cx + cw - bw - ui_px(16), by + (bh - ui_line(UI_SMALL)) * 0.5f, UI_SMALL, ui_col::FAINT, "Esc");
}
