#include "bench.h"

#include <stdio.h>
#include <stdlib.h>

#include "../core/jobs.h"
#include "../core/log.h"
#include "../render/render.h"

void bench_init(Bench *b, Arena *perm)
{
    *b = (Bench){ .frame_ms = ARENA_PUSH_ARRAY(perm, f32, BENCH_MAX_FRAMES) };
}

void bench_begin(Bench *b, f64 now, f64 warmup_s, f64 measure_s)
{
    b->on = true;
    b->start = now;
    b->measure_from = now + warmup_s;
    b->measure_s = measure_s;
    b->last_now = 0;
    b->frames = b->cpu_frames = 0;
    b->posed = 0;
    log_stat("ЗАМЕР: начат, %.1f с прогрева и %.0f с замера; камера у арены, управление камерой отключено", warmup_s, measure_s);
}

static int cmp_f32(const void *a, const void *b)
{
    f32 x = *(const f32 *)a, y = *(const f32 *)b;
    return (x > y) - (x < y);
}

// Время проходов GPU одной строкой; драйвер может не отдавать метки времени (например, D3D12 в WSL)
static void gpu_line(char *out, size_t size, const RenderStats *st)
{
    if (st->samples && st->gpu_total_ms > 0.0)
        snprintf(out, size, "GPU %.3f: тени %.3f, глубина %.3f, сцена %.3f, атмосфера %.3f, стиль %.3f, интерфейс %.3f | между кадрами %.3f",
                 st->gpu_total_ms, st->gpu_ms[PASS_SHADOW], st->gpu_ms[PASS_DEPTH], st->gpu_ms[PASS_SCENE],
                 st->gpu_ms[PASS_ATMOS], st->gpu_ms[PASS_STYLE], st->gpu_ms[PASS_UI], st->gap_ms);
    else
        snprintf(out, size, "GPU: драйвер не отдаёт метки времени");
}

// CPU одной строкой: логика (сим — ввод и мир, позы — люди и список кадра), отправка (сбор и вызовы GL по проходам), swap; мс на кадр
static void cpu_line(char *out, size_t size, const f64 *z, f64 k, const RenderStats *st, bool swap_late)
{
    f64 sim = (z[PZ_INPUT] + z[PZ_PEOPLE] + z[PZ_ARENA] + z[PZ_FIELD] + z[PZ_SOUND]) * k;
    f64 pose = (z[PZ_FIGURES] + z[PZ_POSES] + z[PZ_SCENE]) * k;
    const f64 *c = st->cpu_ms;
    snprintf(out, size,
             "логика %.3f (сим %.3f, позы %.3f: %.0f шт.), отправка %.3f (сбор %.3f; тени %.3f, глубина %.3f, сцена %.3f, "
             "атмосфера %.3f, стиль %.3f, интерфейс %.3f), swap %.3f%s",
             sim + pose, sim, pose, st->poses, (z[PZ_SUBMIT] + z[PZ_UI]) * k, st->prep_ms, c[PASS_SHADOW], c[PASS_DEPTH],
             c[PASS_SCENE], c[PASS_ATMOS], c[PASS_STYLE], c[PASS_UI], z[PZ_SWAP] * k, swap_late ? " после логики" : "");
}

// Все зоны подряд, мс на кадр
static void zones_line(char *out, size_t size, const f64 *z, f64 k, f64 posed)
{
    size_t n = (size_t)snprintf(out, size, "зоны, мс:");
    for (u32 i = 0; i < PZ_COUNT && n < size; i++) {
        char extra[48] = "";
        if (i == PZ_POSES) snprintf(extra, sizeof extra, " (%.0f шт., потоков %u)", posed, jobs_threads());
        n += (size_t)snprintf(out + n, size - n, "%s %s %.3f%s", i ? "," : "", g_prof_name[i], z[i] * k, extra);
    }
}

static void report(Bench *b, f64 seconds, i32 ww, i32 wh, bool swap_late)
{
    RenderStats st;
    render_take_stats(&st);
    f64 z[PZ_COUNT];
    prof_take(z);
    u32 n = b->frames;
    if (!n || !b->cpu_frames) return;
    f64 sum = 0;
    for (u32 i = 0; i < n; i++) sum += b->frame_ms[i];
    qsort(b->frame_ms, n, sizeof *b->frame_ms, cmp_f32);
    f64 k = 1000.0 / b->cpu_frames;
    b->last = (BenchResult){
        .frame_ms = sum / n, .fps = n / seconds, .gpu_ms = st.samples && st.gpu_total_ms > 0.0 ? st.gpu_total_ms : -1.0,
        .logic_ms = (z[PZ_INPUT] + z[PZ_PEOPLE] + z[PZ_ARENA] + z[PZ_FIELD] + z[PZ_SOUND] + z[PZ_FIGURES] + z[PZ_POSES] + z[PZ_SCENE]) * k,
        .submit_ms = (z[PZ_SUBMIT] + z[PZ_UI]) * k, .swap_ms = z[PZ_SWAP] * k, .gap_ms = st.gap_ms,
    };
    for (u32 p = 0; p < PASS_COUNT; p++) {
        b->last.pass_ms[p] = st.gpu_ms[p];
        b->last.cpu_ms[p] = st.cpu_ms[p];
    }
    b->last.prep_ms = st.prep_ms;
    log_stat("ЗАМЕР: окно %dx%d, %u кадров за %.1f с | кадр: среднее %.3f мс (%.0f к/с), медиана %.3f, худший 1%% %.3f",
             ww, wh, n, seconds, sum / n, n / seconds, b->frame_ms[n / 2], b->frame_ms[MIN(n - 1, n * 99 / 100)]);
    char gpu[256], cpu[512], zones[512];
    gpu_line(gpu, sizeof gpu, &st);
    cpu_line(cpu, sizeof cpu, z, k, &st, swap_late);
    zones_line(zones, sizeof zones, z, k, b->posed / b->cpu_frames);
    log_stat("ЗАМЕР: CPU %s | %s", cpu, gpu);
    log_stat("ЗАМЕР: CPU %s", zones);
    log_stat("ЗАМЕР: сцена: видно %.0f, вызовов %.0f, треугольников %.0fk | тени: видно %.0f, вызовов %.0f, треугольников %.0fk | шейдинг блоками: %s",
             st.visible[1], st.draws[1], st.tris[1] / 1000.0, st.visible[0], st.draws[0], st.tris[0] / 1000.0,
             st.vrs ? "да" : "нет");
}

bool bench_frame(Bench *b, f64 now, u32 posed, i32 ww, i32 wh, bool swap_late)
{
    // Время кадра — от начала прошлого кадра до начала этого; счётчики — с начала замера
    if (b->last_now == 0) {
        render_take_stats(&(RenderStats){ 0 });
        f64 z[PZ_COUNT];
        prof_take(z);
    } else {
        if (b->frames < BENCH_MAX_FRAMES) b->frame_ms[b->frames++] = (f32)((now - b->last_now) * 1000.0);
        b->cpu_frames++;
        b->posed += posed;
    }
    b->last_now = now;
    if (now - b->measure_from < b->measure_s) return false;
    report(b, now - b->measure_from, ww, wh, swap_late);
    b->on = false;
    return true;
}

// Имя этапа, добитое пробелами до 12 букв (printf считает байты, а буква кириллицы — два)
static void stage_name(char *out, size_t size, const char *name)
{
    u32 len = 0;
    for (const char *c = name; *c; c++) len += ((u8)*c & 0xC0) != 0x80;
    snprintf(out, size, "%s%*s", name, (int)(len < 12 ? 12 - len : 0), "");
}

void bench_stages_report(const BenchResult *r, u32 n, const char *const *names)
{
    char name[64];
    log_stat("ЭТАПЫ: этап | кадр мс | к/с | +к кадру | GPU мс | логика | отправка | swap");
    for (u32 i = 0; i < n; i++) {
        char gpu[16] = "—";
        if (r[i].gpu_ms >= 0.0) snprintf(gpu, sizeof gpu, "%.3f", r[i].gpu_ms);
        stage_name(name, sizeof name, names[i]);
        log_stat("ЭТАПЫ: %2u %s | %.3f | %5.0f | %+.3f | %s | %.3f | %.3f | %.3f", i, name, r[i].frame_ms, r[i].fps,
                 i ? r[i].frame_ms - r[i - 1].frame_ms : 0.0, gpu, r[i].logic_ms, r[i].submit_ms, r[i].swap_ms);
    }
    // CPU по проходам: сбор и отправка вызовов GL каждого прохода
    log_stat("ЭТАПЫ CPU, мс: этап | сбор | тени | глубина | сцена | атмосфера | стиль | интерфейс");
    for (u32 i = 0; i < n; i++) {
        const f64 *c = r[i].cpu_ms;
        stage_name(name, sizeof name, names[i]);
        log_stat("ЭТАПЫ CPU: %2u %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f", i, name, r[i].prep_ms, c[PASS_SHADOW],
                 c[PASS_DEPTH], c[PASS_SCENE], c[PASS_ATMOS], c[PASS_STYLE], c[PASS_UI]);
    }
    // GPU по проходам: где именно прибавилось на каждом этапе
    if (r[n - 1].gpu_ms < 0.0) return;
    log_stat("ЭТАПЫ GPU, мс: этап | тени | глубина | сцена | атмосфера | стиль | интерфейс | между кадрами");
    for (u32 i = 0; i < n; i++) {
        const f64 *p = r[i].pass_ms;
        stage_name(name, sizeof name, names[i]);
        log_stat("ЭТАПЫ GPU: %2u %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f", i, name, p[PASS_SHADOW], p[PASS_DEPTH],
                 p[PASS_SCENE], p[PASS_ATMOS], p[PASS_STYLE], p[PASS_UI], r[i].gap_ms);
    }
}

void stats_reset(FrameStats *s, f64 now)
{
    s->t = now;
    s->frames = 0;
    s->posed = 0;
    f64 z[PZ_COUNT];
    prof_take(z);
}

void stats_frame(FrameStats *s, f64 now, u32 posed, bool swap_late)
{
    s->frames++;
    s->fps_frames++;
    s->posed += posed;
    if (now - s->fps_t >= 0.5) {
        s->fps = (f32)(s->fps_frames / (now - s->fps_t));
        s->fps_ms = (f32)((now - s->fps_t) * 1000.0 / s->fps_frames);
        s->fps_t = now;
        s->fps_frames = 0;
    }
    if (now - s->t < 2.0) return;
    RenderStats st;
    render_take_stats(&st);
    f64 z[PZ_COUNT];
    prof_take(z);
    f64 k = 1000.0 / s->frames;
    char gpu[256], cpu[512];
    gpu_line(gpu, sizeof gpu, &st);
    cpu_line(cpu, sizeof cpu, z, k, &st, swap_late);
    log_stat("КАДР: %.2f мс, %.0f к/с | CPU: %s | %s", (now - s->t) * k, s->frames / (now - s->t), cpu, gpu);
    log_stat("ОТРИСОВКА: элементов %u | сцена: видно %.0f, вызовов %.0f, треугольников %.0fk | тени: видно %.0f, вызовов %.0f, треугольников %.0fk",
             st.items, st.visible[1], st.draws[1], st.tris[1] / 1000.0, st.visible[0], st.draws[0], st.tris[0] / 1000.0);
    s->shown_ms = (f32)((now - s->t) * k);
    s->shown_fps = (f32)(s->frames / (now - s->t));
    s->t = now;
    s->frames = 0;
    s->posed = 0;
}
