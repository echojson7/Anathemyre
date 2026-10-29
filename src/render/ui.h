// Интерфейс поверх кадра: прямоугольники и пиксельный шрифт 5x7 в одном проходе.
// Координаты логические: окно делится на целый масштаб, чтобы пиксели шрифта оставались ровными.
#ifndef ANA_UI_H
#define ANA_UI_H

#include "../core/types.h"

#define UI_GLYPH_W 5
#define UI_GLYPH_H 7
#define UI_ADVANCE 6     // шаг буквы
#define UI_LINE 10       // шаг строки

// Цвет в байтах RGBA
#define UI_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

bool ui_init(void);
void ui_shutdown(void);
// Начать кадр интерфейса: масштаб выбирается по высоте окна (360 логических строк и больше)
void ui_begin(i32 win_w, i32 win_h);
// Отрисовать накопленное в текущий кадровый буфер
void ui_end(void);

i32  ui_scale(void);
f32  ui_width(void);   // логический размер окна
f32  ui_height(void);

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color);
// Рамка толщиной в один логический пиксель
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 color);
// Текст UTF-8, строчные выводятся заглавными. Возвращает x после последней буквы
f32  ui_text(f32 x, f32 y, u32 color, const char *s);
f32  ui_text_width(const char *s);

#endif
