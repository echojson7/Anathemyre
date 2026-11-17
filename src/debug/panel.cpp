// Окно отладки (F3, слева; docs/ui.md): четыре вкладки — общее (время, сутки, камера, вид), свет (тонмаппинг, слои,
// сравнения), люди (отрезки людей и стенд), таблицы (осмотр мира по столбцам, только чтение). Справа от него — окно
// показателей (metrics.cpp). Мир окна не меняют: всё, что они просят, run_window превращает в намерения или настройки
#include "panel_util.h"
#include "../core/hash.h"
#include "../sim/world.h"
#include "../ui/ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static constexpr f32 WIDTH = 380.0f;    // единицы ui_s: окно отладки
static constexpr f32 METRICS_W = 420.0f; // окно показателей
static constexpr f32 EDGE = 16.0f;      // от края окна
static constexpr f32 PAD = 16.0f;       // внутри окна

static const char *const TABS[DEBUG_TABS] = {"ОБЩЕЕ", "СВЕТ", "ЛЮДИ", "ТАБЛИЦЫ"};

static void tab_main(UiForm &f, const DebugInfo &in, DebugState &d) {
    ui_section(f, "ВРЕМЯ МИРА");
    // Пауза и шаг — кнопками в строку (клавиши Space и «.»)
    f.y += ui_px(4);
    f32 bh = f.row - ui_px(4), gap = ui_px(8), bw = floorf((f.w - gap) * 0.5f);
    if (ui_button(f.x, f.y, bw, bh, in.paused ? "Пауза" : "Идёт", in.paused)) d.pause_toggle = true;
    if (ui_button(f.x + bw + gap, f.y, bw, bh, "Шаг")) d.step_once = true;
    f.y += bh + ui_px(6);
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
    dbg_hm(in.hour, hs, sizeof hs);
    if (ui_slider(f, "час", h, 0.0f, 23.75f, 0.25f, "", hs)) {
        d.hour_set = true;
        d.hour = h;
    }
    u32 cs = in.clock_speed;
    if (ui_choice(f, "ход суток", cs, dbg_day_speeds(), ANA_COUNTOF(CLOCK_SPEEDS))) d.clock_speed_set = (i32)cs;

    ui_section(f, "КАМЕРА И ВИД");
    bool fl = in.cam_free;
    if (ui_toggle(f, "свободный полёт  (F)", fl)) d.flight_toggle = true;
    ui_toggle(f, "отрезки и точки  (L)", d.lines);
}

static void tab_light(UiForm &f, DebugState &d) {
    // Свет (docs/light.md): тонмаппинг на выбор по кадрам, местная экспозиция
    ui_section(f, "СВЕТ");
    static const char *const TONEMAPS[] = {"AgX", "ACES"};
    ui_segment(f, "тонмаппинг", d.tonemap, TONEMAPS, ANA_COUNTOF(TONEMAPS));
    bool local = !d.local_off;
    if (ui_toggle(f, "местная экспозиция", local)) d.local_off = !local;
    bool lamps = !d.lamps_off;
    if (ui_toggle(f, "лампы карты", lamps)) d.lamps_off = !lamps;
    // Слои: что из света показать отдельно (docs/shadow.md, docs/sdf.md, docs/gi.md)
    ui_section(f, "СЛОИ");
    static const char *const SM_VIEWS[] = {"кадр", "страницы", "только тень"};
    ui_choice(f, "тени солнца", d.shadow_pages, SM_VIEWS, ANA_COUNTOF(SM_VIEWS));
    static const char *const SDF_VIEWS[] = {"кадр", "только небо", "поле", "выкл"};
    ui_choice(f, "поле расстояний", d.sdf_view, SDF_VIEWS, ANA_COUNTOF(SDF_VIEWS));
    static const char *const GI_VIEWS[] = {"кадр", "только он", "без зондов", "ячейки", "без ячеек"};
    ui_choice(f, "непрямой свет", d.gi_view, GI_VIEWS, ANA_COUNTOF(GI_VIEWS));
    // Сравнения: старое и упрощённое рядом с нынешним
    ui_section(f, "СРАВНЕНИЕ");
    ui_toggle(f, "ячейки, старое правило  (P)", d.pixel);
    ui_toggle(f, "без кеша земли", d.gcache_off);
    ui_toggle(f, "свет земли без кеша", d.gclight_off);
    ui_toggle(f, "тестовые огни", d.test_lights);
    ui_toggle(f, "огни по клеткам", d.froxel_heat);
    ui_toggle(f, "сканы CC0", d.scans);
    f.y += ui_px(6);
    ui_text(f.x, f.y, UI_SMALL, ui_col::FAINT, "слои и сравнения — «не для замера» в показателях");
    f.y += ui_line(UI_SMALL) + ui_px(4);
}

static void tab_people(UiForm &f, const DebugInfo &in, DebugState &d) {
    // Люди мира: капсулы шага поверх тел, путь выбранного, след касаний стоп (отрезки отладки, L)
    ui_section(f, "ЛЮДИ МИРА");
    ui_toggle(f, "капсулы, путь и след стоп", d.people_lines);
    ui_toggle(f, "стороны боя и цели", d.people_sides);
    ui_toggle(f, "показ тел: поза привязки, без одежды", d.people_show);
    // Стенд людей (stand.cpp): человек у Остова, проверка весов, морфов и клипов
    ui_section(f, "СТЕНД");
    ui_toggle(f, "человек у Остова", d.stand);
    if (!d.stand) {
        f.y += ui_px(6);
        ui_text(f.x, f.y, UI_SMALL, ui_col::FAINT, "кость, скелет, морф и клипы — при включённом стенде");
        f.y += ui_line(UI_SMALL) + ui_px(4);
        return;
    }
    ui_toggle(f, "согнуть кость  (B)", d.stand_bend);
    static const char *const BONES[] = {"локоть", "колено"};
    ui_segment(f, "кость", d.stand_bone, BONES, ANA_COUNTOF(BONES));
    static const char *const SKELS[] = {"game_engine", "mixamo"};
    ui_choice(f, "скелет", d.stand_skel, SKELS, ANA_COUNTOF(SKELS));
    ui_toggle(f, "оба скелета рядом", d.stand_both);
    // Морфы тела в порядке сетки (tools/humanconv MORPHS)
    static const char *const MORPHS[] = {"нет", "женщина", "полнее", "худее", "сильнее", "старше", "живот",
                                         "ж: полнее", "ж: худее", "ж: сильнее", "плечи", "бёдра", "ноги", "торс",
                                         "руки", "шея", "голова", "род 1", "род 2", "нос", "горбинка", "челюсть",
                                         "подбородок", "губы", "глаза", "скулы", "брови", "уши"};
    ui_choice(f, "морф", d.stand_morph, MORPHS, ANA_COUNTOF(MORPHS));
    u32 nclips = 0;
    const char *const *clips = debug_stand_clip_names(&nclips);
    if (nclips) {
        ui_section(f, "СТЕНД: КЛИП");
        ui_choice(f, "клип", d.stand_clip, clips, nclips);
        ui_choice(f, "смешать с", d.stand_clip_b, clips, nclips);
        if (d.stand_clip_b) ui_slider(f, "доля второго", d.stand_blend, 0.0f, 1.0f, 0.05f, "%.2f");
        static const char *const SPEEDS[] = {"×1", "×0.5", "×0.25", "×0.1"};
        static_assert(ANA_COUNTOF(SPEEDS) == ANA_COUNTOF(STAND_SPEEDS), "скорости стенда");
        ui_segment(f, "скорость клипа", d.stand_speed, SPEEDS, ANA_COUNTOF(SPEEDS));
        ui_toggle(f, "пауза клипа", d.stand_pause);
        ui_toggle(f, "на месте", d.stand_inplace);
        ui_toggle(f, "капсулы дорожек", d.stand_caps);
        if (ui_button_row(f, "С начала")) d.stand_restart = true;
    }
    (void)in;
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
    f32 row = ui_line(UI_DATA) + ui_px(7);
    f32 y = floorf(f.y + (row - ui_line(UI_DATA)) * 0.5f);
    f32 m = ui_px(6);
    ui_round(f.x, floorf(f.y + (row - m) * 0.5f), m, m, changed ? ui_col::ON : ui_col::OFF, m * 0.5f);
    f32 nx = ui_text(f.x + ui_px(14), y, UI_DATA, ui_col::DIM, name);
    ui_text_fit(nx + ui_px(10), y, UI_DATA, ui_col::TEXT, value, f.x + f.w - nx - ui_px(10), true);
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
    ui_text(f.x, f.y + ui_px(6), UI_SMALL, ui_col::FAINT, "клик по человеку в мире выбирает строку");
    f.y += ui_px(6) + ui_line(UI_SMALL);
}

// Прокрутка вкладок: у каждой своя, держится между кадрами (только вид, в мир и настройки не уходит)
static UiScroll g_scroll[DEBUG_TABS];

void debug_panel(const DebugInfo &info, const World &w, DebugState &d, WorldInspect &wi) {
    f32 pw = ui_px(WIDTH), pad = ui_px(PAD), edge = ui_px(EDGE);
    f32 x = edge, y = edge;
    f32 max_h = ui_height() - 2 * edge;
    // Справа — показатели (отдельное окно; кнопка «Показатели» в заголовке прячет его)
    if (d.metrics) {
        f32 mw = ui_px(METRICS_W);
        f32 mx = ui_width() - edge - mw;
        if (mx < x + pw + edge) mx = x + pw + edge;  // узкое окно: не наезжать на отладку
        debug_metrics_panel(info, d, mx, y, mw, max_h);
    }
    u32 bg = ui_panel_reserve();
    UiForm f = ui_form(x + pad, y + pad, pw - 2 * pad);
    f.row = ui_px(30);
    f.ctl = ui_px(176);
    // Заголовок: имя окна, клавиша и переключатель окна показателей
    f32 bh = ui_line(UI_SMALL) + ui_px(12);
    f32 ty = f.y + floorf((bh - ui_line(UI_STRONG)) * 0.5f);
    f32 tx = ui_text(f.x, ty, UI_STRONG, ui_col::TEXT, "Отладка");
    ui_text(tx + ui_px(8), f.y + floorf((bh - ui_line(UI_SMALL)) * 0.5f), UI_SMALL, ui_col::FAINT, "F3");
    f32 bw = ui_text_width(UI_STRONG, "Показатели") + ui_px(24);
    if (ui_button(f.x + f.w - bw, f.y, bw, bh, "Показатели", d.metrics)) d.metrics = !d.metrics;
    f.y += bh + ui_px(10);
    if (d.tab >= DEBUG_TABS) d.tab = DEBUG_TAB_MAIN;
    ui_tabs(f.x, f.y, f.w, TABS, DEBUG_TABS, d.tab);
    f.y += ui_line(UI_SMALL) + ui_px(14);
    // Вкладка листается: окно не ниже экрана, заголовок и вкладки стоят на месте. Область — во всю ширину окна
    // (подсветка строк выходит за поля), полоса прокрутки — в правом поле
    UiScroll &sc = g_scroll[d.tab];
    f32 top = f.y, bottom_pad = ui_px(8);
    f32 room = y + max_h - pad - top;
    f.y = ui_scroll_begin(sc, x, top, pw - pad + ui_px(10), room);
    switch (d.tab) {
    case DEBUG_TAB_LIGHT: tab_light(f, d); break;
    case DEBUG_TAB_PEOPLE: tab_people(f, info, d); break;
    case DEBUG_TAB_WORLD: tab_world(f, w, wi); break;
    default: tab_main(f, info, d); break;
    }
    f32 shown = ui_scroll_end(sc, f.y + bottom_pad);
    f32 ph = top + shown + pad - bottom_pad - y;
    ui_panel_at(bg, x, y, pw, ph);
    ui_block(x, y, pw, ph);
}

// Таблица людей по имени (номер в списке таблиц меняется, когда появляются новые)
static u32 people_table(const World &w) {
    Column cols[MAX_COLUMNS];
    u32 n = world_columns(w, cols);
    Table tabs[16];
    u32 nt = tables_of(cols, n, tabs, 16);
    for (u32 i = 0; i < nt; ++i)
        if (!strcmp(tabs[i].name, "people")) return i;
    return 0xFFFFFFFFu;
}

void debug_inspect_mark(const World &w, const WorldInspect &wi, DebugDraw &dbg) {
    const People &p = w.people;
    if (wi.row < 0 || wi.row >= (i32)PEOPLE_CAPACITY || !p.alive[wi.row] || wi.table != people_table(w)) return;
    const u32 s = (u32)wi.row;
    WPos at{p.cx[s], p.cz[s], p.x[s], p.y[s], p.z[s]};
    WPos top = at;
    top.y += 4.0f;
    const u32 c = rgba8(240, 238, 230);
    dbg_line(dbg, at, top, c);
    dbg_cross(dbg, at, 1.0f, c);
    dbg_cross(dbg, top, 0.4f, c);
}

void debug_inspect_pick(const World &w, WorldInspect &wi, const WPos &at) {
    // Люди — таблица с местом в мире: строка ближайшего к точке земли под курсором (не дальше 2 м)
    const People &p = w.people;
    f32 best = 4.0f;  // квадрат радиуса выбора, м²
    i32 row = -1;
    for (u32 i = 0; i < PEOPLE_CAPACITY; ++i) {
        if (!p.alive[i]) continue;
        Vec2 dl = wpos_delta_xz(at.cx, at.cz, at.x, at.z, p.cx[i], p.cz[i], p.x[i], p.z[i]);
        f32 d2 = dl.x * dl.x + dl.y * dl.y;
        if (d2 < best) { best = d2; row = (i32)i; }
    }
    u32 t = people_table(w);
    if (row < 0 || t == 0xFFFFFFFFu) return;
    wi.table = t;
    wi.row = row;
}
