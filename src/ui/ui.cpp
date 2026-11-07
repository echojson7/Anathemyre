// Интерфейс: шрифт из .font, прямоугольники и буквы в пикселях окна (docs/ui.md)
#include "ui.h"
#include "ui_state.h"
#include "../core/log.h"
#include "../render/render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

UiState g_ui_state;

// Формат .font (tools/fontconv/fontconv.py): «AFNT», версия, ширина и высота атласа, число кеглей и букв;
// метрики кеглей; коды букв; буквы по кеглям (x, y, w, h, смещение от пера и линии строки, шаг в 1/64 пикселя); атлас
struct FontGlyphDisk {
    u16 x, y;
    u8 w, h;
    i8 ox, oy;
    u16 adv64;
};

static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

bool ui_init(Arena &perm, Arena &scratch, const char *data_dir) {
    UiState &u = g_ui_state;
    char path[1024];
    snprintf(path, sizeof path, "%sui/plex.font", data_dir);
    usize mark = arena_mark(scratch);
    u8 *d = nullptr;
    usize size = 0;
    if (!platform_read_file(path, scratch, &d, &size)) {
        log_error("нет шрифта %s", path);
        return false;
    }
    bool ok = size >= 16 && !memcmp(d, "AFNT", 4) && rd32(d + 4) == 1;
    u32 aw = ok ? rd16(d + 8) : 0, ah = ok ? rd16(d + 10) : 0;
    u32 ns = ok ? rd16(d + 12) : 0, nc = ok ? rd16(d + 14) : 0;
    usize off_m = 16, off_c = off_m + ns * 8, off_g = off_c + nc * 4, off_a = off_g + (usize)ns * nc * 10;
    ok = ok && ns <= UI_MAX_SIZES && aw % 4 == 0 && size == off_a + (usize)aw * ah;
    if (!ok) {
        log_error("%s: не тот формат", path);
        arena_restore(scratch, mark);
        return false;
    }
    u.nsizes = ns;
    u.nchars = nc;
    for (u32 i = 0; i < ns; ++i) {
        const u8 *m = d + off_m + i * 8;
        u.sizes[i] = {(i16)rd16(m), (i16)rd16(m + 2), (i16)rd16(m + 4), (i16)rd16(m + 6)};
    }
    for (u32 c = 0; c < UI_MAX_CODE; ++c) u.char_index[c] = 0xFFFF;
    for (u32 i = 0; i < nc; ++i) {
        u32 cp = rd32(d + off_c + i * 4);
        if (cp < UI_MAX_CODE) u.char_index[cp] = (u16)i;
    }
    u.glyphs = arena_push<UiGlyph>(perm, (usize)ns * nc);
    for (usize i = 0; i < (usize)ns * nc; ++i) {
        const u8 *g = d + off_g + i * 10;
        u.glyphs[i] = {rd16(g), rd16(g + 2), g[4], g[5], (i8)g[6], (i8)g[7], (f32)rd16(g + 8) / 64.0f};
    }
    ok = render_ui_font(d + off_a, aw, ah);
    arena_restore(scratch, mark);
    if (!ok) {
        log_error("атлас шрифта не загрузился");
        return false;
    }
    u.ready = true;
    log_info("интерфейс: %s, %u кеглей, %u букв", path, ns, nc);
    return true;
}

// Кегль на 1080p при размере текста 1
static const f32 FONT_PX[UI_FONTS] = {12.0f, 16.0f, 26.0f};

void ui_begin(i32 win_w, i32 win_h, const UiConfig &cfg, const Input &in) {
    UiState &u = g_ui_state;
    u.count = 0;
    u.nblocks = 0;
    u.w = (f32)win_w;
    u.h = (f32)win_h;
    f32 base = u.h > 0 ? u.h / 1080.0f : 1.0f;
    u.s = base * cfg.scale;
    u.hair = u.h >= 1400.0f ? 2.0f : 1.0f;
    for (u32 f = 0; f < UI_FONTS; ++f) {
        f32 want = FONT_PX[f] * base * cfg.scale * cfg.text_scale;
        u32 best = 0;
        for (u32 i = 1; i < u.nsizes; ++i)
            if (fabsf((f32)u.sizes[i].px - want) < fabsf((f32)u.sizes[best].px - want)) best = i;
        u.font[f] = best;
    }
    // Ввод виджетов: левая кнопка, отпускание — по смене «зажата»
    u.mx = in.mouse_x;
    u.my = in.mouse_y;
    u.pressed = in.mouse_pressed[MOUSE_LEFT];
    u.released = u.down && !in.mouse_down[MOUSE_LEFT];
    u.down = in.mouse_down[MOUSE_LEFT];
    if (!u.down && !u.pressed) u.active = 0;
}

void ui_end() {
    UiState &u = g_ui_state;
    if (!u.ready) u.count = 0;
    render_ui_set(Span<const RenderUiQuad>{u.quads, u.count});
}

void ui_block(f32 x, f32 y, f32 w, f32 h) {
    UiState &u = g_ui_state;
    if (u.nblocks < UI_MAX_BLOCKS) u.blocks[u.nblocks++] = {x, y, x + w, y + h};
}

bool ui_wants_mouse() {
    const UiState &u = g_ui_state;
    if (u.active) return true;
    for (u32 i = 0; i < u.nblocks; ++i) {
        const UiRect &r = u.blocks[i];
        if (u.mx >= r.x0 && u.mx < r.x1 && u.my >= r.y0 && u.my < r.y1) return true;
    }
    return false;
}

f32 ui_s() { return g_ui_state.s; }
f32 ui_px(f32 units) { return floorf(units * g_ui_state.s + 0.5f); }
f32 ui_width() { return g_ui_state.w; }
f32 ui_height() { return g_ui_state.h; }
f32 ui_hair() { return g_ui_state.hair; }

static void push(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, u32 rgba, f32 feather) {
    UiState &u = g_ui_state;
    if (u.count >= UI_MAX_QUADS_CPU) return;
    RenderUiQuad &q = u.quads[u.count++];
    q = RenderUiQuad{};
    q.x0 = x0;
    q.y0 = y0;
    q.x1 = x1;
    q.y1 = y1;
    q.u0 = u0;
    q.v0 = v0;
    q.u1 = u0 + (x1 - x0);
    q.v1 = v0 + (y1 - y0);
    q.rgba = rgba;
    q.feather = feather;
}

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 rgba) {
    if (w > 0 && h > 0) push(x, y, x + w, y + h, -1, -1, rgba, 0);
}
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather) {
    if (w > 0 && h > 0) push(x, y, x + w, y + h, -1, -1, rgba, feather);
}
u32 ui_reserve() {
    UiState &u = g_ui_state;
    u32 slot = u.count;
    push(0, 0, 0, 0, -1, -1, 0, 0);  // пустой, пока не заполнен
    return slot;
}
void ui_soft_at(u32 slot, f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather) {
    UiState &u = g_ui_state;
    if (slot >= u.count) return;
    u32 keep = u.count;
    u.count = slot;
    push(x, y, x + w, y + h, -1, -1, rgba, feather);
    u.count = keep;
}
void ui_hline(f32 x, f32 y, f32 w, u32 rgba) { ui_rect(floorf(x), floorf(y), floorf(w), g_ui_state.hair, rgba); }
void ui_vline(f32 x, f32 y, f32 h, u32 rgba) { ui_rect(floorf(x), floorf(y), g_ui_state.hair, floorf(h), rgba); }
void ui_frame(f32 x, f32 y, f32 w, f32 h, u32 rgba) {
    f32 t = g_ui_state.hair;
    ui_hline(x, y, w, rgba);
    ui_hline(x, y + h - t, w, rgba);
    ui_vline(x, y + t, h - 2 * t, rgba);
    ui_vline(x + w - t, y + t, h - 2 * t, rgba);
}

// Следующая буква UTF-8; неверный байт — '?'
static u32 next_cp(const char *&s) {
    const u8 *p = (const u8 *)s;
    u32 c = p[0], n = 0;
    if (c < 0x80) n = 1;
    else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (p[1] & 0x3F); n = 2; }
    else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else { c = '?'; n = 1; }
    s += n;
    return c;
}

static const UiGlyph *glyph(u32 size, u32 cp) {
    const UiState &u = g_ui_state;
    u16 i = cp < UI_MAX_CODE ? u.char_index[cp] : (u16)0xFFFF;
    if (i == 0xFFFF) i = u.char_index['?'];
    return &u.glyphs[(usize)size * u.nchars + i];
}

f32 ui_line(UiFont f) {
    const UiFontSize &m = g_ui_state.sizes[g_ui_state.font[f]];
    return (f32)(m.asc + m.desc);
}
f32 ui_cap(UiFont f) { return (f32)g_ui_state.sizes[g_ui_state.font[f]].cap; }

f32 ui_text(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking) {
    UiState &u = g_ui_state;
    if (!u.ready) return x;
    u32 size = u.font[f];
    f32 base = floorf(y + 0.5f) + (f32)u.sizes[size].asc;
    f32 pen = floorf(x + 0.5f);
    while (*s) {
        const UiGlyph *g = glyph(size, next_cp(s));
        if (g->w && g->h) {
            f32 gx = floorf(pen + 0.5f) + g->ox, gy = base + g->oy;
            push(gx, gy, gx + g->w, gy + g->h, g->x, g->y, rgba, 0);
        }
        pen += g->adv + tracking;
    }
    return pen;
}

f32 ui_text_width(UiFont f, const char *s, f32 tracking) {
    UiState &u = g_ui_state;
    if (!u.ready) return 0;
    u32 size = u.font[f];
    f32 w = 0;
    while (*s) w += glyph(size, next_cp(s))->adv + tracking;
    return w > tracking ? w - tracking : w;
}

void ui_text_right(f32 x, f32 y, UiFont f, u32 rgba, const char *s) { ui_text(x - ui_text_width(f, s), y, f, rgba, s); }
void ui_text_center(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking) {
    ui_text(x - floorf(ui_text_width(f, s, tracking) * 0.5f), y, f, rgba, s, tracking);
}
