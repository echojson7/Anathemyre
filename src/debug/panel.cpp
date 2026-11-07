// Панель отладки (F3, docs/ui.md): справа сверху, две вкладки.
// «Общее» — числа кадра и мира, время, сутки, камера, вид; «Мир» — осмотр таблиц мира по столбцам (только чтение).
// Мир панель не меняет: всё, что она просит, run_window превращает в намерения или настройки кадра
#include "debug.h"
#include "../core/hash.h"
#include "../sim/world.h"
#include "../ui/ui.h"
#include <stdio.h>
#include <string.h>

static constexpr f32 WIDTH = 360.0f;   // единицы ui_s
static constexpr f32 EDGE = 20.0f;     // от края окна
static constexpr f32 PAD = 18.0f;      // внутри панели

static const char *const TABS[DEBUG_TABS] = {"ОБЩЕЕ", "МИР"};
static const char *const DAY_SPEEDS[] = {"обычный", "×20", "×120", "стоп"};
static_assert(sizeof DAY_SPEEDS / sizeof *DAY_SPEEDS == sizeof CLOCK_SPEEDS / sizeof *CLOCK_SPEEDS, "скорости суток");

static void hm(f64 hour, char *out, usize n) {
    i32 m = (i32)(hour * 60.0) % 1440;
    if (m < 0) m += 1440;
    snprintf(out, n, "%02d:%02d", m / 60, m % 60);
}

static void tab_main(UiForm &f, const DebugInfo &in, DebugState &d) {
    char t[96];
    ui_section(f, "КАДР");
    snprintf(t, sizeof t, "%.0f · %.2f мс", in.fps, in.frame_ms);
    ui_value(f, "к/с · кадр", t);
    snprintf(t, sizeof t, "%.2f мс", in.frame_max_ms);
    ui_value(f, "худший кадр", t);
    snprintf(t, sizeof t, "%.2f мс", in.cpu_ms);
    ui_value(f, "CPU кадра", t);
    snprintf(t, sizeof t, "%.2f мс", in.gpu_ms);
    ui_value(f, "GPU", t);
    // Проходы кадра на GPU по меткам времени (docs/frame.md, «Замер»)
    for (u32 i = 0; i < in.passes && i < 8; ++i) {
        snprintf(t, sizeof t, "%.2f мс", in.pass_ms[i]);
        char name[48];
        snprintf(name, sizeof name, "  %s", in.pass_names[i] ? in.pass_names[i] : "?");
        ui_value(f, name, t);
    }
    if (!d.forward) {
        snprintf(t, sizeof t, "%u", in.vis_clusters);
        ui_value(f, "видимых кластеров", t);
    }
    snprintf(t, sizeof t, "%.3f мс", in.step_ms);
    ui_value(f, "шаг мира", t);
    ui_value(f, "видеокарта", in.gpu_name ? in.gpu_name : "—");

    ui_section(f, "ВРЕМЯ МИРА");
    snprintf(t, sizeof t, "%llu", (unsigned long long)in.step);
    ui_value(f, "шаг", t);
    snprintf(t, sizeof t, "%016llx", (unsigned long long)in.hash);
    ui_value(f, "хеш", t);
    // Пауза и шаг — кнопками в строку
    f.y += ui_px(6);
    f32 bh = f.row, gap = ui_px(8), bw = (f.w - gap) * 0.5f;
    if (ui_button(f.x, f.y, bw, bh, in.paused ? "Пауза" : "Идёт", in.paused)) d.pause_toggle = true;
    if (ui_button(f.x + bw + gap, f.y, bw, bh, "Шаг")) d.step_once = true;
    f.y += bh + ui_px(4);
    char names[8][16];
    const char *np[8];
    u32 ns = in.nspeeds < 8 ? in.nspeeds : 8;
    for (u32 i = 0; i < ns; ++i) {
        snprintf(names[i], sizeof names[i], "×%g", (double)in.speeds[i]);
        np[i] = names[i];
    }
    u32 sp = in.speed;
    if (ui_choice(f, "скорость", sp, np, ns)) d.speed_set = (i32)sp;

    ui_section(f, "СУТКИ");
    f32 h = (f32)in.hour;
    char hs[16];
    hm(in.hour, hs, sizeof hs);
    if (ui_slider(f, "час", h, 0.0f, 23.75f, 0.25f, "", hs)) {
        d.hour_set = true;
        d.hour = h;
    }
    u32 cs = in.clock_speed;
    if (ui_choice(f, "ход суток", cs, DAY_SPEEDS, ANA_COUNTOF(DAY_SPEEDS))) d.clock_speed_set = (i32)cs;

    ui_section(f, "КАМЕРА");
    snprintf(t, sizeof t, "%.0f  %.0f  %.0f", in.cam[0], in.cam[1], in.cam[2]);
    ui_value(f, "x  y  z, м", t);
    snprintf(t, sizeof t, "%.1f м", in.cam_above);
    ui_value(f, "над землёй", t);
    bool fl = in.cam_free;
    if (ui_toggle(f, "свободный полёт", fl)) d.flight_toggle = true;

    ui_section(f, "ВИД");
    ui_toggle(f, "отрезки и точки", d.lines);
    ui_toggle(f, "пиксельное правило", d.pixel);
    ui_toggle(f, "старый forward", d.forward);
}

// --- Осмотр мира: таблицы — приставки имён столбцов до точки ---
struct Table {
    char name[24];
    u32 first, count;  // столбцы
    i32 alive;         // столбец alive, −1 — таблица без мест (скаляры мира)
};

static u32 tables_of(const Column *cols, u32 n, Table *out, u32 cap) {
    u32 nt = 0;
    for (u32 i = 0; i < n; ++i) {
        const char *dot = strchr(cols[i].name, '.');
        usize len = dot ? (usize)(dot - cols[i].name) : strlen(cols[i].name);
        if (len >= sizeof out[0].name) len = sizeof out[0].name - 1;
        if (nt == 0 || strncmp(out[nt - 1].name, cols[i].name, len) || out[nt - 1].name[len]) {
            if (nt == cap) break;
            Table &t = out[nt++];
            memcpy(t.name, cols[i].name, len);
            t.name[len] = 0;
            t.first = i;
            t.count = 0;
            t.alive = -1;
        }
        Table &t = out[nt - 1];
        t.count++;
        if (dot && !strcmp(dot + 1, "alive") && cols[i].type == COL_U8) t.alive = (i32)i;
    }
    return nt;
}

static const char *type_name(const Column &c) {
    switch (c.type) {
    case COL_U8: return "u8";
    case COL_U16: return "u16";
    case COL_U32: return "u32";
    case COL_I32: return "i32";
    case COL_U64: return "u64";
    case COL_F32: return "f32";
    default: return "байты";
    }
}

static void value_text(const Column &c, u32 i, char *t, usize n) {
    if (c.type == COL_RAW || i >= c.count) {
        snprintf(t, n, "%llu байт", (unsigned long long)c.bytes);
        return;
    }
    const u8 *p = (const u8 *)c.data;
    switch (c.type) {
    case COL_U8: snprintf(t, n, "%u", p[i]); break;
    case COL_U16: { u16 v; memcpy(&v, p + i * 2, 2); snprintf(t, n, "%u", v); } break;
    case COL_U32: { u32 v; memcpy(&v, p + i * 4, 4); snprintf(t, n, "%u", v); } break;
    case COL_I32: { i32 v; memcpy(&v, p + i * 4, 4); snprintf(t, n, "%d", v); } break;
    case COL_U64: { u64 v; memcpy(&v, p + i * 8, 8); snprintf(t, n, "%016llx", (unsigned long long)v); } break;
    case COL_F32: { f32 v; memcpy(&v, p + i * 4, 4); snprintf(t, n, "%.3f", (double)v); } break;
    default: break;
    }
}

// Строка «имя — значение» с меткой изменения слева: светлая, пока столбец менялся в последние полсекунды
static void col_row(UiForm &f, const char *name, const char *value, bool changed) {
    f32 row = ui_px(22);
    f32 y = f.y + (row - ui_line(UI_TEXT)) * 0.5f;
    f32 m = ui_px(4);
    ui_rect(f.x, f.y + (row - m) * 0.5f, m, m, changed ? ui_col::ON : ui_col::OFF);
    ui_text(f.x + ui_px(12), y, UI_TEXT, ui_col::DIM, name);
    ui_text_right(f.x + f.w, y, UI_TEXT, ui_col::TEXT, value);
    f.y += row;
}

static void tab_world(UiForm &f, const World &w, WorldInspect &wi) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    // Изменения столбцов — по хешу за кадр (60 КБ на кадр, только когда вкладка открыта)
    wi.frame++;
    for (u32 i = 0; i < n && i < ANA_COUNTOF(wi.col_hash); ++i) {
        u64 h = hash_bytes(cols[i].data, cols[i].bytes);
        if (h != wi.col_hash[i]) wi.col_changed[i] = wi.frame;
        wi.col_hash[i] = h;
    }
    auto changed = [&](u32 i) { return i < ANA_COUNTOF(wi.col_changed) && wi.frame - wi.col_changed[i] < 30; };

    Table tabs[16];
    u32 nt = tables_of(cols, n, tabs, 16);
    if (!nt) return;
    const char *names[16];
    for (u32 i = 0; i < nt; ++i) names[i] = tabs[i].name;
    if (wi.table >= nt) wi.table = 0;
    u32 sel = wi.table;
    if (ui_choice(f, "таблица", sel, names, nt)) {
        wi.table = sel;
        wi.row = -1;
    }
    const Table &t = tabs[wi.table];
    char v[64];

    if (t.alive < 0) {
        // Скаляры: значения прямо в списке столбцов
        ui_section(f, "ЗНАЧЕНИЯ");
        for (u32 i = t.first; i < t.first + t.count; ++i) {
            value_text(cols[i], 0, v, sizeof v);
            col_row(f, cols[i].name + strlen(t.name) + 1, v, changed(i));
        }
        return;
    }
    const Column &al = cols[t.alive];
    const u8 *alive = (const u8 *)al.data;
    u32 cap = al.count, live = 0;
    for (u32 i = 0; i < cap; ++i) live += alive[i] ? 1 : 0;
    snprintf(v, sizeof v, "%u из %u", live, cap);
    ui_value(f, "живых", v);

    ui_section(f, "СТОЛБЦЫ");
    for (u32 i = t.first; i < t.first + t.count; ++i) {
        const Column &c = cols[i];
        if (c.count > 1) snprintf(v, sizeof v, "%s ×%u", type_name(c), c.count);
        else value_text(c, 0, v, sizeof v);
        col_row(f, c.name + strlen(t.name) + 1, v, changed(i));
    }

    // Строка: стрелками по живым местам или кликом по точке в мире
    ui_section(f, "СТРОКА");
    if (wi.row >= (i32)cap || (wi.row >= 0 && !alive[wi.row])) wi.row = -1;
    if (wi.row < 0)
        for (u32 i = 0; i < cap; ++i)
            if (alive[i]) { wi.row = (i32)i; break; }
    if (wi.row < 0) {
        ui_value(f, "живых строк нет", "");
        return;
    }
    // Соседняя живая строка: ‹ › выбора из трёх (назад, текущая, вперёд)
    u32 pick = 1;
    char rs[24];
    snprintf(rs, sizeof rs, "%d", wi.row);
    const char *shown[3] = {rs, rs, rs};
    if (ui_choice(f, "место", pick, shown, 3)) {
        i32 dir = pick == 0 ? -1 : 1;
        for (u32 k = 1; k < cap; ++k) {
            i32 r = (wi.row + dir * (i32)k + (i32)cap) % (i32)cap;
            if (alive[r]) { wi.row = r; break; }
        }
    }
    for (u32 i = t.first; i < t.first + t.count; ++i) {
        const Column &c = cols[i];
        if (c.count != cap) continue;  // счётчики таблицы — выше
        value_text(c, (u32)wi.row, v, sizeof v);
        col_row(f, c.name + strlen(t.name) + 1, v, changed(i));
    }
    ui_text(f.x, f.y + ui_px(6), UI_SMALL, ui_col::FAINT, "клик по точке в мире выбирает строку");
    f.y += ui_px(6) + ui_line(UI_SMALL);
}

void debug_panel(const DebugInfo &info, const World &w, DebugState &d, WorldInspect &wi) {
    f32 pw = ui_px(WIDTH), pad = ui_px(PAD);
    f32 x = ui_width() - ui_px(EDGE) - pw, y = ui_px(EDGE);
    u32 bg = ui_reserve();
    UiForm f = ui_form(x + pad, y + pad, pw - 2 * pad);
    f.row = ui_px(30);
    f.ctl = ui_px(160);
    ui_text(f.x, f.y, UI_SMALL, ui_col::DIM, "ОТЛАДКА", ui_px(1.2f));
    ui_text_right(f.x + f.w, f.y, UI_SMALL, ui_col::FAINT, "F3");
    f.y += ui_line(UI_SMALL) + ui_px(14);
    ui_tabs(f.x, f.y, f.w, TABS, DEBUG_TABS, d.tab);
    f.y += ui_line(UI_SMALL) + ui_px(10);
    ui_hline(f.x, f.y, f.w, ui_col::LINE);
    f.y += ui_px(2);
    if (d.tab == DEBUG_TAB_WORLD) tab_world(f, w, wi);
    else tab_main(f, info, d);
    f32 ph = f.y + pad - y;
    ui_soft_at(bg, x, y, pw, ph, ui_col::PANEL, ui_px(10));
    ui_block(x, y, pw, ph);
}

// Таблица точек по имени (номер в списке таблиц меняется, когда появляются новые)
static bool dots_table(const World &w, const WorldInspect &wi) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    Table tabs[16];
    u32 nt = tables_of(cols, n, tabs, 16);
    return wi.table < nt && !strcmp(tabs[wi.table].name, "dots");
}

void debug_inspect_mark(const World &w, const WorldInspect &wi, DebugDraw &dbg) {
    if (wi.row < 0 || wi.row >= (i32)DOTS_CAPACITY || !w.dots.alive[wi.row] || !dots_table(w, wi)) return;
    const Dots &d = w.dots;
    WPos p{d.cx[wi.row], d.cz[wi.row], d.x[wi.row], d.y[wi.row], d.z[wi.row]};
    WPos top = p;
    top.y += 4.0f;
    const u32 c = rgba8(240, 238, 230);
    dbg_line(dbg, p, top, c);
    dbg_cross(dbg, p, 1.0f, c);
    dbg_cross(dbg, top, 0.4f, c);
}

void debug_inspect_pick(const World &w, WorldInspect &wi, const WPos &at) {
    // Точки — единственная таблица с местом в мире сейчас; с людьми сюда добавится их таблица
    const Dots &dd = w.dots;
    f32 best = 16.0f;  // квадрат радиуса выбора, м²
    i32 row = -1;
    for (u32 i = 0; i < DOTS_CAPACITY; ++i) {
        if (!dd.alive[i]) continue;
        Vec2 dl = wpos_delta_xz(at.cx, at.cz, at.x, at.z, dd.cx[i], dd.cz[i], dd.x[i], dd.z[i]);
        f32 d2 = dl.x * dl.x + dl.y * dl.y;
        if (d2 < best) { best = d2; row = (i32)i; }
    }
    if (row < 0) return;
    // Таблица точек — по имени, а не по номеру
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    Table tabs[16];
    u32 nt = tables_of(cols, n, tabs, 16);
    for (u32 i = 0; i < nt; ++i)
        if (!strcmp(tabs[i].name, "dots")) wi.table = i;
    wi.row = row;
}
