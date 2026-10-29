// Windows: звук через waveOut. winmm.dll подгружается через LoadLibrary, в импорте exe её нет
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.h"

#include <windows.h>
#include <mmsystem.h>
#include <stdatomic.h>
#include <string.h>

#include "../core/log.h"

#define AUDIO_BLOCK 512   // кадров в буфере, ~10.7 мс при 48 кГц
#define AUDIO_BUFS 5      // в очереди устройства, ~53 мс

typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *PFN_waveOutHdr)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *PFN_waveOutDev)(HWAVEOUT);

static struct {
    HMODULE lib;
    HWAVEOUT dev;
    HANDLE done;  // устройство вернуло буфер
    PFN_waveOutHdr prepare, unprepare, write;
    PFN_waveOutDev reset, close;
    WAVEHDR hdr[AUDIO_BUFS];
    i16 pcm[AUDIO_BUFS][AUDIO_BLOCK * 2];
    f32 mix[AUDIO_BLOCK * 2];
    PlatformAudioFn fn;
    void *user;
    PlatformThread *thread;
    atomic_bool stop;
} g_a;

// GetProcAddress отдаёт FARPROC; приводим через memcpy
static bool sym(const char *name, void *out, size_t size)
{
    FARPROC p = GetProcAddress(g_a.lib, name);
    if (p) memcpy(out, &p, size);
    return p != NULL;
}
#define SYM(name, var) sym(name, &(var), sizeof(var))

static void fill(WAVEHDR *h, i16 *pcm)
{
    g_a.fn(g_a.mix, AUDIO_BLOCK, g_a.user);
    for (u32 i = 0; i < AUDIO_BLOCK * 2; i++) {
        f32 v = g_a.mix[i] < -1.0f ? -1.0f : g_a.mix[i] > 1.0f ? 1.0f : g_a.mix[i];
        pcm[i] = (i16)(v * 32767.0f);
    }
    h->dwFlags &= ~(DWORD)WHDR_DONE;
    g_a.write(g_a.dev, h, sizeof *h);
}

static void audio_thread(void *arg)
{
    (void)arg;
    // Микшер короткий, но опаздывать ему нельзя: щелчки слышнее просадки кадра
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (u32 i = 0; i < AUDIO_BUFS; i++) fill(&g_a.hdr[i], g_a.pcm[i]);
    while (!atomic_load(&g_a.stop)) {
        WaitForSingleObject(g_a.done, 100);
        for (u32 i = 0; i < AUDIO_BUFS && !atomic_load(&g_a.stop); i++)
            if (g_a.hdr[i].dwFlags & WHDR_DONE) fill(&g_a.hdr[i], g_a.pcm[i]);
    }
}

bool platform_audio_start(u32 rate, PlatformAudioFn fn, void *user)
{
    if (g_a.thread) return true;
    g_a.lib = LoadLibraryA("winmm.dll");
    PFN_waveOutOpen open_ = NULL;
    if (!g_a.lib || !SYM("waveOutOpen", open_) || !SYM("waveOutPrepareHeader", g_a.prepare) ||
        !SYM("waveOutUnprepareHeader", g_a.unprepare) || !SYM("waveOutWrite", g_a.write) || !SYM("waveOutReset", g_a.reset) ||
        !SYM("waveOutClose", g_a.close)) {
        log_info("ЗВУК: winmm.dll не загружена, тишина");
        if (g_a.lib) FreeLibrary(g_a.lib);
        g_a.lib = NULL;
        return false;
    }
    WAVEFORMATEX wf = { 0 };
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = rate * 4;
    g_a.done = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!g_a.done || open_(&g_a.dev, WAVE_MAPPER, &wf, (DWORD_PTR)g_a.done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        log_info("ЗВУК: устройство не открылось, тишина");
        if (g_a.done) CloseHandle(g_a.done);
        g_a.done = NULL;
        FreeLibrary(g_a.lib);
        g_a.lib = NULL;
        return false;
    }
    for (u32 i = 0; i < AUDIO_BUFS; i++) {
        WAVEHDR *h = &g_a.hdr[i];
        memset(h, 0, sizeof *h);
        h->lpData = (LPSTR)g_a.pcm[i];
        h->dwBufferLength = sizeof g_a.pcm[i];
        g_a.prepare(g_a.dev, h, sizeof *h);
    }
    g_a.fn = fn;
    g_a.user = user;
    atomic_store(&g_a.stop, false);
    g_a.thread = platform_thread_start(audio_thread, NULL);
    if (!g_a.thread) {
        platform_audio_stop();
        return false;
    }
    log_info("ЗВУК: waveOut, %u ГЦ", rate);
    return true;
}

void platform_audio_stop(void)
{
    atomic_store(&g_a.stop, true);
    if (g_a.done) SetEvent(g_a.done);
    platform_thread_join(g_a.thread);
    g_a.thread = NULL;
    if (g_a.dev) {
        g_a.reset(g_a.dev);
        for (u32 i = 0; i < AUDIO_BUFS; i++) g_a.unprepare(g_a.dev, &g_a.hdr[i], sizeof g_a.hdr[i]);
        g_a.close(g_a.dev);
        g_a.dev = NULL;
    }
    if (g_a.done) CloseHandle(g_a.done);
    g_a.done = NULL;
    if (g_a.lib) FreeLibrary(g_a.lib);
    g_a.lib = NULL;
}
