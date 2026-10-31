// Синтез банка звуков. Металл — затухающие моды с неровными отношениями частот (как у стержня
// и пластины), удары по телу — низкий «тук» со сползающей высотой и глухой шум, шаги — хруст из
// коротких зёрен шума, свист — шум через полосовой фильтр с плывущей частотой
#include "synth.h"

#include <string.h>

#include "../core/log.h"
#include "../core/rng.h"

#define FS ((f32)AUDIO_RATE)
#define TAU (2.0f * PI_F)
#define SYN_MAX (AUDIO_RATE * 5)  // самый длинный вариант, отсчётов

static f32 g_buf[SYN_MAX];  // собираемый вариант
static f32 g_aux[SYN_MAX];  // слой, который ещё нужно отфильтровать
static f32 g_tmp[SYN_MAX];  // звон предмета до дребезга
static Rng g_r;

static f32 rnd(void) { return rng_f32(&g_r); }
static f32 rr(f32 lo, f32 hi) { return rng_range(&g_r, lo, hi); }
static f32 wn(void) { return rng_f32(&g_r) * 2.0f - 1.0f; }
static u32 at(f32 sec)
{
    f32 i = sec * FS;
    return i <= 0.0f ? 0 : i >= (f32)SYN_MAX ? SYN_MAX : (u32)i;
}

// Фильтр с переменным состоянием (Chamberlin): low, band, high за один шаг
typedef struct Svf {
    f32 low, band, high, f, q;
} Svf;

static void svf_set(Svf *s, f32 hz, f32 Q)
{
    s->f = 2.0f * sinf(PI_F * fminf(hz, FS * 0.16f) / FS);
    s->q = 1.0f / Q;
}

static void svf_tick(Svf *s, f32 x)
{
    s->low += s->f * s->band;
    s->high = x - s->low - s->q * s->band;
    s->band += s->f * s->high;
}

// Полоса с единичным усилением на резонансе
static f32 svf_bp(Svf *s, f32 x)
{
    svf_tick(s, x);
    return s->band * s->q;
}

static f32 lp_a(f32 hz) { return 1.0f - expf(-TAU * hz / FS); }

// Затухающая мода: синус с экспоненциальным спадом, tau — до 1/e, с
static void mode(f32 *b, u32 from, u32 n, f32 hz, f32 amp, f32 tau)
{
    if (hz >= FS * 0.45f || from >= n) return;
    f32 w = TAU * hz / FS, c = cosf(w), s = sinf(w), r = expf(-1.0f / (tau * FS));
    f32 x = amp, y = 0.0f;
    for (u32 i = from; i < n; i++) {
        b[i] += y;
        f32 nx = (x * c - y * s) * r;
        y = (x * s + y * c) * r;
        x = nx;
        if (!((i - from) & 255) && fabsf(x) + fabsf(y) < amp * 1e-3f) break;
    }
}

// Удар по металлу: моды стержня и пластины, у нижних — близкая пара (биения), щелчок касания.
// bright — во сколько слабее каждая следующая мода
static void metal(f32 *b, u32 from, u32 n, f32 f0, f32 amp, f32 tau, f32 bright, u32 modes)
{
    static const f32 ratio[] = { 1.0f, 1.59f, 2.76f, 3.91f, 5.40f, 6.80f, 8.93f, 11.2f, 13.34f, 16.1f };
    f32 a = amp;
    for (u32 k = 0; k < modes && k < ARRAY_COUNT(ratio); k++, a *= bright) {
        f32 hz = f0 * ratio[k] * rr(0.985f, 1.015f);
        f32 ak = a * rr(0.6f, 1.0f), t = tau / (1.0f + 0.55f * (f32)k) * rr(0.8f, 1.2f);
        mode(b, from, n, hz, ak, t);
        if (k < 3) mode(b, from, n, hz * rr(1.002f, 1.007f), ak * 0.5f, t * 0.9f);
    }
    u32 tn = at(0.004f);
    for (u32 i = 0; i < tn && from + i < n; i++) b[from + i] += amp * 0.5f * wn() * (1.0f - (f32)i / (f32)tn);
}

// Шум через полосу: нарастание attack, затем спад tau
static void burst(f32 *b, u32 from, u32 n, f32 hz, f32 Q, f32 amp, f32 attack, f32 tau)
{
    Svf s = { 0 };
    svf_set(&s, hz, Q);
    u32 na = at(attack) + 1;
    f32 r = expf(-1.0f / (tau * FS)), e = 1.0f;
    for (u32 i = from; i < n && e > 1e-4f; i++) {
        u32 j = i - from;
        f32 env = j < na ? (f32)j / (f32)na : (e *= r);
        b[i] += amp * env * svf_bp(&s, wn());
    }
}

// Глухой шум: дважды сглаженный
static void lpburst(f32 *b, u32 from, u32 n, f32 hz, f32 amp, f32 attack, f32 tau)
{
    f32 a = lp_a(hz), y1 = 0.0f, y2 = 0.0f;
    u32 na = at(attack) + 1;
    f32 r = expf(-1.0f / (tau * FS)), e = 1.0f;
    // Сглаживание съедает громкость: вернуть примерно к единице
    f32 g = 1.0f / sqrtf(a);
    for (u32 i = from; i < n && e > 1e-4f; i++) {
        u32 j = i - from;
        f32 env = j < na ? (f32)j / (f32)na : (e *= r);
        y1 += a * (wn() - y1);
        y2 += a * (y1 - y2);
        b[i] += amp * env * y2 * g;
    }
}

// «Тук»: синус, высота сползает с f_hi к f_lo, спад tau
static void thump(f32 *b, u32 from, u32 n, f32 f_hi, f32 f_lo, f32 amp, f32 tau)
{
    f32 ph = 0.0f;
    u32 len = at(tau * 7.0f);
    for (u32 j = 0; j < len && from + j < n; j++) {
        f32 t = (f32)j / FS;
        f32 f = f_lo + (f_hi - f_lo) * expf(-t / (tau * 0.5f));
        ph += TAU * f / FS;
        b[from + j] += amp * expf(-t / tau) * fminf(1.0f, t / 0.0015f) * sinf(ph);
    }
}

// Короткий тон с плавными краями, высота от f0 к f1
static void chirp(f32 *b, u32 from, u32 n, f32 f0, f32 f1, f32 dur, f32 amp)
{
    u32 len = at(dur);
    f32 ph = 0.0f;
    for (u32 j = 0; j < len && from + j < n; j++) {
        f32 u = (f32)j / (f32)len;
        ph += TAU * lerpf(f0, f1, u) / FS;
        f32 w = sinf(PI_F * u);
        b[from + j] += amp * w * w * sinf(ph);
    }
}

// Хруст: зёрна шума по 1–len_hi с, гуще в начале; в слой g_aux
static void grains(u32 from, u32 n, u32 count, f32 spread, f32 amp, f32 len_hi)
{
    for (u32 g = 0; g < count; g++) {
        f32 u = rnd();
        u32 t0 = from + at(spread * u * u);
        u32 len = at(rr(0.001f, len_hi)) + 1;
        f32 a = amp * rr(0.3f, 1.0f) * (1.0f - 0.6f * u);
        for (u32 j = 0; j < len && t0 + j < n; j++) {
            f32 e = 1.0f - (f32)j / (f32)len;
            g_aux[t0 + j] += a * wn() * e * e;
        }
    }
}

// Слой g_aux через срез низов hp и верхов lp — в собираемый звук; слой очищается
static void aux_add(f32 *b, u32 n, f32 lp_hz, f32 hp_hz, f32 amp)
{
    f32 al = lp_a(lp_hz), ah = lp_a(hp_hz), l1 = 0.0f, l2 = 0.0f, h = 0.0f;
    for (u32 i = 0; i < n; i++) {
        l1 += al * (g_aux[i] - l1);
        l2 += al * (l1 - l2);
        h += ah * (l2 - h);
        b[i] += amp * (l2 - h);
        g_aux[i] = 0.0f;
    }
}

// Удар по предмету в руке: не колокол, а лязг. Мод много и они плотные (ratio — свой ряд, NULL —
// случайный с шагом spread), хват гасит звон за tau, верхние моды гаснут быстрее (hf).
// grit — дребезг: звон модулирован шумом (ржавчина, неплотный хват, прутья друг о друга)
static void clank(f32 *b, u32 from, u32 n, f32 f0, const f32 *ratio, u32 count, f32 spread, f32 amp, f32 tau, f32 hf,
                  f32 grit)
{
    u32 end = MIN(n, from + at(tau * 8.0f));
    f32 a = amp, r = 1.0f;
    for (u32 k = 0; k < count; k++, a *= 0.84f) {
        if (ratio) r = ratio[k];
        else if (k) r += spread * rr(0.4f, 1.6f);
        f32 t = tau / (1.0f + hf * (r - 1.0f)) * rr(0.7f, 1.3f);
        mode(g_tmp, from, end, f0 * r * rr(0.99f, 1.01f), a * rr(0.5f, 1.0f), t);
    }
    f32 la = lp_a(700.0f), l = 0.0f, g = grit * 4.0f;
    for (u32 i = from; i < end; i++) {
        l += la * (wn() - l);
        b[i] += g_tmp[i] * (1.0f + g * l);
        g_tmp[i] = 0.0f;
    }
    // Касание: щелчок в полосе выше основной ноты
    burst(b, from, n, fminf(f0 * 4.0f, 7000.0f), 1.2f, amp * 0.6f, 0.0003f, 0.003f);
}

// ---------------------------------------------------------------- звуки

static u32 gen_step_sand(f32 *b)
{
    u32 n = at(0.24f);
    thump(b, 0, n, 110.0f, 55.0f, 0.35f, 0.022f);
    grains(0, n, 45, 0.07f, 1.0f, 0.004f);
    u32 toe = at(rr(0.05f, 0.08f));
    grains(toe, n, 25, 0.05f, 0.6f, 0.004f);
    aux_add(b, n, rr(2600.0f, 3600.0f), 250.0f, 1.0f);
    return n;
}

static u32 gen_step_concrete(f32 *b)
{
    u32 n = at(0.2f);
    burst(b, 0, n, rr(2200.0f, 3200.0f), 1.5f, 1.0f, 0.0005f, 0.006f);
    thump(b, 0, n, 170.0f, 95.0f, 0.4f, 0.016f);
    grains(0, n, 12, 0.03f, 0.35f, 0.003f);
    u32 toe = at(rr(0.06f, 0.09f));
    burst(b, toe, n, rr(1600.0f, 2200.0f), 1.2f, 0.35f, 0.001f, 0.01f);
    grains(toe, n, 8, 0.02f, 0.25f, 0.003f);
    aux_add(b, n, 6500.0f, 1400.0f, 1.0f);
    return n;
}

static u32 gen_step_grass(f32 *b)
{
    u32 n = at(0.26f);
    grains(0, n, 70, 0.14f, 0.7f, 0.008f);
    aux_add(b, n, 7000.0f, 1800.0f, 1.0f);
    thump(b, 0, n, 85.0f, 50.0f, 0.3f, 0.03f);
    return n;
}

static u32 gen_step_water(f32 *b)
{
    u32 n = at(0.42f);
    burst(b, 0, n, rr(900.0f, 1300.0f), 0.8f, 1.0f, 0.004f, 0.06f);
    burst(b, 0, n, 400.0f, 0.7f, 0.6f, 0.002f, 0.04f);
    u32 k = 6 + rng_below(&g_r, 5);
    for (u32 i = 0; i < k; i++) {
        f32 f = rr(500.0f, 1400.0f);
        chirp(b, at(rr(0.01f, 0.25f)), n, f, f * 1.6f, rr(0.008f, 0.016f), rr(0.1f, 0.3f));
    }
    return n;
}

static u32 gen_swish(f32 *b, bool heavy)
{
    u32 n = at(heavy ? rr(0.38f, 0.46f) : rr(0.26f, 0.32f));
    f32 lo = heavy ? 140.0f : rr(320.0f, 420.0f), hi = heavy ? rr(550.0f, 750.0f) : rr(1300.0f, 1800.0f);
    Svf s = { 0 };
    f32 la = lp_a(300.0f), l = 0.0f;
    for (u32 i = 0; i < n; i++) {
        f32 u = (f32)i / (f32)n;
        f32 v = powf(u, 0.7f);
        if (!(i & 31)) svf_set(&s, lo + (hi - lo) * sinf(PI_F * v), 2.2f);
        f32 e = sinf(PI_F * v);
        e *= e;
        f32 x = wn();
        b[i] += e * svf_bp(&s, x) * 1.4f;
        if (heavy) {
            l += la * (x - l);
            b[i] += e * l * 0.9f;
        }
    }
    return n;
}

// Стержень со свободными концами: изгибные моды
static const f32 g_bar[] = { 1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f };

static u32 gen_wpn_ressora(f32 *b)
{
    u32 n = at(0.7f);
    // Лист рессоры: плотный звон в 1.5–2.5 кГц, тело листа — низкая мода
    clank(b, 0, n, rr(1500.0f, 2100.0f), NULL, 9, 0.22f, 0.8f, rr(0.07f, 0.11f), 0.5f, 0.3f);
    mode(b, 0, n, rr(180.0f, 260.0f), 0.35f, 0.05f);
    burst(b, 0, n, rr(4000.0f, 5500.0f), 2.0f, 0.3f, 0.001f, 0.02f);
    return n;
}

static u32 gen_wpn_sekach(f32 *b)
{
    u32 n = at(0.5f);
    clank(b, 0, n, rr(560.0f, 820.0f), NULL, 11, 0.35f, 0.9f, rr(0.05f, 0.08f), 0.8f, 0.55f);
    thump(b, 0, n, 220.0f, 120.0f, 0.4f, 0.02f);
    burst(b, 0, n, 2400.0f, 0.9f, 0.4f, 0.0005f, 0.012f);
    return n;
}

static u32 gen_wpn_armatura(f32 *b)
{
    u32 n = at(0.45f);
    // Прутья бьются друг о друга вразнобой
    u32 rods = 3 + rng_below(&g_r, 3);
    for (u32 k = 0; k < rods; k++)
        clank(b, at(rr(0.0f, 0.022f)), n, rr(750.0f, 1500.0f), g_bar, 5, 0.0f, rr(0.3f, 0.6f), rr(0.025f, 0.05f), 0.4f, 0.3f);
    // Бетон: короткий стук и крошка
    lpburst(b, 0, n, 1700.0f, 0.8f, 0.0005f, 0.012f);
    thump(b, 0, n, 160.0f, 80.0f, 0.5f, 0.025f);
    grains(at(0.003f), n, 26, 0.05f, 0.45f, 0.003f);
    aux_add(b, n, 4500.0f, 600.0f, 1.0f);
    return n;
}

static u32 gen_wpn_bagor(f32 *b)
{
    u32 n = at(0.3f);
    // Сухое дерево: низкие моды гаснут за десятки миллисекунд
    static const f32 wood[] = { 1.0f, 2.57f, 4.9f, 7.6f };
    clank(b, 0, n, rr(360.0f, 520.0f), wood, 4, 0.0f, 0.9f, rr(0.018f, 0.03f), 1.2f, 0.1f);
    lpburst(b, 0, n, 1300.0f, 0.6f, 0.0005f, 0.008f);
    thump(b, 0, n, 180.0f, 110.0f, 0.4f, 0.02f);
    return n;
}

static u32 gen_wpn_hook(f32 *b)
{
    u32 n = at(0.35f);
    clank(b, 0, n, rr(1050.0f, 1450.0f), NULL, 7, 0.3f, 0.7f, rr(0.04f, 0.07f), 0.6f, 0.4f);
    // Крюк брякнул на древке
    clank(b, at(rr(0.03f, 0.05f)), n, rr(1600.0f, 2000.0f), NULL, 4, 0.3f, 0.2f, 0.03f, 0.6f, 0.4f);
    mode(b, 0, n, rr(380.0f, 480.0f), 0.3f, 0.02f);
    return n;
}

static u32 gen_wpn_balka(f32 *b)
{
    u32 n = at(1.0f);
    // Двутавр: низкий плотный лязг полок и стенки, тяжёлое тело
    clank(b, 0, n, rr(130.0f, 185.0f), NULL, 14, 0.45f, 0.9f, rr(0.09f, 0.13f), 0.35f, 0.55f);
    thump(b, 0, n, 110.0f, 50.0f, 0.9f, 0.05f);
    lpburst(b, 0, n, 650.0f, 0.5f, 0.0005f, 0.03f);
    return n;
}

// Лезвие скользнуло по лезвию: трение рывками (прилип, сорвался) через узкие полосы, высота сползает
static u32 gen_scrape(f32 *b)
{
    u32 n = at(rr(0.18f, 0.32f));
    Svf s[3] = { 0 };
    f32 f0 = rr(2400.0f, 3200.0f), drop = rr(0.75f, 0.9f), rate = rr(45.0f, 90.0f), ph = 0.0f;
    static const f32 mul[3] = { 1.0f, 1.47f, 2.11f };
    for (u32 i = 0; i < n; i++) {
        f32 u = (f32)i / (f32)n;
        if (!(i & 31))
            for (u32 k = 0; k < 3; k++) svf_set(&s[k], f0 * mul[k] * lerpf(1.0f, drop, u), 14.0f);
        ph += rate * rr(0.6f, 1.4f) / FS;
        f32 slip = 0.0f;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            slip = rr(0.5f, 1.0f);
        }
        f32 x = wn() * 0.35f + slip * 3.0f;
        f32 e = fminf(1.0f, u * 25.0f) * (1.0f - u) * (1.0f - u);
        b[i] += e * (svf_bp(&s[0], x) + 0.7f * svf_bp(&s[1], x) + 0.4f * svf_bp(&s[2], x));
    }
    return n;
}

static u32 gen_hit_cut(f32 *b)
{
    u32 n = at(0.35f);
    burst(b, 0, n, rr(1500.0f, 2500.0f), 1.2f, 0.8f, 0.002f, 0.03f);
    lpburst(b, 0, n, 700.0f, 0.6f, 0.002f, 0.05f);
    thump(b, 0, n, 120.0f, 60.0f, 0.7f, 0.04f);
    grains(at(0.005f), n, 20, 0.05f, 0.3f, 0.004f);
    aux_add(b, n, 5000.0f, 1200.0f, 1.0f);
    return n;
}

static u32 gen_hit_blunt(f32 *b)
{
    u32 n = at(0.45f);
    thump(b, 0, n, rr(85.0f, 105.0f), 45.0f, 1.0f, 0.07f);
    lpburst(b, 0, n, 450.0f, 0.7f, 0.001f, 0.05f);
    grains(0, n, 8, 0.02f, 0.3f, 0.003f);
    aux_add(b, n, 2500.0f, 400.0f, 1.0f);
    return n;
}

static u32 gen_hit_pierce(f32 *b)
{
    u32 n = at(0.3f);
    burst(b, 0, n, rr(1700.0f, 2100.0f), 1.5f, 0.8f, 0.0008f, 0.012f);
    thump(b, 0, n, 140.0f, 70.0f, 0.6f, 0.03f);
    lpburst(b, at(0.004f), n, 900.0f, 0.4f, 0.004f, 0.03f);
    return n;
}

static u32 gen_hit_armor(f32 *b)
{
    u32 n = at(0.5f);
    // Мятый лист на теле: тело глушит звон почти сразу
    clank(b, 0, n, rr(380.0f, 560.0f), NULL, 10, 0.35f, 0.8f, rr(0.05f, 0.08f), 0.7f, 0.6f);
    thump(b, 0, n, 110.0f, 55.0f, 0.8f, 0.05f);
    burst(b, 0, n, 1200.0f, 1.0f, 0.4f, 0.001f, 0.015f);
    return n;
}

static u32 gen_hit_chop(f32 *b)
{
    u32 n = at(0.45f);
    thump(b, 0, n, rr(95.0f, 115.0f), 50.0f, 1.0f, 0.06f);
    burst(b, 0, n, rr(1400.0f, 2000.0f), 1.0f, 0.7f, 0.001f, 0.025f);
    lpburst(b, 0, n, 600.0f, 0.8f, 0.001f, 0.05f);
    grains(at(0.01f), n, 14, 0.04f, 0.4f, 0.003f);
    aux_add(b, n, 4000.0f, 900.0f, 1.0f);
    return n;
}

static u32 gen_hit_crush(f32 *b)
{
    u32 n = at(0.7f);
    thump(b, 0, n, rr(65.0f, 80.0f), 32.0f, 1.0f, 0.1f);
    lpburst(b, 0, n, 320.0f, 1.0f, 0.001f, 0.08f);
    thump(b, at(0.03f), n, 120.0f, 60.0f, 0.4f, 0.04f);
    grains(0, n, 30, 0.07f, 0.4f, 0.004f);
    aux_add(b, n, 3000.0f, 300.0f, 1.0f);
    return n;
}

static u32 gen_crack(f32 *b)
{
    u32 n = at(0.25f);
    u32 k = 3 + rng_below(&g_r, 4);
    for (u32 i = 0; i < k; i++) burst(b, at(rr(0.0f, 0.04f)), n, rr(2000.0f, 4000.0f), 2.0f, rr(0.5f, 1.0f), 0.0002f, 0.003f);
    thump(b, 0, n, 200.0f, 100.0f, 0.4f, 0.015f);
    return n;
}

static u32 gen_bodyfall(f32 *b)
{
    u32 n = at(0.8f);
    thump(b, 0, n, 70.0f, 35.0f, 1.0f, 0.09f);
    lpburst(b, 0, n, 280.0f, 0.8f, 0.002f, 0.07f);
    u32 t2 = at(rr(0.09f, 0.15f));
    thump(b, t2, n, 90.0f, 45.0f, 0.5f, 0.05f);
    lpburst(b, t2, n, 400.0f, 0.4f, 0.002f, 0.04f);
    grains(0, n, 40, 0.35f, 0.18f, 0.005f);
    aux_add(b, n, 2500.0f, 300.0f, 1.0f);
    // Снаряжение брякнуло
    for (u32 i = 0; i < 3; i++) metal(b, at(rr(0.0f, 0.2f)), n, rr(900.0f, 1400.0f), 0.08f, 0.05f, 0.6f, 4);
    return n;
}

static u32 gen_drop_metal(f32 *b)
{
    u32 n = at(1.1f);
    f32 base = rr(550.0f, 850.0f), t = 0.0f, gap = rr(0.12f, 0.18f), a = 1.0f;
    for (u32 k = 0; k < 4; k++) {
        clank(b, at(t), n, base * rr(0.97f, 1.03f), NULL, 8, 0.3f, a, 0.12f, 0.6f, 0.35f);
        thump(b, at(t), n, 150.0f, 80.0f, a * 0.4f, 0.015f);
        t += gap;
        gap *= rr(0.6f, 0.8f);
        a *= rr(0.45f, 0.65f);
    }
    grains(at(t), n, 15, 0.1f, 0.15f, 0.004f);
    aux_add(b, n, 5000.0f, 1500.0f, 1.0f);
    return n;
}

// Скрежет клинка о ножны: шум через две узкие полосы, частоты плывут от f0 к f1
static void scrape(f32 *b, u32 n, f32 t_end, f32 f0, f32 f1, bool rise, f32 amp)
{
    Svf s1 = { 0 }, s2 = { 0 };
    u32 len = at(t_end);
    for (u32 i = 0; i < len && i < n; i++) {
        f32 u = (f32)i / (f32)len;
        if (!(i & 31)) {
            svf_set(&s1, lerpf(f0, f1, u), 12.0f);
            svf_set(&s2, lerpf(f0, f1, u) * 1.62f, 12.0f);
        }
        f32 e = rise ? u * u : 1.0f - u;
        e *= fminf(1.0f, (1.0f - u) * 12.0f) * fminf(1.0f, u * 30.0f);
        f32 x = wn();
        b[i] += amp * e * (svf_bp(&s1, x) + svf_bp(&s2, x)) * 2.5f;
    }
}

static u32 gen_draw(f32 *b)
{
    u32 n = at(0.6f);
    f32 te = rr(0.28f, 0.34f);
    scrape(b, n, te, rr(1900.0f, 2200.0f), rr(3000.0f, 3400.0f), true, 0.6f);
    clank(b, at(te), n, rr(1800.0f, 2600.0f), NULL, 6, 0.25f, 0.45f, 0.14f, 0.5f, 0.1f);
    lpburst(b, 0, n, 1200.0f, 0.2f, 0.01f, 0.04f);
    return n;
}

static u32 gen_sheath(f32 *b)
{
    u32 n = at(0.5f);
    f32 te = rr(0.26f, 0.3f);
    scrape(b, n, te, rr(3300.0f, 3700.0f), rr(2100.0f, 2400.0f), false, 0.45f);
    thump(b, at(te), n, 220.0f, 120.0f, 0.6f, 0.015f);
    metal(b, at(te), n, rr(400.0f, 600.0f), 0.3f, 0.06f, 0.5f, 4);
    burst(b, at(te), n, 1500.0f, 1.0f, 0.3f, 0.0005f, 0.01f);
    return n;
}

static u32 gen_scuff(f32 *b)
{
    u32 n = at(0.3f);
    grains(0, n, 30, 0.12f, 0.6f, 0.006f);
    aux_add(b, n, 2000.0f, 200.0f, 1.0f);
    // Ткань: шум с дрожью 25–40 Гц
    f32 fl = rr(25.0f, 40.0f);
    u32 len = at(0.2f);
    for (u32 i = 0; i < len; i++) {
        f32 u = (f32)i / (f32)len, e = sinf(PI_F * u);
        g_aux[i] = wn() * e * (0.6f + 0.4f * sinf(TAU * fl * (f32)i / FS));
    }
    aux_add(b, n, 6000.0f, 1500.0f, 0.25f);
    return n;
}

static u32 gen_ui_tick(f32 *b)
{
    u32 n = at(0.03f);
    for (u32 i = 0; i < n; i++) {
        f32 t = (f32)i / FS, e = expf(-t / 0.004f);
        b[i] += e * (sinf(TAU * 1900.0f * t) + 0.3f * sinf(TAU * 5700.0f * t)) + 0.1f * e * wn();
    }
    return n;
}

static u32 gen_ui_order(f32 *b)
{
    u32 n = at(0.12f);
    static const f32 f[2] = { 1250.0f, 940.0f }, t0[2] = { 0.0f, 0.045f };
    for (u32 k = 0; k < 2; k++)
        for (u32 j = 0; j < at(0.03f); j++) {
            u32 i = at(t0[k]) + j;
            if (i >= n) break;
            f32 t = (f32)j / FS, e = fminf(1.0f, t / 0.002f) * expf(-t / 0.012f);
            b[i] += e * (sinf(TAU * f[k] * t) + 0.25f * sinf(TAU * 3.0f * f[k] * t)) + 0.08f * e * wn();
        }
    return n;
}

static u32 gen_far_groan(f32 *b)
{
    u32 n = at(rr(4.0f, 4.8f));
    f32 f0 = rr(38.0f, 70.0f), ph = 0.0f, wob = rr(0.2f, 0.5f);
    Svf s = { 0 };
    for (u32 i = 0; i < n; i++) {
        f32 t = (f32)i / FS, u = (f32)i / (f32)n;
        f32 f = f0 * (1.0f - 0.12f * u + 0.03f * sinf(TAU * wob * t));
        ph += TAU * f / FS;
        if (ph > TAU) ph -= TAU;
        // Гармоники sin(kφ) рекуррентно: sin(kφ) = 2cosφ·sin((k-1)φ) − sin((k-2)φ)
        f32 c2 = 2.0f * cosf(ph), s0 = 0.0f, s1 = sinf(ph), x = s1;
        // Гармоники гаснут быстро (1/k^1.5) и полоса почти стоит: иначе стон рычал, как мотор на подъёме
        for (u32 k = 2; k <= 8; k++) {
            f32 sk = c2 * s1 - s0;
            s0 = s1;
            s1 = sk;
            x += sk / ((f32)k * sqrtf((f32)k));
        }
        if (!(i & 63)) svf_set(&s, 150.0f + 90.0f * sinf(PI_F * u), 3.0f);
        f32 a = u < 0.35f ? u / 0.35f : 1.0f - (u - 0.35f) / 0.65f;
        b[i] += 0.6f * a * a * svf_bp(&s, x) * 2.0f;
    }
    // Скрип: металл цепляется и срывается — щелчки с плывущим темпом через узкий резонатор
    Svf r = { 0 };
    svf_set(&r, rr(600.0f, 1100.0f), 18.0f);
    f32 rate = rr(18.0f, 40.0f), next = rr(0.8f, 1.2f), end = rr(2.4f, 3.2f);
    for (u32 i = at(next); i < n; i++) {
        f32 t = (f32)i / FS, imp = 0.0f;
        if (t >= next && t < end) {
            imp = rr(0.5f, 1.0f);
            rate *= rr(0.93f, 1.08f);
            rate = clampf(rate, 10.0f, 60.0f);
            next = t + 1.0f / rate;
        }
        b[i] += 0.5f * svf_bp(&r, imp) * 6.0f;
    }
    return n;
}

static u32 gen_far_bang(f32 *b)
{
    u32 n = at(3.0f);
    thump(b, 0, n, 55.0f, 28.0f, 1.0f, 0.35f);
    lpburst(b, 0, n, 200.0f, 0.8f, 0.003f, 0.3f);
    metal(b, 0, n, rr(90.0f, 150.0f), 0.7f, 1.3f, 0.7f, 8);
    lpburst(b, at(0.25f), n, 120.0f, 0.4f, 0.05f, 0.6f);
    return n;
}

// Обрывок радио: треск и шум эфира, вспышки тона — знаки, смысл которых утерян
static u32 gen_far_radio(f32 *b)
{
    u32 n = at(3.5f);
    Svf s = { 0 }, tone = { 0 };
    svf_set(&s, 1800.0f, 0.8f);
    f32 fade = rr(0.3f, 0.8f), ft = rr(700.0f, 1500.0f), ph = 0.0f, wh = 0.0f;
    svf_set(&tone, ft, 3.0f);
    // Знаки: длинные и короткие посылки
    f32 sym_t = rr(0.3f, 0.6f), sym_end = sym_t;
    bool on = false;
    for (u32 i = 0; i < n; i++) {
        f32 t = (f32)i / FS, u = (f32)i / (f32)n;
        if (t >= sym_end && t < 2.8f) {
            on = !on && rnd() < 0.8f;
            sym_end = t + (on ? (rnd() < 0.4f ? 0.18f : 0.06f) : rr(0.05f, 0.2f));
        } else if (t >= 2.8f) {
            on = false;
        }
        f32 env = fminf(1.0f, t / 0.3f) * fminf(1.0f, (1.0f - u) * 7.0f);
        f32 am = 0.55f + 0.45f * sinf(TAU * fade * t);
        f32 x = svf_bp(&s, wn()) * 0.35f * am;
        if (rnd() < 0.0015f) x += rr(-1.0f, 1.0f);
        ph += TAU * ft * (1.0f + 0.004f * sinf(TAU * 5.0f * t)) / FS;
        if (ph > TAU) ph -= TAU;
        x += on ? 0.25f * svf_bp(&tone, sinf(ph)) * 1.5f : 0.0f;
        wh += TAU * lerpf(2500.0f, 1200.0f, u) / FS;
        if (wh > TAU) wh -= TAU;
        x += 0.05f * sinf(wh);
        b[i] += env * x;
    }
    return n;
}

// Лист железа треплет ветер: серия лязгов вразнобой, то чаще, то реже
static u32 gen_wind_rattle(f32 *b)
{
    u32 n = at(rr(0.8f, 1.5f));
    f32 f0 = rr(280.0f, 480.0f), t = 0.0f, a = rr(0.5f, 0.8f);
    while (t < (f32)n / FS - 0.25f) {
        clank(b, at(t), n, f0 * rr(0.95f, 1.05f), NULL, 8, 0.4f, a, rr(0.03f, 0.06f), 0.7f, 0.8f);
        t += rnd() < 0.3f ? rr(0.02f, 0.05f) : rr(0.07f, 0.2f);
        a = clampf(a * rr(0.7f, 1.3f), 0.2f, 1.0f);
    }
    return n;
}

// Скрип металла на ветру: трение рывками, частота рывков плывёт; два резонанса — петля и лист
static u32 gen_wind_creak(f32 *b)
{
    u32 n = at(rr(0.7f, 1.3f));
    Svf r1 = { 0 }, r2 = { 0 };
    svf_set(&r1, rr(700.0f, 1000.0f), 6.0f);
    svf_set(&r2, rr(1600.0f, 2200.0f), 9.0f);
    f32 f0 = rr(55.0f, 80.0f), f1 = f0 * rr(1.3f, 1.9f), ph = 0.0f;
    for (u32 i = 0; i < n; i++) {
        f32 u = (f32)i / (f32)n;
        ph += lerpf(f0, f1, sinf(PI_F * 0.5f * u)) * rr(0.85f, 1.15f) / FS;
        f32 imp = 0.0f;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            imp = rr(0.6f, 1.0f);
        }
        f32 e = sinf(PI_F * u);
        b[i] += e * (svf_bp(&r1, imp) * 5.0f + svf_bp(&r2, imp) * 3.0f);
    }
    return n;
}

// ---------------------------------------------------------------- удары вещей по материалу

static u32 gen_impact_metal(f32 *b)
{
    u32 n = at(0.6f);
    // Один удар: лязг гаснет быстро, вещь не в руке, но и не колокол — дребезг и глухой низ
    clank(b, 0, n, rr(480.0f, 900.0f), NULL, 8, 0.3f, 0.9f, rr(0.07f, 0.1f), 0.7f, 0.4f);
    thump(b, 0, n, 160.0f, 85.0f, 0.45f, 0.014f);
    grains(0, n, 10, 0.05f, 0.15f, 0.003f);
    aux_add(b, n, 5000.0f, 1500.0f, 1.0f);
    return n;
}

static u32 gen_impact_wood(f32 *b)
{
    u32 n = at(0.3f);
    // Сухое дерево ниже и глуше древка багра
    static const f32 wood[] = { 1.0f, 2.57f, 4.9f, 7.6f };
    clank(b, 0, n, rr(230.0f, 380.0f), wood, 4, 0.0f, 0.8f, rr(0.015f, 0.025f), 1.4f, 0.15f);
    lpburst(b, 0, n, 1000.0f, 0.5f, 0.0005f, 0.007f);
    thump(b, 0, n, 150.0f, 85.0f, 0.5f, 0.02f);
    return n;
}

static u32 gen_impact_stone(f32 *b)
{
    u32 n = at(0.35f);
    // Тяжёлый тупой стук без звона, сверху сухой щелчок и крошка
    thump(b, 0, n, 130.0f, 60.0f, 0.8f, 0.028f);
    burst(b, 0, n, rr(1100.0f, 1700.0f), 1.0f, 0.6f, 0.0004f, 0.01f);
    lpburst(b, 0, n, 600.0f, 0.5f, 0.001f, 0.025f);
    grains(at(0.01f), n, 30, 0.12f, 0.35f, 0.004f);
    aux_add(b, n, 4500.0f, 900.0f, 1.0f);
    return n;
}

static u32 gen_impact_soft(f32 *b)
{
    u32 n = at(0.3f);
    // Свёрток, ткань, кожа: шлепок без высоты
    thump(b, 0, n, 85.0f, 45.0f, 0.6f, 0.035f);
    lpburst(b, 0, n, 380.0f, 0.8f, 0.002f, 0.035f);
    grains(0, n, 25, 0.08f, 0.2f, 0.004f);
    aux_add(b, n, 2200.0f, 250.0f, 1.0f);
    return n;
}

// ---------------------------------------------------------------- сборка

static u32 gen(u8 sfx, f32 *b)
{
    switch (sfx) {
    case SFX_STEP_SAND:     return gen_step_sand(b);
    case SFX_STEP_CONCRETE: return gen_step_concrete(b);
    case SFX_STEP_GRASS:    return gen_step_grass(b);
    case SFX_STEP_WATER:    return gen_step_water(b);
    case SFX_SWISH:         return gen_swish(b, false);
    case SFX_SWISH_HEAVY:   return gen_swish(b, true);
    case SFX_WPN_RESSORA:   return gen_wpn_ressora(b);
    case SFX_WPN_SEKACH:    return gen_wpn_sekach(b);
    case SFX_WPN_ARMATURA:  return gen_wpn_armatura(b);
    case SFX_WPN_BAGOR:     return gen_wpn_bagor(b);
    case SFX_WPN_HOOK:      return gen_wpn_hook(b);
    case SFX_WPN_BALKA:     return gen_wpn_balka(b);
    case SFX_SCRAPE:        return gen_scrape(b);
    case SFX_HIT_CUT:       return gen_hit_cut(b);
    case SFX_HIT_BLUNT:     return gen_hit_blunt(b);
    case SFX_HIT_PIERCE:    return gen_hit_pierce(b);
    case SFX_HIT_CHOP:      return gen_hit_chop(b);
    case SFX_HIT_CRUSH:     return gen_hit_crush(b);
    case SFX_HIT_ARMOR:     return gen_hit_armor(b);
    case SFX_CRACK:         return gen_crack(b);
    case SFX_BODYFALL:      return gen_bodyfall(b);
    case SFX_DROP_METAL:    return gen_drop_metal(b);
    case SFX_DRAW:          return gen_draw(b);
    case SFX_SHEATH:        return gen_sheath(b);
    case SFX_SCUFF:         return gen_scuff(b);
    case SFX_UI_TICK:       return gen_ui_tick(b);
    case SFX_UI_ORDER:      return gen_ui_order(b);
    case SFX_FAR_GROAN:     return gen_far_groan(b);
    case SFX_FAR_BANG:      return gen_far_bang(b);
    case SFX_FAR_RADIO:     return gen_far_radio(b);
    case SFX_WIND_RATTLE:   return gen_wind_rattle(b);
    case SFX_WIND_CREAK:    return gen_wind_creak(b);
    case SFX_IMPACT_METAL:  return gen_impact_metal(b);
    case SFX_IMPACT_WOOD:   return gen_impact_wood(b);
    case SFX_IMPACT_STONE:  return gen_impact_stone(b);
    case SFX_IMPACT_SOFT:   return gen_impact_soft(b);
    default:                return 0;
    }
}

// Вариантов на звук: шагов больше всего — они повторяются чаще остальных
static const u8 g_variants[SFX_COUNT] = {
    [SFX_STEP_SAND] = 6, [SFX_STEP_CONCRETE] = 6, [SFX_STEP_GRASS] = 6, [SFX_STEP_WATER] = 4,
    [SFX_SWISH] = 3, [SFX_SWISH_HEAVY] = 3,
    [SFX_WPN_RESSORA] = 5, [SFX_WPN_SEKACH] = 5, [SFX_WPN_ARMATURA] = 5, [SFX_WPN_BAGOR] = 4, [SFX_WPN_HOOK] = 4,
    [SFX_WPN_BALKA] = 4, [SFX_SCRAPE] = 4,
    [SFX_HIT_CUT] = 4, [SFX_HIT_BLUNT] = 4, [SFX_HIT_PIERCE] = 3, [SFX_HIT_CHOP] = 4, [SFX_HIT_CRUSH] = 3,
    [SFX_HIT_ARMOR] = 4, [SFX_CRACK] = 2,
    [SFX_BODYFALL] = 3, [SFX_DROP_METAL] = 3, [SFX_DRAW] = 3, [SFX_SHEATH] = 2, [SFX_SCUFF] = 3,
    [SFX_UI_TICK] = 1, [SFX_UI_ORDER] = 1, [SFX_FAR_GROAN] = 3, [SFX_FAR_BANG] = 3, [SFX_FAR_RADIO] = 2,
    [SFX_WIND_RATTLE] = 4, [SFX_WIND_CREAK] = 4,
    [SFX_IMPACT_METAL] = 4, [SFX_IMPACT_WOOD] = 3, [SFX_IMPACT_STONE] = 4, [SFX_IMPACT_SOFT] = 3,
};

bool synth_build(SfxBank *bank, Arena *perm)
{
    memset(bank, 0, sizeof *bank);
    u32 csr = AUDIO_CSR_GET();
    AUDIO_FLUSH_DENORMALS();
    rng_seed(&g_r, 0x5EED50u, 3);
    for (u32 s = 0; s < SFX_COUNT; s++) {
        bank->count[s] = MIN(g_variants[s], SFX_VARIANTS);
        for (u32 v = 0; v < bank->count[s]; v++) {
            u32 n = gen((u8)s, g_buf);
            if (!n) {
                AUDIO_CSR_SET(csr);
                return false;
            }
            // Края без щелчка, пик к 0.95
            u32 fade = MIN(n, at(0.005f));
            for (u32 i = 0; i < fade; i++) g_buf[n - 1 - i] *= (f32)i / (f32)fade;
            f32 peak = 1e-6f;
            for (u32 i = 0; i < n; i++) peak = fmaxf(peak, fabsf(g_buf[i]));
            f32 k = 0.95f * 32767.0f / peak;
            i16 *d = arena_push(perm, (size_t)n * sizeof *d, _Alignof(i16));
            if (!d) {
                AUDIO_CSR_SET(csr);
                return false;
            }
            for (u32 i = 0; i < n; i++) d[i] = (i16)(g_buf[i] * k);
            bank->data[s][v] = d;
            bank->len[s][v] = n;
            bank->bytes += (size_t)n * sizeof *d;
            memset(g_buf, 0, (size_t)n * sizeof *g_buf);
        }
    }
    AUDIO_CSR_SET(csr);
    return true;
}
