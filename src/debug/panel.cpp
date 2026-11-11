// Панель отладки (F3, docs/ui.md): справа сверху, три вкладки; не выше окна, вкладка листается колесом и полосой.
// «Общее» — числа кадра и мира, время, сутки, камера, вид; «Мир» — осмотр таблиц мира по столбцам (только чтение).
// Мир панель не меняет: всё, что она просит, run_window превращает в намерения или настройки кадра
#include "debug.h"
#include "../core/hash.h"
#include "../sim/world.h"
#include "../ui/ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static constexpr f32 WIDTH = 360.0f;   // единицы ui_s
static constexpr f32 EDGE = 20.0f;     // от края окна
static constexpr f32 PAD = 18.0f;      // внутри панели

static const char *const TABS[DEBUG_TABS] = {"ОБЩЕЕ", "КАДР", "МИР"};
static const char *const DAY_SPEEDS[] = {"обычный", "×20", "×120", "стоп"};
static_assert(sizeof DAY_SPEEDS / sizeof *DAY_SPEEDS == sizeof CLOCK_SPEEDS / sizeof *CLOCK_SPEEDS, "скорости суток");

static void hm(f64 hour, char *out, usize n) {
    i32 m = (i32)(hour * 60.0) % 1440;
    if (m < 0) m += 1440;
    snprintf(out, n, "%02d:%02d", m / 60, m % 60);
}

// Счётчик коротко: 950, 12.3к, 862к, 4.56М
static void count_str(u64 v, char *out, usize n) {
    if (v < 10000) snprintf(out, n, "%llu", (unsigned long long)v);
    else if (v < 100000) snprintf(out, n, "%.1fк", (double)v / 1e3);
    else if (v < 1000000) snprintf(out, n, "%.0fк", (double)v / 1e3);
    else snprintf(out, n, "%.2fМ", (double)v / 1e6);
}

// Два счётчика в строку: «a · b»
static void pair_value(UiForm &f, const char *label, u64 a, u64 b) {
    char sa[24], sb[24], t[64];
    count_str(a, sa, sizeof sa);
    count_str(b, sb, sizeof sb);
    snprintf(t, sizeof t, "%s · %s", sa, sb);
    ui_value(f, label, t);
}

static void tab_main(UiForm &f, const DebugInfo &in, DebugState &d) {
    char t[96];
    ui_section(f, "КАДР");
    // Замер с отладочным режимом нельзя сравнивать с бюджетом (docs/budget.md) — первой строкой
    if (in.unfair && in.unfair[0]) ui_value(f, "! не для замера", in.unfair, ui_col::WARN);
    else ui_value(f, "замер", "честный");
    snprintf(t, sizeof t, "%.0f · %.2f мс", in.fps, in.frame_ms);
    ui_value(f, "к/с · кадр", t);
    snprintf(t, sizeof t, "%.2f мс", in.frame_max_ms);
    ui_value(f, "худший кадр", t);
    snprintf(t, sizeof t, "%.2f мс", in.cpu_ms);
    ui_value(f, "CPU кадра", t);
    snprintf(t, sizeof t, "%.2f мс", in.gpu_ms);
    ui_value(f, "GPU", t);
    // Проходы кадра на GPU по меткам времени (docs/frame.md, «Замер»)
    for (u32 i = 0; i < in.passes && i < DEBUG_MAX_PASSES; ++i) {
        snprintf(t, sizeof t, "%.2f мс", in.pass_ms[i]);
        char name[48];
        snprintf(name, sizeof name, "  %s", in.pass_names[i] ? in.pass_names[i] : "?");
        ui_value(f, name, t);
    }
    snprintf(t, sizeof t, "%u", in.vis_clusters);
    ui_value(f, "видимых кластеров", t);
    // Работа освещения (docs/tiles.md): сколько чего посчитано, а не за сколько
    if (in.work) {
        pair_value(f, "плитки неба · земли", in.work_tiles[0], in.work_tiles[1]);
        pair_value(f, "плитки сеток · смеси", in.work_tiles[2], in.work_tiles[3]);
        count_str(in.work_tiles[4], t, sizeof t);
        ui_value(f, "плитки земли на пиксель", t);
        pair_value(f, "пиксели земли · сеток", in.work_px[1], in.work_px[2]);
        pair_value(f, "поверхности земли · сеток", in.work_surf[0], in.work_surf[1]);
        count_str(in.work_shadow, t, sizeof t);
        ui_value(f, "выборки тени земли", t);
        // Кеш земли (docs/gcache.md): страниц в кадре и запечено за кадр; поверхности земли мимо кеша
        if (in.gc_off) {
            snprintf(t, sizeof t, "выкл: %s", in.gc_off);
            ui_value(f, "! кеш земли", t, ui_col::WARN);  // вся земля — на пиксель, кадр медленнее
        } else {
            pair_value(f, "кеш земли: страниц · запечено", in.work_gc[0], in.work_gc[1]);
        }
        pair_value(f, "земля мимо кеша · без места", in.work_gc[3], in.work_gc[2]);
        // Тени солнца страницами (docs/shadow.md): страниц в кадре и нарисовано в нём; без места и выборок без
        // страницы (оба должны быть 0); вызовы кластеров в новые страницы
        pair_value(f, "тени: страниц · нарисовано", in.work_sm[0], in.work_sm[1]);
        pair_value(f, "тени: без места · без страницы", in.work_sm[2], in.work_sm[3]);
        pair_value(f, "тени: вызовов · выборок", in.work_sm[4], in.work_sm[5]);
        // Шаг светила (6б-2): страниц нового набора, отложенных стройкой в фоне, и сколько света уже на нём
        snprintf(t, sizeof t, "%u · %u %%", in.work_sm[6], in.work_sm[7]);
        ui_value(f, "тени: стройка шага · перетекание", t);
        // Поле расстояний мира (docs/sdf.md): выборки светом; засыпка и сборка — только когда кирпичи грязные
        pair_value(f, "поле: выборок · кирпичей", in.work_sdf[0], in.work_sdf[2]);
        snprintf(t, sizeof t, "%u", in.work_sdf[1]);
        ui_value(f, "поле: треугольников засыпки", t);
    }
    // Переполнения (docs/graph.md «Переполнения»): что-то не нарисовано или считается медленным путём
    for (u32 i = 0; i < in.overflows && i < DEBUG_MAX_OVERFLOWS; ++i) {
        if (in.over_cap[i]) snprintf(t, sizeof t, "%llu из %llu", (unsigned long long)in.over_want[i],
                                     (unsigned long long)in.over_cap[i]);
        else snprintf(t, sizeof t, "%llu", (unsigned long long)in.over_want[i]);
        char name[64];
        snprintf(name, sizeof name, "! %s", in.over_what[i] ? in.over_what[i] : "?");
        ui_value(f, name, t);
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
    ui_toggle(f, "тестовые огни", d.test_lights);
    ui_toggle(f, "огни по клеткам", d.froxel_heat);
    ui_toggle(f, "без кеша земли", d.gcache_off);
    static const char *const SM_VIEWS[] = {"кадр", "страницы", "только тень"};
    ui_choice(f, "тени солнца", d.shadow_pages, SM_VIEWS, ANA_COUNTOF(SM_VIEWS));
    static const char *const SDF_VIEWS[] = {"кадр", "только небо", "поле", "выкл"};
    ui_choice(f, "поле расстояний", d.sdf_view, SDF_VIEWS, ANA_COUNTOF(SDF_VIEWS));
    // Свет (docs/light.md): тонмаппинг на выбор по кадрам; освещённость земли и глаз — «Единицы», «Экспозиция»
    ui_section(f, "СВЕТ");
    static const char *const TONEMAPS[] = {"AgX", "ACES"};
    ui_choice(f, "тонмаппинг", d.tonemap, TONEMAPS, ANA_COUNTOF(TONEMAPS));
    if (in.light) {
        snprintf(t, sizeof t, "%.4g · %.4g лк", (double)in.lux_key, (double)in.lux_sky);
        ui_value(f, "светило · небо", t);
        snprintf(t, sizeof t, "%.2f → %.2f", (double)in.ev100, (double)in.ev100_goal);
        ui_value(f, "EV100 глаза → цель", t);
    }

    // Люди мира: капсулы шага поверх тел, путь выбранного, след касаний стоп (отрезки отладки, L)
    ui_section(f, "ЛЮДИ");
    ui_toggle(f, "капсулы, путь и след стоп", d.people_lines);
    // Стенд людей (stand.cpp): человек у руины, проверка весов и морфов
    ui_section(f, "ЛЮДИ: СТЕНД");
    ui_toggle(f, "человек у руины", d.stand);
    ui_toggle(f, "согнуть кость (B)", d.stand_bend);
    static const char *const BONES[] = {"локоть", "колено"};
    ui_choice(f, "кость", d.stand_bone, BONES, ANA_COUNTOF(BONES));
    static const char *const SKELS[] = {"game_engine", "mixamo"};
    ui_choice(f, "скелет", d.stand_skel, SKELS, ANA_COUNTOF(SKELS));
    static const char *const MORPHS[] = {"нет", "полнее", "худее", "сильнее", "нос", "челюсть"};
    ui_choice(f, "морф", d.stand_morph, MORPHS, ANA_COUNTOF(MORPHS));
    ui_toggle(f, "оба скелета рядом", d.stand_both);
    u32 nclips = 0;
    const char *const *clips = debug_stand_clip_names(&nclips);
    if (nclips) {
        ui_choice(f, "клип", d.stand_clip, clips, nclips);
        ui_choice(f, "смешать с", d.stand_clip_b, clips, nclips);
        if (d.stand_clip_b) ui_slider(f, "доля второго", d.stand_blend, 0.0f, 1.0f, 0.05f, "%.2f");
        static const char *const SPEEDS[] = {"×1", "×0.5", "×0.25", "×0.1"};
        static_assert(ANA_COUNTOF(SPEEDS) == ANA_COUNTOF(STAND_SPEEDS), "скорости стенда");
        ui_choice(f, "скорость клипа", d.stand_speed, SPEEDS, ANA_COUNTOF(SPEEDS));
        ui_toggle(f, "пауза клипа", d.stand_pause);
        if (ui_button_row(f, "с начала")) d.stand_restart = true;
        ui_toggle(f, "на месте", d.stand_inplace);
        ui_toggle(f, "капсулы дорожек", d.stand_caps);
    }
    char sv[24];
    count_str(in.work_skin, sv, sizeof sv);
    ui_value(f, "скинено вершин", in.work ? sv : "—");
}

// --- Кадр по частям (docs/frame_time.md): график последних кадров и части за секунду ---
static void frame_graph(UiForm &f, const FrameProf &p) {
    f32 h = ui_px(64), hair = ui_hair();
    f32 bw = floorf(f.w / (f32)PROF_HISTORY);
    if (bw < 1) bw = 1;
    u32 shown = (u32)(f.w / bw);
    if (shown > p.hist_count) shown = p.hist_count;
    f32 top_y = f.y + ui_line(UI_SMALL) + ui_px(4);
    // Верх графика — ближайшая сверху ступень: так масштаб не прыгает от кадра к кадру
    f32 peak = 0;
    for (u32 k = 0; k < shown; ++k) {
        f32 v = p.hist[(p.hist_pos + PROF_HISTORY - 1 - k) % PROF_HISTORY];
        if (v > peak) peak = v;
    }
    static const f32 STEPS[] = {1, 2, 4, 8, 16, 33, 66, 133, 266};
    f32 scale = STEPS[ANA_COUNTOF(STEPS) - 1];
    for (f32 st : STEPS)
        if (peak <= st) { scale = st; break; }
    char t[32];
    snprintf(t, sizeof t, "%g мс", (double)scale);
    ui_text(f.x, f.y, UI_SMALL, ui_col::DIM, t);
    snprintf(t, sizeof t, "%u кадров", shown);
    ui_text_right(f.x + f.w, f.y, UI_SMALL, ui_col::FAINT, t);
    ui_hline(f.x, top_y, f.w, ui_col::LINE);
    ui_hline(f.x, top_y + h, f.w, ui_col::LINE);
    // 60 к/с — отметка, если влезает
    if (scale > 16.7f) ui_hline(f.x, top_y + h - h * 16.7f / scale, f.w, ui_col::LINE);
    // Кадры справа налево, новый — справа; вдвое длиннее среднего — ярче
    f32 avg = (f32)p.total_avg_last;
    for (u32 k = 0; k < shown; ++k) {
        f32 v = p.hist[(p.hist_pos + PROF_HISTORY - 1 - k) % PROF_HISTORY];
        f32 bh = h * (v < scale ? v : scale) / scale;
        if (bh < hair) bh = hair;
        f32 x = f.x + f.w - (f32)(k + 1) * bw;
        ui_rect(x, top_y + h - bh, bw, bh, avg > 0 && v > 2 * avg ? ui_col::TEXT : ui_col::DIM);
    }
    f.y = top_y + h + ui_px(8);
}

static void tab_frame(UiForm &f, const DebugInfo &in) {
    char t[96];
    ui_section(f, "КАДР ЗА СЕКУНДУ");
    snprintf(t, sizeof t, "%.0f · %.2f мс", in.fps, in.frame_ms);
    ui_value(f, "к/с · кадр", t);
    snprintf(t, sizeof t, "%.2f мс", in.frame_max_ms);
    ui_value(f, "худший кадр", t);
    snprintf(t, sizeof t, "%.2f · %.2f мс", in.gpu_ms, in.gpu_max_ms);
    ui_value(f, "GPU · худший", t);
    if (!in.prof) return;
    const FrameProf &p = *in.prof;
    f.y += ui_px(6);
    frame_graph(f, p);
    // Части: среднее, наибольшее за секунду и сколько эта часть заняла в худшем кадре секунды
    ui_section(f, "ПО ЧАСТЯМ, МС");
    ui_value(f, "", "сред · макс · худш");
    for (u32 i = 0; i < p.nparts && i < DEBUG_MAX_FRAME_PARTS; ++i) {
        snprintf(t, sizeof t, "%.2f · %.2f · %.2f", p.avg_last[i], p.max_last[i], p.worst_last[i]);
        ui_value(f, p.names[i] ? p.names[i] : "?", t);
    }
    snprintf(t, sizeof t, "%.2f", p.worst_total_last);
    ui_value(f, "худший кадр целиком", t);
    ui_text(f.x, f.y + ui_px(6), UI_SMALL, ui_col::FAINT, "ожидание, картинка, показ — CPU ждёт");
    f.y += ui_px(6) + ui_line(UI_SMALL);
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

// Прокрутка вкладок: у каждой своя, держится между кадрами (только вид, в мир и настройки не уходит)
static UiScroll g_scroll[DEBUG_TABS];

void debug_panel(const DebugInfo &info, const World &w, DebugState &d, WorldInspect &wi) {
    f32 pw = ui_px(WIDTH), pad = ui_px(PAD), edge = ui_px(EDGE);
    f32 x = ui_width() - edge - pw, y = edge;
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
    // Вкладка листается: панель не ниже окна, заголовок и вкладки стоят на месте. Область — во всю ширину
    // панели (подсветка строк выходит за поля), полоса прокрутки — в правом поле
    UiScroll &sc = g_scroll[d.tab < DEBUG_TABS ? d.tab : 0];
    f32 top = f.y, bottom_pad = ui_px(8);
    f32 room = ui_height() - edge - pad - top;
    f.y = ui_scroll_begin(sc, x, top, pw - pad + ui_px(12), room);
    if (d.tab == DEBUG_TAB_WORLD) tab_world(f, w, wi);
    else if (d.tab == DEBUG_TAB_FRAME) tab_frame(f, info);
    else tab_main(f, info, d);
    f32 shown = ui_scroll_end(sc, f.y + bottom_pad);
    f32 ph = top + shown + pad - bottom_pad - y;
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
