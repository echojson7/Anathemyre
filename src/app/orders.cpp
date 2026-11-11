// Приказы игрока в окне: выбор человека, «идти», «атаковать», притяжение точек и приказы командной строки
#include "orders.h"
#include "../core/log.h"
#include "../debug/debug.h"
#include "../platform/platform.h"
#include "../sim/ground.h"
#include "../sim/world.h"
#include "../view/people.h"
#include "../view/view.h"
#include <stdio.h>
#include <string.h>

void orders_parse(const Args &a, WindowOrders &o) {
    for (const char *q = a.attack; q && *q && o.natk < ORDERS_MAX;) {
        unsigned long long f = 0;
        unsigned sl = 0, fo = 0;
        if (sscanf(q, " %llu %u %u", &f, &sl, &fo) == 3) o.atk[o.natk++] = {(u64)f, sl, fo};
        else log_warn("--attack: непонятный приказ %s", q);
        const char *e = strchr(q, ';');
        q = e ? e + 1 : nullptr;
    }
    for (const char *q = a.go; q && *q && o.ngo < ORDERS_MAX;) {
        unsigned long long f = 0;
        unsigned sl = 0, g = 0;
        float x = 0, z = 0, y = 0;
        if (sscanf(q, " %llu %u %f %f %f %u", &f, &sl, &x, &z, &y, &g) == 6) o.go[o.ngo++] = {(u64)f, sl, g, x, z, y};
        else log_warn("--go: непонятный приказ %s", q);
        const char *e = strchr(q, ';');
        q = e ? e + 1 : nullptr;
    }
}

// Номер человека с поколением для намерения
static u64 person_id(const Extract &seen, u32 slot) { return (u64)slot | ((u64)seen.person_gen[slot] << 16); }

void orders_frame(const Args &a, const WindowOrders &o, const OrdersFrame &f, const Input &in, const Input &win,
                  FixedArray<Intent> &pending) {
    const World &w = *f.w;
    const Extract &seen = *f.seen;
    Camera &cam = *f.cam;
    PeopleView &pv = *f.pv;
    // Мышь (docs/ui.md, «Управление»): левая — выбрать человека; мимо — снять выбор и притянуть точки (с Ctrl —
    // вспышка). Правая — приказ выбранному «идти» (Shift — бег, Ctrl — трусца), по другому человеку — «атаковать»,
    // Alt + правая — вращать камеру. C — камера за выбранным. R — отпустить точки
    WPos at;
    if (win.mouse_pressed[MOUSE_LEFT]) {
        i32 hit = f.inspecting ? -1 : people_view_pick(pv, seen, cam, f.ww, f.wh, in.mouse_x, in.mouse_y);
        if (hit >= 0) {
            pv.sel = hit;
            pv.sel_gen = seen.person_gen[hit];
        } else if (view_screen_to_ground(cam, f.ww, f.wh, in.mouse_x, in.mouse_y, w.terrain, at)) {
            if (f.inspecting) debug_inspect_pick(w, *f.inspect, at);
            else {
                pv.sel = -1;
                cam.follow = false;
                pending.push_back({0, win.key_down[KEY_CTRL] ? (u32)INTENT_BURST : (u32)INTENT_ATTRACT, at, 0});
            }
        }
    }
    auto order_go = [&](u32 slot, u16 gen, const WPos &to, u32 gait) {
        pending.push_back({0, INTENT_GO, to, (u64)slot | ((u64)gen << 16) | ((u64)gait << 32)});
        if ((i32)slot == pv.sel) {
            pv.mark_on = true;
            pv.mark = to;
            pv.mark_fade = 1.0f;
            pv.mark_step = w.step;
        }
    };
    const bool right = win.mouse_pressed[MOUSE_RIGHT] && !win.key_down[KEY_ALT] && pv.sel >= 0;
    i32 foe = right ? people_view_pick(pv, seen, cam, f.ww, f.wh, in.mouse_x, in.mouse_y) : -1;
    if (foe >= 0 && foe != pv.sel) {
        const u64 me = (u64)(u32)pv.sel | ((u64)pv.sel_gen << 16);  // поколение выбора: умер — приказ пропадёт
        pending.push_back({0, INTENT_ATTACK, WPos{}, me | (person_id(seen, (u32)foe) << 32)});
    } else if (right && view_screen_to_support(cam, f.ww, f.wh, in.mouse_x, in.mouse_y, w.terrain, &w.coll, at)) {
        order_go((u32)pv.sel, pv.sel_gen, at,
                 win.key_down[KEY_SHIFT] ? GAIT_RUN : win.key_down[KEY_CTRL] ? GAIT_JOG : GAIT_WALK);
    }
    if (win.key_pressed[KEY_C] && pv.sel >= 0) cam.follow = !cam.follow;

    // Командная строка (видео, проверка повтора): выбор в первые кадры, приказы — в свой кадр
    if (a.select >= 0 && pv.sel < 0 && seen.person_gen[a.select] && f.frame < 4) {
        pv.sel = a.select;
        pv.sel_gen = seen.person_gen[a.select];
        cam.follow = a.follow;
    }
    for (u32 i = 0; i < o.ngo; ++i) {
        const WindowOrders::Go &g = o.go[i];
        if (g.frame != f.frame || g.slot >= PEOPLE_CAPACITY || !seen.person_gen[g.slot]) continue;
        WPos to;
        to.x = g.x;
        to.z = g.z;
        wpos_rebase(to);
        to.y = ground_at(w.terrain, to).height + g.y;  // y — над землёй (плита, пол здания)
        order_go(g.slot, seen.person_gen[g.slot], to, g.gait);
    }
    for (u32 i = 0; i < o.natk; ++i) {
        const WindowOrders::Attack &k = o.atk[i];
        if (k.frame != f.frame || k.slot >= PEOPLE_CAPACITY || k.foe >= PEOPLE_CAPACITY || !seen.person_gen[k.slot] ||
            !seen.person_gen[k.foe])
            continue;
        pending.push_back({0, INTENT_ATTACK, WPos{}, person_id(seen, k.slot) | (person_id(seen, k.foe) << 32)});
    }
    if (win.key_pressed[KEY_R]) pending.push_back({0, INTENT_RELEASE, WPos{}, 0});
}
