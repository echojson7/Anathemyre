// Интерфейс (docs/ui.md): свой немедленный UI поверх кадра — прямоугольники с мягким краем и буквы IBM Plex Sans.
// Кадр интерфейса: ui_begin → рисование и виджеты → ui_end (передаёт прямоугольники рендеру).
// Мир не читает и не меняет: виджеты меняют только переменные вызывающего
#pragma once
#include "../core/mem.h"
#include "../platform/platform.h"

// Цвет sRGB в байтах
constexpr u32 ui_rgba(u32 r, u32 g, u32 b, u32 a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }

// Палитра: графит, белый и серый текст, без цветных рамок
namespace ui_col {
constexpr u32 PANEL = ui_rgba(14, 15, 16, 240);  // смешение в линейном свете: альфа выше, чем кажется
constexpr u32 SHADE = ui_rgba(0, 0, 0, 150);        // затемнение кадра под меню
constexpr u32 LINE = ui_rgba(255, 255, 255, 26);
constexpr u32 HOVER = ui_rgba(255, 255, 255, 12);
constexpr u32 TEXT = ui_rgba(236, 234, 228);
constexpr u32 DIM = ui_rgba(150, 149, 144);
constexpr u32 FAINT = ui_rgba(98, 98, 95);
constexpr u32 ON = ui_rgba(226, 224, 216);
constexpr u32 OFF = ui_rgba(64, 64, 63);
constexpr u32 WARN = ui_rgba(255, 140, 90);  // предупреждение: замер не честный (docs/budget.md)
}  // namespace ui_col

// Кегли на 1080p при размере текста 1: мелкий (подписи разделов), основной, крупный (счётчик к/с, заголовки)
enum UiFont : u32 { UI_SMALL, UI_TEXT, UI_TITLE, UI_FONTS };

struct UiConfig {
    f32 scale = 1.0f;       // масштаб интерфейса (рамки, отступы)
    f32 text_scale = 1.0f;  // размер текста, отдельно от рамок
};

// Шрифт data/ui/plex.font и атлас — в видеопамять (после render_init). false — нет файла или ошибка формата
bool ui_init(Arena &perm, Arena &scratch, const char *data_dir);
// Начать кадр: размер окна в пикселях, ввод этого кадра (для виджетов)
void ui_begin(i32 win_w, i32 win_h, const UiConfig &cfg, const Input &in);
// Передать прямоугольники рендеру (render_ui_set)
void ui_end();
// Прямоугольник занят интерфейсом в этом кадре: мышь над ним не уходит в мир
void ui_block(f32 x, f32 y, f32 w, f32 h);
// Мышь над интерфейсом или тянется виджет — мир и камера её не получают
bool ui_wants_mouse();

// Размеры: пикселей окна на единицу раскладки (1080p и масштаб 1 — 1.0); ui_px — округлённое в пиксели
f32 ui_s();
f32 ui_px(f32 units);
f32 ui_width();
f32 ui_height();

// Рисование, координаты — пиксели окна
void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 rgba);
// Панель без рамки: края мягко уходят в прозрачность на feather пикселей внутрь
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather);
// Место под прямоугольник, который станет известен позже (фон панели, высота которой — после содержимого)
u32 ui_reserve();
void ui_soft_at(u32 slot, f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather);
// Тонкая линия: 1 пиксель окна, на 1440p и выше — 2
f32 ui_hair();
void ui_hline(f32 x, f32 y, f32 w, u32 rgba);
void ui_vline(f32 x, f32 y, f32 h, u32 rgba);
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 rgba);

// Текст UTF-8. y — верх строки (ui_line); возвращает x после последней буквы. tracking — добавка между
// буквами в пикселях (заголовки заглавными чуть разрежены)
f32 ui_text(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking = 0);
f32 ui_text_width(UiFont f, const char *s, f32 tracking = 0);
// Высота строки и заглавной буквы кегля, пиксели
f32 ui_line(UiFont f);
f32 ui_cap(UiFont f);
// Текст, выровненный по правому краю x / по центру x
void ui_text_right(f32 x, f32 y, UiFont f, u32 rgba, const char *s);
void ui_text_center(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking = 0);

// --- Виджеты (src/ui/widgets.cpp) ---
// Строки формы: подпись слева, управление справа, одна высота. Номер виджета — хеш подписи и id
struct UiForm {
    f32 x, y, w;   // левый верх и ширина формы
    f32 row;       // высота строки
    f32 ctl;       // ширина колонки управления справа
};
UiForm ui_form(f32 x, f32 y, f32 w);
// Подпись раздела заглавными мелким кеглем и линия под ней
void ui_section(UiForm &f, const char *title);
// Строка «подпись — значение» (только чтение)
void ui_value(UiForm &f, const char *label, const char *value, u32 rgba = ui_col::TEXT);
bool ui_toggle(UiForm &f, const char *label, bool &v);
// Ползунок; fmt — printf-формат значения справа от подписи (например "%.2f×"), shown — готовый текст вместо него
bool ui_slider(UiForm &f, const char *label, f32 &v, f32 lo, f32 hi, f32 step, const char *fmt,
               const char *shown = nullptr);
// Выбор из списка стрелками ‹ ›
bool ui_choice(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count);
// Кнопка во всю ширину формы
bool ui_button_row(UiForm &f, const char *label);
// Кнопка где угодно; active — нажатое состояние (переключатель)
bool ui_button(f32 x, f32 y, f32 w, f32 h, const char *label, bool active = false);
// Вкладки в строку: возвращает true, если выбрана другая
bool ui_tabs(f32 x, f32 y, f32 w, const char *const *names, u32 count, u32 &cur);
// Строка справки: клавиша в рамке слева, описание справа
void ui_key_row(UiForm &f, const char *key, const char *what);
