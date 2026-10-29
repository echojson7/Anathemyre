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

#define FONT_COUNT ARRAY_COUNT(g_font)
_Static_assert(FONT_COUNT < (ATLAS / CELL) * (ATLAS / CELL), "шрифт не влезает в атлас");

static struct {
    u32 prog, vao, vbo, tex;
    u8  lut_ascii[128];   // номер буквы + 1, 0 — нет
    u8  lut_cyr[0x60];    // U+0400..U+045F
    i32 scale, win_w, win_h;
    u32 count;            // вершин в кадре
    UiVertex v[UI_MAX_QUADS * 6];
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
    pix[ATLAS * ATLAS - 1] = 255;  // сплошная заливка

    U.prog = render_program("ui.vert", "ui.frag");
    if (!U.prog) return false;
    glCreateTextures(GL_TEXTURE_2D, 1, &U.tex);
    glTextureStorage2D(U.tex, 1, GL_R8, ATLAS, ATLAS);
    glTextureSubImage2D(U.tex, 0, 0, 0, ATLAS, ATLAS, GL_RED, GL_UNSIGNED_BYTE, pix);
    glTextureParameteri(U.tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(U.tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glCreateBuffers(1, &U.vbo);
    glNamedBufferStorage(U.vbo, (GLsizeiptr)sizeof U.v, NULL, GL_DYNAMIC_STORAGE_BIT);
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
    if (U.tex) glDeleteTextures(1, &U.tex);
    if (U.vbo) glDeleteBuffers(1, &U.vbo);
    if (U.vao) glDeleteVertexArrays(1, &U.vao);
}

void ui_begin(i32 win_w, i32 win_h)
{
    U.win_w = win_w;
    U.win_h = win_h;
    U.scale = MAX(1, win_h / 360);
    U.count = 0;
}

i32 ui_scale(void) { return U.scale; }
f32 ui_width(void) { return (f32)(U.win_w / U.scale); }
f32 ui_height(void) { return (f32)(U.win_h / U.scale); }

static void quad(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c)
{
    if (U.count + 6 > ARRAY_COUNT(U.v) || w <= 0.0f || h <= 0.0f) return;
    f32 s = (f32)U.scale;
    x *= s; y *= s; w *= s; h *= s;
    UiVertex *v = &U.v[U.count];
    v[0] = (UiVertex){ x, y, u0, v0, c };
    v[1] = (UiVertex){ x + w, y, u1, v0, c };
    v[2] = (UiVertex){ x + w, y + h, u1, v1, c };
    v[3] = v[0];
    v[4] = v[2];
    v[5] = (UiVertex){ x, y + h, u0, v1, c };
    U.count += 6;
}

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color)
{
    const f32 t = (ATLAS - 0.5f) / ATLAS;
    quad(x, y, w, h, t, t, t, t, color);
}

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

f32 ui_text_width(const char *s)
{
    f32 w = 0;
    while (*s) {
        utf8_next(&s);
        w += UI_ADVANCE;
    }
    return w > 0 ? w - 1 : 0;
}

void ui_end(void)
{
    if (!U.count) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, U.win_w, U.win_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glNamedBufferSubData(U.vbo, 0, (GLsizeiptr)(U.count * sizeof(UiVertex)), U.v);
    glUseProgram(U.prog);
    glUniform2f(0, (f32)U.win_w, (f32)U.win_h);
    glBindTextureUnit(0, U.tex);
    glBindVertexArray(U.vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)U.count);
    glDisable(GL_BLEND);
}
