// Одежда людей (docs/wear.md): набор вещей от зерна человека. Вещь — своя сетка wear_* на костях тела людей
// (tools/humanconv/wear.py): у человека с одеждой своя копия каждой вещи, скиннинг берёт ту же палитру костей и
// те же веса морфов, что у тела. Тело под вещью не рисуется (маска областей тела). Знает об одежде только вид:
// шаг мира и повтор её не видят
#pragma once
#include "appear.h"

struct ViewAssets;

enum WearKind : u32 {
    WK_TROUSERS, WK_HAKAMA, WK_SHIRT, WK_JACKET, WK_COAT, WK_VEST, WK_SASH, WK_BELT, WK_WRAPS_LEG, WK_WRAPS_ARM,
    WK_BOOTS, WK_HOOD, WK_PAULDRON, WK_GLOVES, WK_COUNT
};
extern const char *const WEAR_NAMES[WK_COUNT];
constexpr u32 WEAR_MAX_ITEMS = 10;  // вещей на человеке

// Место вещи в ViewAssets::wear (−1 — сетки нет или она не сходится с телом)
struct WearBind {
    i16 at[WK_COUNT];
};
void wear_bind(WearBind &b, const ViewAssets &a, const ViewBody &human);

struct Outfit {
    u32 count;
    u8 kind[WEAR_MAX_ITEMS];   // снизу вверх по слоям
    u32 seed[WEAR_MAX_ITEMS];  // зерно экземпляра вещи: младший байт — тон ткани, следующий — износ (cloth.glsl)
    u32 hide;                  // надетые вещи (биты WearKind): закрытое ими у тела и нижних вещей не рисуется
    f32 hip, back;             // толщина одежды у мест ношения оружия, м
};
Outfit wear_make(u32 seed, const Appear &a, const WearBind &b, const ViewAssets &assets);

// Копии сеток вещей (скиннинг пишет в копию): запас на вид вещи, свободная копия берётся раньше новой
constexpr u32 WEAR_COPIES_MAX = 48;  // копий одного вида вещи (= людей с телом в кадре)
struct WearCopies {
    u32 mesh[WK_COUNT][WEAR_COPIES_MAX];
    bool used[WK_COUNT][WEAR_COPIES_MAX];
    u32 count[WK_COUNT];
};
constexpr u8 WEAR_NO_COPY = 0xFF;  // копии нет (не хватило места) — вещь не надета
void wear_take(WearCopies &c, const Outfit &o, u8 copy[WEAR_MAX_ITEMS], const ViewAssets &assets, const WearBind &b,
               u32 skin);
void wear_release(WearCopies &c, Outfit &o, const u8 copy[WEAR_MAX_ITEMS]);
