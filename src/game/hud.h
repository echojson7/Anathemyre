// Интерфейс игры в духе Kenshi: выбор своих кликом и рамкой, панель отряда внизу,
// окно осмотра (тело по частям, навыки, 9 слотов, оружие, инвентарь-сетка).
// Чужих (бойцов арены) можно осмотреть, но не трогать их вещи.
#ifndef ANA_HUD_H
#define ANA_HUD_H

#include "../platform/platform.h"
#include "arena_view.h"
#include "squad.h"

typedef enum LookKind { LOOK_NONE, LOOK_SQUAD, LOOK_ARENA } LookKind;

typedef struct Hud {
    bool lmb_was, rmb_was;
    bool dragging, drag_live;   // нажата ЛКМ в мире; рамка уже тянется
    f32  drag_x, drag_y;        // начало рамки, логические координаты
    bool inspect;               // окно осмотра открыто
    u8   look_kind;
    u32  look;                  // номер в отряде или в ростере арены
    Item held;                  // вещь на курсоре
    f32  last_click_t;
    i32  last_click_card;
    i32  focus;                 // запрос камере: навестись на члена отряда, -1 — нет
} Hud;

// Что интерфейс видит в мире на этом кадре
typedef struct HudWorld {
    Squad              *squad;
    const ArenaSession *arena;
    const ArenaView    *aview;
    M4                  view_proj;
    f32                 time;
} HudWorld;

void hud_init(Hud *h);
// Между ui_begin и отрисовкой кадра. true — мышь над интерфейсом или занята им: ПКМ не приказ
bool hud_input(Hud *h, const Input *in, HudWorld *w);
// После render_frame, перед ui_end
void hud_draw(const Hud *h, const Input *in, const HudWorld *w);
// Открыть осмотр (для снимков и горячих клавиш)
void hud_open_inspect(Hud *h, u8 kind, u32 index);
// Где стоит боец арены по номеру в ростере; false — сейчас не на ринге
bool hud_arena_pos(const ArenaView *v, const ArenaSession *s, u32 roster, V3 *out);

#endif
