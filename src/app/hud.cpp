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
    f32 px = ui_px(16), py = ui_px(6);
    // Подложка едва видна: цифры читаются и на ярком небе
    ui_round(cx - w * 0.5f - px, y - py, w + 2 * px, h + 2 * py, ui_rgba(12, 13, 14, 150), ui_px(8));
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

static const char *const TABS[HUD_TABS] = {"Графика", "Управление", "Интерфейс"};
static const char *const FPS_NAMES[FPS_MODES] = {"выкл", "к/с", "подробно"};
static const char *const QUALITY_NAMES[] = {"Низко", "Средне", "Высоко", "Ультра"};
static const char *const AA_NAMES[] = {"выкл", "FXAA", "TAA"};

static void tab_screen(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "ОКНО");
    ui_tip(f, "Окно на весь экран; то же — клавиша F11");
    ch |= ui_toggle(f, "Весь экран", s.fullscreen);
    ui_tip(f, "Кадр ждёт обновления экрана: без разрывов, к/с не выше частоты экрана");
    ch |= ui_toggle(f, "Вертикальная синхронизация", s.vsync);
    // Ступени света (docs/light.md, «Ступени»): каждая — тот же облик, ниже — дешевле. «Качество света» — набор:
    // ставит три части разом и показывает их общую ступень; части разные — «своё», ни один сегмент не выбран
    ui_section(f, "КАЧЕСТВО");
    u32 q = s.shadow_quality;
    bool same = s.gi_quality == q && s.air_quality == q;
    if (!same) q = ANA_COUNTOF(QUALITY_NAMES);
    ui_tip(f, same ? "Все три части разом; ниже — быстрее, облик тот же" : "Части выбраны по-разному; здесь — все разом");
    if (ui_segment(f, same ? "Качество света" : "Качество света: своё", q, QUALITY_NAMES, ANA_COUNTOF(QUALITY_NAMES))) {
        s.shadow_quality = s.gi_quality = s.air_quality = q;
        ch = true;
    }
    ui_tip(f, "Мягкость теней солнца и ламп, тени людей у ламп");
    ch |= ui_segment(f, "Тени", s.shadow_quality, QUALITY_NAMES, ANA_COUNTOF(QUALITY_NAMES));
    ui_tip(f, "Свет от стен и земли, затенение в углах");
    ch |= ui_segment(f, "Отражённый свет", s.gi_quality, QUALITY_NAMES, ANA_COUNTOF(QUALITY_NAMES));
    ui_tip(f, "Лучи в пыли и облака; на «Низко» — без лучей");
    ch |= ui_segment(f, "Воздух и облака", s.air_quality, QUALITY_NAMES, ANA_COUNTOF(QUALITY_NAMES));
    // Сглаживание краёв (docs/aa.md): TAA — края и мерцание мелочи, FXAA — только края, дешевле
    ui_tip(f, "TAA — края и мерцание мелочи; FXAA — только края, дешевле");
    ch |= ui_segment(f, "Сглаживание", s.aa, AA_NAMES, ANA_COUNTOF(AA_NAMES));
    ui_section(f, "КАМЕРА");
    ui_tip(f, "Поле зрения по вертикали");
    ch |= ui_slider(f, "Поле зрения", s.fov_deg, 40.0f, 100.0f, 1.0f, "%.0f°");
    // Эффекты камеры (docs/lens.md, docs/glare.md): силы — в data/sky.txt, здесь — включить и выключить
    ui_tip(f, "Солнце, лампы и блики сияют: свет рассеивается вокруг ярких мест");
    ch |= ui_toggle(f, "Свечение", s.glare);
    ui_tip(f, "Отражения солнца в линзах объектива — цепочка цветных пятен через кадр");
    ch |= ui_toggle(f, "Блики объектива", s.lens_flare);
    ui_tip(f, "Едва заметная цветная кайма у краёв кадра, как у настоящего объектива");
    ch |= ui_toggle(f, "Хроматическая аберрация", s.aberration);
    ui_tip(f, "Края кадра чуть темнее середины");
    ch |= ui_toggle(f, "Виньетка", s.vignette);
    ui_tip(f, "Мелкое зерно, как на плёнке; без него тени и небо глаже");
    ch |= ui_toggle(f, "Зерно плёнки", s.grain);
}

static void keys_column(UiForm &f, const char *title, const char *const (*rows)[2], u32 n) {
    ui_section(f, title);
    for (u32 i = 0; i < n; ++i) ui_key_row(f, rows[i][0], rows[i][1]);
}

static void tab_controls(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "МЫШЬ И КАМЕРА");
    ui_tip(f, "Скорость поворота камеры мышью");
    ch |= ui_slider(f, "Чувствительность", s.mouse_sens, 0.2f, 4.0f, 0.05f, "%.2f×");
    ch |= ui_toggle(f, "Инвертировать по вертикали", s.invert_y);
    ui_tip(f, "Мышь вправо — взгляд влево");
    ch |= ui_toggle(f, "Инвертировать по горизонтали", s.invert_x);
    ui_tip(f, "Сглаживание движения камеры; 0 — без сглаживания");
    ch |= ui_slider(f, "Плавность камеры", s.cam_smooth, 0.0f, 2.0f, 0.1f, "%.1f×");
    ui_section(f, "КАМЕРА");
    ui_tip(f, "Вблизи вдоль земли, издалека сверху");
    ch |= ui_toggle(f, "Наклон от расстояния", s.cam_tilt_zoom);
    ui_tip(f, "Колесо приближает к точке под курсором");
    ch |= ui_toggle(f, "Приближать к курсору", s.cam_zoom_cursor);
    ui_tip(f, "Курсор у края экрана двигает камеру");
    ch |= ui_toggle(f, "Двигать у края экрана", s.cam_edge_pan);
    ui_tip(f, "Скорость движения камеры клавишами");
    ch |= ui_slider(f, "Скорость движения", s.cam_pan_speed, 0.25f, 3.0f, 0.05f, "%.2f×");
    // Справка по клавишам — два столбца (клавиши — src/view/camera.cpp, src/app/orders.cpp, src/debug/debug.cpp)
    static const char *const CAM[][2] = {
        {"СКМ, Alt ПКМ", "вращать"}, {"Q  E", "повернуть"}, {"Shift СКМ", "тащить землю"}, {"Колесо", "ближе, дальше"},
        {"W A S D", "двигать"},      {"Shift", "быстрее"},
        {"F", "полёт (Q E — вниз, вверх)"},     {"C", "за выбранным"}};
    static const char *const WORLD[][2] = {{"ЛКМ", "выбрать человека"}, {"ЛКМ мимо", "снять выбор"},
                                           {"ПКМ", "идти шагом"},        {"Ctrl ПКМ", "трусцой"},
                                           {"Shift ПКМ", "бегом"},       {"ПКМ по человеку", "напасть"}};
    static const char *const MAIN[][2] = {{"Esc", "настройки"}, {"F11", "весь экран"}, {"F3", "отладка"}};
    static const char *const DBG[][2] = {{"Space", "пауза"},       {".  ,", "шаг вперёд, назад"},
                                          {"−  =", "скорость мира"}, {"[  ]", "час назад, вперёд"},
                                          {"T", "ход суток"},        {"L", "отрезки и точки"},
                                          {"P", "ячейки (старое)"},  {"B", "стенд: согнуть"},
                                          {"F5", "перечитать данные"}, {"F1", "справка в журнал"}};
    f32 gap = ui_px(28), cw = floorf((f.w - gap) * 0.5f);
    UiForm l = f, r = f;
    l.w = r.w = cw;
    r.x = f.x + cw + gap;
    keys_column(l, "КАМЕРА", CAM, ANA_COUNTOF(CAM));
    keys_column(l, "ЛЮДИ", WORLD, ANA_COUNTOF(WORLD));
    keys_column(r, "ОБЩЕЕ", MAIN, ANA_COUNTOF(MAIN));
    if (s.debug) keys_column(r, "ОТЛАДКА", DBG, ANA_COUNTOF(DBG));
    f.y = l.y > r.y ? l.y : r.y;
}

static void tab_ui(UiForm &f, Settings &s, bool &ch) {
    ui_section(f, "РАЗМЕР");
    ui_tip(f, "Размер окон и отступов; текст растёт вместе с ними");
    ch |= ui_slider(f, "Масштаб интерфейса", s.ui_scale, 0.75f, 2.0f, 0.05f, "%.2f×");
    ui_tip(f, "Размер букв отдельно от окон");
    ch |= ui_slider(f, "Размер текста", s.text_scale, 0.75f, 1.5f, 0.05f, "%.2f×");
    ui_section(f, "НА ЭКРАНЕ");
    ui_tip(f, "Сверху по центру; «подробно» — ещё CPU, GPU и шаг мира");
    ch |= ui_segment(f, "Счётчик к/с", s.fps, FPS_NAMES, FPS_MODES);
    ui_section(f, "ОТЛАДКА");
    ui_tip(f, "Клавиши отладки (Space, [ ], T, L, P…) и окна F3: отладка слева, показатели справа");
    ch |= ui_toggle(f, "Отладка  (F3 и клавиши)", s.debug);
    if (!s.debug) {
        f.y += ui_px(4);
        ui_text(f.x, f.y, UI_SMALL, ui_col::FAINT, "Выключено: клавиши отладки и F3 не работают");
        f.y += ui_line(UI_SMALL);
    }
}

// Прокрутка содержимого меню (на малом окне или крупном масштабе вкладка может не влезть)
static UiScroll g_menu_scroll[HUD_TABS];

void hud_menu(HudMenu &m, Settings &s) {
    if (!m.open) return;
    f32 W = ui_width(), H = ui_height();
    ui_rect(0, 0, W, H, ui_col::SHADE);
    ui_block(0, 0, W, H);  // под меню мир мышь не получает
    f32 pw = ui_px(900), ph = ui_px(660);
    if (pw > W - ui_px(20)) pw = W - ui_px(20);
    if (ph > H - ui_px(20)) ph = H - ui_px(20);
    f32 x = floorf((W - pw) * 0.5f), y = floorf((H - ph) * 0.5f);
    ui_panel(x, y, pw, ph);

    // Слева — заголовок, вкладки столбцом и выход внизу
    f32 side = ui_px(220), pad = ui_px(28);
    ui_rect(x + side, y + ui_px(1), ui_hair(), ph - ui_px(2), ui_col::LINE);
    f32 sx = x + ui_px(14), sw = side - ui_px(28);
    ui_text(sx + ui_px(16), y + pad, UI_TITLE, ui_col::TEXT, "Настройки");
    f32 ty = y + pad + ui_line(UI_TITLE) + ui_px(22);
    if (m.tab >= HUD_TABS) m.tab = HUD_TAB_SCREEN;
    ui_tabs_column(sx, ty, sw, ui_px(40), TABS, HUD_TABS, m.tab);
    f32 bh = ui_px(36);
    if (ui_button(sx, y + ph - pad - bh, sw, bh, "Выйти из игры")) m.quit = true;

    // Справа — содержимое вкладки (листается), снизу подсказка строки под мышью и «Продолжить»
    f32 cx = x + side + pad, cw = pw - side - 2 * pad;
    f32 foot = bh + ui_px(26);
    f32 top = y + pad - ui_px(12), room = ph - pad - foot - (top - y);
    UiScroll &sc = g_menu_scroll[m.tab];
    UiForm f = ui_form(cx, 0, cw - ui_px(14));
    f.ctl = ui_px(300);
    f.row = ui_px(40);
    f.y = ui_scroll_begin(sc, cx - ui_px(10), top, cw + ui_px(20), room);
    bool ch = false;
    switch (m.tab) {
    case HUD_TAB_SCREEN: tab_screen(f, s, ch); break;
    case HUD_TAB_CONTROLS: tab_controls(f, s, ch); break;
    default: tab_ui(f, s, ch); break;
    }
    ui_scroll_end(sc, f.y + ui_px(8));
    m.changed |= ch;

    f32 by = y + ph - pad - bh;
    ui_hline(cx, by - ui_px(14), cw, ui_col::LINE);
    f32 bw = ui_px(170);
    const char *tip = ui_tip_hovered();
    if (tip) ui_text_fit(cx, floorf(by + (bh - ui_line(UI_TEXT)) * 0.5f), UI_TEXT, ui_col::DIM, tip, cw - bw - ui_px(60));
    if (ui_button(cx + cw - bw, by, bw, bh, "Продолжить", false, true)) m.open = false;
    ui_text_right(cx + cw - bw - ui_px(14), floorf(by + (bh - ui_line(UI_SMALL)) * 0.5f), UI_SMALL, ui_col::FAINT, "Esc");
}
