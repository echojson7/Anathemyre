// Интерфейс поверх кадра: прямоугольники и буквы IBM Plex Sans (data/ui/plex.font) в одном проходе.
// Координаты логические: окно делится на дробный масштаб слоя. Буквы — ровные кегли из файла,
// ставятся в целые пиксели окна; кегль считается от высоты окна отдельно от масштаба рамок.
#ifndef ANA_UI_H
#define ANA_UI_H

#include "../core/types.h"

// Кегли на 1080p: основной 13 (в нижней полосе 12), мелкий 10, крупный 18
enum { UI_FONT_TEXT, UI_FONT_SMALL, UI_FONT_BIG, UI_FONTS };

// Цвет в байтах RGBA
#define UI_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

bool ui_init(const char *data_dir);
void ui_shutdown(void);
// Слои со своим масштабом: нижняя полоса чуть крупнее мира, окна и остальное — заметно крупнее.
// UI_DEBUG — масштаб окон, рисуется последним: отладочное меню поверх окон
enum { UI_BOTTOM, UI_TOP, UI_DEBUG, UI_LAYERS };

// Начать кадр интерфейса; текущий слой — UI_TOP
void ui_begin(i32 win_w, i32 win_h);
// Куда идут следующие прямоугольники и буквы; размеры и масштаб ниже — для текущего слоя
void ui_layer(u32 layer);
u32  ui_current_layer(void);
// Отрисовать накопленное в текущий кадровый буфер
void ui_end(void);

f32  ui_scale(void);
f32  ui_width(void);   // логический размер окна
f32  ui_height(void);
f32  ui_text_scale(void);  // кегль относительно 1080p
f32  ui_hair(void);        // толщина тонкой линии, логических единиц (1 пиксель окна, на 1440p и выше — 2)
// Кегль следующих букв (UI_FONT_*); ui_begin ставит основной
void ui_font(u32 font);
// Высота строки и заглавной буквы текущего кегля в логических единицах слоя
f32  ui_line(void);
f32  ui_cap(void);
// Множители из отладочного меню, 1 — по умолчанию; действуют со следующего ui_begin
void ui_set_user_scale(f32 frames, f32 text);

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color);
// Панель без рамки: заливка, края на feather логических единиц мягко уходят в прозрачность
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 color, f32 feather);
void ui_rect_grad(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom);
// Заливка панели: градиент со сканлайнами и живым шумом
void ui_panel_fill(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom);
// Картинка из текстуры ui_set_image (HDR: тонмаппинг и зерно в шейдере), uv в долях текстуры
void ui_image(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 tint);
void ui_set_image(u32 tex);   // RbTex (render/rb.h)
void ui_set_time(f32 t);
// Мелким кеглем (числа в клетках), y — верх строки мелкого кегля
f32  ui_num(f32 x, f32 y, u32 color, const char *s);
f32  ui_num_width(const char *s);
// Тонкие линии и рамка толщиной ui_hair, внутрь прямоугольника
void ui_hline(f32 x, f32 y, f32 w, u32 color);
void ui_vline(f32 x, f32 y, f32 h, u32 color);
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 color);
// Текст UTF-8 текущим кеглем, строчные выводятся заглавными; y — верх строки ui_line.
// Возвращает x после последней буквы
f32  ui_text(f32 x, f32 y, u32 color, const char *s);
f32  ui_text_width(const char *s);
u32  ui_text_fit(const char *s, f32 w);  // сколько байт строки влезает в ширину w

#endif
