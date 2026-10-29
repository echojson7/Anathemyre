#include "jobs.h"

#include <stdatomic.h>

#include "../platform/platform.h"

static struct {
    u32             threads;
    PlatformThread *workers[JOBS_MAX_THREADS];
    PlatformSema   *wake;
    atomic_uint     next;     // следующий свободный номер
    atomic_uint     busy;     // рабочие, ещё не закончившие текущую задачу
    atomic_bool     quit;
    JobFn           fn;
    void           *ctx;
    u32             count, grain;
} g_jobs = { .threads = 1 };

static void run_chunks(void)
{
    for (;;) {
        u32 b = atomic_fetch_add_explicit(&g_jobs.next, g_jobs.grain, memory_order_relaxed);
        if (b >= g_jobs.count) break;
        u32 e = b + g_jobs.grain < g_jobs.count ? b + g_jobs.grain : g_jobs.count;
        g_jobs.fn(g_jobs.ctx, b, e);
    }
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        platform_sema_wait(g_jobs.wake);
        if (atomic_load_explicit(&g_jobs.quit, memory_order_acquire)) break;
        run_chunks();
        atomic_fetch_sub_explicit(&g_jobs.busy, 1, memory_order_release);
    }
}

bool jobs_init(u32 threads)
{
    if (!threads) threads = platform_cpu_count();
    if (threads > JOBS_MAX_THREADS) threads = JOBS_MAX_THREADS;
    g_jobs.threads = 1;
    atomic_store(&g_jobs.quit, false);
    if (threads == 1) return true;
    g_jobs.wake = platform_sema_create();
    if (!g_jobs.wake) return false;
    for (u32 k = 1; k < threads; k++) {
        g_jobs.workers[k] = platform_thread_start(worker, NULL);
        if (!g_jobs.workers[k]) {
            jobs_shutdown();
            return false;
        }
        g_jobs.threads = k + 1;
    }
    return true;
}

void jobs_shutdown(void)
{
    if (g_jobs.threads > 1) {
        atomic_store_explicit(&g_jobs.quit, true, memory_order_release);
        platform_sema_post(g_jobs.wake, g_jobs.threads - 1);
        for (u32 k = 1; k < g_jobs.threads; k++) platform_thread_join(g_jobs.workers[k]);
    }
    if (g_jobs.wake) platform_sema_destroy(g_jobs.wake);
    g_jobs.wake = NULL;
    g_jobs.threads = 1;
}

u32 jobs_threads(void) { return g_jobs.threads; }

void jobs_for(u32 count, u32 grain, JobFn fn, void *ctx)
{
    if (!grain) grain = 1;
    if (g_jobs.threads == 1 || count <= grain) {
        if (count) fn(ctx, 0, count);
        return;
    }
    // Будить не больше рабочих, чем есть кусков
    u32 chunks = (count + grain - 1) / grain;
    u32 helpers = chunks - 1 < g_jobs.threads - 1 ? chunks - 1 : g_jobs.threads - 1;
    g_jobs.fn = fn;
    g_jobs.ctx = ctx;
    g_jobs.count = count;
    g_jobs.grain = grain;
    atomic_store_explicit(&g_jobs.next, 0, memory_order_relaxed);
    atomic_store_explicit(&g_jobs.busy, helpers, memory_order_release);
    platform_sema_post(g_jobs.wake, helpers);
    run_chunks();
    while (atomic_load_explicit(&g_jobs.busy, memory_order_acquire) != 0) platform_cpu_relax();
}
