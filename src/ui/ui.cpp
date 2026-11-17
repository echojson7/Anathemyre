// Интерфейс: шрифт из .font, прямоугольники и буквы в пикселях окна, снятие текста (docs/ui.md)
#include "ui.h"
#include "ui_state.h"
#include "../core/log.h"
#include "../render/render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

UiState g_ui_state;

// Формат .font версии 2 (tools/fontconv/fontconv.py, docs/ui.md «Шрифт»): «AFNT», версия, ширина и высота атласа,
// число начертаний, кеглей и букв; коды букв; на начертание — метрики кеглей, буквы по кеглям (x, y, w, h,
// смещение от пера и линии строки, шаг в 1/64 пикселя), пары кернинга; атлас
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

static bool font_parse(Arena &perm, const u8 *d, usize size, u32 *aw, u32 *ah, usize *atlas_off) {
    UiState &u = g_ui_state;
    if (size < 20 || memcmp(d, "AFNT", 4) || rd32(d + 4) != 2) return false;
    *aw = rd16(d + 8);
    *ah = rd16(d + 10);
    u32 nf = rd16(d + 12), ns = rd16(d + 14), nc = rd16(d + 16);
    if (!nf || nf > UI_MAX_FACES || !ns || ns > UI_MAX_SIZES || !nc || *aw % 4) return false;
    usize at = 20;
    if (at + (usize)nc * 4 > size) return false;
    for (u32 c = 0; c < UI_MAX_CODE; ++c) u.char_index[c] = 0xFFFF;
    for (u32 i = 0; i < nc; ++i) {
        u32 cp = rd32(d + at + i * 4);
        if (cp < UI_MAX_CODE) u.char_index[cp] = (u16)i;
    }
    at += (usize)nc * 4;
    for (u32 f = 0; f < nf; ++f) {
        UiFace &F = u.faces[f];
        usize need = (usize)ns * 8 + (usize)ns * nc * 10 + 4;
        if (at + need > size) return false;
        for (u32 i = 0; i < ns; ++i) {
            const u8 *m = d + at + i * 8;
            F.sizes[i] = {(i16)rd16(m), (i16)rd16(m + 2), (i16)rd16(m + 4), (i16)rd16(m + 6)};
        }
        at += (usize)ns * 8;
        F.glyphs = arena_push<UiGlyph>(perm, (usize)ns * nc);
        for (usize i = 0; i < (usize)ns * nc; ++i) {
            const u8 *g = d + at + i * 10;
            F.glyphs[i] = {rd16(g), rd16(g + 2), g[4], g[5], (i8)g[6], (i8)g[7], (f32)rd16(g + 8) / 64.0f};
        }
        at += (usize)ns * nc * 10;
        F.nkern = rd32(d + at);
        at += 4;
        if (at + (usize)F.nkern * 6 > size) return false;
        F.kern = arena_push<UiKern>(perm, F.nkern ? F.nkern : 1);
        for (u32 i = 0; i < F.nkern; ++i) {
            const u8 *k = d + at + i * 6;
            F.kern[i] = {((u32)rd16(k) << 16) | rd16(k + 2), (i32)(i16)rd16(k + 4)};
        }
        at += (usize)F.nkern * 6;
        // Пары идут по возрастанию (конвертер перебирает a, потом b): поиск делением пополам
        for (u32 i = 1; i < F.nkern; ++i)
            if (F.kern[i].pair <= F.kern[i - 1].pair) return false;
    }
    u.nfaces = nf;
    u.nsizes = ns;
    u.nchars = nc;
    *atlas_off = at;
    return size == at + (usize)*aw * *ah;
}

bool ui_init(Arena &perm, Arena &scratch, const char *data_dir) {
    UiState &u = g_ui_state;
    char path[1024];
    snprintf(path, sizeof path, "%sui/inter.font", data_dir);
    usize mark = arena_mark(scratch);
    u8 *d = nullptr;
    usize size = 0;
    if (!platform_read_file(path, scratch, &d, &size)) {
        log_error("нет шрифта %s", path);
        return false;
    }
    u32 aw = 0, ah = 0;
    usize off_a = 0;
    if (!font_parse(perm, d, size, &aw, &ah, &off_a)) {
        log_error("%s: не тот формат", path);
        arena_restore(scratch, mark);
        return false;
    }
    bool ok = render_ui_font(d + off_a, aw, ah);
    arena_restore(scratch, mark);
    if (!ok) {
        log_error("атлас шрифта не загрузился");
        return false;
    }
    u.ready = true;
    log_info("интерфейс: %s, %u начертания, %u кеглей, %u букв", path, u.nfaces, u.nsizes, u.nchars);
    return true;
}

// Кегль на 1080p при размере текста 1 и начертание (0 — обычное, 1 — полужирное)
static const f32 FONT_PX[UI_FONTS] = {12.0f, 15.0f, 22.0f, 15.0f, 14.0f};
static const u32 FONT_FACE[UI_FONTS] = {1, 0, 1, 1, 0};

void ui_begin(i32 win_w, i32 win_h, const UiConfig &cfg, const Input &in, f64 dt) {
    UiState &u = g_ui_state;
    u.count = 0;
    u.nblocks = 0;
    u.w = (f32)win_w;
    u.h = (f32)win_h;
    f32 base = u.h > 0 ? u.h / 1080.0f : 1.0f;
    u.s = base * cfg.scale;
    u.hair = u.h >= 1400.0f ? 2.0f : 1.0f;
    for (u32 f = 0; f < UI_FONTS; ++f) {
        u32 face = FONT_FACE[f] < u.nfaces ? FONT_FACE[f] : 0;
        const UiFace &F = u.faces[face];
        f32 want = FONT_PX[f] * base * cfg.scale * cfg.text_scale;
        u32 best = 0;
        for (u32 i = 1; i < u.nsizes; ++i)
            if (fabsf((f32)F.sizes[i].px - want) < fabsf((f32)F.sizes[best].px - want)) best = i;
        u.font[f] = best;
        u.face[f] = face;
    }
    // Ввод виджетов: левая кнопка, отпускание — по смене «зажата»
    u.mx = in.mouse_x;
    u.my = in.mouse_y;
    u.pressed = in.mouse_pressed[MOUSE_LEFT];
    u.released = u.down && !in.mouse_down[MOUSE_LEFT];
    u.down = in.mouse_down[MOUSE_LEFT];
    if (!u.down && !u.pressed) u.active = 0;
    u.wheel = in.wheel;
    u.clip_on = false;
    u.hidden = false;
    u.tip = nullptr;
    u.dt = dt;
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

bool ui_hover(f32 x, f32 y, f32 w, f32 h) {
    const UiState &u = g_ui_state;
    if (u.hidden) return false;
    if (u.clip_on && !(u.mx >= u.clip.x0 && u.mx < u.clip.x1 && u.my >= u.clip.y0 && u.my < u.clip.y1)) return false;
    return u.mx >= x && u.mx < x + w && u.my >= y && u.my < y + h;
}
bool ui_clicked(f32 x, f32 y, f32 w, f32 h) { return g_ui_state.pressed && ui_hover(x, y, w, h); }

f32 ui_s() { return g_ui_state.s; }
f32 ui_px(f32 units) { return floorf(units * g_ui_state.s + 0.5f); }
f32 ui_width() { return g_ui_state.w; }
f32 ui_height() { return g_ui_state.h; }
f32 ui_hair() { return g_ui_state.hair; }

// Прямоугольник в кадр. Обрезку считает GPU (форма — по полному прямоугольнику), здесь — только отбор целиком
// невидимых; свёрнутый раздел ничего не рисует
static RenderUiQuad *push(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, u32 rgba) {
    UiState &u = g_ui_state;
    if (u.hidden || u.count >= UI_MAX_QUADS_CPU) return nullptr;
    UiRect c = u.clip_on ? u.clip : UiRect{-1e9f, -1e9f, 1e9f, 1e9f};
    if (x1 <= c.x0 || y1 <= c.y0 || x0 >= c.x1 || y0 >= c.y1) return nullptr;
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
    q.cx0 = c.x0;
    q.cy0 = c.y0;
    q.cx1 = c.x1;
    q.cy1 = c.y1;
    q.rgba = rgba;
    return &q;
}

void ui_rect(f32 x, f32 y, f32 w, f32 h, u32 rgba) {
    if (w > 0 && h > 0) push(x, y, x + w, y + h, -1, -1, rgba);
}
void ui_round(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 radius) {
    if (w <= 0 || h <= 0) return;
    if (RenderUiQuad *q = push(x, y, x + w, y + h, -1, -1, rgba)) q->radius = radius;
}
void ui_stroke(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 radius, f32 width) {
    if (w <= 0 || h <= 0) return;
    if (RenderUiQuad *q = push(x, y, x + w, y + h, -1, -1, rgba)) {
        q->radius = radius;
        q->stroke = width > 0 ? width : 1;
    }
}
void ui_soft(f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather) {
    if (w <= 0 || h <= 0) return;
    if (RenderUiQuad *q = push(x, y, x + w, y + h, -1, -1, rgba)) q->feather = feather;
}
u32 ui_reserve() {
    UiState &u = g_ui_state;
    if (u.count >= UI_MAX_QUADS_CPU) return UI_MAX_QUADS_CPU;
    u32 slot = u.count++;
    u.quads[slot] = RenderUiQuad{};  // пустой, пока не заполнен
    u.quads[slot].u0 = u.quads[slot].v0 = -1;
    return slot;
}
// Прямоугольник в место ui_reserve: без обрезки и свёртки
static RenderUiQuad *at_slot(u32 slot, f32 x, f32 y, f32 w, f32 h, u32 rgba) {
    UiState &u = g_ui_state;
    if (slot >= u.count) return nullptr;
    u32 keep = u.count;
    bool clip = u.clip_on, hidden = u.hidden;
    u.count = slot;
    u.clip_on = u.hidden = false;
    RenderUiQuad *q = push(x, y, x + w, y + h, -1, -1, rgba);
    u.count = keep;
    u.clip_on = clip;
    u.hidden = hidden;
    return q;
}
void ui_soft_at(u32 slot, f32 x, f32 y, f32 w, f32 h, u32 rgba, f32 feather) {
    if (RenderUiQuad *q = at_slot(slot, x, y, w, h, rgba)) q->feather = feather;
}

// Окно: тень растушёвкой за краем, скруглённая панель и тонкая светлая кромка
static constexpr f32 PANEL_R = 8.0f, SHADOW_PAD = 14.0f;
u32 ui_panel_reserve() {
    u32 s = ui_reserve();
    ui_reserve();
    ui_reserve();
    return s;
}
void ui_panel_at(u32 slot, f32 x, f32 y, f32 w, f32 h) {
    f32 r = ui_px(PANEL_R), sp = ui_px(SHADOW_PAD);
    if (RenderUiQuad *q = at_slot(slot, x - sp, y - sp * 0.5f, w + 2 * sp, h + 2 * sp, ui_col::SHADOW)) {
        q->radius = r + sp;
        q->feather = sp * 1.6f;
    }
    if (RenderUiQuad *q = at_slot(slot + 1, x, y, w, h, ui_col::PANEL)) q->radius = r;
    if (RenderUiQuad *q = at_slot(slot + 2, x, y, w, h, ui_col::LINE)) {
        q->radius = r;
        q->stroke = ui_hair();
    }
}
void ui_panel(f32 x, f32 y, f32 w, f32 h) { ui_panel_at(ui_panel_reserve(), x, y, w, h); }

void ui_clip(f32 x, f32 y, f32 w, f32 h) {
    UiState &u = g_ui_state;
    u.clip = {x, y, x + w, y + h};
    u.clip_on = true;
}
void ui_clip_off() { g_ui_state.clip_on = false; }
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

static u32 char_of(u32 cp) {
    const UiState &u = g_ui_state;
    u16 i = cp < UI_MAX_CODE ? u.char_index[cp] : (u16)0xFFFF;
    return i == 0xFFFF ? u.char_index['?'] : i;
}

// Поправка шага пары букв, пиксели кегля px
static f32 kern(const UiFace &F, u32 a, u32 b, f32 px) {
    u32 key = (a << 16) | b, lo = 0, hi = F.nkern;
    while (lo < hi) {
        u32 mid = (lo + hi) >> 1;
        if (F.kern[mid].pair < key) lo = mid + 1;
        else hi = mid;
    }
    return lo < F.nkern && F.kern[lo].pair == key ? (f32)F.kern[lo].k * px / 1024.0f : 0.0f;
}

f32 ui_line(UiFont f) {
    const UiState &u = g_ui_state;
    const UiFontSize &m = u.faces[u.face[f]].sizes[u.font[f]];
    return (f32)(m.asc + m.desc);
}
f32 ui_cap(UiFont f) {
    const UiState &u = g_ui_state;
    return (f32)u.faces[u.face[f]].sizes[u.font[f]].cap;
}

// Проход по буквам строки с кернингом: на каждую — буква и x пера (дробный)
template <typename Fn> static f32 walk(UiFont f, const char *s, f32 tracking, Fn fn) {
    const UiState &u = g_ui_state;
    const UiFace &F = u.faces[u.face[f]];
    u32 size = u.font[f];
    f32 px = (f32)F.sizes[size].px;
    f32 pen = 0;
    u32 prev = 0xFFFFFFFFu;
    while (*s) {
        u32 ci = char_of(next_cp(s));
        if (prev != 0xFFFFFFFFu && F.nkern) pen += kern(F, prev, ci, px);
        const UiGlyph &g = F.glyphs[(usize)size * u.nchars + ci];
        fn(g, pen);
        pen += g.adv + tracking;
        prev = ci;
    }
    return pen;
}

f32 ui_text(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking) {
    UiState &u = g_ui_state;
    if (!u.ready || u.hidden) return x;
    f32 base = floorf(y + 0.5f) + (f32)u.faces[u.face[f]].sizes[u.font[f]].asc;
    f32 x0 = floorf(x + 0.5f);
    f32 end = walk(f, s, tracking, [&](const UiGlyph &g, f32 pen) {
        if (!g.w || !g.h) return;
        f32 gx = floorf(x0 + pen + 0.5f) + g.ox, gy = base + g.oy;
        push(gx, gy, gx + g.w, gy + g.h, g.x, g.y, rgba);
    });
    return x0 + end;
}

f32 ui_text_width(UiFont f, const char *s, f32 tracking) {
    if (!g_ui_state.ready || !*s) return 0;
    f32 w = walk(f, s, tracking, [](const UiGlyph &, f32) {});
    return w > tracking ? w - tracking : w;
}

void ui_text_right(f32 x, f32 y, UiFont f, u32 rgba, const char *s) { ui_text(x - ui_text_width(f, s), y, f, rgba, s); }
void ui_text_center(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 tracking) {
    ui_text(x - floorf(ui_text_width(f, s, tracking) * 0.5f), y, f, rgba, s, tracking);
}

f32 ui_text_fit(f32 x, f32 y, UiFont f, u32 rgba, const char *s, f32 w, bool right) {
    char cut[256];
    snprintf(cut, sizeof cut, "%s", s);
    usize n = strlen(cut);
    if (ui_text_width(f, cut) > w) {
        f32 dots = ui_text_width(f, "…");
        while (n > 0 && ui_text_width(f, cut) + dots > w) {
            do n--; while (n > 0 && ((u8)cut[n] & 0xC0) == 0x80);  // по границе буквы UTF-8
            cut[n] = 0;
        }
        snprintf(cut + n, sizeof cut - n, "…");
    }
    if (right) {
        ui_text_right(x + w, y, f, rgba, cut);
        return x + w;
    }
    return ui_text(x, y, f, rgba, cut);
}

// --- Снятие текста ---
void ui_capture_begin(char *buf, usize cap) {
    UiState &u = g_ui_state;
    u.cap_buf = cap ? buf : nullptr;
    u.cap_cap = cap;
    u.cap_len = 0;
    if (cap) buf[0] = 0;
}
void ui_capture_line(const char *s) {
    UiState &u = g_ui_state;
    if (!u.cap_buf || u.cap_len + 1 >= u.cap_cap) return;
    int k = snprintf(u.cap_buf + u.cap_len, u.cap_cap - u.cap_len, "%s\n", s);
    if (k > 0) u.cap_len = u.cap_len + (usize)k < u.cap_cap ? u.cap_len + (usize)k : u.cap_cap - 1;
}
usize ui_capture_end() {
    UiState &u = g_ui_state;
    usize n = u.cap_len;
    u.cap_buf = nullptr;
    u.cap_cap = u.cap_len = 0;
    return n;
}
bool ui_capturing() { return g_ui_state.cap_buf != nullptr; }
