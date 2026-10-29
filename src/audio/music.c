// Музыка. Всё считается на лету: три голоса пэда (по две пилы, расстроенные между каналами) через
// мягкий срез верхов, суб-бас на тонике аккорда, редкие стеклянные ноты, плёнка (плавание высоты, шип).
// Пьеса держит один лад и тонику; аккорды сменяются раз в 12–20 с, голоса скользят к новым нотам
#include "music.h"

#include <string.h>

#include "../core/math3d.h"
#include "../core/rng.h"
#include "audio.h"

#define FS ((f32)AUDIO_RATE)
#define PAD 3
#define LEADS 4

// Лады от тоники, полутоны
static const u8 g_modes[4][7] = {
    { 0, 1, 3, 5, 7, 8, 10 },  // фригийский: тяжёлый, вторая ступень прижата к тонике
    { 0, 2, 3, 5, 7, 8, 10 },  // натуральный минор
    { 0, 2, 3, 5, 7, 9, 10 },  // дорийский: светлее — долина
    { 0, 2, 4, 5, 7, 9, 10 },  // миксолидийский: долина
};

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
    f32  pad_f[PAD], pad_fw[PAD], pad_ph[PAD][2];
    f32  sub_ph, sub_f, sub_fw, swell_ph;
    Svf2 lp[2];
    f32  cut_ph;
    Lead lead[LEADS];
    i32  motif;            // вторая нота мотива на следующую долю, -1 — нет
    f32  wow_ph, flut_ph;
    u32  noise;
    f32  hiss_lp;
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

static f32 midi_hz(i32 m) { return 440.0f * powf(2.0f, (f32)(m - 69) / 12.0f); }

// Ступень лада (может уходить за октаву в обе стороны) в ноту
static i32 note(i32 deg)
{
    i32 oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    return g_mu.root + 12 * oct + g_modes[g_mu.mode][deg - oct * 7];
}

static void chord(bool glide)
{
    for (u32 k = 0; k < PAD; k++) {
        // Трезвучие ступени, верхний голос иногда уходит на нону — грязнее и тревожнее
        i32 d = g_mu.degree + 2 * (i32)k + (k == 2 && rng_f32(&g_mu.rng) < 0.35f ? 4 : 0);
        g_mu.pad_fw[k] = midi_hz(note(d) + 12);
        if (!glide) g_mu.pad_f[k] = g_mu.pad_fw[k];
    }
    g_mu.sub_fw = midi_hz(note(g_mu.degree));
    if (!glide) g_mu.sub_f = g_mu.sub_fw;
    g_mu.chord_left = 10 + rng_below(&g_mu.rng, 7);
}

static void piece_start(bool green)
{
    g_mu.playing = true;
    g_mu.green = green;
    g_mu.t_left = rr(120.0f, 180.0f);
    g_mu.mode = (u8)((green ? 2 : 0) + rng_below(&g_mu.rng, 2));
    g_mu.root = green ? 40 + (i32)rng_below(&g_mu.rng, 5) : 36 + (i32)rng_below(&g_mu.rng, 5);
    g_mu.degree = 0;
    g_mu.pulse = rr(1.0f, 1.4f);
    g_mu.pulse_t = g_mu.pulse;
    g_mu.motif = -1;
    chord(false);
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
    *l = (Lead){ .f = midi_hz(note(deg) + 24), .amp = amp, .pan = rr(-0.45f, 0.45f), .on = true };
}

// Доля: смена аккорда, редкая нота или мотив из двух
static void beat(void)
{
    if (g_mu.motif >= 0) {
        lead_start(g_mu.motif, rr(0.5f, 0.8f));
        g_mu.motif = -1;
    } else if (rng_f32(&g_mu.rng) < (g_mu.green ? 0.26f : 0.16f)) {
        i32 d = g_mu.degree + 2 * (i32)rng_below(&g_mu.rng, 4);
        lead_start(d, rr(0.6f, 1.0f));
        if (rng_f32(&g_mu.rng) < 0.3f) g_mu.motif = d + (rng_f32(&g_mu.rng) < 0.5f ? -1 : 1);
    }
    if (--g_mu.chord_left == 0) {
        // Чаще всего обратно к тонике; во фригийском — тяжёлая вторая ступень
        static const i32 step[6] = { 5, 3, 6, 1, 4, 2 };
        g_mu.degree = g_mu.degree != 0 && rng_f32(&g_mu.rng) < 0.45f ? 0 : step[rng_below(&g_mu.rng, 6)];
        chord(true);
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
    // Вход за 6 с, уход за последние 10
    f32 env_want = g_mu.t_left > 10.0f ? 1.0f : g_mu.t_left / 10.0f;
    g_mu.env += (env_want - g_mu.env) * fminf(1.0f, dt / 3.0f);
    if ((g_mu.pulse_t -= dt) <= 0.0f) {
        g_mu.pulse_t += g_mu.pulse;
        if (g_mu.t_left > 12.0f) beat();
    }
    // Срез верхов пэда медленно дышит; в долине светлее
    g_mu.cut_ph = wrap(g_mu.cut_ph + dt * 0.021f);
    f32 cut = (green ? 520.0f : 330.0f) + (green ? 420.0f : 300.0f) * (0.5f + 0.5f * sin01(g_mu.cut_ph));
    f32 cf = 2.0f * sinf(PI_F * cut / FS), cq = 1.0f / 0.9f;
    g_mu.swell_ph = wrap(g_mu.swell_ph + dt * 0.05f);
    f32 sub_a = 0.09f * (0.7f + 0.3f * sin01(g_mu.swell_ph));
    f32 glide = 1.0f - expf(-1.0f / (1.8f * FS));
    f32 g = level * g_mu.env;
    f32 lead_decay = expf(-1.0f / (2.4f * FS)), lead_att = 1.0f / (0.04f * FS);
    for (u32 j = 0; j < n; j++) {
        // Плёнка: медленное плавание и дрожь высоты
        g_mu.wow_ph = wrap(g_mu.wow_ph + 0.31f / FS);
        g_mu.flut_ph = wrap(g_mu.flut_ph + 4.3f / FS);
        f32 wow = 1.0f + 0.0024f * sin01(g_mu.wow_ph) + 0.0006f * sin01(g_mu.flut_ph);
        f32 pad[2] = { 0.0f, 0.0f };
        for (u32 k = 0; k < PAD; k++) {
            g_mu.pad_f[k] += (g_mu.pad_fw[k] - g_mu.pad_f[k]) * glide;
            f32 f = g_mu.pad_f[k] * wow / FS;
            g_mu.pad_ph[k][0] = wrap(g_mu.pad_ph[k][0] + f * 0.9965f);
            g_mu.pad_ph[k][1] = wrap(g_mu.pad_ph[k][1] + f * 1.0035f);
            pad[0] += 2.0f * g_mu.pad_ph[k][0] - 1.0f;
            pad[1] += 2.0f * g_mu.pad_ph[k][1] - 1.0f;
        }
        for (u32 c = 0; c < 2; c++) {
            Svf2 *s = &g_mu.lp[c];
            s->low += cf * s->band;
            f32 hi = pad[c] * 0.05f - s->low - cq * s->band;
            s->band += cf * hi;
            pad[c] = s->low / (1.0f + fabsf(s->low));
        }
        g_mu.sub_f += (g_mu.sub_fw - g_mu.sub_f) * glide;
        g_mu.sub_ph = wrap(g_mu.sub_ph + g_mu.sub_f * wow / FS);
        f32 sub = sub_a * sin01(g_mu.sub_ph);
        f32 ld[2] = { 0.0f, 0.0f };
        for (u32 i = 0; i < LEADS; i++) {
            Lead *q = &g_mu.lead[i];
            if (!q->on) continue;
            q->env = q->age < (u32)(0.04f * FS) ? (f32)q->age * lead_att : q->env * lead_decay;
            if (q->age++ > (u32)FS && q->env < 1e-3f) {
                q->on = false;
                continue;
            }
            q->ph = wrap(q->ph + q->f * wow / FS);
            f32 p2 = q->ph * 2.003f, p3 = q->ph * 3.0f;
            f32 x = sin01(q->ph) + 0.22f * sin01(p2 - floorf(p2)) + 0.08f * sin01(p3 - floorf(p3));
            x *= 0.045f * q->amp * q->env;
            ld[0] += x * (1.0f - q->pan);
            ld[1] += x * (1.0f + q->pan);
        }
        // Шип плёнки
        u32 z = g_mu.noise;
        z ^= z << 13;
        z ^= z >> 17;
        z ^= z << 5;
        g_mu.noise = z;
        f32 wn = (f32)(i32)z * (1.0f / 2147483648.0f);
        g_mu.hiss_lp += 0.3f * (wn - g_mu.hiss_lp);
        f32 hiss = (wn - g_mu.hiss_lp) * 0.0012f;
        f32 ol = (pad[0] * 0.55f + sub + ld[0] + hiss) * g, orr = (pad[1] * 0.55f + sub + ld[1] + hiss) * g;
        l[j] += ol;
        r[j] += orr;
        send[j] += (pad[0] + pad[1]) * 0.12f * g + (ld[0] + ld[1]) * 0.45f * g;
    }
}
