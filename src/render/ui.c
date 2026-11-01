#include "ui.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"

#include "rb.h"
#include "render.h"

#define UI_MAX_QUADS 16384

typedef struct UiVertex {
    f32 x, y, u, v;
    u32 color;
} UiVertex;

// Формат .font (tools/fontconv/fontconv.py), little-endian:
// "AFNT", u32 версия 1, u16 ширина и высота атласа, u16 кеглей, u16 букв;
// по кеглю: i16 размер, подъём, спуск, высота заглавной; u32 коды букв;
// по кеглю по букве: u16 x, y в атласе, u8 w, h, i8 смещение от пера и от линии строки, u16 шаг пера ×64; атлас R8
#define FONT_SIZES_MAX 48
#define FONT_CHARS_MAX 160
#define FONT_FILE "ui/plex.font"
#define FONT_TRACK 0.04f   // разрядка заглавных, доля кегля

typedef struct FontGlyph {
    u16 x, y;
    u8  w, h;
    i8  ox, oy;
    u16 adv;   // ×64
} FontGlyph;

typedef struct FontSize {
    i16 px, asc, desc, cap;
} FontSize;

// Кегли интерфейса на 1080p: основной, мелкий (числа в клетках), крупный (часы)
static const f32 g_font_px[UI_FONTS] = { 14.0f, 11.0f, 19.0f };

// Привязки текстур (ui.frag, upscale.frag)
enum { B_UI_ATLAS = 20, B_UI_IMAGE = 21 };

#define UI_BOXES 32
#define UI_PAD   2.0f  // запас вокруг занятых частей цели

static struct {
    RbPipe pipe;
    RbPipe pipe_up;            // слой рисуется в свою цель и накладывается на окно
    RbBuf vbo;                 // кольцо кадров: RB_FRAMES × UI_LAYERS кусков по слою
    RbTex tex, target[UI_LAYERS];
    i32 tw[UI_LAYERS], th[UI_LAYERS];
    // Шрифт
    u32 aw, ah;                // атлас
    u32 sizes, chars;
    FontSize size[FONT_SIZES_MAX];
    u32 cp[FONT_CHARS_MAX];
    FontGlyph glyph[FONT_SIZES_MAX][FONT_CHARS_MAX];
    u8  lut_ascii[128];        // номер буквы + 1, 0 — нет
    u8  lut_cyr[0x60];         // U+0400..U+045F
    u32 font;                  // текущий UI_FONT_*
    u32 font_size[UI_FONTS];   // номер кегля в файле на этот кадр
    f32 scale[UI_LAYERS];      // пикселей окна на логическую единицу
    f32 text_k;                // кегль относительно 1080p с ползунком
    f32 user_scale, user_text; // ползунки отладочного меню
    u32 layer;
    i32 win_w, win_h;
    RbTex image;               // текстура для ui_image
    f32 time;
    u32 count[UI_LAYERS];      // вершин в кадре
    // Занятые части цели в кадре (x0, y0, x1, y1) с запасом UI_PAD, не пересекаются.
    // Чистятся и накладываются на окно только они
    f32 box[UI_LAYERS][UI_BOXES][4];
    u32 boxes[UI_LAYERS];
    UiVertex v[UI_LAYERS][UI_MAX_QUADS * 6];
} U;

// Номер буквы + 1; строчные — как заглавные (интерфейс пишет заглавными), 0 — нет
static u32 find_glyph(u32 cp)
{
    if (cp >= 'a' && cp <= 'z') cp -= 32;
    else if (cp >= 0x0430 && cp <= 0x044F) cp -= 0x20;
    else if (cp == 0x0451) cp = 0x0401;
    if (cp < 128) return U.lut_ascii[cp];
    if (cp >= 0x0400 && cp < 0x0460) return U.lut_cyr[cp - 0x0400];
    for (u32 i = 0; i < U.chars; i++)
        if (U.cp[i] == cp) return i + 1;
    return 0;
}

static bool font_load(const char *data_dir, u8 **pix)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", data_dir, FONT_FILE);
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("ИНТЕРФЕЙС: нет шрифта %s", path);
        return false;
    }
    char magic[4];
    u32 ver = 0;
    u16 hd[4];
    bool ok = fread(magic, 4, 1, f) == 1 && !memcmp(magic, "AFNT", 4) && fread(&ver, 4, 1, f) == 1 && ver == 1 &&
              fread(hd, sizeof hd, 1, f) == 1 && hd[2] >= 1 && hd[2] <= FONT_SIZES_MAX && hd[3] >= 1 && hd[3] <= FONT_CHARS_MAX;
    if (ok) {
        U.aw = hd[0], U.ah = hd[1], U.sizes = hd[2], U.chars = hd[3];
        ok = fread(U.size, sizeof(FontSize), U.sizes, f) == U.sizes && fread(U.cp, sizeof(u32), U.chars, f) == U.chars;
        for (u32 i = 0; ok && i < U.sizes; i++) ok = fread(U.glyph[i], sizeof(FontGlyph), U.chars, f) == U.chars;
    }
    if (ok) {
        *pix = malloc((size_t)U.aw * U.ah);  // один раз при запуске
        ok = *pix && fread(*pix, (size_t)U.aw * U.ah, 1, f) == 1;
    }
    fclose(f);
    if (!ok) {
        log_error("ИНТЕРФЕЙС: шрифт %s повреждён", path);
        return false;
    }
    for (u32 i = 0; i < U.chars; i++) {
        if (U.cp[i] < 128) U.lut_ascii[U.cp[i]] = (u8)(i + 1);
        else if (U.cp[i] >= 0x0400 && U.cp[i] < 0x0460) U.lut_cyr[U.cp[i] - 0x0400] = (u8)(i + 1);
    }
    return true;
}

bool ui_init(const char *data_dir)
{
    u8 *pix = NULL;
    if (!font_load(data_dir, &pix)) return false;
    pix[U.aw * U.ah - 1] = 255;  // сплошная заливка

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
    if (U.pipe && U.pipe_up) {
        U.tex = rb_tex_make(&(RbTexDesc){ .fmt = RB_FMT_R8, .w = (i32)U.aw, .h = (i32)U.ah, .usage = RB_TEX_SAMPLED });
        rb_tex_write(U.tex, 0, 0, 0, 0, (i32)U.aw, (i32)U.ah, 1, pix, 0);
    }
    free(pix);
    if (!U.pipe || !U.pipe_up) return false;
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
    // От высоты окна, база 1080p, шаг 0.25. 1440p: нижняя полоса ×1.75, окна и остальное ×2
    static const f32 k[UI_LAYERS] = { 1.3125f, 1.5f, 1.5f };
    const f32 base = (f32)win_h / 1080.0f;
    for (u32 i = 0; i < UI_LAYERS; i++) {
        U.scale[i] = fmaxf(1.0f, roundf(base * k[i] * U.user_scale * 4.0f) * 0.25f);
        U.count[i] = 0;
        U.boxes[i] = 0;
    }
    // Кегль — ровный размер из файла: ближайший к нужному, без растяжения
    U.text_k = base * U.user_text;
    for (u32 f = 0; f < UI_FONTS; f++) {
        f32 want = g_font_px[f] * U.text_k;
        u32 best = 0;
        for (u32 i = 1; i < U.sizes; i++)
            if (fabsf((f32)U.size[i].px - want) < fabsf((f32)U.size[best].px - want)) best = i;
        U.font_size[f] = best;
    }
    U.font = UI_FONT_TEXT;
    U.layer = UI_TOP;
}

void ui_layer(u32 layer) { U.layer = MIN(layer, UI_LAYERS - 1); }
u32 ui_current_layer(void) { return U.layer; }
void ui_font(u32 font) { U.font = MIN(font, UI_FONTS - 1); }
f32 ui_scale(void) { return U.scale[U.layer]; }
f32 ui_width(void) { return floorf((f32)U.win_w / U.scale[U.layer]); }
f32 ui_height(void) { return floorf((f32)U.win_h / U.scale[U.layer]); }
f32 ui_text_scale(void) { return U.text_k; }
f32 ui_hair(void) { return fmaxf(1.0f, roundf(U.scale[U.layer] * 0.67f)) / U.scale[U.layer]; }

static const FontSize *cur_size(void) { return &U.size[U.font_size[U.font]]; }
// Строка: заглавные по середине, сверху и снизу по трети высоты заглавной
static f32 line_px(void) { return roundf((f32)cur_size()->cap * 1.9f); }
f32 ui_line(void) { return line_px() / U.scale[U.layer]; }
f32 ui_cap(void) { return (f32)cur_size()->cap / U.scale[U.layer]; }

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

// Логический прямоугольник в пиксели окна: края по сетке, тонкая линия не пропадает
static void quad2(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c, u32 c2)
{
    const f32 f = U.scale[U.layer];
    f32 x0 = roundf(x * f), y0 = roundf(y * f);
    f32 pw = fmaxf(roundf((x + w) * f) - x0, w > 0.0f ? 1.0f : 0.0f);
    f32 ph = fmaxf(roundf((y + h) * f) - y0, h > 0.0f ? 1.0f : 0.0f);
    quad2_px(x0, y0, pw, ph, u0, v0, u1, v1, c, c2);
}

static void quad(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 c)
{
    quad2(x, y, w, h, u0, v0, u1, v1, c, c);
}

static void solid_uv(f32 *u, f32 *v)
{
    *u = ((f32)U.aw - 0.5f) / (f32)U.aw;
    *v = ((f32)U.ah - 0.5f) / (f32)U.ah;
}

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 color)
{
    f32 u, v;
    solid_uv(&u, &v);
    quad(x, y, w, h, u, v, u, v, color);
}

void ui_rect_grad(f32 x, f32 y, f32 w, f32 h, u32 top, u32 bottom)
{
    f32 u, v;
    solid_uv(&u, &v);
    quad2(x, y, w, h, u, v, u, v, top, bottom);
}

// Четыре цвета по углам: верх-лево, верх-право, низ-право, низ-лево
static void quad4_px(f32 x, f32 y, f32 w, f32 h, u32 c0, u32 c1, u32 c2, u32 c3)
{
    u32 *n = &U.count[U.layer];
    if (*n + 6 > ARRAY_COUNT(U.v[0]) || w <= 0.0f || h <= 0.0f) return;
    f32 u, v;
    solid_uv(&u, &v);
    box_add(x, y, w, h);
    UiVertex *q = &U.v[U.layer][*n];
    UiVertex a = { x, y, u, v, c0 }, b = { x + w, y, u, v, c1 }, c = { x + w, y + h, u, v, c2 }, d = { x, y + h, u, v, c3 };
    // Диагональ через непрозрачный угол: у угловых кусков растушёвка не ломается
    if ((c1 >> 24) || (c3 >> 24)) {
        q[0] = a, q[1] = b, q[2] = d, q[3] = b, q[4] = c, q[5] = d;
    } else {
        q[0] = a, q[1] = b, q[2] = c, q[3] = a, q[4] = c, q[5] = d;
    }
    *n += 6;
}

// Панель без рамки: заливка, края на feather логических единиц уходят в прозрачность
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 color, f32 feather)
{
    const f32 f = U.scale[U.layer];
    f32 x0 = roundf(x * f), y0 = roundf(y * f), x1 = roundf((x + w) * f), y1 = roundf((y + h) * f);
    f32 e = fminf(roundf(feather * f), fminf((x1 - x0), (y1 - y0)) * 0.5f);
    const u32 o = color & 0x00FFFFFFu, c = color;
    f32 ix0 = x0 + e, iy0 = y0 + e, ix1 = x1 - e, iy1 = y1 - e;
    quad4_px(ix0, iy0, ix1 - ix0, iy1 - iy0, c, c, c, c);
    quad4_px(ix0, y0, ix1 - ix0, e, o, o, c, c);       // верх
    quad4_px(ix0, iy1, ix1 - ix0, e, c, c, o, o);      // низ
    quad4_px(x0, iy0, e, iy1 - iy0, o, c, c, o);       // лево
    quad4_px(ix1, iy0, e, iy1 - iy0, c, o, o, c);      // право
    quad4_px(x0, y0, e, e, o, o, c, o);                // углы
    quad4_px(ix1, y0, e, e, o, o, o, c);
    quad4_px(ix1, iy1, e, e, c, o, o, o);
    quad4_px(x0, iy1, e, e, o, c, o, o);
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

// Тонкие линии — ровно в ui_hair, внутрь прямоугольника
void ui_hline(f32 x, f32 y, f32 w, u32 color)
{
    const f32 f = U.scale[U.layer], t = ui_hair() * f;
    f32 x0 = roundf(x * f), x1 = roundf((x + w) * f);
    f32 u, v;
    solid_uv(&u, &v);
    quad2_px(x0, roundf(y * f), x1 - x0, t, u, v, u, v, color, color);
}

void ui_vline(f32 x, f32 y, f32 h, u32 color)
{
    const f32 f = U.scale[U.layer], t = ui_hair() * f;
    f32 y0 = roundf(y * f), y1 = roundf((y + h) * f);
    f32 u, v;
    solid_uv(&u, &v);
    quad2_px(roundf(x * f), y0, t, y1 - y0, u, v, u, v, color, color);
}

void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 color)
{
    const f32 f = U.scale[U.layer], t = ui_hair() * f;
    f32 x0 = roundf(x * f), y0 = roundf(y * f), x1 = roundf((x + w) * f), y1 = roundf((y + h) * f);
    f32 u, v;
    solid_uv(&u, &v);
    quad2_px(x0, y0, x1 - x0, t, u, v, u, v, color, color);
    quad2_px(x0, y1 - t, x1 - x0, t, u, v, u, v, color, color);
    quad2_px(x0, y0 + t, t, y1 - y0 - 2 * t, u, v, u, v, color, color);
    quad2_px(x1 - t, y0 + t, t, y1 - y0 - 2 * t, u, v, u, v, color, color);
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

// Шаг пера с разрядкой, пиксели окна
static f32 pen_step(const FontGlyph *g, const FontSize *fs)
{
    return (f32)g->adv / 64.0f + roundf((f32)fs->px * FONT_TRACK);
}

static const FontGlyph *glyph_of(u32 cp)
{
    u32 g = find_glyph(cp);
    if (!g) g = find_glyph('?');
    return g ? &U.glyph[U.font_size[U.font]][g - 1] : NULL;
}

// y — верх строки ui_line; заглавные по её середине. Буквы ставятся в целые пиксели окна
f32 ui_text(f32 x, f32 y, u32 color, const char *s)
{
    const f32 f = U.scale[U.layer], iw = 1.0f / (f32)U.aw, ih = 1.0f / (f32)U.ah;
    const FontSize *fs = cur_size();
    f32 pen = roundf(x * f);
    const f32 base = roundf(y * f) + roundf((line_px() + (f32)fs->cap) * 0.5f);
    while (*s) {
        const FontGlyph *g = glyph_of(utf8_next(&s));
        if (!g) continue;
        if (g->w && g->h) {
            f32 u0 = (f32)g->x * iw, v0 = (f32)g->y * ih;
            quad2_px(roundf(pen) + g->ox, base + g->oy, g->w, g->h, u0, v0, u0 + g->w * iw, v0 + g->h * ih, color, color);
        }
        pen += pen_step(g, fs);
    }
    return pen / f;
}

f32 ui_text_width(const char *s)
{
    const FontSize *fs = cur_size();
    f32 w = 0;
    bool any = false;
    while (*s) {
        const FontGlyph *g = glyph_of(utf8_next(&s));
        if (!g) continue;
        w += pen_step(g, fs);
        any = true;
    }
    if (any) w -= roundf((f32)fs->px * FONT_TRACK);
    return w / U.scale[U.layer];
}

// Сколько байт строки влезает в ширину w
u32 ui_text_fit(const char *s, f32 w)
{
    const FontSize *fs = cur_size();
    const f32 lim = w * U.scale[U.layer];
    const char *p = s;
    f32 pen = 0;
    while (*p) {
        const char *q = p;
        const FontGlyph *g = glyph_of(utf8_next(&q));
        f32 step = g ? pen_step(g, fs) : 0.0f;
        if (pen + step - roundf((f32)fs->px * FONT_TRACK) > lim) break;
        pen += step;
        p = q;
    }
    return (u32)(p - s);
}

// Числа — мелким кеглем, верх строки y
f32 ui_num(f32 x, f32 y, u32 color, const char *s)
{
    u32 was = U.font;
    U.font = UI_FONT_SMALL;
    f32 r = ui_text(x, y, color, s);
    U.font = was;
    return r;
}

f32 ui_num_width(const char *s)
{
    u32 was = U.font;
    U.font = UI_FONT_SMALL;
    f32 r = ui_text_width(s);
    U.font = was;
    return r;
}

// Слой в свою цель (размер окна), затем на окно
static void draw_layer(u32 l)
{
    i32 tw = U.win_w, th = U.win_h;
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
    rb_bind_tex(B_UI_ATLAS, U.target[l], RB_SMP_NEAREST);
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
