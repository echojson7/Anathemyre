// Linux: звук через ALSA. libasound подгружается через dlopen, заголовков нет.
// В WSLg и PipeWire «default» ведёт в их сервер через плагин ALSA
#include "platform.h"

#include <dlfcn.h>
#include <stdatomic.h>
#include <string.h>

#include "../core/log.h"

#define AUDIO_BLOCK 512             // кадров за запрос к микшеру, ~10.7 мс при 48 кГц
#define AUDIO_LATENCY_US 50000u     // буфер устройства

// Нужное из asoundlib.h
typedef struct snd_pcm snd_pcm_t;
enum { SND_PCM_STREAM_PLAYBACK = 0, SND_PCM_FORMAT_S16_LE = 2, SND_PCM_ACCESS_RW_INTERLEAVED = 3 };

typedef int (*PFN_snd_pcm_open)(snd_pcm_t **, const char *, int, int);
typedef int (*PFN_snd_pcm_set_params)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
typedef long (*PFN_snd_pcm_writei)(snd_pcm_t *, const void *, unsigned long);
typedef int (*PFN_snd_pcm_recover)(snd_pcm_t *, int, int);
typedef int (*PFN_snd_pcm_close)(snd_pcm_t *);
typedef int (*PFN_snd_pcm_drop)(snd_pcm_t *);
typedef int (*PFN_snd_config_free)(void);

static struct {
    void *lib;
    snd_pcm_t *pcm;
    PFN_snd_pcm_writei writei;
    PFN_snd_pcm_recover recover;
    PFN_snd_pcm_close close;
    PFN_snd_pcm_drop drop;
    PlatformAudioFn fn;
    void *user;
    PlatformThread *thread;
    atomic_bool stop;
} g_a;

// dlsym отдаёт void*, к указателю на функцию — через memcpy
static void *sym(const char *name, void *out, size_t size)
{
    void *p = dlsym(g_a.lib, name);
    if (p) memcpy(out, &p, size);
    return p;
}
#define SYM(name, var) sym(name, &(var), sizeof(var))

static void audio_thread(void *arg)
{
    (void)arg;
    static f32 mix[AUDIO_BLOCK * 2];
    static i16 pcm[AUDIO_BLOCK * 2];
    while (!atomic_load(&g_a.stop)) {
        g_a.fn(mix, AUDIO_BLOCK, g_a.user);
        for (u32 i = 0; i < AUDIO_BLOCK * 2; i++) {
            f32 v = mix[i] < -1.0f ? -1.0f : mix[i] > 1.0f ? 1.0f : mix[i];
            pcm[i] = (i16)(v * 32767.0f);
        }
        const i16 *p = pcm;
        unsigned long left = AUDIO_BLOCK;
        while (left && !atomic_load(&g_a.stop)) {
            long n = g_a.writei(g_a.pcm, p, left);
            if (n < 0) {
                // Опустошение буфера или пауза системы: поднять и писать дальше
                if (g_a.recover(g_a.pcm, (int)n, 1) < 0) return;
                continue;
            }
            left -= (unsigned long)n;
            p += n * 2;
        }
    }
}

bool platform_audio_start(u32 rate, PlatformAudioFn fn, void *user)
{
    if (g_a.thread) return true;
    g_a.lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!g_a.lib) {
        log_info("ЗВУК: libasound.so.2 нет, тишина");
        return false;
    }
    PFN_snd_pcm_open open_ = NULL;
    PFN_snd_pcm_set_params set_params = NULL;
    if (!SYM("snd_pcm_open", open_) || !SYM("snd_pcm_set_params", set_params) || !SYM("snd_pcm_writei", g_a.writei) ||
        !SYM("snd_pcm_recover", g_a.recover) || !SYM("snd_pcm_close", g_a.close) || !SYM("snd_pcm_drop", g_a.drop)) {
        log_info("ЗВУК: в libasound нет нужных функций, тишина");
        dlclose(g_a.lib);
        g_a.lib = NULL;
        return false;
    }
    if (open_(&g_a.pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0 ||
        set_params(g_a.pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, rate, 1, AUDIO_LATENCY_US) < 0) {
        log_info("ЗВУК: устройство не открылось, тишина");
        if (g_a.pcm) g_a.close(g_a.pcm);
        g_a.pcm = NULL;
        // Разобранный конфиг ALSA кэшируется глобально: освободить, иначе он висит до выхода
        PFN_snd_config_free free_cfg = NULL;
        if (SYM("snd_config_update_free_global", free_cfg)) free_cfg();
        dlclose(g_a.lib);
        g_a.lib = NULL;
        return false;
    }
    g_a.fn = fn;
    g_a.user = user;
    atomic_store(&g_a.stop, false);
    g_a.thread = platform_thread_start(audio_thread, NULL);
    if (!g_a.thread) {
        platform_audio_stop();
        return false;
    }
    log_info("ЗВУК: ALSA, %u ГЦ", rate);
    return true;
}

void platform_audio_stop(void)
{
    // Поток допишет текущий блок и выйдет; ALSA из двух потоков сразу не трогаем
    atomic_store(&g_a.stop, true);
    platform_thread_join(g_a.thread);
    g_a.thread = NULL;
    if (g_a.pcm) {
        g_a.drop(g_a.pcm);
        g_a.close(g_a.pcm);
    }
    g_a.pcm = NULL;
    if (g_a.lib) {
        PFN_snd_config_free free_cfg = NULL;
        if (SYM("snd_config_update_free_global", free_cfg)) free_cfg();
        dlclose(g_a.lib);
    }
    g_a.lib = NULL;
}
