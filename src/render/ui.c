#include "ui.h"

#include <string.h>

#include "gl.h"
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

static struct {
    u32 prog, vao, vbo, tex;
    u32 prog_up;               // слой рисуется в своём масштабе и ровно растягивается на окно
    u32 fbo[UI_LAYERS], target[UI_LAYERS];
    i32 tw[UI_LAYERS], th[UI_LAYERS];  // размер цели, логические пиксели слоя
    u8  lut_ascii[128];   // номер буквы + 1, 0 — нет
    u8  lut_cyr[0x60];    // U+0400..U+045F
    f32 scale[UI_LAYERS];
    u32 layer;
    i32 win_w, win_h;
    u32 image;            // текстура для ui_image
    f32 time;
    u32 count[UI_LAYERS];  // вершин в кадре
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

    U.prog = render_program("ui.vert", "ui.frag");
    U.prog_up = render_program("fullscreen.vert", "upscale.frag");
    if (!U.prog || !U.prog_up) return false;
    glCreateFramebuffers(UI_LAYERS, U.fbo);
    glCreateTextures(GL_TEXTURE_2D, 1, &U.tex);
    glTextureStorage2D(U.tex, 1, GL_R8, ATLAS, ATLAS);
    glTextureSubImage2D(U.tex, 0, 0, 0, ATLAS, ATLAS, GL_RED, GL_UNSIGNED_BYTE, pix);
    glTextureParameteri(U.tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(U.tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glCreateBuffers(1, &U.vbo);
    glNamedBufferStorage(U.vbo, (GLsizeiptr)sizeof U.v[0], NULL, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &U.vao);
    glVertexArrayVertexBuffer(U.vao, 0, U.vbo, 0, sizeof(UiVertex));
    glEnableVertexArrayAttrib(U.vao, 0);
    glEnableVertexArrayAttrib(U.vao, 1);
    glEnableVertexArrayAttrib(U.vao, 2);
    glVertexArrayAttribFormat(U.vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribFormat(U.vao, 1, 2, GL_FLOAT, GL_FALSE, 8);
    glVertexArrayAttribFormat(U.vao, 2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 16);
    glVertexArrayAttribBinding(U.vao, 0, 0);
    glVertexArrayAttribBinding(U.vao, 1, 0);
    glVertexArrayAttribBinding(U.vao, 2, 0);
    return true;
}

void ui_shutdown(void)
{
    if (U.prog) glDeleteProgram(U.prog);
    if (U.prog_up) glDeleteProgram(U.prog_up);
    glDeleteTextures(UI_LAYERS, U.target);
    glDeleteFramebuffers(UI_LAYERS, U.fbo);
    if (U.tex) glDeleteTextures(1, &U.tex);
    if (U.vbo) glDeleteBuffers(1, &U.vbo);
    if (U.vao) glDeleteVertexArrays(1, &U.vao);
}

void ui_begin(i32 win_w, i32 win_h)
{
    U.win_w = win_w;
    U.win_h = win_h;
    // Шаг 0.25 от высоты окна. 1440p: нижняя полоса ×1.75, окна и остальное ×2.25
    static const f32 k[UI_LAYERS] = { 1.17f, 1.5f };
    for (u32 i = 0; i < UI_LAYERS; i++) {
        U.scale[i] = fmaxf(1.0f, roundf((f32)win_h / 960.0f * k[i] * 4.0f) * 0.25f);
        U.count[i] = 0;
    }
    U.layer = UI_TOP;
}

void ui_layer(u32 layer) { U.layer = MIN(layer, UI_LAYERS - 1); }
f32 ui_scale(void) { return U.scale[U.layer]; }
f32 ui_width(void) { return floorf((f32)U.win_w / U.scale[U.layer]); }
f32 ui_height(void) { return floorf((f32)U.win_h / U.scale[U.layer]); }

static void quad2(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c, u32 c2)
{
    u32 *n = &U.count[U.layer];
    if (*n + 6 > ARRAY_COUNT(U.v[0]) || w <= 0.0f || h <= 0.0f) return;
    UiVertex *v = &U.v[U.layer][*n];
    v[0] = (UiVertex){ x, y, u0, v0, c };
    v[1] = (UiVertex){ x + w, y, u1, v0, c };
    v[2] = (UiVertex){ x + w, y + h, u1, v1, c2 };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (UiVertex){ x, y + h, u0, v1, c2 };
    *n += 6;
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

void ui_set_image(u32 tex) { U.image = tex; }
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

f32 ui_text(f32 x, f32 y, u32 color, const char *s)
{
    const f32 inv = 1.0f / ATLAS;
    while (*s) {
        u32 g = find_glyph(utf8_next(&s));
        if (g) {
            g--;
            f32 u0 = (f32)((g % (ATLAS / CELL)) * CELL) * inv, v0 = (f32)((g / (ATLAS / CELL)) * CELL) * inv;
            quad(x, y, UI_GLYPH_W, UI_GLYPH_H, u0, v0, u0 + UI_GLYPH_W * inv, v0 + UI_GLYPH_H * inv, color);
        }
        x += UI_ADVANCE;
    }
    return x;
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
    return w > 0 ? w - 1 : 0;
}

// Слой в свою цель, затем на окно: ровные пиксели при дробном масштабе (тот же фильтр, что у кадра)
static void draw_layer(u32 l)
{
    i32 tw = (i32)floorf((f32)U.win_w / U.scale[l]), th = (i32)floorf((f32)U.win_h / U.scale[l]);
    if (tw != U.tw[l] || th != U.th[l]) {
        if (U.target[l]) glDeleteTextures(1, &U.target[l]);
        glCreateTextures(GL_TEXTURE_2D, 1, &U.target[l]);
        glTextureStorage2D(U.target[l], 1, GL_RGBA8, tw, th);
        glTextureParameteri(U.target[l], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(U.target[l], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(U.target[l], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(U.target[l], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glNamedFramebufferTexture(U.fbo[l], GL_COLOR_ATTACHMENT0, U.target[l], 0);
        U.tw[l] = tw;
        U.th[l] = th;
    }
    // Прозрачный фон: цвет копится уже умноженным на альфу
    glBindFramebuffer(GL_FRAMEBUFFER, U.fbo[l]);
    glViewport(0, 0, tw, th);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glNamedBufferSubData(U.vbo, 0, (GLsizeiptr)(U.count[l] * sizeof(UiVertex)), U.v[l]);
    glUseProgram(U.prog);
    glUniform2f(0, (f32)tw, (f32)th);
    glUniform1f(1, U.time);
    glBindTextureUnit(0, U.tex);
    if (U.image) glBindTextureUnit(1, U.image);
    glBindVertexArray(U.vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)U.count[l]);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, U.win_w, U.win_h);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(U.prog_up);
    glUniform2f(43, (f32)U.win_w, (f32)U.win_h);
    glBindTextureUnit(0, U.target[l]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void ui_end(void)
{
    if (U.win_w <= 0 || U.win_h <= 0) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    for (u32 l = 0; l < UI_LAYERS; l++)
        if (U.count[l]) draw_layer(l);
    glDisable(GL_BLEND);
}
