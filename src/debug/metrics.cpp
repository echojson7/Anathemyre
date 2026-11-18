// Окно показателей (F3, справа; docs/ui.md «Показатели»): только числа кадра, освещения и мира, разделы
// сворачиваются. «Скопировать всё» снимает весь текст окна (и свёрнутые разделы) в буфер обмена.
// Имена строк освещения те же, что были в панели F3: по ним сверяют замеры
#include "panel_util.h"
#include "../ui/ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Разделы — биты DebugState::metrics_closed
enum MetricsFold : u32 { MF_FRAME, MF_GRAPH, MF_PARTS, MF_PASSES, MF_WORK, MF_OVER, MF_LIGHT, MF_WORLD };

// Два счётчика в строку: «a · b»
static void pair_row(UiForm &f, const char *label, u64 a, u64 b) {
    char sa[24], sb[24], t[64];
    dbg_count(a, sa, sizeof sa);
    dbg_count(b, sb, sizeof sb);
    snprintf(t, sizeof t, "%s · %s", sa, sb);
    ui_value(f, label, t);
}

static bool fold(UiForm &f, DebugState &d, u32 bit, const char *title, const char *note = nullptr, bool capture = true) {
    bool open = !(d.metrics_closed & (1u << bit));
    ui_fold(f, title, open, open ? nullptr : note, capture);
    d.metrics_closed = open ? d.metrics_closed & ~(1u << bit) : d.metrics_closed | (1u << bit);
    return open;
}

// График последних кадров (docs/frame_time.md): кадры справа налево, новый — справа
static void frame_graph(UiForm &f, const FrameProf &p) {
    f32 h = ui_px(60), hair = ui_hair();
    f32 bw = floorf(f.w / (f32)PROF_HISTORY);
    if (bw < 1) bw = 1;
    u32 shown = (u32)(f.w / bw);
    if (shown > p.hist_count) shown = p.hist_count;
    f.y += ui_px(4);
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
    ui_round(f.x, top_y, f.w, h, ui_col::SIDE, ui_px(3));
    // 60 к/с — отметка, если влезает
    if (scale > 16.7f) ui_hline(f.x, top_y + h - h * 16.7f / scale, f.w, ui_col::LINE);
    // Кадр вдвое длиннее среднего — акцентом
    f32 avg = (f32)p.total_avg_last;
    for (u32 k = 0; k < shown; ++k) {
        f32 v = p.hist[(p.hist_pos + PROF_HISTORY - 1 - k) % PROF_HISTORY];
        f32 bh = h * (v < scale ? v : scale) / scale;
        if (bh < hair) bh = hair;
        f32 x = f.x + f.w - (f32)(k + 1) * bw;
        ui_rect(x, top_y + h - bh, bw, bh, avg > 0 && v > 2 * avg ? ui_col::ON : ui_col::DIM);
    }
    f.y = top_y + h + ui_px(6);
}

static void metrics_body(UiForm &f, const DebugInfo &in, DebugState &d) {
    char t[160], note[64];
    // Кадр. Замер с отладочным режимом нельзя сравнивать с бюджетом (docs/budget.md) — первой строкой
    snprintf(note, sizeof note, "%.0f к/с · %.2f мс", in.fps, in.frame_ms);
    if (fold(f, d, MF_FRAME, "КАДР", note) || ui_capturing()) {
        if (in.unfair && in.unfair[0]) ui_value(f, "! не для замера", in.unfair, ui_col::WARN);
        else ui_value(f, "замер", "честный", ui_col::GOOD);
        snprintf(t, sizeof t, "%.0f · %.2f мс", in.fps, in.frame_ms);
        ui_value(f, "к/с · кадр", t);
        snprintf(t, sizeof t, "%.2f мс", in.frame_max_ms);
        ui_value(f, "худший кадр", t);
        snprintf(t, sizeof t, "%.2f мс", in.cpu_ms);
        ui_value(f, "CPU кадра", t);
        snprintf(t, sizeof t, "%.2f · %.2f мс", in.gpu_ms, in.gpu_max_ms);
        ui_value(f, "GPU · худший", t);
        snprintf(t, sizeof t, "%.3f мс", in.step_ms);
        ui_value(f, "шаг мира", t);
        snprintf(t, sizeof t, "%u", in.vis_clusters);
        ui_value(f, "видимых кластеров", t);
        ui_value(f, "видеокарта", in.gpu_name ? in.gpu_name : "—");
    }
    // График — только на экране, в снятый текст не идёт
    if (in.prof && fold(f, d, MF_GRAPH, "ГРАФИК КАДРОВ", nullptr, false) && !ui_capturing()) frame_graph(f, *in.prof);

    // Кадр по частям (docs/frame_time.md): среднее, наибольшее за секунду и доля в худшем кадре секунды
    if (in.prof && (fold(f, d, MF_PARTS, "ЧАСТИ КАДРА, МС") || ui_capturing())) {
        const FrameProf &p = *in.prof;
        ui_value(f, "", "сред · макс · худш", ui_col::FAINT);
        for (u32 i = 0; i < p.nparts && i < DEBUG_MAX_FRAME_PARTS; ++i) {
            snprintf(t, sizeof t, "%.2f · %.2f · %.2f", p.avg_last[i], p.max_last[i], p.worst_last[i]);
            ui_value(f, p.names[i] ? p.names[i] : "?", t);
        }
        snprintf(t, sizeof t, "%.2f", p.worst_total_last);
        ui_value(f, "худший кадр целиком", t);
        ui_value(f, "", "ожидание, картинка, показ — CPU ждёт", ui_col::FAINT);
    }

    // Проходы кадра на GPU по меткам времени (docs/frame.md, «Замер»)
    if (in.passes) {
        snprintf(note, sizeof note, "%u", in.passes);
        if (fold(f, d, MF_PASSES, "ПРОХОДЫ GPU, МС", note) || ui_capturing()) {
            for (u32 i = 0; i < in.passes && i < DEBUG_MAX_PASSES; ++i) {
                snprintf(t, sizeof t, "%.2f", in.pass_ms[i]);
                ui_value(f, in.pass_names[i] ? in.pass_names[i] : "?", t);
            }
        }
    }

    // Работа освещения (docs/tiles.md): сколько чего посчитано, а не за сколько. Имена строк не менять без нужды:
    // по ним сверяют замеры
    if (fold(f, d, MF_WORK, "ОСВЕЩЕНИЕ") || ui_capturing()) {
        if (!in.work) {
            ui_value(f, "работа освещения", "ещё не прочитана", ui_col::FAINT);
        } else {
            pair_row(f, "плитки неба · земли", in.work_tiles[0], in.work_tiles[1]);
            pair_row(f, "плитки сеток · смеси", in.work_tiles[2], in.work_tiles[3]);
            dbg_count(in.work_tiles[4], t, sizeof t);
            ui_value(f, "плитки земли на пиксель", t);
            pair_row(f, "пиксели земли · сеток", in.work_px[1], in.work_px[2]);
            pair_row(f, "поверхности земли · сеток", in.work_surf[0], in.work_surf[1]);
            dbg_count(in.work_shadow, t, sizeof t);
            ui_value(f, "выборки тени земли", t);
            // Кеш земли (docs/gcache.md): страниц в кадре и запечено за кадр; поверхности земли мимо кеша
            if (in.gc_off) {
                snprintf(t, sizeof t, "выкл: %s", in.gc_off);
                ui_value(f, "! кеш земли", t, ui_col::WARN);  // вся земля — на пиксель, кадр медленнее
            } else {
                pair_row(f, "кеш земли: страниц · запечено", in.work_gc[0], in.work_gc[1]);
            }
            pair_row(f, "земля мимо кеша · без места", in.work_gc[3], in.work_gc[2]);
            // Тени солнца страницами (docs/shadow.md): без места и без страницы — оба должны быть 0
            pair_row(f, "тени: страниц · нарисовано", in.work_sm[0], in.work_sm[1]);
            pair_row(f, "тени: без места · без страницы", in.work_sm[2], in.work_sm[3]);
            pair_row(f, "тени: вызовов · выборок", in.work_sm[4], in.work_sm[5]);
            // Шаг светила (6б-2): страниц нового набора, отложенных стройкой в фоне, и сколько света уже на нём
            snprintf(t, sizeof t, "%u · %u %%", in.work_sm[6], in.work_sm[7]);
            ui_value(f, "тени: стройка шага · перетекание", t);
            // Поле расстояний мира (docs/sdf.md): выборки светом; засыпка и сборка — только когда кирпичи грязные
            pair_row(f, "поле: выборок · кирпичей", in.work_sdf[0], in.work_sdf[2]);
            snprintf(t, sizeof t, "%u", in.work_sdf[1]);
            ui_value(f, "поле: треугольников засыпки", t);
            // Зонды непрямого света (docs/gi.md): посчитаны в кадре и их лучи; выборки поля лучами
            pair_row(f, "зонды: посчитано · лучей", in.work_gi[0], in.work_gi[1]);
            snprintf(t, sizeof t, "%u", in.work_gi[2]);
            ui_value(f, "зонды: выборок поля", t);
            // Ячейки света (docs/lcache.md): живые и новые в кадре; посчитанные и их лучи; без места и отложенные
            pair_row(f, "ячейки: живых · новых", in.work_lc[0], in.work_lc[1]);
            pair_row(f, "ячейки: посчитано · лучей", in.work_lc[2], in.work_lc[3]);
            pair_row(f, "ячейки: без места · ждут", in.work_lc[4], in.work_lc[5]);
            // Тени огней картами (docs/lamp_shadows.md): грани рисуются, только когда огонь получил место
            pair_row(f, "огни: граней · вызовов в них", in.work_lsm[0], in.work_lsm[1]);
            snprintf(t, sizeof t, "%u", in.work_lsm[2]);
            ui_value(f, "огни: выборок карт", t);
            // Воздух (docs/air.md): клетки с пылью; шаги лучей солнца и их выборки страниц теней
            dbg_count(in.work_air[0], t, sizeof t);
            ui_value(f, "воздух: клеток", t);
            pair_row(f, "воздух: шагов · выборок", in.work_air[1], in.work_air[2]);
            dbg_count(in.work_skin, t, sizeof t);
            ui_value(f, "скинено вершин", t);
        }
    }

    // Переполнения (docs/graph.md «Переполнения»): что-то не нарисовано или считается медленным путём
    if (in.overflows) {
        snprintf(note, sizeof note, "%u", in.overflows);
        if (fold(f, d, MF_OVER, "ПЕРЕПОЛНЕНИЯ", note) || ui_capturing()) {
            for (u32 i = 0; i < in.overflows && i < DEBUG_MAX_OVERFLOWS; ++i) {
                if (in.over_cap[i])
                    snprintf(t, sizeof t, "%llu из %llu", (unsigned long long)in.over_want[i],
                             (unsigned long long)in.over_cap[i]);
                else snprintf(t, sizeof t, "%llu", (unsigned long long)in.over_want[i]);
                char name[64];
                snprintf(name, sizeof name, "! %s", in.over_what[i] ? in.over_what[i] : "?");
                ui_value(f, name, t, ui_col::WARN);
            }
        }
    }

    // Глаз (docs/light.md, «Единицы», «Экспозиция»): освещённость земли от светила и неба, у глаза, EV100
    if (in.light) {
        snprintf(note, sizeof note, "EV100 %.2f", (double)in.ev100);
        if (fold(f, d, MF_LIGHT, "СВЕТ", note) || ui_capturing()) {
            snprintf(t, sizeof t, "%.4g · %.4g лк", (double)in.lux_key, (double)in.lux_sky);
            ui_value(f, "светило · небо", t);
            snprintf(t, sizeof t, "%.4g лк", (double)in.lux_eye);
            ui_value(f, "замер глаза", t);
            snprintf(t, sizeof t, "%.4g · %.4g лк", (double)in.lux_cam, (double)in.lux_view);
            ui_value(f, "у камеры · в кадре", t);
            snprintf(t, sizeof t, "%.2f → %.2f", (double)in.ev100, (double)in.ev100_goal);
            ui_value(f, "EV100 глаза → цель", t);
            snprintf(t, sizeof t, "%.2f %.2f %.2f · %.0f%%", (double)in.wb_white[0], (double)in.wb_white[1],
                     (double)in.wb_white[2], (double)(in.wb_degree * 100.0f));
            ui_value(f, "цвет света · привыкание", t);
        }
    }

    // Мир: шаг и хеш (повтор), время, камера
    char hs[16];
    dbg_hm(in.hour, hs, sizeof hs);
    snprintf(note, sizeof note, "%s · шаг %llu", hs, (unsigned long long)in.step);
    if (fold(f, d, MF_WORLD, "МИР", note) || ui_capturing()) {
        snprintf(t, sizeof t, "%llu", (unsigned long long)in.step);
        ui_value(f, "шаг", t);
        snprintf(t, sizeof t, "%016llx", (unsigned long long)in.hash);
        ui_value(f, "хеш", t);
        f32 sp = in.speeds && in.speed < in.nspeeds ? in.speeds[in.speed] : 1.0f;
        snprintf(t, sizeof t, "×%g%s", (double)sp, in.paused ? " · пауза" : "");
        ui_value(f, "скорость мира", t);
        snprintf(t, sizeof t, "%s · %s", hs, dbg_day_speeds()[in.clock_speed < 4 ? in.clock_speed : 0]);
        ui_value(f, "час · ход суток", t);
        snprintf(t, sizeof t, "%.0f  %.0f  %.0f", in.cam[0], in.cam[1], in.cam[2]);
        ui_value(f, "камера x  y  z, м", t);
        snprintf(t, sizeof t, "%.1f м%s", in.cam_above, in.cam_free ? " · полёт" : "");
        ui_value(f, "над землёй", t);
    }
    ui_fold_end(f);
}

static UiScroll g_metrics_scroll;
static char g_copy_buf[16384];

void debug_metrics_panel(const DebugInfo &in, DebugState &d, f32 x, f32 y, f32 w, f32 max_h) {
    f32 pad = ui_px(16);
    u32 bg = ui_panel_reserve();
    UiForm f = ui_form(x + pad, y + pad, w - 2 * pad);
    f.font = UI_DATA;
    // Заголовок: имя окна и «Скопировать всё» (после нажатия — «скопировано» на две секунды)
    f32 bh = ui_line(UI_SMALL) + ui_px(12);
    ui_text(f.x, f.y + floorf((bh - ui_line(UI_STRONG)) * 0.5f), UI_STRONG, ui_col::TEXT, "Показатели");
    bool fresh = d.copied_ns && platform_time_ns() - d.copied_ns < 2000000000ull;
    const char *bl = fresh ? "Скопировано ✓" : "Скопировать всё";
    f32 bw = ui_text_width(UI_TEXT, "Скопировать всё") + ui_px(24);
    bool copy = ui_button(f.x + f.w - bw, f.y, bw, bh, bl, fresh);
    f.y += bh + ui_px(4);

    // Снятие текста — тем же проходом, что рисует: строки окна и буфер не расходятся
    if (copy) {
        ui_capture_begin(g_copy_buf, sizeof g_copy_buf);
        ui_capture_line("Anathemyre — показатели (F3)");
    }
    f32 top = f.y, room = max_h - (top - y) - pad;
    f.y = ui_scroll_begin(g_metrics_scroll, x, top, w - pad + ui_px(10), room);
    metrics_body(f, in, d);
    f32 shown = ui_scroll_end(g_metrics_scroll, f.y + ui_px(4));
    if (copy) {
        ui_capture_end();
        if (platform_clipboard_set(g_copy_buf)) d.copied_ns = platform_time_ns();
    }
    f32 ph = top + shown + pad - y;
    ui_panel_at(bg, x, y, w, ph);
    ui_block(x, y, w, ph);
}
