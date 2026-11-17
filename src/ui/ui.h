// Интерфейс (docs/ui.md): свой немедленный UI поверх кадра — прямоугольники (скруглённые, обводкой, с мягким
// краем) и буквы Inter. Кадр интерфейса: ui_begin → рисование и виджеты → ui_end (передаёт прямоугольники рендеру).
// Мир не читает и не меняет: виджеты меняют только переменные вызывающего
#pragma once
#include "../core/mem.h"
#include "../platform/platform.h"

// Цвет sRGB в байтах
constexpr u32 ui_rgba(u32 r, u32 g, u32 b, u32 a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }

// Палитра: графит, белый и серый текст, один приглушённый песочный акцент — «включено» и «выбрано»
namespace ui_col {
constexpr u32 PANEL = ui_rgba(15, 16, 18, 246);    // смешение в линейном свете: альфа выше, чем кажется
constexpr u32 SHADOW = ui_rgba(0, 0, 0, 110);      // тень под панелью
constexpr u32 SHADE = ui_rgba(0, 0, 0, 150);       // затемнение кадра под меню
constexpr u32 SIDE = ui_rgba(255, 255, 255, 6);    // столбец вкладок, поле управления
constexpr u32 LINE = ui_rgba(255, 255, 255, 24);
constexpr u32 HOVER = ui_rgba(255, 255, 255, 14);
constexpr u32 TEXT = ui_rgba(234, 232, 226);
constexpr u32 DIM = ui_rgba(152, 151, 146);
constexpr u32 FAINT = ui_rgba(104, 104, 101);
constexpr u32 ON = ui_rgba(214, 176, 112);         // акцент: включено, выбрано
constexpr u32 ON_BG = ui_rgba(214, 176, 112, 40);  // подложка выбранного
constexpr u32 INK = ui_rgba(22, 20, 17);           // текст на акценте
constexpr u32 OFF = ui_rgba(72, 72, 71);
constexpr u32 GOOD = ui_rgba(150, 196, 128);       // замер честный
constexpr u32 WARN = ui_rgba(255, 140, 90);        // предупреждение: замер не честный (docs/budget.md)
}  // namespace ui_col

// Кегли и начертания (docs/ui.md «Шрифт»): мелкий полужирный — заголовки разделов заглавными, основной — строки,
// крупный — заголовки окон и счётчик к/с, полужирный — выбранное и кнопки, данные — строки показателей
enum UiFont : u32 { UI_SMALL, UI_TEXT, UI_TITLE, UI_STRONG, UI_DATA, UI_FONTS };

struct UiConfig {
    f32 scale = 1.0f;       // масштаб интерфейса (рамки, отступы)
    f32 text_scale = 1.0f;  // размер текста, отдельно от рамок
};

// Шрифт data/ui/inter.font и атлас — в видеопамять (после render_init). false — нет файла или ошибка формата
bool ui_init(Arena &perm, Arena &scratch, const char *data_dir);
// Начать кадр: размер окна в пикселях, ввод этого кадра (для виджетов), время кадра (подсказки, «скопировано»)
void ui_begin(i32 win_w, i32 win_h, const UiConfig &cfg, const Input &in, f64 dt = 0.0);
// Передать прямоугольники рендеру (render_ui_set)
void ui_end();
// Прямоугольник занят интерфейсом в этом кадре: мышь над ним не уходит в мир
void ui_block(f32 x, f32 y, f32 w, f32 h);
// Мышь над интерфейсом или тянется виджет — мир и камера её не получают
bool ui_wants_mouse();
// Мышь над прямоугольником (с учётом обрезки), нажата в этом кадре
bool ui_hover(f32 x, f32 y, f32 w, f32 h);
bool ui_clicked(f32 x, f32 y, f32 w, f32 h);

// Размеры: пикселей окна на единицу раскладки (1080p и масштаб 1 — 1.0); ui_px — округлённое в пиксели
f32 ui_s();
f32 ui_px(f32 units);
f32 ui_width();
f32 ui_height();

// Рисование, координаты — пиксели окна
void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 rgba);
// Скруглённый прямоугольник и обводка (толщина width) со сглаженным краем
void ui_round(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 radius);
void ui_stroke(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 radius, f32 width);
// Панель без рамки: края мягко уходят в прозрачность на feather пикселей внутрь
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather);
// Место под прямоугольник, который станет известен позже (фон панели, высота которой — после содержимого)
u32 ui_reserve();
void ui_soft_at(u32 slot, f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather);
// Окно: мягкая тень и скруглённая панель с тонкой кромкой. Высота известна после содержимого — ui_panel_reserve
// до него (три места подряд) и ui_panel_at после
void ui_panel(f32 x, f32 y, f32 w, f32 h);
u32 ui_panel_reserve();
void ui_panel_at(u32 slot, f32 x, f32 y, f32 w, f32 h);
// Обрезка: всё, что нарисовано после ui_clip, видно только внутри прямоугольника; виджеты за ним мышь не ловят
void ui_clip(f32 x, f32 y, f32 w, f32 h);
void ui_clip_off();
// Тонкая линия: 1 пиксель окна, на 1440p и выше — 2
f32 ui_hair();
void ui_hline(f32 x, f32 y, f32 w, u32 rgba);
void ui_vline(f32 x, f32 y, f32 h, u32 rgba);
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 rgba);

// Текст UTF-8. y — верх строки (ui_line); возвращает x после последней буквы. tracking — добавка между
// буквами в пикселях (заголовки заглавными чуть разрежены). Пары букв — с кернингом шрифта
f32 ui_text(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking = 0);
f32 ui_text_width(UiFont f, const char *s, f32 tracking = 0);
// Высота строки и заглавной буквы кегля, пиксели
f32 ui_line(UiFont f);
f32 ui_cap(UiFont f);
// Текст, выровненный по правому краю x / по центру x
void ui_text_right(f32 x, f32 y, UiFont f, u32 rgba, const char *s);
void ui_text_center(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking = 0);
// Текст в ширину w: не влезает — обрезан многоточием по границе буквы. Возвращает x после текста
f32 ui_text_fit(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 w, bool right = false);

// --- Снятие текста (панель показателей, «Скопировать всё») ---
// Пока снятие идёт, разделы и строки «подпись — значение» дописываются в buf строками текста (и свёрнутые тоже)
void ui_capture_begin(char *buf, usize cap);
// Своя строка в снятие (если идёт)
void ui_capture_line(const char *s);
// Конец снятия: длина текста
usize ui_capture_end();
bool ui_capturing();

// --- Виджеты (src/ui/widgets.cpp) ---
// Строки формы: подпись слева, управление справа, одна высота. Номер виджета — адрес его переменной
struct UiForm {
    f32 x, y, w;       // левый верх и ширина формы
    f32 row;           // высота строки
    f32 ctl;           // ширина колонки управления справа
    UiFont font = UI_TEXT;  // кегль строк
    const char *tip = nullptr;  // подсказка следующей строки (ui_tip): видна внизу окна, пока мышь над строкой
};
UiForm ui_form(f32 x, f32 y, f32 w);
// Подсказка к следующей строке формы; ui_tip_hovered — подсказка строки под мышью в этом кадре (или nullptr)
void ui_tip(UiForm &f, const char *tip);
const char *ui_tip_hovered();
// Подпись раздела заглавными мелким кеглем и линия под ней
void ui_section(UiForm &f, const char *title);
// Раздел, который сворачивается щелчком по заголовку. Возвращает open; свёрнутый раздел при снятии текста всё
// равно отдаёт строки (вызывающий рисует содержимое при open || ui_capturing(), виджеты свёрнутого не видны)
// capture = false — заголовок в снятый текст не идёт (раздел без строк, например график)
bool ui_fold(UiForm &f, const char *title, bool &open, const char *note = nullptr, bool capture = true);
// Конец свёрнутых разделов: дальше строки снова видны
void ui_fold_end(UiForm &f);
// Строка «подпись — значение» (только чтение)
void ui_value(UiForm &f, const char *label, const char *value, u32 rgba = ui_col::TEXT);
bool ui_toggle(UiForm &f, const char *label, bool &v);
// Ползунок; fmt — printf-формат значения справа от подписи (например "%.2f×"), shown — готовый текст вместо него
bool ui_slider(UiForm &f, const char *label, f32 &v, f32 lo, f32 hi, f32 step, const char *fmt,
               const char *shown = nullptr);
// Выбор из списка стрелками ‹ ›
bool ui_choice(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count);
// Выбор из короткого списка — все варианты в ряд (сегменты); не влезают в колонку управления — стрелками ‹ ›
bool ui_segment(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count);
// Кнопка во всю ширину формы
bool ui_button_row(UiForm &f, const char *label);
// Кнопка где угодно; active — нажатое состояние (переключатель), accent — главная кнопка окна
bool ui_button(f32 x, f32 y, f32 w, f32 h, const char *label, bool active = false, bool accent = false);
// Вкладки в строку: возвращает true, если выбрана другая
bool ui_tabs(f32 x, f32 y, f32 w, const char *const *names, u32 count, u32 &cur);
// Вкладки столбцом (меню настроек): высота строки row; возвращает true, если выбрана другая
bool ui_tabs_column(f32 x, f32 y, f32 w, f32 row, const char *const *names, u32 count, u32 &cur);
// Область прокрутки: содержимое между begin и end обрезано по прямоугольнику и сдвинуто на offset.
// Колесо над областью листает её (миру не достаётся), полоса справа тянется мышью, клик мимо ползунка — на
// страницу. Высота содержимого известна после рисования: begin зажимает offset по прошлому кадру, end — по этому
struct UiScroll {
    f32 offset = 0;   // на сколько пикселей содержимое уехало вверх
    f32 content = 0;  // высота содержимого прошлого кадра
    f32 grab = 0;     // где взяли ползунок, от его верха
    f32 x = 0, y = 0, w = 0, h = 0;
};
// Возвращает y, с которого рисовать содержимое
f32 ui_scroll_begin(UiScroll &s, f32 x, f32 y, f32 w, f32 h);
// bottom — y под последней строкой содержимого (в тех же сдвинутых координатах). Возвращает видимую высоту
// области: меньше h, когда содержимое короче
f32 ui_scroll_end(UiScroll &s, f32 bottom);
// Строка справки: клавиша в рамке слева, описание справа
void ui_key_row(UiForm &f, const char *key, const char *what);
