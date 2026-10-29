// Банк звуков: каждый звук — несколько вариантов, синтезированных при старте из шума,
// фильтров и затухающих мод (металл). Свой seed: банк одинаков при каждом запуске
#ifndef ANA_SYNTH_H
#define ANA_SYNTH_H

#include "audio.h"

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

// Затухающие хвосты фильтров уходят в денормальные числа, а они в десятки раз медленнее:
// сбрасывать их в ноль. Регистр у каждого потока свой; поток игры возвращает прежний —
// бой и мир считаются по обычным правилам
#if defined(__SSE__) || defined(__x86_64__)
#define AUDIO_CSR_GET() _mm_getcsr()
#define AUDIO_CSR_SET(v) _mm_setcsr(v)
#define AUDIO_FLUSH_DENORMALS() _mm_setcsr(_mm_getcsr() | 0x8040u)
#else
#define AUDIO_CSR_GET() 0u
#define AUDIO_CSR_SET(v) ((void)(v))
#define AUDIO_FLUSH_DENORMALS() ((void)0)
#endif

#define SFX_VARIANTS 6

typedef struct SfxBank {
    const i16 *data[SFX_COUNT][SFX_VARIANTS];
    u32 len[SFX_COUNT][SFX_VARIANTS];
    u8  count[SFX_COUNT];
    size_t bytes;
} SfxBank;

bool synth_build(SfxBank *b, Arena *perm);

#endif
