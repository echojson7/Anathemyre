// Интерфейс поверх кадра: прямоугольники и пиксельный шрифт 5x7 в одном проходе.
// Координаты логические: окно делится на дробный масштаб слоя, слой растягивается фильтром ровных пикселей.
#ifndef ANA_UI_H
#define ANA_UI_H

#include "../core/types.h"

#define UI_GLYPH_W 5
#define UI_GLYPH_H 7
#define UI_ADVANCE 6     // шаг буквы
#define UI_LINE 10       // шаг строки
#define UI_SMALL_W 3     // мелкие цифры
#define UI_SMALL_H 5

// Цвет в байтах RGBA
#define UI_RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

bool ui_init(void);
void ui_shutdown(void);
// Слои со своим масштабом: нижняя полоса чуть крупнее мира, окна и остальное — заметно крупнее
enum { UI_BOTTOM, UI_TOP, UI_LAYERS };

// Начать кадр интерфейса; текущий слой — UI_TOP
void ui_begin(i32 win_w, i32 win_h);
// Куда идут следующие прямоугольники и буквы; размеры и масштаб ниже — для текущего слоя
void ui_layer(u32 layer);
// Отрисовать накопленное в текущий кадровый буфер
void ui_end(void);

f32  ui_scale(void);
f32  ui_width(void);   // логический размер окна
f32  ui_height(void);

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color);
void ui_rect_grad(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom);
// Заливка панели: градиент со сканлайнами и живым шумом
void ui_panel_fill(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom);
// Картинка из текстуры ui_set_image (HDR: тонмаппинг и зерно в шейдере), uv в долях текстуры
void ui_image(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 tint);
void ui_set_image(u32 tex);
void ui_set_time(f32 t);
// Мелкие цифры 3x5 (и / - . % + :)
f32  ui_num(f32 x, f32 y, u32 color, const char *s);
f32  ui_num_width(const char *s);
// Рамка толщиной в один логический пиксель
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 color);
// Текст UTF-8, строчные выводятся заглавными. Возвращает x после последней буквы
f32  ui_text(f32 x, f32 y, u32 color, const char *s);
f32  ui_text_width(const char *s);

#endif
