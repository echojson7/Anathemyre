// Микшер. Поток игры кладёт звуки в короткую очередь под спин-замком; поток звука забирает
// её раз в запрос платформы и считает блоками по MIX_BLOCK кадров: громкость, панорама и глушение
// пересчитываются на блок и плавно ведутся внутри него. Память вся статическая, malloc нет
#include "audio.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "../core/clock.h"
#include "../core/log.h"
#include "../core/rng.h"
#include "../platform/platform.h"
#include "music.h"
#include "synth.h"

#define FS ((f32)AUDIO_RATE)
#define TAU (2.0f * PI_F)
#define MAX_VOICES 48
#define QUEUE_MAX 256
#define MIX_BLOCK 64
#define SOUND_SPEED 343.0f   // м/с: дальний удар доходит позже, чем виден
#define ECHO_MAX (AUDIO_RATE * 6 / 10)
#define BANK_BYTES MB(12)

// ref — ближе этого громкость полная, дальше падает как ref/d; maxd — дальше не слышно.
// gain — громкость на ref, rev — доля в эхо, jit — разброс высоты ±
typedef struct SfxDef {
    f32 ref, maxd, gain, rev, jit;
} SfxDef;

static const SfxDef g_def[SFX_COUNT] = {
    // Шаги еле слышны: на обычной дистанции камеры — шорох, вблизи — отчётливо
    [SFX_STEP_SAND]     = { 1.0f, 22.0f, 0.24f, 0.10f, 0.10f },
    [SFX_STEP_CONCRETE] = { 1.0f, 26.0f, 0.22f, 0.25f, 0.08f },
    [SFX_STEP_GRASS]    = { 1.0f, 18.0f, 0.20f, 0.05f, 0.10f },
    [SFX_STEP_WATER]    = { 1.0f, 24.0f, 0.30f, 0.08f, 0.12f },
    [SFX_SWISH]         = { 2.0f, 30.0f, 0.32f, 0.15f, 0.12f },
    [SFX_SWISH_HEAVY]   = { 2.0f, 36.0f, 0.42f, 0.15f, 0.10f },
    // Оружие о оружие слышно далеко: громче всего в бою
    [SFX_WPN_RESSORA]   = { 3.0f, 100.0f, 0.62f, 0.50f, 0.05f },
    [SFX_WPN_SEKACH]    = { 3.0f, 100.0f, 0.68f, 0.45f, 0.05f },
    [SFX_WPN_ARMATURA]  = { 3.0f, 90.0f, 0.66f, 0.40f, 0.06f },
    [SFX_WPN_BAGOR]     = { 3.0f, 70.0f, 0.60f, 0.35f, 0.06f },
    [SFX_WPN_HOOK]      = { 3.0f, 80.0f, 0.50f, 0.40f, 0.05f },
    [SFX_WPN_BALKA]     = { 3.0f, 120.0f, 0.80f, 0.55f, 0.04f },
    [SFX_SCRAPE]        = { 2.0f, 45.0f, 0.32f, 0.30f, 0.08f },
    [SFX_HIT_CUT]       = { 2.0f, 45.0f, 0.55f, 0.20f, 0.10f },
    [SFX_HIT_BLUNT]     = { 2.0f, 55.0f, 0.65f, 0.25f, 0.10f },
    [SFX_HIT_PIERCE]    = { 2.0f, 40.0f, 0.50f, 0.20f, 0.10f },
    [SFX_HIT_CHOP]      = { 2.0f, 50.0f, 0.62f, 0.22f, 0.10f },
    [SFX_HIT_CRUSH]     = { 2.5f, 70.0f, 0.78f, 0.30f, 0.08f },
    [SFX_HIT_ARMOR]     = { 3.0f, 80.0f, 0.60f, 0.45f, 0.08f },
    [SFX_CRACK]         = { 1.5f, 30.0f, 0.45f, 0.15f, 0.10f },
    [SFX_BODYFALL]      = { 2.0f, 55.0f, 0.60f, 0.30f, 0.10f },
    [SFX_DROP_METAL]    = { 2.0f, 55.0f, 0.45f, 0.40f, 0.08f },
    [SFX_DRAW]          = { 1.5f, 30.0f, 0.32f, 0.25f, 0.06f },
    [SFX_SHEATH]        = { 1.5f, 26.0f, 0.28f, 0.20f, 0.06f },
    [SFX_SCUFF]         = { 1.5f, 20.0f, 0.24f, 0.10f, 0.12f },
    [SFX_UI_TICK]       = { 1.0f, 1.0f, 0.10f, 0.00f, 0.03f },
    [SFX_UI_ORDER]      = { 1.0f, 1.0f, 0.12f, 0.00f, 0.02f },
    // Даль: слышно за километр, но тихо, глухо и с запозданием
    [SFX_FAR_GROAN]     = { 60.0f, 2500.0f, 0.30f, 0.90f, 0.12f },
    [SFX_FAR_BANG]      = { 60.0f, 2500.0f, 0.55f, 0.90f, 0.10f },
    [SFX_FAR_RADIO]     = { 30.0f, 2500.0f, 0.30f, 0.60f, 0.04f },
    // Обломки на ветру: где-то рядом, тихо
    [SFX_WIND_RATTLE]   = { 8.0f, 120.0f, 0.22f, 0.50f, 0.10f },
    [SFX_WIND_CREAK]    = { 8.0f, 120.0f, 0.16f, 0.50f, 0.12f },
    // Вещи падают и бьются: тише боя, громкость ещё от скорости удара (view/sfx.c)
    [SFX_IMPACT_METAL]  = { 2.0f, 55.0f, 0.42f, 0.35f, 0.10f },
    [SFX_IMPACT_WOOD]   = { 2.0f, 40.0f, 0.40f, 0.25f, 0.10f },
    [SFX_IMPACT_STONE]  = { 2.0f, 45.0f, 0.48f, 0.25f, 0.10f },
    [SFX_IMPACT_SOFT]   = { 1.5f, 25.0f, 0.36f, 0.10f, 0.12f },
};

typedef struct Cmd {
    u8  sfx;
    bool ui;
    f32 gain, pitch, delay;
    V3  pos;
} Cmd;

typedef struct Voice {
    const i16 *data;
    u32  len;
    f32  pos, step;
    f32  gain;
    V3   at;
    u8   sfx;
    bool on, ui;
    u32  wait;             // кадров до начала
    f32  gl, gr, send;     // усиления на начало блока
    f32  lpa, lp;          // глушение воздухом
} Voice;

// Фильтр с переменным состоянием, как в синтезе
typedef struct Svf {
    f32 low, band, f, q;
} Svf;

static void svf_set(Svf *s, f32 hz, f32 Q)
{
    s->f = 2.0f * sinf(PI_F * fminf(hz, FS * 0.16f) / FS);
    s->q = 1.0f / Q;
}

static inline f32 svf_bp(Svf *s, f32 x)
{
    s->low += s->f * s->band;
    f32 high = x - s->low - s->q * s->band;
    s->band += s->f * high;
    return s->band * s->q;
}

// Погода ветра: держится минуту-две и плавно сменяется соседней. Числа — доли слоёв
#define WIND_HOWLS 3
enum { WIND_CALM, WIND_STEADY, WIND_GUSTY, WIND_STORM, WIND_MOODS };
typedef struct WindMoodDef {
    f32 level;      // общая сила
    f32 depth;      // глубина порывов
    f32 rate;       // как часто сменяются порывы
    f32 rumble;     // гул низов
    f32 rush;       // шум потока в середине
    f32 howl;       // свист и вой в щелях мегаструктур
    f32 sand;       // песок и пыль
    f32 pitch;      // высота воя
    f32 debris;     // как часто треплет обломки
} WindMoodDef;

static const WindMoodDef g_wind_mood[WIND_MOODS] = {
    //                 сила  порывы  темп  гул   поток  вой   песок высота обломки
    [WIND_CALM]   = { 0.45f, 0.35f, 0.6f, 0.9f, 0.45f, 0.3f, 0.25f, 0.85f, 0.3f },
    [WIND_STEADY] = { 0.80f, 0.45f, 1.0f, 1.0f, 0.85f, 0.6f, 0.60f, 1.00f, 0.7f },
    [WIND_GUSTY]  = { 0.85f, 1.00f, 1.8f, 1.1f, 1.00f, 1.0f, 0.80f, 1.10f, 1.3f },
    [WIND_STORM]  = { 1.10f, 0.60f, 1.4f, 1.4f, 1.40f, 1.2f, 1.50f, 1.20f, 1.6f },
};

// Ревербератор: четыре гребня с затуханием верхов и два фазовых фильтра на канал (схема Шрёдера)
#define COMBS 4
#define ALLPASS 2
static const u32 g_comb_len[COMBS] = { 1695, 1760, 1623, 1548 };
static const u32 g_ap_len[ALLPASS] = { 605, 480 };
#define STEREO_SPREAD 25

typedef struct Reverb {
    f32 comb[2][COMBS][1800];
    f32 comb_lp[2][COMBS];
    u32 ci[2][COMBS];
    f32 ap[2][ALLPASS][640];
    u32 ai[2][ALLPASS];
} Reverb;

// Вход от игры
static struct {
    atomic_flag lock;
    Cmd  q[QUEUE_MAX];
    u32  n;
    V3   lis, right;
    AudioScene scene;
    f32  volume, music;
    bool on, music_on;
} g_in = { .lock = ATOMIC_FLAG_INIT, .right = { 1, 0, 0 }, .volume = 0.8f, .music = 0.7f, .on = true, .music_on = true };

// Слушатель на стороне игры: отсекать дальние звуки до очереди
static V3 g_lis_game;

// Состояние потока звука
static struct {
    SfxBank bank;
    u8  *pool;            // память банка, BANK_BYTES
    PlatformThread *synth;
    atomic_bool bank_ready;
    f32  synth_ms;
    bool ready, started, offline;  // offline — микшер зовёт проверка без окна
    Voice v[MAX_VOICES];
    Cmd  q[QUEUE_MAX];
    V3   lis, right;
    AudioScene want, sc;  // заданный фон и плавно идущий к нему
    f32  vol, vol_want, music, music_want;
    bool on;
    Rng  rng;
    u32  noise;           // быстрый шум ветра и воды
    // Ветер на канал: гул низов, шум потока, свист в щелях, шипение песка
    f32  wr1[2], wr2[2], wh[2], sand_fl[2];
    Svf  rush[2], howl[2][WIND_HOWLS];
    f32  howl_f[WIND_HOWLS], howl_fw[WIND_HOWLS], howl_a[WIND_HOWLS], howl_aw[WIND_HOWLS], howl_t[WIND_HOWLS];
    f32  gust, gust_want, gust_slow, gust_slow_want, gust_t, gust_slow_t;
    // Погода ветра: текущая, к которой идём, сколько ещё держится; откуда дует
    WindMoodDef wm;
    u8   mood;
    f32  mood_t, dir, dir_want, debris_t;
    // Гул мегаструктур
    f32  hum_ph[4], hum_am;
    // Вода
    Svf  river_bp;
    f32  river_lp, river_g, river_pan;
    f32  bub_ph, bub_f, bub_t, bub_a;
    // Дальние звуки
    f32  far_t;
    // Эхо от стен: одна дальняя отражённая копия и ревербератор
    f32  echo[ECHO_MAX];
    u32  echo_i, echo_len;
    f32  echo_lp;
    Reverb rv;
    f32  buf_l[MIX_BLOCK], buf_r[MIX_BLOCK], buf_s[MIX_BLOCK];
    u32  peak_voices;
} g_m;

static void lock(void)
{
    while (atomic_flag_test_and_set_explicit(&g_in.lock, memory_order_acquire)) platform_cpu_relax();
}
static void unlock(void) { atomic_flag_clear_explicit(&g_in.lock, memory_order_release); }

static inline f32 fast_noise(void)
{
    u32 x = g_m.noise;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_m.noise = x;
    return (f32)(i32)x * (1.0f / 2147483648.0f);
}

static f32 smoothstep(f32 a, f32 b, f32 x)
{
    f32 t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Банк собирается в своём потоке: старт игры его не ждёт, фон (ветер, гул) звучит сразу,
// звуки из банка — когда он готов (~0.15 с)
static void synth_job(void *arg)
{
    (void)arg;
    f64 t0 = time_now();
    Arena pool = { .base = g_m.pool, .size = BANK_BYTES };
    bool ok = synth_build(&g_m.bank, &pool);
    g_m.synth_ms = (f32)((time_now() - t0) * 1000.0);
    atomic_store_explicit(&g_m.bank_ready, ok, memory_order_release);
}

bool audio_init(Arena *perm)
{
    if (g_m.ready) return true;
    g_m.pool = arena_push(perm, BANK_BYTES, 16);
    if (!g_m.pool) return false;
    rng_seed(&g_m.rng, 0xA0D10u, 5);
    g_m.noise = 0x9E3779B9u;
    g_m.vol = g_m.vol_want = 0.8f;
    g_m.music = g_m.music_want = 0.7f;
    // Музыка своя в каждом запуске: звук на мир не влияет
    music_init((u32)(time_now() * 1000.0) ^ 0x6D757331u);
    g_m.on = true;
    g_m.far_t = 9.0f;
    g_m.echo_len = (u32)(0.42f * FS);
    g_m.mood = WIND_STEADY;
    g_m.wm = g_wind_mood[WIND_STEADY];
    g_m.mood_t = rng_range(&g_m.rng, 50.0f, 110.0f);
    g_m.dir = g_m.dir_want = rng_range(&g_m.rng, 0.0f, TAU);
    g_m.debris_t = 6.0f;
    for (u32 k = 0; k < WIND_HOWLS; k++) g_m.howl_f[k] = g_m.howl_fw[k] = 300.0f + 250.0f * (f32)k;
    svf_set(&g_m.river_bp, 900.0f, 0.6f);
    atomic_store(&g_m.bank_ready, false);
    g_m.synth = platform_thread_start(synth_job, NULL);
    if (!g_m.synth) return false;
    g_m.ready = true;
    return true;
}

static bool bank_ready(void) { return atomic_load_explicit(&g_m.bank_ready, memory_order_acquire); }

static void platform_cb(f32 *out, u32 frames, void *user)
{
    (void)user;
    audio_mix(out, frames);
}

bool audio_start(void)
{
    if (!g_m.ready) return false;
    g_m.started = platform_audio_start(AUDIO_RATE, platform_cb, NULL);
    return g_m.started;
}

void audio_stop(void)
{
    if (g_m.started) platform_audio_stop();
    g_m.started = false;
    platform_thread_join(g_m.synth);
    g_m.synth = NULL;
}

f32 audio_max_dist(u8 sfx) { return sfx < SFX_COUNT ? g_def[sfx].maxd : 0.0f; }

static void push(const Cmd *c)
{
    // Устройства нет — очередь никто не забирает; без этого при включении звука прозвучало бы всё накопленное
    if (!g_m.started && !g_m.offline) return;
    lock();
    if (g_in.n < QUEUE_MAX) g_in.q[g_in.n++] = *c;
    unlock();
}

void audio_play(u8 sfx, V3 pos, f32 gain, f32 pitch, f32 delay)
{
    if (!g_m.ready || !bank_ready() || sfx >= SFX_COUNT || gain <= 0.0f) return;
    V3 d = v3_sub(pos, g_lis_game);
    if (v3_dot(d, d) > g_def[sfx].maxd * g_def[sfx].maxd) return;
    push(&(Cmd){ .sfx = sfx, .gain = gain, .pitch = pitch, .delay = delay, .pos = pos });
}

void audio_play_ui(u8 sfx, f32 gain)
{
    if (!g_m.ready || !bank_ready() || sfx >= SFX_COUNT) return;
    push(&(Cmd){ .sfx = sfx, .ui = true, .gain = gain, .pitch = 1.0f });
}

void audio_listener(V3 pos, V3 right, const AudioScene *scene)
{
    g_lis_game = pos;
    if (!g_m.started && !g_m.offline) return;
    lock();
    g_in.lis = pos;
    g_in.right = right;
    g_in.scene = *scene;
    unlock();
}

void audio_volume(f32 master, bool on)
{
    lock();
    g_in.volume = master;
    g_in.on = on;
    unlock();
}

void audio_music(f32 level, bool on)
{
    lock();
    g_in.music = level;
    g_in.music_on = on;
    unlock();
}

// Усиления голоса для текущего слушателя
static void voice_target(const Voice *v, f32 *gl, f32 *gr, f32 *send, f32 *lpa)
{
    const SfxDef *d = &g_def[v->sfx];
    if (v->ui) {
        *gl = *gr = v->gain * d->gain * 0.7071f;
        *send = 0.0f;
        *lpa = 1.0f;
        return;
    }
    V3 dv = v3_sub(v->at, g_m.lis);
    f32 dist = v3_len(dv);
    f32 g = v->gain * d->gain * d->ref / fmaxf(dist, d->ref) * (1.0f - smoothstep(0.6f * d->maxd, d->maxd, dist));
    // Вблизи звук посередине, дальше уходит в сторону; до конца не уводим — в наушниках режет
    f32 hx = dv.x, hz = dv.z, hl = sqrtf(hx * hx + hz * hz);
    f32 pan = hl > 1e-3f ? (hx * g_m.right.x + hz * g_m.right.z) / hl : 0.0f;
    pan *= 0.75f * clampf(dist / 3.0f, 0.0f, 1.0f);
    *gl = g * sqrtf(0.5f * (1.0f - pan));
    *gr = g * sqrtf(0.5f * (1.0f + pan));
    *send = v->gain * d->gain * d->ref / fmaxf(dist, d->ref * 2.0f) * d->rev * g_m.sc.room * (0.35f + 0.65f * clampf(dist / 40.0f, 0.0f, 1.0f));
    // Воздух съедает верхи: на 60 м граница ~10 кГц, на 500 м ~2 кГц
    f32 fc = 20000.0f / (1.0f + dist / 60.0f);
    *lpa = fc >= 19000.0f ? 1.0f : 1.0f - expf(-TAU * fc / FS);
}

static void voice_start(const Cmd *c)
{
    const SfxBank *b = &g_m.bank;
    if (!b->count[c->sfx]) return;
    // Свободный голос, иначе самый тихий
    Voice *v = NULL;
    f32 quiet = 1e9f;
    for (u32 i = 0; i < MAX_VOICES; i++) {
        Voice *w = &g_m.v[i];
        if (!w->on) {
            v = w;
            break;
        }
        f32 lv = w->gl + w->gr;
        if (lv < quiet) {
            quiet = lv;
            v = w;
        }
    }
    const SfxDef *d = &g_def[c->sfx];
    u32 var = rng_below(&g_m.rng, b->count[c->sfx]);
    memset(v, 0, sizeof *v);
    v->data = b->data[c->sfx][var];
    v->len = b->len[c->sfx][var];
    v->sfx = c->sfx;
    v->ui = c->ui;
    v->at = c->pos;
    v->gain = c->gain;
    v->step = fmaxf(0.25f, c->pitch * (1.0f + rng_range(&g_m.rng, -d->jit, d->jit)));
    f32 delay = c->delay + (c->ui ? 0.0f : v3_len(v3_sub(c->pos, g_m.lis)) / SOUND_SPEED);
    v->wait = (u32)(fmaxf(0.0f, delay) * FS);
    voice_target(v, &v->gl, &v->gr, &v->send, &v->lpa);
    v->on = true;
}

// Дальний звук мира в случайной стороне
static void far_event(void)
{
    f32 r = rng_f32(&g_m.rng);
    u8 sfx = r < 0.45f ? SFX_FAR_GROAN : r < 0.75f ? SFX_FAR_BANG : SFX_FAR_RADIO;
    f32 a = rng_range(&g_m.rng, 0.0f, TAU);
    f32 dist = sfx == SFX_FAR_RADIO ? rng_range(&g_m.rng, 60.0f, 250.0f) : rng_range(&g_m.rng, 150.0f, 700.0f);
    Cmd c = { .sfx = sfx, .gain = fminf(1.0f, g_m.sc.far), .pitch = rng_range(&g_m.rng, 0.8f, 1.1f),
              .pos = v3(g_m.lis.x + sinf(a) * dist, g_m.lis.y + rng_range(&g_m.rng, 0.0f, 40.0f), g_m.lis.z + cosf(a) * dist) };
    voice_start(&c);
}

static void mix_voices(u32 n)
{
    f32 inv = 1.0f / (f32)n;
    u32 active = 0;
    for (u32 k = 0; k < MAX_VOICES; k++) {
        Voice *v = &g_m.v[k];
        if (!v->on) continue;
        active++;
        u32 j0 = 0;
        if (v->wait >= n) {
            v->wait -= n;
            continue;
        }
        j0 = v->wait;
        v->wait = 0;
        f32 gl, gr, send, lpa;
        voice_target(v, &gl, &gr, &send, &lpa);
        f32 dgl = (gl - v->gl) * inv, dgr = (gr - v->gr) * inv, ds = (send - v->send) * inv;
        f32 cl = v->gl + dgl * (f32)j0, cr = v->gr + dgr * (f32)j0, cs = v->send + ds * (f32)j0;
        v->lpa = lpa;
        const i16 *d = v->data;
        for (u32 j = j0; j < n; j++) {
            u32 i = (u32)v->pos;
            if (i + 1 >= v->len) {
                v->on = false;
                break;
            }
            f32 fr = v->pos - (f32)i;
            f32 s = ((f32)d[i] + ((f32)d[i + 1] - (f32)d[i]) * fr) * (1.0f / 32768.0f);
            v->lp += v->lpa * (s - v->lp);
            s = v->lp;
            g_m.buf_l[j] += s * cl;
            g_m.buf_r[j] += s * cr;
            g_m.buf_s[j] += s * cs;
            cl += dgl;
            cr += dgr;
            cs += ds;
            v->pos += v->step;
        }
        v->gl = gl;
        v->gr = gr;
        v->send = send;
    }
    g_m.peak_voices = MAX(g_m.peak_voices, active);
}

// Следующая погода: обычно соседняя; буря короче остальных
static void wind_mood_next(void)
{
    static const u8 next[WIND_MOODS][4] = {
        [WIND_CALM]   = { WIND_STEADY, WIND_STEADY, WIND_GUSTY, WIND_STEADY },
        [WIND_STEADY] = { WIND_CALM, WIND_GUSTY, WIND_GUSTY, WIND_STORM },
        [WIND_GUSTY]  = { WIND_STEADY, WIND_STEADY, WIND_STORM, WIND_CALM },
        [WIND_STORM]  = { WIND_GUSTY, WIND_STEADY, WIND_GUSTY, WIND_STEADY },
    };
    g_m.mood = next[g_m.mood][rng_below(&g_m.rng, 4)];
    g_m.mood_t = g_m.mood == WIND_STORM ? rng_range(&g_m.rng, 35.0f, 75.0f) : rng_range(&g_m.rng, 55.0f, 140.0f);
    // С новой погодой ветер чуть заходит с другой стороны
    g_m.dir_want = g_m.dir + rng_range(&g_m.rng, -1.2f, 1.2f);
}

// Порывы: медленная волна (10–25 с) и быстрая (1–4 с), обе к случайной цели; темп — от погоды.
// Свист: у каждой щели своя высота и громкость, обе плывут
static void gusts(f32 dt)
{
    if ((g_m.mood_t -= dt) <= 0.0f) wind_mood_next();
    const WindMoodDef *t = &g_wind_mood[g_m.mood];
    f32 km = fminf(1.0f, dt / 15.0f);
    f32 *cur = &g_m.wm.level;
    const f32 *want = &t->level;
    for (u32 i = 0; i < sizeof(WindMoodDef) / sizeof(f32); i++) cur[i] += (want[i] - cur[i]) * km;
    g_m.dir += (g_m.dir_want - g_m.dir) * fminf(1.0f, dt / 20.0f);
    f32 rate = fmaxf(g_m.wm.rate, 0.3f);
    if ((g_m.gust_t -= dt * rate) <= 0.0f) {
        g_m.gust_t = rng_range(&g_m.rng, 0.8f, 4.0f);
        f32 r = rng_f32(&g_m.rng);
        g_m.gust_want = r * r;
    }
    if ((g_m.gust_slow_t -= dt) <= 0.0f) {
        g_m.gust_slow_t = rng_range(&g_m.rng, 10.0f, 25.0f);
        g_m.gust_slow_want = rng_f32(&g_m.rng);
    }
    g_m.gust += (g_m.gust_want - g_m.gust) * fminf(1.0f, dt * rate / 1.5f);
    g_m.gust_slow += (g_m.gust_slow_want - g_m.gust_slow) * fminf(1.0f, dt / 6.0f);
    static const f32 lo[WIND_HOWLS] = { 200.0f, 380.0f, 900.0f }, hi[WIND_HOWLS] = { 450.0f, 900.0f, 2100.0f };
    for (u32 k = 0; k < WIND_HOWLS; k++) {
        if ((g_m.howl_t[k] -= dt) <= 0.0f) {
            g_m.howl_t[k] = rng_range(&g_m.rng, 2.5f, 9.0f);
            g_m.howl_fw[k] = rng_range(&g_m.rng, lo[k], hi[k]);
            f32 r = rng_f32(&g_m.rng);
            g_m.howl_aw[k] = r < 0.3f ? 0.0f : r;
        }
        g_m.howl_f[k] += (g_m.howl_fw[k] - g_m.howl_f[k]) * fminf(1.0f, dt / 2.5f);
        g_m.howl_a[k] += (g_m.howl_aw[k] - g_m.howl_a[k]) * fminf(1.0f, dt / 2.0f);
    }
}

// Ветер треплет обломки: где-то с подветренной стороны дребезжит лист или скрипит петля
static void debris_event(f32 g)
{
    u8 sfx = rng_f32(&g_m.rng) < 0.6f ? SFX_WIND_RATTLE : SFX_WIND_CREAK;
    f32 a = g_m.dir + PI_F + rng_range(&g_m.rng, -1.4f, 1.4f), dist = rng_range(&g_m.rng, 10.0f, 45.0f);
    Cmd c = { .sfx = sfx, .gain = clampf(0.4f + g * g_m.wm.level, 0.3f, 1.3f), .pitch = rng_range(&g_m.rng, 0.85f, 1.15f),
              .pos = v3(g_m.lis.x + sinf(a) * dist, g_m.lis.y + rng_range(&g_m.rng, 0.0f, 8.0f), g_m.lis.z + cosf(a) * dist) };
    voice_start(&c);
}

// Фон блока: ветер, гул, вода — сразу в сухой сигнал и немного в эхо
static void mix_ambient(u32 n)
{
    const f32 dt = (f32)n / FS;
    gusts(dt);
    const AudioScene *sc = &g_m.sc;
    const WindMoodDef *m = &g_m.wm;
    f32 g = clampf((0.55f * g_m.gust_slow + 0.6f * g_m.gust) * m->depth + 0.5f * (1.0f - m->depth), 0.0f, 1.0f);
    f32 level = sc->wind * m->level * (0.35f + 0.65f * g);
    if (!sc->green && sc->wind > 0.25f && bank_ready() && (g_m.debris_t -= dt) <= 0.0f) {
        g_m.debris_t = rng_range(&g_m.rng, 4.0f, 14.0f) / fmaxf(m->debris * (0.4f + g), 0.2f);
        if (g > 0.35f) debris_event(g);
    }
    // Откуда дует: с той стороны громче; порыв ещё и проходит по сторонам
    f32 side = 0.3f * (sinf(g_m.dir) * g_m.right.x + cosf(g_m.dir) * g_m.right.z) + 0.15f * (g_m.gust - g_m.gust_want);
    f32 sg[2] = { 1.0f - side, 1.0f + side };
    for (u32 c = 0; c < 2; c++) {
        svf_set(&g_m.rush[c], (380.0f + 650.0f * g) * m->pitch * (c ? 1.05f : 0.95f), 0.8f);
        for (u32 k = 0; k < WIND_HOWLS; k++)
            svf_set(&g_m.howl[c][k], g_m.howl_f[k] * m->pitch * (1.0f + 0.25f * g) * (c ? 1.012f : 0.988f), k == 2 ? 16.0f : 10.0f);
    }
    const f32 ar = 0.0207f, ah = 0.28f, af = 0.0026f;  // 160 Гц, ~2.5 кГц, дрожь песка ~20 Гц
    f32 kr = 1.2f * level * m->rumble, km = 0.22f * level * m->rush * (0.4f + 0.6f * g);
    f32 kh[WIND_HOWLS], ks = 0.045f * level * m->sand * (0.3f + g);
    // Узкая полоса из шума тихая сама по себе: свисту нужен большой множитель
    for (u32 k = 0; k < WIND_HOWLS; k++) kh[k] = (k == 2 ? 0.3f : 0.8f) * level * m->howl * (0.15f + 0.85f * g * g) * g_m.howl_a[k];
    // Гул: низкие ноты с медленным биением; верхние гармоники — чтобы было слышно и в динамиках ноутбука
    static const f32 hum_f[4] = { 41.2f, 55.3f, 82.6f, 123.9f }, hum_a[4] = { 1.0f, 0.7f, 0.45f, 0.25f };
    g_m.hum_am += dt * 0.07f;
    if (g_m.hum_am > 1.0f) g_m.hum_am -= 1.0f;
    f32 hum = sc->hum * 0.011f * (0.9f + 0.1f * sinf(TAU * g_m.hum_am));
    // Вода: шум в полосе, низкий плеск и пузыри
    f32 river = sc->river ? g_m.river_g : 0.0f;
    f32 rl = river * sqrtf(0.5f * (1.0f - g_m.river_pan)), rrr = river * sqrtf(0.5f * (1.0f + g_m.river_pan));
    for (u32 j = 0; j < n; j++) {
        f32 out[2];
        for (u32 c = 0; c < 2; c++) {
            f32 x = fast_noise();
            g_m.wr1[c] += ar * (x - g_m.wr1[c]);
            g_m.wr2[c] += ar * (g_m.wr1[c] - g_m.wr2[c]);
            g_m.wh[c] += ah * (x - g_m.wh[c]);
            g_m.sand_fl[c] += af * (fabsf(x) - g_m.sand_fl[c]);
            f32 w = g_m.wr2[c] * kr + svf_bp(&g_m.rush[c], x) * km;
            for (u32 k = 0; k < WIND_HOWLS; k++) w += svf_bp(&g_m.howl[c][k], x) * kh[k];
            // Песок сечёт неровно: шипение дрожит
            w += (x - g_m.wh[c]) * ks * (0.4f + 1.2f * g_m.sand_fl[c]);
            out[c] = w * sg[c];
        }
        f32 h = 0.0f;
        for (u32 k = 0; k < 4; k++) {
            g_m.hum_ph[k] += hum_f[k] / FS;
            if (g_m.hum_ph[k] >= 1.0f) g_m.hum_ph[k] -= 1.0f;
            h += hum_a[k] * sinf(TAU * g_m.hum_ph[k]);
        }
        out[0] += h * hum;
        out[1] += h * hum;
        if (river > 0.0f) {
            f32 x = fast_noise();
            g_m.river_lp += 0.03f * (x - g_m.river_lp);
            f32 w = svf_bp(&g_m.river_bp, x) * 0.5f + g_m.river_lp * 2.0f;
            if (g_m.bub_t <= 0.0f && (fast_noise() > 0.9993f)) {
                g_m.bub_t = 0.012f;
                g_m.bub_f = 500.0f + 900.0f * (0.5f + 0.5f * fast_noise());
                g_m.bub_a = 0.15f + 0.15f * fast_noise();
                g_m.bub_ph = 0.0f;
            }
            if (g_m.bub_t > 0.0f) {
                g_m.bub_t -= 1.0f / FS;
                g_m.bub_f *= 1.00008f;
                g_m.bub_ph += g_m.bub_f / FS;
                f32 u = 1.0f - g_m.bub_t / 0.012f;
                w += g_m.bub_a * sinf(PI_F * u) * sinf(TAU * g_m.bub_ph);
            }
            out[0] += w * rl;
            out[1] += w * rrr;
        }
        g_m.buf_l[j] += out[0];
        g_m.buf_r[j] += out[1];
        g_m.buf_s[j] += (out[0] + out[1]) * 0.08f;
    }
}

static inline f32 comb(u32 ch, u32 k, f32 x, f32 fb, f32 damp)
{
    Reverb *r = &g_m.rv;
    u32 len = g_comb_len[k] + ch * STEREO_SPREAD;
    f32 *buf = r->comb[ch][k];
    u32 i = r->ci[ch][k];
    f32 y = buf[i];
    r->comb_lp[ch][k] = y + (r->comb_lp[ch][k] - y) * damp;
    buf[i] = x + r->comb_lp[ch][k] * fb;
    r->ci[ch][k] = i + 1 >= len ? 0 : i + 1;
    return y;
}

static inline f32 allpass(u32 ch, u32 k, f32 x)
{
    Reverb *r = &g_m.rv;
    u32 len = g_ap_len[k] + ch * STEREO_SPREAD;
    f32 *buf = r->ap[ch][k];
    u32 i = r->ai[ch][k];
    f32 b = buf[i];
    buf[i] = x + b * 0.5f;
    r->ai[ch][k] = i + 1 >= len ? 0 : i + 1;
    return b - x;
}

// Эхо: дальняя стена (задержка ~0.4 с, глухая), потом хвост ревербератора
static void mix_reverb(u32 n)
{
    f32 fb = 0.80f + 0.06f * clampf(g_m.sc.room, 0.0f, 1.0f), damp = 0.35f;
    f32 echo_g = 0.45f * clampf(g_m.sc.room, 0.0f, 1.0f);
    for (u32 j = 0; j < n; j++) {
        f32 x = g_m.buf_s[j];
        f32 e = g_m.echo[g_m.echo_i];
        g_m.echo_lp += 0.25f * (e - g_m.echo_lp);
        g_m.echo[g_m.echo_i] = x + g_m.echo_lp * 0.25f;
        g_m.echo_i = g_m.echo_i + 1 >= g_m.echo_len ? 0 : g_m.echo_i + 1;
        f32 in = (x + g_m.echo_lp * echo_g) * 0.12f;
        for (u32 ch = 0; ch < 2; ch++) {
            f32 y = 0.0f;
            for (u32 k = 0; k < COMBS; k++) y += comb(ch, k, in, fb, damp);
            for (u32 k = 0; k < ALLPASS; k++) y = allpass(ch, k, y);
            f32 echo_dry = g_m.echo_lp * echo_g * (ch ? 0.8f : 1.0f);
            if (ch) g_m.buf_r[j] += y + echo_dry;
            else g_m.buf_l[j] += y + echo_dry;
        }
    }
}

static void drain_input(void)
{
    u32 n;
    lock();
    n = g_in.n;
    memcpy(g_m.q, g_in.q, n * sizeof *g_in.q);
    g_in.n = 0;
    g_m.lis = g_in.lis;
    g_m.right = g_in.right;
    g_m.want = g_in.scene;
    g_m.vol_want = g_in.volume;
    g_m.music_want = g_in.music_on ? g_in.music : 0.0f;
    g_m.on = g_in.on;
    unlock();
    for (u32 i = 0; i < n; i++) voice_start(&g_m.q[i]);
}

void audio_mix(f32 *out, u32 frames)
{
    if (!g_m.ready) {
        memset(out, 0, (size_t)frames * 2 * sizeof *out);
        return;
    }
    AUDIO_FLUSH_DENORMALS();
    drain_input();
    // Вода: ближайшая точка русла, громкость от расстояния, панорама
    if (g_m.want.river) {
        V3 dv = v3_sub(g_m.want.river_pos, g_m.lis);
        f32 d = sqrtf(dv.x * dv.x + dv.z * dv.z);
        g_m.river_g = 0.09f * 6.0f / fmaxf(d, 6.0f);
        g_m.river_pan = d > 1e-3f ? 0.6f * (dv.x * g_m.right.x + dv.z * g_m.right.z) / d : 0.0f;
    }
    for (u32 done = 0; done < frames;) {
        u32 n = MIN(MIX_BLOCK, frames - done);
        f32 dt = (f32)n / FS, k = fminf(1.0f, dt / 1.2f);
        // Фон меняется за секунду-другую: переход из зала наружу не щёлкает
        g_m.sc.wind += (g_m.want.wind - g_m.sc.wind) * k;
        g_m.sc.hum += (g_m.want.hum - g_m.sc.hum) * k;
        g_m.sc.room += (g_m.want.room - g_m.sc.room) * k;
        g_m.sc.far = g_m.want.far;
        g_m.sc.river = g_m.want.river;
        g_m.sc.green = g_m.want.green;
        g_m.music += (g_m.music_want - g_m.music) * fminf(1.0f, dt / 0.3f);
        g_m.vol += ((g_m.on ? g_m.vol_want : 0.0f) - g_m.vol) * fminf(1.0f, dt / 0.05f);
        if (g_m.sc.far > 0.02f && bank_ready() && (g_m.far_t -= dt) <= 0.0f) {
            g_m.far_t = rng_range(&g_m.rng, 14.0f, 45.0f) / fmaxf(g_m.sc.far, 0.25f);
            far_event();
        }
        memset(g_m.buf_l, 0, sizeof g_m.buf_l);
        memset(g_m.buf_r, 0, sizeof g_m.buf_r);
        memset(g_m.buf_s, 0, sizeof g_m.buf_s);
        mix_voices(n);
        mix_ambient(n);
        if (g_m.music > 1e-3f) music_mix(g_m.buf_l, g_m.buf_r, g_m.buf_s, n, g_m.sc.green, g_m.music);
        mix_reverb(n);
        f32 *o = out + (size_t)done * 2;
        for (u32 j = 0; j < n; j++) {
            // Мягкий потолок: громкая свалка не хрипит
            for (u32 c = 0; c < 2; c++) {
                f32 x = (c ? g_m.buf_r[j] : g_m.buf_l[j]) * g_m.vol;
                x = clampf(x, -3.0f, 3.0f);
                o[j * 2 + c] = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
            }
        }
        done += n;
    }
}

// ---------------------------------------------------------------- проверка без окна

static bool write_wav(const char *path, const i16 *pcm, u32 frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    u32 data = frames * 4, rate = AUDIO_RATE, byte_rate = AUDIO_RATE * 4, riff = 36 + data, fmt = 16;
    u16 pcm_fmt = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f);
    fwrite(&pcm_fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    fwrite(pcm, 4, frames, f);
    fclose(f);
    return true;
}

// Сценка по секундам; слушатель в начале координат, вправо — +X
typedef struct DemoCue {
    f32 t;
    u8  sfx;
    f32 x, z, gain;
} DemoCue;

int audio_demo(const char *wav_path)
{
    Arena mem;
    if (!arena_init(&mem, MB(32))) return 1;
    if (!audio_init(&mem)) return 1;
    platform_thread_join(g_m.synth);
    g_m.synth = NULL;
    if (!bank_ready()) return 1;
    g_m.offline = true;
    log_stat("ЗВУК: банк %u звуков, %.1f МБ, синтез %.0f мс (в своём потоке)", (unsigned)SFX_COUNT,
             (f64)g_m.bank.bytes / (1024.0 * 1024.0), (f64)g_m.synth_ms);
    // Хвост голосов оружия: сколько звенит до −40 дБ от пика (колокол звенел бы секунды)
    for (u32 sfx = SFX_WPN_RESSORA; sfx <= SFX_SCRAPE; sfx++) {
        f32 tail = 0.0f;
        for (u32 v = 0; v < g_m.bank.count[sfx]; v++) {
            const i16 *d = g_m.bank.data[sfx][v];
            u32 last = 0;
            for (u32 i = 0; i < g_m.bank.len[sfx][v]; i++)
                if (d[i] > 311 || d[i] < -311) last = i;
            tail += (f32)last / FS / (f32)g_m.bank.count[sfx];
        }
        log_stat("ЗВУК: голос %u: хвост до -40 дБ %.0f мс", sfx - SFX_WPN_RESSORA, (f64)(tail * 1000.0f));
    }
    // 0–26 с — сценка; 26–46 — четыре погоды ветра по 5 с; 46–91 — музыка без фона
    const f32 seconds = 91.0f, t_moods = 26.0f, t_music = 46.0f;
    const u32 frames = (u32)(seconds * FS), step = AUDIO_RATE / 60;
    i16 *pcm = ARENA_PUSH_ARRAY(&mem, i16, (size_t)frames * 2);
    f32 *mix = ARENA_PUSH_ARRAY(&mem, f32, (size_t)step * 2);
    if (!pcm || !mix) return 1;
    music_init(0x6D757331u);
    audio_music(0.7f, false);
    // Бой в 4 м справа впереди: достал; рессора бьёт, секач закрылся (лезвие скользнуло); попадание;
    // балка в рессору; уворот; арматура в шлем; балка с переломом, падение, выпавшее оружие;
    // арматура в древко багра; тесак врубился; в конце убрал
    static const DemoCue cues[] = {
        { 5.5f, SFX_DRAW, 3, -3, 1 },
        { 6.3f, SFX_SWISH, 3, -3, 1 }, { 6.55f, SFX_WPN_SEKACH, 3, -3, 0.9f }, { 6.553f, SFX_WPN_RESSORA, 3, -3, 0.55f },
        { 6.565f, SFX_SCRAPE, 3, -3, 0.8f },
        { 7.4f, SFX_SWISH, 3, -3, 1 }, { 7.62f, SFX_HIT_CUT, 3, -3, 1 },
        { 8.6f, SFX_SWISH_HEAVY, 3, -3, 1 }, { 8.95f, SFX_WPN_RESSORA, 3, -3, 1.1f }, { 8.953f, SFX_WPN_BALKA, 3, -3, 0.65f },
        { 9.7f, SFX_SCUFF, 3, -3, 1 },
        { 10.4f, SFX_SWISH, 3, -3, 1 }, { 10.62f, SFX_HIT_ARMOR, 3, -3, 1 }, { 10.622f, SFX_WPN_ARMATURA, 3, -3, 0.35f },
        { 11.5f, SFX_SWISH_HEAVY, 3, -3, 1 }, { 11.85f, SFX_HIT_CRUSH, 3, -3, 1 }, { 11.86f, SFX_CRACK, 3, -3, 1 },
        { 12.3f, SFX_BODYFALL, 3, -3, 1 }, { 12.5f, SFX_DROP_METAL, 3.3f, -2.5f, 1 },
        { 12.0f, SFX_HIT_PIERCE, -6, -8, 1 }, { 13.0f, SFX_WPN_SEKACH, -30, -50, 1 },
        { 13.5f, SFX_WPN_BAGOR, 3, -3, 0.9f }, { 13.503f, SFX_WPN_ARMATURA, 3, -3, 0.55f },
        { 14.0f, SFX_HIT_CHOP, 3, -3, 1 },
        { 14.8f, SFX_SHEATH, 3, -3, 1 },
        { 15.5f, SFX_UI_TICK, 0, 0, 1 }, { 16.0f, SFX_UI_ORDER, 0, 0, 1 },
        { 3.0f, SFX_FAR_GROAN, -300, -200, 1 }, { 17.0f, SFX_FAR_BANG, 400, -100, 1 }, { 19.5f, SFX_FAR_RADIO, -80, 120, 1 },
        { 18.0f, SFX_WIND_RATTLE, 20, -25, 1 }, { 21.5f, SFX_WIND_CREAK, -25, 10, 1 },
        // Вещи: лом падает на бетон (удар, отскок), обломок бетона, лом на песок (глуше, с подложкой),
        // удар по железу, стук дерева, свёрток
        { 0.8f, SFX_IMPACT_METAL, 2, -2, 1 }, { 1.02f, SFX_IMPACT_METAL, 2.1f, -2, 0.4f },
        { 1.4f, SFX_IMPACT_STONE, -2, -2, 1 },
        { 1.9f, SFX_IMPACT_METAL, 2, -3, 0.5f }, { 1.9f, SFX_IMPACT_SOFT, 2, -3, 0.6f },
        { 2.3f, SFX_IMPACT_METAL, 1, -2, 1.2f }, { 2.6f, SFX_IMPACT_WOOD, -1, -2, 1 }, { 2.8f, SFX_IMPACT_SOFT, 1, -2, 1 },
    };
    bool fired[ARRAY_COUNT(cues)] = { 0 };
    f64 mix_s = 0.0;
    f32 next_step_near = 0.3f, next_step_far = 0.5f, next_step_valley = 21.0f;
    u32 mood_on = 0xFFu;
    for (u32 done = 0; done < frames; done += step) {
        f32 t = (f32)done / FS;
        bool valley = t >= 20.5f && t < t_moods;
        AudioScene sc = { .wind = valley ? 0.35f : 1.0f, .hum = valley ? 0.15f : 1.0f, .room = valley ? 0.2f : 0.55f, .far = 0.0f };
        if (valley) {
            sc.river = true;
            sc.green = true;
            sc.river_pos = v3(-7.0f, 0.0f, 0.0f);
        }
        if (t >= t_moods && t < t_music) {
            // Погода по очереди, без случайной смены
            u32 m = MIN((u32)((t - t_moods) / 5.0f), WIND_MOODS - 1);
            if (m != mood_on) {
                mood_on = m;
                g_m.mood = (u8)m;
                g_m.wm = g_wind_mood[m];
                g_m.mood_t = 1e9f;
            }
            sc.hum = 0.3f;
        } else if (t >= t_music) {
            if (mood_on != 0xFEu) {
                mood_on = 0xFEu;
                audio_music(0.7f, true);
                music_cue(0.0f);
            }
            sc = (AudioScene){ .room = 0.55f };
        }
        audio_listener(v3(0, 0, 0), v3(1, 0, 0), &sc);
        // Прохожий идёт слева направо в 2 м перед слушателем; второй — в 18 м по бетону
        if (t >= next_step_near && t < 20.0f) {
            audio_play(SFX_STEP_SAND, v3(-15.0f + 1.4f * t, 0, -2.0f), 0.5f, 1.0f, 0.0f);
            next_step_near += 0.55f;
        }
        if (t >= next_step_far && t < 20.0f) {
            audio_play(SFX_STEP_CONCRETE, v3(10.0f, 0, -15.0f), 0.6f, 1.0f, 0.0f);
            next_step_far += 0.6f;
        }
        if (valley && t >= next_step_valley) {
            u8 s = t < 23.5f ? SFX_STEP_GRASS : SFX_STEP_WATER;
            audio_play(s, v3(-2.0f, 0, -1.5f), 0.6f, 1.0f, 0.0f);
            next_step_valley += 0.5f;
        }
        for (u32 k = 0; k < ARRAY_COUNT(cues); k++)
            if (!fired[k] && t >= cues[k].t) {
                fired[k] = true;
                if (cues[k].sfx == SFX_UI_TICK || cues[k].sfx == SFX_UI_ORDER) audio_play_ui(cues[k].sfx, cues[k].gain);
                else audio_play(cues[k].sfx, v3(cues[k].x, 0, cues[k].z), cues[k].gain, 1.0f, 0.0f);
            }
        u32 n = MIN(step, frames - done);
        f64 t0 = time_now();
        audio_mix(mix, n);
        mix_s += time_now() - t0;
        for (u32 j = 0; j < n * 2; j++) pcm[(size_t)done * 2 + j] = (i16)(clampf(mix[j], -1.0f, 1.0f) * 32767.0f);
    }
    // Громкость по отрезкам: ветер, бой, долина, погоды ветра, музыка
    static const f32 seg[][2] = { { 0.5f, 5.0f }, { 6.0f, 15.0f }, { 21.0f, 26.0f }, { 27.5f, 31.0f },
                                  { 32.5f, 36.0f }, { 37.5f, 41.0f }, { 42.5f, 46.0f }, { 52.0f, 91.0f } };
    static const char *seg_name[] = { "ВЕТЕР И ШАГИ", "БОЙ В 4 М", "ДОЛИНА", "ЗАТИШЬЕ", "РОВНЫЙ ВЕТЕР", "ПОРЫВЫ", "БУРЯ",
                                      "МУЗЫКА" };
    for (u32 s = 0; s < ARRAY_COUNT(seg); s++) {
        f64 sum = 0.0;
        i32 peak = 0;
        u32 a = (u32)(seg[s][0] * FS), b = (u32)(seg[s][1] * FS);
        for (u32 i = a * 2; i < b * 2; i++) {
            sum += (f64)pcm[i] * pcm[i];
            peak = MAX(peak, pcm[i] < 0 ? -pcm[i] : pcm[i]);
        }
        f64 rms = sqrt(sum / (f64)((b - a) * 2)) / 32768.0;
        log_stat("ЗВУК: %s: RMS %.1f дБ, пик %.1f дБ", seg_name[s], 20.0 * log10(rms + 1e-9), 20.0 * log10(peak / 32768.0 + 1e-9));
    }
    log_stat("ЗВУК: микшер %.2f мс на секунду звука (%.2f%% одного ядра), голосов до %u", mix_s * 1000.0 / seconds,
             mix_s / seconds * 100.0, g_m.peak_voices);
    int rc = 0;
    if (wav_path) {
        if (write_wav(wav_path, pcm, frames)) log_stat("ЗВУК: записан %s, %.0f с", wav_path, (f64)seconds);
        else rc = 1;
    }
    g_m.ready = false;
    arena_free(&mem);
    return rc;
}
