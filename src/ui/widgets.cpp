// Виджеты немедленного интерфейса: строки формы, переключатели, ползунки, выбор, кнопки, вкладки (docs/ui.md)
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Раскладка формы на 1080p, единицы ui_s
static constexpr f32 ROW = 36.0f;
static constexpr f32 CTL = 230.0f;

static bool inside(f32 x, f32 y, f32 w, f32 h) {
    const UiState &u = g_ui_state;
    return u.mx >= x && u.mx < x + w && u.my >= y && u.my < y + h;
}
// Мышь над виджетом: за краем обрезки (уехал из области прокрутки) — нет
static bool hit(f32 x, f32 y, f32 w, f32 h) {
    const UiState &u = g_ui_state;
    if (u.clip_on && !(u.mx >= u.clip.x0 && u.mx < u.clip.x1 && u.my >= u.clip.y0 && u.my < u.clip.y1)) return false;
    return inside(x, y, w, h);
}

// Номер виджета: адрес его переменной (у кнопок — подпись и место)
static u64 wid(const void *p) { return (u64)(usize)p; }

UiForm ui_form(f32 x, f32 y, f32 w) { return UiForm{x, y, w, ui_px(ROW), ui_px(CTL)}; }

// Строка формы: подсветка под мышью, подпись по центру строки по высоте
static f32 text_y(const UiForm &f, UiFont font) { return floorf(f.y + (f.row - ui_line(font)) * 0.5f); }

static bool row_begin(UiForm &f, const char *label) {
    bool hov = hit(f.x, f.y, f.w, f.row);
    if (hov) ui_rect(f.x - ui_px(8), f.y, f.w + ui_px(16), f.row, ui_col::HOVER);
    ui_text(f.x, text_y(f, UI_TEXT), UI_TEXT, ui_col::TEXT, label);
    return hov;
}

void ui_section(UiForm &f, const char *title) {
    f.y += ui_px(10);
    ui_text(f.x, f.y, UI_SMALL, ui_col::DIM, title, ui_px(1.2f));
    f.y += ui_line(UI_SMALL) + ui_px(5);
    ui_hline(f.x, f.y, f.w, ui_col::LINE);
    f.y += ui_px(4);
}

void ui_value(UiForm &f, const char *label, const char *value, u32 rgba) {
    f32 row = ui_px(24);
    f32 y = floorf(f.y + (row - ui_line(UI_TEXT)) * 0.5f);
    f32 lx = ui_text(f.x, y, UI_TEXT, ui_col::DIM, label);
    // Длинное значение (имя видеокарты) обрезается многоточием, не наезжая на подпись
    char cut[96];
    snprintf(cut, sizeof cut, "%s", value);
    f32 room = f.x + f.w - lx - ui_px(12);
    usize n = strlen(cut);
    if (ui_text_width(UI_TEXT, cut) > room) {
        while (n > 0 && ui_text_width(UI_TEXT, cut) + ui_text_width(UI_TEXT, "…") > room) {
            do n--; while (n > 0 && ((u8)cut[n] & 0xC0) == 0x80);  // по границе буквы UTF-8
            cut[n] = 0;
        }
        snprintf(cut + n, sizeof cut - n, "…");
    }
    ui_text_right(f.x + f.w, y, UI_TEXT, rgba, cut);
    f.y += row;
}

bool ui_toggle(UiForm &f, const char *label, bool &v) {
    bool hov = row_begin(f, label);
    bool changed = false;
    if (hov && g_ui_state.pressed) {
        v = !v;
        changed = true;
    }
    f32 tw = ui_px(38), th = ui_px(18), k = th - 2 * ui_px(3);
    f32 tx = f.x + f.w - tw, ty = floorf(f.y + (f.row - th) * 0.5f);
    if (v) {
        ui_rect(tx, ty, tw, th, ui_col::ON);
        ui_rect(tx + tw - ui_px(3) - k, ty + ui_px(3), k, k, ui_col::PANEL);
    } else {
        ui_frame(tx, ty, tw, th, hov ? ui_col::DIM : ui_col::OFF);
        ui_rect(tx + ui_px(3), ty + ui_px(3), k, k, hov ? ui_col::DIM : ui_col::OFF);
    }
    f.y += f.row;
    return changed;
}

bool ui_slider(UiForm &f, const char *label, f32 &v, f32 lo, f32 hi, f32 step, const char *fmt, const char *shown) {
    UiState &u = g_ui_state;
    row_begin(f, label);
    char val[32];
    if (shown) snprintf(val, sizeof val, "%s", shown);
    else snprintf(val, sizeof val, fmt, (double)v);
    f32 vw = ui_px(58);
    ui_text_right(f.x + f.w, text_y(f, UI_TEXT), UI_TEXT, ui_col::DIM, val);
    f32 tx = f.x + f.w - f.ctl, tw = f.ctl - vw - ui_px(10);
    f32 cy = floorf(f.y + f.row * 0.5f);
    u64 id = wid(&v);
    bool changed = false;
    if (u.pressed && hit(tx - ui_px(6), f.y, tw + ui_px(12), f.row)) u.active = id;
    if (u.active == id && u.down) {
        f32 k = (u.mx - tx) / tw;
        k = k < 0 ? 0 : (k > 1 ? 1 : k);
        f32 nv = lo + (hi - lo) * k;
        if (step > 0) nv = lo + floorf((nv - lo) / step + 0.5f) * step;
        if (nv != v) {
            v = nv;
            changed = true;
        }
    }
    f32 k = hi > lo ? (v - lo) / (hi - lo) : 0;
    k = k < 0 ? 0 : (k > 1 ? 1 : k);
    f32 t = 2 * ui_hair();
    ui_rect(tx, cy - t * 0.5f, tw, t, ui_col::OFF);
    ui_rect(tx, cy - t * 0.5f, floorf(tw * k), t, ui_col::DIM);
    f32 kw = ui_px(4), kh = ui_px(14);
    ui_rect(floorf(tx + tw * k - kw * 0.5f), floorf(cy - kh * 0.5f), kw, kh, u.active == id ? ui_col::TEXT : ui_col::ON);
    f.y += f.row;
    return changed;
}

bool ui_choice(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count) {
    UiState &u = g_ui_state;
    bool hov = row_begin(f, label);
    f32 cx = f.x + f.w - f.ctl, ty = text_y(f, UI_TEXT);
    bool changed = false;
    if (hov && u.pressed && count) {
        if (hit(cx, f.y, f.ctl * 0.5f, f.row)) v = (v + count - 1) % count;
        else if (hit(cx + f.ctl * 0.5f, f.y, f.ctl * 0.5f, f.row)) v = (v + 1) % count;
        changed = true;
    }
    if (v >= count) v = 0;
    u32 arrow = hov ? ui_col::TEXT : ui_col::DIM;
    ui_text(cx, ty, UI_TEXT, arrow, "‹");
    ui_text_right(cx + f.ctl, ty, UI_TEXT, arrow, "›");
    if (count) ui_text_center(cx + f.ctl * 0.5f, ty, UI_TEXT, ui_col::TEXT, names[v]);
    f.y += f.row;
    return changed;
}

bool ui_button(f32 x, f32 y, f32 w, f32 h, const char *label, bool active) {
    UiState &u = g_ui_state;
    bool hov = hit(x, y, w, h);
    if (active) ui_rect(x, y, w, h, ui_col::ON);
    else if (hov) ui_rect(x, y, w, h, ui_col::HOVER);
    if (!active) ui_frame(x, y, w, h, hov ? ui_col::DIM : ui_col::LINE);
    ui_text_center(x + w * 0.5f, floorf(y + (h - ui_line(UI_TEXT)) * 0.5f), UI_TEXT,
                   active ? ui_col::PANEL : (hov ? ui_col::TEXT : ui_col::DIM), label);
    return hov && u.pressed;
}

bool ui_button_row(UiForm &f, const char *label) {
    f32 h = f.row - ui_px(6);
    bool r = ui_button(f.x, f.y + ui_px(3), f.w, h, label);
    f.y += f.row;
    return r;
}

bool ui_tabs(f32 x, f32 y, f32 w, const char *const *names, u32 count, u32 &cur) {
    UiState &u = g_ui_state;
    f32 tr = ui_px(1.2f), gap = ui_px(26);
    f32 lh = ui_line(UI_SMALL);
    bool changed = false;
    f32 px = x;
    for (u32 i = 0; i < count; ++i) {
        f32 tw = ui_text_width(UI_SMALL, names[i], tr);
        bool hov = hit(px - gap * 0.5f, y - ui_px(6), tw + gap, lh + ui_px(14));
        if (hov && u.pressed && cur != i) {
            cur = i;
            changed = true;
        }
        bool on = cur == i;
        ui_text(px, y, UI_SMALL, on ? ui_col::TEXT : (hov ? ui_col::ON : ui_col::DIM), names[i], tr);
        if (on) ui_rect(px, y + lh + ui_px(6), floorf(tw), 2 * ui_hair(), ui_col::ON);
        px += tw + gap;
    }
    (void)w;
    return changed;
}

void ui_key_row(UiForm &f, const char *key, const char *what) {
    f32 row = ui_px(28);
    f32 kw = ui_px(96);
    f32 y = floorf(f.y + (row - ui_line(UI_SMALL)) * 0.5f);
    f32 bw = ui_text_width(UI_SMALL, key) + ui_px(12);
    ui_frame(f.x, f.y + ui_px(3), bw, row - ui_px(6), ui_col::LINE);
    ui_text(f.x + ui_px(6), y, UI_SMALL, ui_col::TEXT, key);
    ui_text(f.x + kw, floorf(f.y + (row - ui_line(UI_TEXT)) * 0.5f), UI_TEXT, ui_col::DIM, what);
    f.y += row;
}

// --- Прокрутка ---
static constexpr f32 WHEEL_STEP = 72.0f;  // единицы ui_s на щелчок колеса (две-три строки)

static f32 scroll_max(const UiScroll &s) { return s.content > s.h ? s.content - s.h : 0.0f; }

f32 ui_scroll_begin(UiScroll &s, f32 x, f32 y, f32 w, f32 h) {
    UiState &u = g_ui_state;
    s.x = x;
    s.y = y;
    s.w = w;
    s.h = h > 0 ? h : 0;
    if (u.wheel != 0 && inside(x, y, w, s.h)) {
        s.offset -= u.wheel * ui_px(WHEEL_STEP);
        u.wheel = 0;  // одна область на щелчок
    }
    f32 m = scroll_max(s);
    s.offset = s.offset < 0 ? 0 : (s.offset > m ? m : s.offset);
    s.offset = floorf(s.offset + 0.5f);  // буквы в целых пикселях
    ui_clip(x, y, w, s.h);
    return y - s.offset;
}

f32 ui_scroll_end(UiScroll &s, f32 bottom) {
    UiState &u = g_ui_state;
    ui_clip_off();
    s.content = bottom - (s.y - s.offset);
    f32 m = scroll_max(s);
    if (s.offset > m) s.offset = m;
    if (m <= 0) return s.content < s.h ? s.content : s.h;
    // Полоса у правого края области: тонкая дорожка, ползунок шире под мышью
    f32 tw = ui_px(10), tx = s.x + s.w - tw;
    f32 th = floorf(s.h * s.h / s.content);
    if (th < ui_px(28)) th = ui_px(28);
    f32 room = s.h - th;
    f32 ty = s.y + floorf(room * s.offset / m + 0.5f);
    u64 id = (u64)(usize)&s;
    bool hov = inside(tx, s.y, tw, s.h);
    if (hov && u.pressed) {
        u.active = id;
        if (u.my >= ty && u.my < ty + th) s.grab = u.my - ty;
        else {
            s.offset += u.my < ty ? -s.h : s.h;  // на страницу к мыши
            s.offset = s.offset < 0 ? 0 : (s.offset > m ? m : s.offset);
            s.grab = -1;
        }
    }
    if (u.active == id && u.down && s.grab >= 0 && room > 0) {
        f32 k = (u.my - s.grab - s.y) / room;
        k = k < 0 ? 0 : (k > 1 ? 1 : k);
        s.offset = floorf(k * m + 0.5f);
    }
    ty = s.y + floorf(room * s.offset / m + 0.5f);
    bool on = hov || u.active == id;
    f32 bw = on ? ui_px(4) : 2 * ui_hair();
    f32 bx = floorf(tx + (tw - bw) * 0.5f);
    ui_rect(floorf(tx + (tw - ui_hair()) * 0.5f), s.y, ui_hair(), s.h, ui_col::LINE);
    ui_rect(bx, ty, bw, th, u.active == id ? ui_col::TEXT : (on ? ui_col::ON : ui_col::DIM));
    return s.h;
}
