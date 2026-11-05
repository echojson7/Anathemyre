#include "debug.h"
#include "../core/log.h"

void debug_keys(const Input &in, DebugState &d) {
    if (in.key_pressed[KEY_PERIOD]) d.step_once = true;
    if (in.key_pressed[KEY_L]) d.lines = !d.lines;
    if (in.key_pressed[KEY_F1]) debug_print_help();
}

void autoplay_intents(u64 step, Rng &rng, FixedArray<Intent> &out) {
    // Ритм нарочно неровный: намерения попадают на разные фазы шага
    auto at = [&rng]() {
        WPos p;
        p.x = rng_range(rng, -60.0f, 60.0f);
        p.z = rng_range(rng, -60.0f, 60.0f);
        wpos_rebase(p);
        return p;
    };
    if (step % 97 == 13) out.push_back({step, INTENT_ATTRACT, at()});
    if (step % 331 == 200) out.push_back({step, INTENT_BURST, at()});
    if (step % 500 == 499) out.push_back({step, INTENT_RELEASE, WPos{}});
}

void debug_print_help() {
    log_info("клавиши: Space пауза, . шаг на паузе, - = медленнее/быстрее, L отрезки, F11 весь экран, Esc выход");
    log_info("мышь: левая — притянуть точки, правая — вспышка, средняя — отпустить, колесо — масштаб");
}
