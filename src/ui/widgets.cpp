// Виджеты немедленного интерфейса: строки формы, разделы (и сворачиваемые), переключатели, ползунки, выбор,
// сегменты, кнопки, вкладки, прокрутка (docs/ui.md)
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Раскладка формы на 1080p, единицы ui_s
static constexpr f32 ROW = 36.0f;
static constexpr f32 CTL = 230.0f;
static constexpr f32 R_CTL = 5.0f;  // скругление кнопок и полей

static bool inside(f32 x, f32 y, f32 w, f32 h) {
    const UiState &u = g_ui_state;
    return u.mx >= x && u.mx < x + w && u.my >= y && u.my < y + h;
}
// Мышь над виджетом: за краем обрезки (уехал из области прокрутки) или в свёрнутом разделе — нет
static bool hit(f32 x, f32 y, f32 w, f32 h) { return ui_hover(x, y, w, h); }

// Номер виджета: адрес его переменной (у кнопок — подпись и место)
static u64 wid(const void *p) { return (u64)(usize)p; }

UiForm ui_form(f32 x, f32 y, f32 w) {
    g_ui_state.hidden = false;
    UiForm f{};
    f.x = x;
    f.y = y;
    f.w = w;
    f.row = ui_px(ROW);
    f.ctl = ui_px(CTL);
    return f;
}

void ui_tip(UiForm &f, const char *tip) { f.tip = tip; }
const char *ui_tip_hovered() { return g_ui_state.tip; }

static f32 text_y(const UiForm &f, UiFont font) { return floorf(f.y + (f.row - ui_line(font)) * 0.5f); }

// Строка формы: подсветка под мышью, подпись по центру строки по высоте, подсказка строки — в кадр
// reserve — сколько справа занимает управление (по умолчанию колонка f.ctl)
static bool row_begin(UiForm &f, const char *label, f32 reserve = -1) {
    UiState &u = g_ui_state;
    bool hov = hit(f.x - ui_px(8), f.y, f.w + ui_px(16), f.row);
    if (hov) {
        ui_round(f.x - ui_px(8), f.y + ui_px(1), f.w + ui_px(16), f.row - ui_px(2), ui_col::HOVER, ui_px(4));
        if (f.tip) u.tip = f.tip;
    }
    f.tip = nullptr;
    ui_text_fit(f.x, text_y(f, f.font), f.font, ui_col::TEXT, label, f.w - (reserve < 0 ? f.ctl : reserve) - ui_px(12));
    return hov;
}

static void capture_pair(const char *label, const char *value) {
    if (!ui_capturing()) return;
    char t[256];
    if (label && label[0]) snprintf(t, sizeof t, "%s: %s", label, value);
    else snprintf(t, sizeof t, "%s", value);
    ui_capture_line(t);
}

void ui_section(UiForm &f, const char *title) {
    g_ui_state.hidden = false;
    if (ui_capturing()) {
        char t[96];
        snprintf(t, sizeof t, "\n[%s]", title);
        ui_capture_line(t);
    }
    f.y += ui_px(14);
    ui_text(f.x, f.y, UI_SMALL, ui_col::DIM, title, ui_px(1.0f));
    f.y += ui_line(UI_SMALL) + ui_px(6);
    ui_hline(f.x, f.y, f.w, ui_col::LINE);
    f.y += ui_px(4);
}

bool ui_fold(UiForm &f, const char *title, bool &open, const char *note, bool capture) {
    UiState &u = g_ui_state;
    u.hidden = false;
    if (capture && ui_capturing()) {
        char t[96];
        snprintf(t, sizeof t, "\n[%s]", title);
        ui_capture_line(t);
    }
    f.y += ui_px(8);
    f32 h = ui_line(UI_SMALL) + ui_px(12);
    bool hov = hit(f.x - ui_px(8), f.y, f.w + ui_px(16), h);
    if (hov) ui_round(f.x - ui_px(8), f.y, f.w + ui_px(16), h, ui_col::HOVER, ui_px(4));
    if (hov && u.pressed) open = !open;
    f32 ty = floorf(f.y + (h - ui_line(UI_SMALL)) * 0.5f);
    // Стрелка — мелким кеглем: ▼ открыт, ▶ свёрнут
    f32 aw = ui_px(18);
    ui_text(f.x, ty, UI_SMALL, hov ? ui_col::TEXT : ui_col::FAINT, open ? "▼" : "▶");
    ui_text(f.x + aw, ty, UI_SMALL, hov ? ui_col::TEXT : ui_col::DIM, title, ui_px(1.0f));
    if (note && note[0]) ui_text_right(f.x + f.w, ty, UI_SMALL, ui_col::FAINT, note);
    f.y += h;
    ui_hline(f.x, f.y, f.w, ui_col::LINE);
    f.y += ui_px(4);
    if (!open) u.hidden = true;
    return open;
}

void ui_fold_end(UiForm &) { g_ui_state.hidden = false; }

void ui_value(UiForm &f, const char *label, const char *value, u32 rgba) {
    capture_pair(label, value);
    if (g_ui_state.hidden) return;
    UiFont font = f.font;
    f32 row = ui_line(font) + ui_px(f.font == UI_DATA ? 7 : 10);
    f32 gap = ui_px(14);
    f32 lw = ui_text_width(font, label), vw = ui_text_width(font, value);
    f32 y = floorf(f.y + (row - ui_line(font)) * 0.5f);
    if (lw + gap + vw <= f.w || !label[0]) {
        ui_text(f.x, y, font, ui_col::DIM, label);
        f32 room = f.w - (label[0] ? lw + gap : 0);
        ui_text_fit(f.x + f.w - room, y, font, rgba, value, room, true);
        f.y += row;
        return;
    }
    // Не влезает в строку: подпись сверху, значение под ней справа — ничего не обрезается
    ui_text_fit(f.x, y, font, ui_col::DIM, label, f.w);
    f32 y2 = y + ui_line(font);
    ui_text_fit(f.x + ui_px(16), y2, font, rgba, value, f.w - ui_px(16), true);
    f.y += row + ui_line(font);
}

bool ui_toggle(UiForm &f, const char *label, bool &v) {
    if (g_ui_state.hidden) return false;
    bool hov = row_begin(f, label, ui_px(34));
    bool changed = false;
    if (hov && g_ui_state.pressed) {
        v = !v;
        changed = true;
    }
    // Переключатель-капсула: включён — акцент и кружок справа
    f32 tw = ui_px(34), th = ui_px(18), k = th - 2 * ui_px(3);
    f32 tx = f.x + f.w - tw, ty = floorf(f.y + (f.row - th) * 0.5f);
    if (v) {
        ui_round(tx, ty, tw, th, ui_col::ON, th * 0.5f);
        ui_round(tx + tw - ui_px(3) - k, ty + ui_px(3), k, k, ui_col::INK, k * 0.5f);
    } else {
        ui_round(tx, ty, tw, th, ui_col::SIDE, th * 0.5f);
        ui_stroke(tx, ty, tw, th, hov ? ui_col::DIM : ui_col::OFF, th * 0.5f, ui_hair());
        ui_round(tx + ui_px(3), ty + ui_px(3), k, k, hov ? ui_col::TEXT : ui_col::DIM, k * 0.5f);
    }
    f.y += f.row;
    return changed;
}

bool ui_slider(UiForm &f, const char *label, f32 &v, f32 lo, f32 hi, f32 step, const char *fmt, const char *shown) {
    UiState &u = g_ui_state;
    if (u.hidden) return false;
    bool hov = row_begin(f, label);
    char val[32];
    if (shown) snprintf(val, sizeof val, "%s", shown);
    else snprintf(val, sizeof val, fmt, (double)v);
    f32 vw = ui_px(56);
    ui_text_right(f.x + f.w, text_y(f, f.font), f.font, ui_col::TEXT, val);
    f32 tx = f.x + f.w - f.ctl, tw = f.ctl - vw - ui_px(12);
    f32 cy = floorf(f.y + f.row * 0.5f);
    u64 id = wid(&v);
    bool changed = false;
    if (u.pressed && hit(tx - ui_px(8), f.y, tw + ui_px(16), f.row)) u.active = id;
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
    f32 t = ui_px(4);
    ui_round(tx, cy - t * 0.5f, tw, t, ui_col::OFF, t * 0.5f);
    ui_round(tx, cy - t * 0.5f, floorf(tw * k), t, ui_col::ON, t * 0.5f);
    bool on = u.active == id || hov;
    f32 kd = on ? ui_px(14) : ui_px(12);
    ui_round(floorf(tx + tw * k - kd * 0.5f), floorf(cy - kd * 0.5f), kd, kd, on ? ui_col::TEXT : ui_col::ON, kd * 0.5f);
    f.y += f.row;
    return changed;
}

bool ui_choice(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count) {
    UiState &u = g_ui_state;
    if (u.hidden) return false;
    bool hov = row_begin(f, label);
    f32 bh = f.row - ui_px(10), by = floorf(f.y + (f.row - bh) * 0.5f);
    f32 cx = f.x + f.w - f.ctl, ty = text_y(f, f.font);
    bool changed = false;
    // v ≥ count — ничего не выбрано («—»): стрелки ведут к последнему и к первому
    if (hov && u.pressed && count) {
        if (hit(cx, f.y, f.ctl * 0.5f, f.row)) v = v < count ? (v + count - 1) % count : count - 1, changed = true;
        else if (hit(cx + f.ctl * 0.5f, f.y, f.ctl * 0.5f, f.row)) v = v < count ? (v + 1) % count : 0, changed = true;
    }
    ui_round(cx, by, f.ctl, bh, ui_col::SIDE, ui_px(R_CTL));
    u32 arrow = hov ? ui_col::TEXT : ui_col::FAINT;
    f32 ap = ui_px(10);
    ui_text(cx + ap, ty, f.font, arrow, "‹");
    ui_text_right(cx + f.ctl - ap, ty, f.font, arrow, "›");
    if (count) {
        const char *name = v < count ? names[v] : "—";
        f32 room = f.ctl - 2 * ui_px(26);
        f32 nw = ui_text_width(f.font, name);
        if (nw > room) nw = room;
        ui_text_fit(cx + floorf((f.ctl - nw) * 0.5f), ty, f.font, ui_col::TEXT, name, room);
    }
    f.y += f.row;
    return changed;
}

bool ui_segment(UiForm &f, const char *label, u32 &v, const char *const *names, u32 count) {
    UiState &u = g_ui_state;
    if (u.hidden) return false;
    // Ширина сегментов — поровну, по самому длинному варианту; не влезают — стрелками
    f32 pad = ui_px(6), maxw = 0;
    for (u32 i = 0; i < count; ++i) {
        f32 w = ui_text_width(UI_STRONG, names[i]);
        if (w > maxw) maxw = w;
    }
    f32 sw = floorf(f.ctl / (f32)(count ? count : 1));
    if (!count || maxw + 2 * pad > sw) return ui_choice(f, label, v, names, count);
    bool hov = row_begin(f, label);
    (void)hov;
    f32 bh = f.row - ui_px(10), by = floorf(f.y + (f.row - bh) * 0.5f);
    f32 bx = f.x + f.w - sw * (f32)count;
    ui_round(bx, by, sw * (f32)count, bh, ui_col::SIDE, ui_px(R_CTL));
    bool changed = false;
    f32 ty = floorf(by + (bh - ui_line(UI_STRONG)) * 0.5f);
    for (u32 i = 0; i < count; ++i) {
        f32 sx = bx + sw * (f32)i;
        bool sh = hit(sx, by, sw, bh);
        if (sh && u.pressed && v != i) {
            v = i;
            changed = true;
        }
        bool on = v == i;
        if (on) {
            ui_round(sx + ui_px(2), by + ui_px(2), sw - ui_px(4), bh - ui_px(4), ui_col::ON_BG, ui_px(R_CTL - 1));
            ui_stroke(sx + ui_px(2), by + ui_px(2), sw - ui_px(4), bh - ui_px(4), ui_col::ON, ui_px(R_CTL - 1), ui_hair());
        } else if (sh) {
            ui_round(sx + ui_px(2), by + ui_px(2), sw - ui_px(4), bh - ui_px(4), ui_col::HOVER, ui_px(R_CTL - 1));
        }
        ui_text_center(sx + sw * 0.5f, ty, on ? UI_STRONG : UI_TEXT, on ? ui_col::ON : (sh ? ui_col::TEXT : ui_col::DIM),
                       names[i]);
    }
    f.y += f.row;
    return changed;
}

bool ui_button(f32 x, f32 y, f32 w, f32 h, const char *label, bool active, bool accent) {
    UiState &u = g_ui_state;
    if (u.hidden) return false;
    bool hov = hit(x, y, w, h);
    f32 r = ui_px(R_CTL);
    u32 tc;
    UiFont font = UI_STRONG;
    if (accent) {
        ui_round(x, y, w, h, hov ? ui_col::TEXT : ui_col::ON, r);
        tc = ui_col::INK;
    } else if (active) {
        ui_round(x, y, w, h, ui_col::ON_BG, r);
        ui_stroke(x, y, w, h, ui_col::ON, r, ui_hair());
        tc = ui_col::ON;
    } else {
        ui_round(x, y, w, h, hov ? ui_col::HOVER : ui_col::SIDE, r);
        ui_stroke(x, y, w, h, hov ? ui_col::DIM : ui_col::LINE, r, ui_hair());
        tc = hov ? ui_col::TEXT : ui_col::DIM;
        font = UI_TEXT;
    }
    ui_text_center(x + w * 0.5f, floorf(y + (h - ui_line(font)) * 0.5f), font, tc, label);
    return hov && u.pressed;
}

bool ui_button_row(UiForm &f, const char *label) {
    if (g_ui_state.hidden) return false;
    f32 h = f.row - ui_px(8);
    bool r = ui_button(f.x, f.y + ui_px(4), f.w, h, label);
    f.y += f.row;
    return r;
}

bool ui_tabs(f32 x, f32 y, f32 w, const char *const *names, u32 count, u32 &cur) {
    // Вкладки поровну во всю ширину: выбранная — подложка и акцент снизу
    UiState &u = g_ui_state;
    f32 tr = ui_px(1.0f);
    f32 h = ui_line(UI_SMALL) + ui_px(14);
    f32 tw = floorf(w / (f32)(count ? count : 1));
    bool changed = false;
    ui_round(x, y, w, h, ui_col::SIDE, ui_px(R_CTL));
    for (u32 i = 0; i < count; ++i) {
        f32 tx = x + tw * (f32)i;
        bool hov = hit(tx, y, tw, h);
        if (hov && u.pressed && cur != i) {
            cur = i;
            changed = true;
        }
        bool on = cur == i;
        if (on) ui_round(tx + ui_px(2), y + ui_px(2), tw - ui_px(4), h - ui_px(4), ui_col::ON_BG, ui_px(R_CTL - 1));
        else if (hov) ui_round(tx + ui_px(2), y + ui_px(2), tw - ui_px(4), h - ui_px(4), ui_col::HOVER, ui_px(R_CTL - 1));
        ui_text_center(tx + tw * 0.5f, floorf(y + (h - ui_line(UI_SMALL)) * 0.5f), UI_SMALL,
                       on ? ui_col::ON : (hov ? ui_col::TEXT : ui_col::DIM), names[i], tr);
    }
    return changed;
}

bool ui_tabs_column(f32 x, f32 y, f32 w, f32 row, const char *const *names, u32 count, u32 &cur) {
    UiState &u = g_ui_state;
    bool changed = false;
    for (u32 i = 0; i < count; ++i) {
        f32 ry = y + row * (f32)i;
        bool hov = hit(x, ry, w, row - ui_px(4));
        if (hov && u.pressed && cur != i) {
            cur = i;
            changed = true;
        }
        bool on = cur == i;
        if (on) {
            ui_round(x, ry, w, row - ui_px(4), ui_col::ON_BG, ui_px(R_CTL));
            ui_round(x, ry + ui_px(8), ui_px(3), row - ui_px(20), ui_col::ON, ui_px(1.5f));
        } else if (hov) {
            ui_round(x, ry, w, row - ui_px(4), ui_col::HOVER, ui_px(R_CTL));
        }
        UiFont font = on ? UI_STRONG : UI_TEXT;
        ui_text(x + ui_px(16), floorf(ry + (row - ui_px(4) - ui_line(font)) * 0.5f), font,
                on ? ui_col::TEXT : (hov ? ui_col::TEXT : ui_col::DIM), names[i]);
    }
    return changed;
}

void ui_key_row(UiForm &f, const char *key, const char *what) {
    if (g_ui_state.hidden) return;
    f32 row = ui_px(29);
    f32 kw = ui_px(118);
    f32 kh = ui_line(UI_SMALL) + ui_px(8);
    f32 bw = ui_text_width(UI_SMALL, key) + ui_px(14);
    f32 ky = floorf(f.y + (row - kh) * 0.5f);
    ui_round(f.x, ky, bw, kh, ui_col::SIDE, ui_px(4));
    ui_stroke(f.x, ky, bw, kh, ui_col::LINE, ui_px(4), ui_hair());
    ui_text(f.x + ui_px(7), floorf(ky + (kh - ui_line(UI_SMALL)) * 0.5f), UI_SMALL, ui_col::TEXT, key);
    ui_text_fit(f.x + kw, floorf(f.y + (row - ui_line(UI_TEXT)) * 0.5f), UI_TEXT, ui_col::DIM, what, f.w - kw);
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
    u.hidden = false;
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
    f32 bw = on ? ui_px(6) : ui_px(4);
    f32 bx = floorf(tx + (tw - bw) * 0.5f);
    ui_round(bx, ty, bw, th, u.active == id ? ui_col::TEXT : (on ? ui_col::DIM : ui_col::OFF), bw * 0.5f);
    return s.h;
}
