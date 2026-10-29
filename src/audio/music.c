// Музыка. Всё считается на лету. Пэд — мягкие тоны (основной, октава, квинта сверху, тихо), у каждого
// своё медленное дыхание и пара расстроенных копий слева и справа. Аккорды не скользят по высоте
// (скольжение пил звучало как мотор): старый аккорд гаснет, новый проступает. Над ним воздух — шум через
// узкие полосы на нотах аккорда, редкие стеклянные ноты, плёнка. У музыки своё долгое эхо
#include "music.h"

#include <string.h>

#include "../core/math3d.h"
#include "../core/rng.h"
#include "audio.h"

#define FS ((f32)AUDIO_RATE)
#define PADV 8   // два аккорда по четыре голоса: один гаснет, другой проступает
#define LEADS 4
#define AIR 2
#define MCOMBS 4
#define MAPS 2

// Лады от тоники, полутоны
static const u8 g_modes[4][7] = {
    { 0, 1, 3, 5, 7, 8, 10 },  // фригийский: тяжёлый, вторая ступень прижата к тонике
    { 0, 2, 3, 5, 7, 8, 10 },  // натуральный минор
    { 0, 2, 3, 5, 7, 9, 10 },  // дорийский: светлее — долина
    { 0, 2, 4, 5, 7, 9, 10 },  // миксолидийский: долина
};

// Эхо музыки: длиннее и мягче мирового — пространство без стен
static const u32 g_mcomb[MCOMBS] = { 2473, 2767, 3217, 3557 };
static const u32 g_map[MAPS] = { 556, 441 };
#define MSPREAD 23

typedef struct PadVoice {
    f32 f, a, aw, rate;   // высота; громкость, к которой идёт, и скорость (за кадр)
    f32 ph[2];            // левая и правая копии
    f32 br, br_ph;        // дыхание: частота, фаза
} PadVoice;

typedef struct Lead {
    f32  ph, f, amp, env, pan;
    u32  age;  // кадров с начала ноты
    bool on;
} Lead;

typedef struct Svf2 {
    f32 low, band;
} Svf2;

static struct {
    Rng  rng;
    bool playing, green, cut_short;
    f32  t_left;           // до конца пьесы или паузы, с
    f32  env;              // огибающая пьесы
    u8   mode;
    i32  root, degree;
    f32  pulse, pulse_t;   // доля, с; до следующей
    u32  chord_left;       // долей до смены аккорда
    PadVoice pad[PADV];
    Svf2 air[2][AIR];
    f32  air_f[AIR], air_ph;
    Lead lead[LEADS];
    i32  motif;            // вторая нота мотива на следующую долю, -1 — нет
    f32  wow_ph;
    u32  noise;
    f32  hiss_lp;
    // Эхо
    f32  comb[2][MCOMBS][3600], comb_lp[2][MCOMBS];
    u32  ci[2][MCOMBS];
    f32  ap[2][MAPS][600];
    u32  ai[2][MAPS];
} g_mu;

static f32 rr(f32 lo, f32 hi) { return rng_range(&g_mu.rng, lo, hi); }

// Синус по фазе 0..1: парабола с поправкой, ошибка ~0.1%
static inline f32 sin01(f32 p)
{
    f32 x = p < 0.5f ? p : p - 0.5f;
    f32 y = 16.0f * x * (0.5f - x);
    y = y * (0.775f + 0.225f * y);
    return p < 0.5f ? y : -y;
}

static inline f32 wrap(f32 p) { return p >= 1.0f ? p - 1.0f : p; }
static inline f32 frac(f32 p) { return p - floorf(p); }

static f32 midi_hz(i32 m) { return 440.0f * powf(2.0f, (f32)(m - 69) / 12.0f); }

// Ступень лада (может уходить за октаву в обе стороны) в ноту
static i32 note(i32 deg)
{
    i32 oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    return g_mu.root + 12 * oct + g_modes[g_mu.mode][deg - oct * 7];
}

// Новый аккорд: звучащие голоса гаснут за fade с, новые за столько же проступают
static void chord(f32 fade)
{
    f32 rate = 1.0f / (fmaxf(fade, 0.05f) * FS);
    for (u32 i = 0; i < PADV; i++) {
        g_mu.pad[i].aw = 0.0f;
        g_mu.pad[i].rate = rate;
    }
    // Тоника внизу тихо, трезвучие октавой выше; верхний иногда уходит на нону
    i32 deg[4] = { g_mu.degree, g_mu.degree, g_mu.degree + 2, g_mu.degree + 4 };
    if (rng_f32(&g_mu.rng) < 0.3f) deg[3] += 4;
    static const f32 gain[4] = { 0.55f, 1.0f, 0.8f, 0.6f };
    for (u32 k = 0; k < 4; k++) {
        // Самый тихий голос — под новую ноту
        PadVoice *v = NULL;
        for (u32 i = 0; i < PADV; i++)
            if (g_mu.pad[i].aw == 0.0f && (!v || g_mu.pad[i].a < v->a)) v = &g_mu.pad[i];
        if (!v) break;
        v->f = midi_hz(note(deg[k]) + (k ? 12 : 0));
        v->aw = gain[k];
        v->br = rr(0.025f, 0.07f);
        v->br_ph = rng_f32(&g_mu.rng);
    }
    // Воздух на квинте и терции две октавы выше
    g_mu.air_f[0] = midi_hz(note(g_mu.degree + 4) + 24);
    g_mu.air_f[1] = midi_hz(note(g_mu.degree + 2) + 36);
    g_mu.chord_left = 12 + rng_below(&g_mu.rng, 9);
}

static void piece_start(bool green)
{
    g_mu.playing = true;
    g_mu.green = green;
    g_mu.t_left = rr(120.0f, 180.0f);
    g_mu.mode = (u8)((green ? 2 : 0) + rng_below(&g_mu.rng, 2));
    g_mu.root = green ? 40 + (i32)rng_below(&g_mu.rng, 5) : 36 + (i32)rng_below(&g_mu.rng, 5);
    g_mu.degree = 0;
    g_mu.pulse = rr(1.2f, 1.6f);
    g_mu.pulse_t = g_mu.pulse;
    g_mu.motif = -1;
    for (u32 i = 0; i < PADV; i++) g_mu.pad[i].a = 0.0f;
    chord(6.0f);
}

static void lead_start(i32 deg, f32 amp)
{
    Lead *l = &g_mu.lead[0];
    for (u32 i = 0; i < LEADS; i++) {
        if (!g_mu.lead[i].on) {
            l = &g_mu.lead[i];
            break;
        }
        if (g_mu.lead[i].env < l->env) l = &g_mu.lead[i];
    }
    *l = (Lead){ .f = midi_hz(note(deg) + 24), .amp = amp, .pan = rr(-0.5f, 0.5f), .on = true };
}

// Доля: смена аккорда, редкая нота или мотив из двух
static void beat(void)
{
    if (g_mu.motif >= 0) {
        lead_start(g_mu.motif, rr(0.4f, 0.7f));
        g_mu.motif = -1;
    } else if (rng_f32(&g_mu.rng) < (g_mu.green ? 0.16f : 0.09f)) {
        i32 d = g_mu.degree + 2 * (i32)rng_below(&g_mu.rng, 4);
        lead_start(d, rr(0.6f, 1.0f));
        if (rng_f32(&g_mu.rng) < 0.3f) g_mu.motif = d + (rng_f32(&g_mu.rng) < 0.5f ? -1 : 1);
    }
    if (--g_mu.chord_left == 0) {
        // Чаще всего обратно к тонике; во фригийском — тяжёлая вторая ступень
        static const i32 step[6] = { 5, 3, 6, 1, 4, 2 };
        g_mu.degree = g_mu.degree != 0 && rng_f32(&g_mu.rng) < 0.45f ? 0 : step[rng_below(&g_mu.rng, 6)];
        chord(rr(5.0f, 8.0f));
    }
}

void music_init(u32 seed)
{
    memset(&g_mu, 0, sizeof g_mu);
    rng_seed(&g_mu.rng, seed, 9);
    g_mu.noise = seed | 1u;
    g_mu.t_left = 4.0f;
    g_mu.motif = -1;
}

bool music_playing(void) { return g_mu.playing; }

void music_cue(f32 sec)
{
    g_mu.playing = false;
    g_mu.env = 0.0f;
    g_mu.t_left = sec;
}

static inline f32 mcomb(u32 ch, u32 k, f32 x)
{
    u32 len = g_mcomb[k] + ch * MSPREAD, i = g_mu.ci[ch][k];
    f32 *buf = g_mu.comb[ch][k], y = buf[i];
    g_mu.comb_lp[ch][k] = y + (g_mu.comb_lp[ch][k] - y) * 0.45f;
    buf[i] = x + g_mu.comb_lp[ch][k] * 0.92f;
    g_mu.ci[ch][k] = i + 1 >= len ? 0 : i + 1;
    return y;
}

static inline f32 mallpass(u32 ch, u32 k, f32 x)
{
    u32 len = g_map[k] + ch * MSPREAD, i = g_mu.ai[ch][k];
    f32 *buf = g_mu.ap[ch][k], b = buf[i];
    buf[i] = x + b * 0.5f;
    g_mu.ai[ch][k] = i + 1 >= len ? 0 : i + 1;
    return b - x;
}

void music_mix(f32 *l, f32 *r, f32 *send, u32 n, bool green, f32 level)
{
    const f32 dt = (f32)n / FS;
    g_mu.t_left -= dt;
    if (!g_mu.playing) {
        if (g_mu.t_left <= 0.0f) piece_start(green);
        else return;
    }
    // Ушли в долину или обратно: пьеса доигрывает за 8 с, следующая — в новом ладу
    if (green != g_mu.green && g_mu.t_left > 8.0f) {
        g_mu.t_left = 8.0f;
        g_mu.green = green;
        g_mu.cut_short = true;
    }
    if (g_mu.t_left <= 0.0f) {
        g_mu.playing = false;
        g_mu.t_left = g_mu.cut_short ? 3.0f : rr(20.0f, 40.0f);
        g_mu.cut_short = false;
        for (u32 i = 0; i < LEADS; i++) g_mu.lead[i].on = false;
        return;
    }
    // Вход за ~8 с, уход за последние 12 с
    f32 env_want = g_mu.t_left > 12.0f ? 1.0f : g_mu.t_left / 12.0f;
    g_mu.env += (env_want - g_mu.env) * fminf(1.0f, dt / 4.0f);
    if ((g_mu.pulse_t -= dt) <= 0.0f) {
        g_mu.pulse_t += g_mu.pulse;
        if (g_mu.t_left > 14.0f) beat();
    }
    // Воздух дышит медленно
    g_mu.air_ph = wrap(g_mu.air_ph + dt * 0.017f);
    f32 air_a = 0.012f * (0.5f + 0.5f * sin01(g_mu.air_ph)) * (green ? 1.2f : 1.0f);
    f32 af[AIR], aq = 1.0f / 35.0f;
    for (u32 k = 0; k < AIR; k++) af[k] = 2.0f * sinf(PI_F * fminf(g_mu.air_f[k], 6000.0f) / FS);
    // Дыхание голосов пэда — на блок
    f32 br[PADV];
    for (u32 i = 0; i < PADV; i++) {
        PadVoice *v = &g_mu.pad[i];
        v->br_ph = wrap(v->br_ph + dt * v->br);
        br[i] = 0.65f + 0.35f * sin01(v->br_ph);
    }
    f32 g = level * g_mu.env;
    f32 lead_decay = expf(-1.0f / (3.5f * FS)), lead_att = 1.0f / (0.12f * FS);
    for (u32 j = 0; j < n; j++) {
        // Плёнка: медленное плавание высоты
        g_mu.wow_ph = wrap(g_mu.wow_ph + 0.23f / FS);
        f32 wow = 1.0f + 0.0015f * sin01(g_mu.wow_ph);
        f32 pad[2] = { 0.0f, 0.0f };
        for (u32 i = 0; i < PADV; i++) {
            PadVoice *v = &g_mu.pad[i];
            if (v->a < v->aw) v->a = fminf(v->aw, v->a + v->rate);
            else if (v->a > v->aw) v->a = fmaxf(v->aw, v->a - v->rate);
            if (v->a <= 0.0f) continue;
            f32 f = v->f * wow / FS, a = v->a * br[i];
            for (u32 c = 0; c < 2; c++) {
                v->ph[c] = wrap(v->ph[c] + f * (c ? 1.0025f : 0.9975f));
                f32 p = v->ph[c];
                pad[c] += a * (sin01(p) + 0.22f * sin01(frac(p * 2.0f)) + 0.06f * sin01(frac(p * 3.0f)));
            }
        }
        // Шум для воздуха и плёнки
        u32 z = g_mu.noise;
        z ^= z << 13;
        z ^= z >> 17;
        z ^= z << 5;
        g_mu.noise = z;
        f32 wn = (f32)(i32)z * (1.0f / 2147483648.0f);
        f32 air[2] = { 0.0f, 0.0f };
        for (u32 c = 0; c < 2; c++) {
            f32 x = c ? -wn : wn;
            for (u32 k = 0; k < AIR; k++) {
                Svf2 *s = &g_mu.air[c][k];
                s->low += af[k] * s->band;
                f32 hi = x - s->low - aq * s->band;
                s->band += af[k] * hi;
                air[c] += s->band * aq;
            }
        }
        f32 ld[2] = { 0.0f, 0.0f };
        for (u32 i = 0; i < LEADS; i++) {
            Lead *q = &g_mu.lead[i];
            if (!q->on) continue;
            q->env = q->age < (u32)(0.12f * FS) ? (f32)q->age * lead_att : q->env * lead_decay;
            if (q->age++ > (u32)FS && q->env < 1e-3f) {
                q->on = false;
                continue;
            }
            q->ph = wrap(q->ph + q->f * wow / FS);
            f32 x = sin01(q->ph) + 0.18f * sin01(frac(q->ph * 2.003f));
            x *= 0.03f * q->amp * q->env;
            ld[0] += x * (1.0f - q->pan);
            ld[1] += x * (1.0f + q->pan);
        }
        g_mu.hiss_lp += 0.3f * (wn - g_mu.hiss_lp);
        f32 hiss = (wn - g_mu.hiss_lp) * 0.0007f;
        // Своё эхо: пэд немного, ноты и воздух почти целиком
        f32 dry[2], in[2];
        for (u32 c = 0; c < 2; c++) {
            dry[c] = pad[c] * 0.042f + ld[c] * 0.5f + air[c] * air_a * 0.4f + hiss;
            in[c] = (pad[c] * 0.042f * 0.5f + ld[c] + air[c] * air_a) * 0.1f;
        }
        for (u32 c = 0; c < 2; c++) {
            f32 y = 0.0f;
            for (u32 k = 0; k < MCOMBS; k++) y += mcomb(c, k, in[c] + in[c ^ 1] * 0.3f);
            for (u32 k = 0; k < MAPS; k++) y = mallpass(c, k, y);
            dry[c] += y * 0.9f;
        }
        l[j] += dry[0] * g;
        r[j] += dry[1] * g;
        send[j] += (ld[0] + ld[1]) * 0.1f * g;
    }
}
