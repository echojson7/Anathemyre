#include "ui.h"

#include <stddef.h>
#include <string.h>

#include "rb.h"
#include "render.h"

#define UI_MAX_QUADS 16384
#define ATLAS 128
#define CELL 8   // ячейка буквы в атласе

typedef struct UiVertex {
    f32 x, y, u, v;
    u32 color;
} UiVertex;

// Шрифт: 7 строк по 5 точек через '|'. alias — взять рисунок другой буквы (кириллица = латиница)
typedef struct GlyphArt {
    u32 cp;
    const char *art;
    u32 alias;
} GlyphArt;

static const GlyphArt g_font[] = {
    { '!', "..#..|..#..|..#..|..#..|..#..|.....|..#..", 0 },
    { '"', ".#.#.|.#.#.|.....|.....|.....|.....|.....", 0 },
    { '#', ".#.#.|#####|.#.#.|.#.#.|.#.#.|#####|.#.#.", 0 },
    { '%', "##..#|##.#.|...#.|..#..|.#...|.#.##|#..##", 0 },
    { '\'', "..#..|..#..|.....|.....|.....|.....|.....", 0 },
    { '(', "...#.|..#..|.#...|.#...|.#...|..#..|...#.", 0 },
    { ')', ".#...|..#..|...#.|...#.|...#.|..#..|.#...", 0 },
    { '*', ".....|..#..|#.#.#|.###.|#.#.#|..#..|.....", 0 },
    { '+', ".....|..#..|..#..|#####|..#..|..#..|.....", 0 },
    { ',', ".....|.....|.....|.....|.##..|..#..|.#...", 0 },
    { '-', ".....|.....|.....|#####|.....|.....|.....", 0 },
    { '.', ".....|.....|.....|.....|.....|.##..|.##..", 0 },
    { '/', "....#|...#.|...#.|..#..|.#...|.#...|#....", 0 },
    { '0', ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.", 0 },
    { '1', "..#..|.##..|..#..|..#..|..#..|..#..|.###.", 0 },
    { '2', ".###.|#...#|....#|...#.|..#..|.#...|#####", 0 },
    { '3', "#####|...#.|..#..|...#.|....#|#...#|.###.", 0 },
    { '4', "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.", 0 },
    { '5', "#####|#....|####.|....#|....#|#...#|.###.", 0 },
    { '6', "..##.|.#...|#....|####.|#...#|#...#|.###.", 0 },
    { '7', "#####|....#|...#.|..#..|.#...|.#...|.#...", 0 },
    { '8', ".###.|#...#|#...#|.###.|#...#|#...#|.###.", 0 },
    { '9', ".###.|#...#|#...#|.####|....#|...#.|.##..", 0 },
    { ':', ".....|.##..|.##..|.....|.##..|.##..|.....", 0 },
    { ';', ".....|.##..|.##..|.....|.##..|..#..|.#...", 0 },
    { '<', "...#.|..#..|.#...|#....|.#...|..#..|...#.", 0 },
    { '=', ".....|.....|#####|.....|#####|.....|.....", 0 },
    { '>', ".#...|..#..|...#.|....#|...#.|..#..|.#...", 0 },
    { '?', ".###.|#...#|....#|...#.|..#..|.....|..#..", 0 },
    { '[', ".###.|.#...|.#...|.#...|.#...|.#...|.###.", 0 },
    { ']', ".###.|...#.|...#.|...#.|...#.|...#.|.###.", 0 },
    { '_', ".....|.....|.....|.....|.....|.....|#####", 0 },
    { '|', "..#..|..#..|..#..|..#..|..#..|..#..|..#..", 0 },
    { 'A', ".###.|#...#|#...#|#####|#...#|#...#|#...#", 0 },
    { 'B', "####.|#...#|#...#|####.|#...#|#...#|####.", 0 },
    { 'C', ".###.|#...#|#....|#....|#....|#...#|.###.", 0 },
    { 'D', "###..|#..#.|#...#|#...#|#...#|#..#.|###..", 0 },
    { 'E', "#####|#....|#....|####.|#....|#....|#####", 0 },
    { 'F', "#####|#....|#....|####.|#....|#....|#....", 0 },
    { 'G', ".###.|#...#|#....|#.###|#...#|#...#|.####", 0 },
    { 'H', "#...#|#...#|#...#|#####|#...#|#...#|#...#", 0 },
    { 'I', ".###.|..#..|..#..|..#..|..#..|..#..|.###.", 0 },
    { 'J', "..###|...#.|...#.|...#.|...#.|#..#.|.##..", 0 },
    { 'K', "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#", 0 },
    { 'L', "#....|#....|#....|#....|#....|#....|#####", 0 },
    { 'M', "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#", 0 },
    { 'N', "#...#|#...#|##..#|#.#.#|#..##|#...#|#...#", 0 },
    { 'O', ".###.|#...#|#...#|#...#|#...#|#...#|.###.", 0 },
    { 'P', "####.|#...#|#...#|####.|#....|#....|#....", 0 },
    { 'Q', ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#", 0 },
    { 'R', "####.|#...#|#...#|####.|#.#..|#..#.|#...#", 0 },
    { 'S', ".####|#....|#....|.###.|....#|....#|####.", 0 },
    { 'T', "#####|..#..|..#..|..#..|..#..|..#..|..#..", 0 },
    { 'U', "#...#|#...#|#...#|#...#|#...#|#...#|.###.", 0 },
    { 'V', "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..", 0 },
    { 'W', "#...#|#...#|#...#|#.#.#|#.#.#|#.#.#|.#.#.", 0 },
    { 'X', "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#", 0 },
    { 'Y', "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..", 0 },
    { 'Z', "#####|....#|...#.|..#..|.#...|#....|#####", 0 },
    { 0x0410, NULL, 'A' },
    { 0x0411, "#####|#....|#....|####.|#...#|#...#|####.", 0 },
    { 0x0412, NULL, 'B' },
    { 0x0413, "#####|#....|#....|#....|#....|#....|#....", 0 },
    { 0x0414, ".###.|.#.#.|.#.#.|.#.#.|.#.#.|#####|#...#", 0 },
    { 0x0415, NULL, 'E' },
    { 0x0401, ".#.#.|#####|#....|####.|#....|#....|#####", 0 },
    { 0x0416, "#.#.#|#.#.#|.###.|..#..|.###.|#.#.#|#.#.#", 0 },
    { 0x0417, ".###.|#...#|....#|..##.|....#|#...#|.###.", 0 },
    { 0x0418, "#...#|#...#|#..##|#.#.#|##..#|#...#|#...#", 0 },
    { 0x0419, ".#.#.|#...#|#..##|#.#.#|##..#|#...#|#...#", 0 },
    { 0x041A, NULL, 'K' },
    { 0x041B, "..###|.#..#|.#..#|.#..#|.#..#|.#..#|#...#", 0 },
    { 0x041C, NULL, 'M' },
    { 0x041D, NULL, 'H' },
    { 0x041E, NULL, 'O' },
    { 0x041F, "#####|#...#|#...#|#...#|#...#|#...#|#...#", 0 },
    { 0x0420, NULL, 'P' },
    { 0x0421, NULL, 'C' },
    { 0x0422, NULL, 'T' },
    { 0x0423, "#...#|#...#|#...#|.####|....#|....#|.###.", 0 },
    { 0x0424, "..#..|.###.|#.#.#|#.#.#|#.#.#|.###.|..#..", 0 },
    { 0x0425, NULL, 'X' },
    { 0x0426, "#..#.|#..#.|#..#.|#..#.|#..#.|#####|....#", 0 },
    { 0x0427, "#...#|#...#|#...#|.####|....#|....#|....#", 0 },
    { 0x0428, "#.#.#|#.#.#|#.#.#|#.#.#|#.#.#|#.#.#|#####", 0 },
    { 0x0429, "#.#.#|#.#.#|#.#.#|#.#.#|#.#.#|#####|....#", 0 },
    { 0x042A, "##...|.#...|.#...|.###.|.#..#|.#..#|.###.", 0 },
    { 0x042B, "#...#|#...#|#...#|##..#|#.#.#|#.#.#|##..#", 0 },
    { 0x042C, "#....|#....|#....|####.|#...#|#...#|####.", 0 },
    { 0x042D, ".###.|#...#|....#|..###|....#|#...#|.###.", 0 },
    { 0x042E, "#..#.|#.#.#|#.#.#|###.#|#.#.#|#.#.#|#..#.", 0 },
    { 0x042F, ".####|#...#|#...#|.####|..#.#|.#..#|#...#", 0 },
    { 0x00D7, ".....|#...#|.#.#.|..#..|.#.#.|#...#|.....", 0 },  // ×
    { 0x00B7, ".....|.....|.....|..#..|.....|.....|.....", 0 },  // ·
    { 0x2014, NULL, '-' },                                     // —
    { 0x2013, NULL, '-' },                                     // –
    { 0x2192, ".....|..#..|...#.|#####|...#.|..#..|.....", 0 },  // →
    // Глифы ушедших: знаки на стенах, смысл утерян
    { 0xE000, "#.#.#|#.#.#|#####|..#..|#####|#...#|#.#.#", 0 },
    { 0xE001, "###..|#.#..|#.###|..#.#|###.#|#...#|#####", 0 },
    { 0xE002, "..#..|.#.#.|#...#|#####|#...#|.#.#.|..#..", 0 },
    { 0xE003, "##.##|#...#|.#.#.|..#..|.#.#.|#...#|##.##", 0 },
};

// Мелкие цифры 3x5 для чисел на схемах и полосках
static const GlyphArt g_small[] = {
    { '0', "###|#.#|#.#|#.#|###", 0 }, { '1', ".#.|##.|.#.|.#.|###", 0 }, { '2', "##.|..#|.#.|#..|###", 0 },
    { '3', "##.|..#|.#.|..#|##.", 0 }, { '4', "#.#|#.#|###|..#|..#", 0 }, { '5', "###|#..|##.|..#|##.", 0 },
    { '6', ".##|#..|###|#.#|###", 0 }, { '7', "###|..#|.#.|.#.|.#.", 0 }, { '8', "###|#.#|###|#.#|###", 0 },
    { '9', "###|#.#|###|..#|##.", 0 }, { '/', "..#|..#|.#.|#..|#..", 0 }, { '-', "...|...|###|...|...", 0 },
    { '.', "...|...|...|...|.#.", 0 }, { '%', "#.#|..#|.#.|#..|#.#", 0 }, { '+', "...|.#.|###|.#.|...", 0 },
    { ':', "...|.#.|...|.#.|...", 0 },
};

#define FONT_COUNT ARRAY_COUNT(g_font)
#define SMALL_COUNT ARRAY_COUNT(g_small)
_Static_assert(FONT_COUNT + SMALL_COUNT < (ATLAS / CELL) * (ATLAS / CELL), "шрифт не влезает в атлас");

// Привязки текстур (ui.frag, upscale.frag)
enum { B_UI_ATLAS = 20, B_UI_IMAGE = 21 };

#define UI_BOXES 32
#define UI_PAD   2.0f  // апскейл смешивает соседние пиксели цели: накладываем с запасом 1, чистим с запасом 2

static struct {
    RbPipe pipe;
    RbPipe pipe_up;            // слой рисуется в своём масштабе и ровно растягивается на окно
    RbBuf vbo;                 // кольцо кадров: RB_FRAMES × UI_LAYERS кусков по слою
    RbTex tex, target[UI_LAYERS];
    i32 tw[UI_LAYERS], th[UI_LAYERS];  // размер цели: одна точка шрифта — один пиксель цели
    u8  lut_ascii[128];   // номер буквы + 1, 0 — нет
    u8  lut_cyr[0x60];    // U+0400..U+045F
    f32 scale[UI_LAYERS];  // пикселей окна на логическую единицу (рамки, отступы)
    f32 text[UI_LAYERS];   // пикселей окна на точку шрифта
    f32 f[UI_LAYERS];      // пикселей цели на логическую единицу: scale / text
    f32 user_scale, user_text;  // ползунки отладочного меню
    u32 layer;
    i32 win_w, win_h;
    RbTex image;          // текстура для ui_image
    f32 time;
    u32 count[UI_LAYERS];  // вершин в кадре
    // Занятые части цели в кадре (x0, y0, x1, y1) с запасом UI_PAD, не пересекаются.
    // Чистятся и накладываются на окно только они
    f32 box[UI_LAYERS][UI_BOXES][4];
    u32 boxes[UI_LAYERS];
    UiVertex v[UI_LAYERS][UI_MAX_QUADS * 6];
} U;

static u32 find_glyph(u32 cp)
{
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    else if (cp >= 0x0430 && cp <= 0x044F) cp -= 0x20;
    else if (cp == 0x0451) cp = 0x0401;
    if (cp < 128) return U.lut_ascii[cp];
    if (cp >= 0x0400 && cp < 0x0460) return U.lut_cyr[cp - 0x0400];
    for (u32 i = 0; i < FONT_COUNT; i++)
        if (g_font[i].cp == cp) return i + 1;
    return 0;
}

bool ui_init(void)
{
    static u8 pix[ATLAS * ATLAS];
    memset(pix, 0, sizeof pix);
    for (u32 i = 0; i < FONT_COUNT; i++) {
        const GlyphArt *g = &g_font[i];
        if (g->cp < 128) U.lut_ascii[g->cp] = (u8)(i + 1);
        else if (g->cp >= 0x0400 && g->cp < 0x0460) U.lut_cyr[g->cp - 0x0400] = (u8)(i + 1);
    }
    for (u32 i = 0; i < FONT_COUNT; i++) {
        const char *art = g_font[i].art;
        if (!art) art = g_font[find_glyph(g_font[i].alias) - 1].art;
        u32 cx = (i % (ATLAS / CELL)) * CELL, cy = (i / (ATLAS / CELL)) * CELL;
        for (u32 y = 0; y < UI_GLYPH_H; y++)
            for (u32 x = 0; x < UI_GLYPH_W; x++)
                if (art[y * 6 + x] == '#') pix[(cy + y) * ATLAS + cx + x] = 255;
    }
    for (u32 i = 0; i < SMALL_COUNT; i++) {
        u32 k = FONT_COUNT + i, cx = (k % (ATLAS / CELL)) * CELL, cy = (k / (ATLAS / CELL)) * CELL;
        for (u32 y = 0; y < UI_SMALL_H; y++)
            for (u32 x = 0; x < UI_SMALL_W; x++)
                if (g_small[i].art[y * 4 + x] == '#') pix[(cy + y) * ATLAS + cx + x] = 255;
    }
    pix[ATLAS * ATLAS - 1] = 255;  // сплошная заливка

    // Слой копит цвет по альфе в свою цель; на окно — уже умноженным на альфу
    U.pipe = rb_pipe_make(&(RbPipeDesc){
        .vs = { "ui.vert" }, .fs = { "ui.frag" }, .name = "интерфейс", .blend = RB_BLEND_ALPHA, .color = { RB_FMT_RGBA8 },
        .layout = { .attr = { { 0, 0, RB_ATTR_F32x2, offsetof(UiVertex, x) }, { 1, 0, RB_ATTR_F32x2, offsetof(UiVertex, u) },
                              { 2, 0, RB_ATTR_UN8x4, offsetof(UiVertex, color) } },
                    .attrs = 3, .stride = { sizeof(UiVertex) } },
    });
    U.pipe_up = rb_pipe_make(&(RbPipeDesc){
        .vs = { "fullscreen.vert" }, .fs = { "upscale.frag" }, .name = "интерфейс на окно", .blend = RB_BLEND_PREMUL,
    });
    if (!U.pipe || !U.pipe_up) return false;
    U.tex = rb_tex_make(&(RbTexDesc){ .fmt = RB_FMT_R8, .w = ATLAS, .h = ATLAS, .usage = RB_TEX_SAMPLED });
    rb_tex_write(U.tex, 0, 0, 0, 0, ATLAS, ATLAS, 1, pix, 0);
    U.vbo = rb_buf_make(RB_FRAMES * sizeof U.v, RB_BUF_VERTEX | RB_BUF_DYNAMIC, NULL);
    U.user_scale = U.user_text = 1.0f;
    return U.tex && U.vbo;
}

void ui_shutdown(void)
{
    rb_pipe_free(&U.pipe);
    rb_pipe_free(&U.pipe_up);
    for (u32 l = 0; l < UI_LAYERS; l++) rb_tex_free(&U.target[l]);
    rb_tex_free(&U.tex);
    rb_buf_free(&U.vbo);
}

void ui_begin(i32 win_w, i32 win_h)
{
    U.win_w = win_w;
    U.win_h = win_h;
    // От высоты окна, база 1080p, шаг 0.25. 1440p: нижняя полоса ×1.75, окна и остальное ×2,
    // текст везде ×1.5 (буква 10.5 пикселя). Текст мельче рамок, поэтому считается отдельно
    static const f32 k[UI_LAYERS] = { 1.3125f, 1.5f, 1.5f };
    const f32 base = (f32)win_h / 1080.0f;
    for (u32 i = 0; i < UI_LAYERS; i++) {
        U.scale[i] = fmaxf(1.0f, roundf(base * k[i] * U.user_scale * 4.0f) * 0.25f);
        U.text[i] = fminf(U.scale[i], fmaxf(1.0f, roundf(base * 1.125f * U.user_text * 4.0f) * 0.25f));
        U.f[i] = U.scale[i] / U.text[i];
        U.count[i] = 0;
        U.boxes[i] = 0;
    }
    U.layer = UI_TOP;
}

void ui_layer(u32 layer) { U.layer = MIN(layer, UI_LAYERS - 1); }
f32 ui_scale(void) { return U.scale[U.layer]; }
f32 ui_width(void) { return floorf((f32)U.win_w / U.scale[U.layer]); }
f32 ui_height(void) { return floorf((f32)U.win_h / U.scale[U.layer]); }
f32 ui_text_scale(void) { return U.text[U.layer]; }
f32 ui_advance(void) { return UI_ADVANCE / U.f[U.layer]; }
f32 ui_line(void) { return UI_LINE / U.f[U.layer]; }

void ui_set_user_scale(f32 frames, f32 text)
{
    U.user_scale = frames;
    U.user_text = text;
}

static bool box_touch(const f32 *a, const f32 *b)
{
    return a[0] <= b[2] && b[0] <= a[2] && a[1] <= b[3] && b[1] <= a[3];
}

// Добавить прямоугольник к занятым частям слоя: сливается со всеми, которых касается
static void box_add(f32 x, f32 y, f32 w, f32 h)
{
    f32 (*b)[4] = U.box[U.layer];
    u32 *n = &U.boxes[U.layer];
    f32 r[4] = { x - UI_PAD, y - UI_PAD, x + w + UI_PAD, y + h + UI_PAD };
    // Обычно следующая буква попадает в последний прямоугольник
    if (*n && b[*n - 1][0] <= r[0] && b[*n - 1][1] <= r[1] && b[*n - 1][2] >= r[2] && b[*n - 1][3] >= r[3]) return;
    for (u32 i = 0; i < *n;) {
        if (!box_touch(b[i], r) && *n < UI_BOXES) { i++; continue; }
        r[0] = fminf(r[0], b[i][0]); r[1] = fminf(r[1], b[i][1]);
        r[2] = fmaxf(r[2], b[i][2]); r[3] = fmaxf(r[3], b[i][3]);
        --*n;
        for (u32 k = 0; k < 4; k++) b[i][k] = b[*n][k];
        i = 0;
    }
    memcpy(b[(*n)++], r, sizeof r);
}

static void quad2_px(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c, u32 c2)
{
    u32 *n = &U.count[U.layer];
    if (*n + 6 > ARRAY_COUNT(U.v[0]) || w <= 0.0f || h <= 0.0f) return;
    box_add(x, y, w, h);
    UiVertex *v = &U.v[U.layer][*n];
    v[0] = (UiVertex){ x, y, u0, v0, c };
    v[1] = (UiVertex){ x + w, y, u1, v0, c };
    v[2] = (UiVertex){ x + w, y + h, u1, v1, c2 };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (UiVertex){ x, y + h, u0, v1, c2 };
    *n += 6;
}

// Логический прямоугольник в пиксели цели: края по сетке, тонкая линия не пропадает
static void quad2(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c, u32 c2)
{
    const f32 f = U.f[U.layer];
    f32 x0 = roundf(x * f), y0 = roundf(y * f);
    f32 pw = fmaxf(roundf((x + w) * f) - x0, w > 0.0f ? 1.0f : 0.0f);
    f32 ph = fmaxf(roundf((y + h) * f) - y0, h > 0.0f ? 1.0f : 0.0f);
    quad2_px(x0, y0, pw, ph, u0, v0, u1, v1, c, c2);
}

static void quad(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c)
{
    quad2(x, y, w, h, u0, v0, u1, v1, c, c);
}

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color)
{
    const f32 t = (ATLAS - 0.5f) / ATLAS;
    quad(x, y, w, h, t, t, t, t, color);
}

void ui_rect_grad(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom)
{
    const f32 t = (ATLAS - 0.5f) / ATLAS;
    quad2(x, y, w, h, t, t, t, t, top, bottom);
}

// u < -0.5 — заливка панели: шейдер добавляет сканлайны и шум
void ui_panel_fill(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom)
{
    quad2(x, y, w, h, -1, -1, -1, -1, top, bottom);
}

// u >= 2 — картинка из ui_set_image (портреты, HDR)
void ui_image(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 tint)
{
    quad(x, y, w, h, u0 + 2.0f, v0, u1 + 2.0f, v1, tint);
}

void ui_set_image(RbTex tex) { U.image = tex; }
void ui_set_time(f32 t) { U.time = t; }

void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 color)
{
    ui_rect(x, y, w, 1, color);
    ui_rect(x, y + h - 1, w, 1, color);
    ui_rect(x, y + 1, 1, h - 2, color);
    ui_rect(x + w - 1, y + 1, 1, h - 2, color);
}

static u32 utf8_next(const char **ps)
{
    const u8 *s = (const u8 *)*ps;
    u32 c = s[0], n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else { c &= 0x07; n = 3; }
    u32 i = 1;
    for (; i <= n && (s[i] & 0xC0) == 0x80; i++) c = (c << 6) | (s[i] & 0x3F);
    *ps += i;
    return c;
}

// Буква — ровно 5x7 пикселей цели; по высоте строка по центру логических 7 единиц, как раньше
f32 ui_text(f32 x, f32 y, u32 color, const char *s)
{
    const f32 inv = 1.0f / ATLAS, f = U.f[U.layer];
    f32 px = roundf(x * f), py = roundf((y + UI_GLYPH_H * 0.5f) * f - UI_GLYPH_H * 0.5f);
    while (*s) {
        u32 g = find_glyph(utf8_next(&s));
        if (g) {
            g--;
            f32 u0 = (f32)((g % (ATLAS / CELL)) * CELL) * inv, v0 = (f32)((g / (ATLAS / CELL)) * CELL) * inv;
            quad2_px(px, py, UI_GLYPH_W, UI_GLYPH_H, u0, v0, u0 + UI_GLYPH_W * inv, v0 + UI_GLYPH_H * inv, color, color);
        }
        px += UI_ADVANCE;
    }
    return px / f;
}

f32 ui_num(f32 x, f32 y, u32 color, const char *s)
{
    const f32 inv = 1.0f / ATLAS;
    for (; *s; s++) {
        for (u32 i = 0; i < SMALL_COUNT; i++) {
            if (g_small[i].cp != (u32)(u8)*s) continue;
            u32 k = FONT_COUNT + i;
            f32 u0 = (f32)((k % (ATLAS / CELL)) * CELL) * inv, v0 = (f32)((k / (ATLAS / CELL)) * CELL) * inv;
            quad(x, y, UI_SMALL_W, UI_SMALL_H, u0, v0, u0 + UI_SMALL_W * inv, v0 + UI_SMALL_H * inv, color);
            break;
        }
        x += UI_SMALL_W + 1;
    }
    return x;
}

f32 ui_num_width(const char *s)
{
    f32 n = (f32)strlen(s);
    return n > 0 ? n * (UI_SMALL_W + 1) - 1 : 0;
}

f32 ui_text_width(const char *s)
{
    f32 w = 0;
    while (*s) {
        utf8_next(&s);
        w += UI_ADVANCE;
    }
    return w > 0 ? (w - 1) / U.f[U.layer] : 0;
}

// Слой в свою цель, затем на окно: ровные пиксели при дробном масштабе (тот же фильтр, что у кадра)
static void draw_layer(u32 l)
{
    i32 tw = (i32)floorf((f32)U.win_w / U.text[l]), th = (i32)floorf((f32)U.win_h / U.text[l]);
    if (tw != U.tw[l] || th != U.th[l]) {
        rb_tex_free(&U.target[l]);
        U.target[l] = rb_tex_make(&(RbTexDesc){ .fmt = RB_FMT_RGBA8, .w = tw, .h = th, .usage = RB_TEX_TARGET | RB_TEX_SAMPLED });
        U.tw[l] = tw;
        U.th[l] = th;
    }
    // Прозрачный фон: цвет копится уже умноженным на альфу
    rb_pass_begin(&(RbPass){ .color = { { .tex = U.target[l] } } });
    static const f32 clear[4] = { 0, 0, 0, 0 };
    for (u32 i = 0; i < U.boxes[l]; i++) {
        const f32 *b = U.box[l][i];
        i32 x0 = MAX((i32)floorf(b[0]), 0), y0 = MAX((i32)floorf(b[1]), 0);
        i32 x1 = MIN((i32)ceilf(b[2]), tw), y1 = MIN((i32)ceilf(b[3]), th);
        if (x1 > x0 && y1 > y0) rb_clear_rect(x0, th - y1, x1 - x0, y1 - y0, clear, false);
    }
    size_t off = ((size_t)rb_frame_slot() * UI_LAYERS + l) * sizeof U.v[0];
    rb_buf_write(U.vbo, off, U.count[l] * sizeof(UiVertex), U.v[l]);
    rb_bind_pipe(U.pipe);
    rb_bind_vertex(0, U.vbo, off);
    f32 push[4] = { (f32)tw, (f32)th, U.time, 0.0f };
    rb_push(0, sizeof push, push);
    rb_bind_tex(B_UI_ATLAS, U.tex, RB_SMP_NEAREST_REPEAT);
    if (U.image) rb_bind_tex(B_UI_IMAGE, U.image, RB_SMP_NEAREST);
    rb_draw(U.count[l], 0, 1);
    rb_pass_end();

    rb_pass_begin(&(RbPass){ 0 });
    rb_bind_pipe(U.pipe_up);
    f32 out[4] = { (f32)U.win_w, (f32)U.win_h, 0.0f, 0.0f };
    rb_push(0, sizeof out, out);
    rb_bind_tex(B_UI_ATLAS, U.target[l], RB_SMP_LINEAR);
    const f32 sx = (f32)U.win_w / (f32)tw, sy = (f32)U.win_h / (f32)th;
    for (u32 i = 0; i < U.boxes[l]; i++) {
        const f32 *b = U.box[l][i];
        f32 x0 = fmaxf(floorf(b[0]) + 1, 0), y0 = fmaxf(floorf(b[1]) + 1, 0);
        f32 x1 = fminf(ceilf(b[2]) - 1, (f32)tw), y1 = fminf(ceilf(b[3]) - 1, (f32)th);
        if (x1 <= x0 || y1 <= y0) continue;
        i32 wx0 = (i32)floorf(x0 * sx), wx1 = (i32)ceilf(x1 * sx);
        i32 wy0 = (i32)floorf(((f32)th - y1) * sy), wy1 = (i32)ceilf(((f32)th - y0) * sy);
        rb_scissor(wx0, wy0, wx1 - wx0, wy1 - wy0);
        rb_draw(3, 0, 1);
    }
    rb_pass_end();
}

void ui_end(void)
{
    if (U.win_w <= 0 || U.win_h <= 0) return;
    for (u32 l = 0; l < UI_LAYERS; l++)
        if (U.count[l]) draw_layer(l);
}
