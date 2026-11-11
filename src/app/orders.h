// Приказы игрока в окне (docs/ui.md «Управление»): мышь и клавиши выбора, приказы из командной строки (--go,
// --attack, --select) — всё в намерения до ближайшего шага. Окно (run_window.cpp) зовёт раз за кадр
#pragma once
#include "app.h"
#include "../sim/intent.h"

struct Input;
struct Camera;
struct Extract;
struct PeopleView;
struct WorldInspect;

constexpr u32 ORDERS_MAX = 32;

// Приказы командной строки по кадрам: «кадр место x z y аллюр» (--go) и «кадр место цель» (--attack), через «;»
struct WindowOrders {
    struct Go {
        u64 frame;
        u32 slot, gait;
        f32 x, z, y;
    } go[ORDERS_MAX];
    u32 ngo = 0;
    struct Attack {
        u64 frame;
        u32 slot, foe;
    } atk[ORDERS_MAX];
    u32 natk = 0;
};
void orders_parse(const Args &a, WindowOrders &o);

// Что видит кадр приказов: мир, последняя выписка, камера и тела людей, размер окна
struct OrdersFrame {
    const World *w;
    const Extract *seen;
    Camera *cam;
    PeopleView *pv;
    WorldInspect *inspect;
    i32 ww, wh;
    u64 frame;
    bool inspecting;  // вкладка «Мир» панели: левая кнопка выбирает строку точки, а не приказывает
};
// Мышь (in — для координат, win — кнопки и клавиши миру), C — камера за выбранным, R — отпустить точки,
// приказы командной строки этого кадра → pending
void orders_frame(const Args &a, const WindowOrders &o, const OrdersFrame &f, const Input &in, const Input &win,
                  FixedArray<Intent> &pending);
