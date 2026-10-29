// Интерфейс игры в духе Kenshi, оформление в духе Peripeteia (панели старых машин).
// Внизу слева — выбранный: портрет, схема тела в Т-позе с ранами, режим шага.
// Внизу — отряд миниатюрами. Внизу справа — кнопки окон. Вверху справа — сутки и скорость.
// Окна: инвентарь (I) и персонаж (P). Чужих (бойцов арены) можно осмотреть, но не трогать вещи.
#ifndef ANA_HUD_H
#define ANA_HUD_H

#include "../platform/platform.h"
#include "arena_view.h"
#include "squad.h"

typedef enum LookKind { LOOK_NONE, LOOK_SQUAD, LOOK_ARENA } LookKind;

// Ячейки атласа портретов
#define HUD_PORTRAIT_FOCUS 8        // выбранный: в панели и в окне персонажа
#define HUD_PORTRAIT_FOCUS_PX 80
#define HUD_PORTRAIT_CARD_PX 54     // миниатюры отряда: ячейка = номер в отряде

typedef struct Hud {
    bool lmb_was;
    bool dragging, drag_live;   // нажата ЛКМ в мире; рамка уже тянется
    f32  drag_x, drag_y;        // начало рамки, логические координаты
    bool win_inv, win_char;     // окна инвентаря и персонажа
    u8   look_kind;             // кого показывает панель выбранного и окна
    u32  look;                  // номер в отряде или в ростере арены
    Item held;                  // вещь на курсоре
    f32  last_click_t;
    i32  last_click_card;
    i32  focus;                 // запрос камере: навестись на члена отряда, -1 — нет
    u8   speed, speed_before;   // 0 пауза, 1..3 — множитель времени
    u8   win_drag;              // окно тянут за шапку: 1 инвентарь, 2 персонаж
    f32  grab_x, grab_y;        // где взяли относительно угла окна
    f32  inv_x, inv_y;          // углы окон; < 0 — место по умолчанию
    f32  char_x, char_y;
} Hud;

// Что интерфейс видит в мире на этом кадре
typedef struct HudWorld {
    Squad              *squad;
    const ArenaSession *arena;
    const ArenaView    *aview;
    M4                  view_proj;
    f32                 time;       // реальное время, с
    f32                 tod;        // время суток 0..1
    u32                 day;
} HudWorld;

void hud_init(Hud *h);
// Между ui_begin и отрисовкой кадра. true — мышь над интерфейсом или занята им: ПКМ не приказ
bool hud_input(Hud *h, const Input *in, HudWorld *w);
// После render_frame и портретов, перед ui_end
void hud_draw(const Hud *h, const Input *in, const HudWorld *w);
// Показать персонажа в панели выбранного (для снимков и горячих клавиш)
void hud_look(Hud *h, u8 kind, u32 index);
bool hud_window_open(const Hud *h);
// Боец арены по номеру в ростере: номер в текущем бою или -1, позиция на ринге
i32  hud_arena_fighter(const ArenaView *v, const ArenaSession *s, u32 roster);
bool hud_arena_pos(const ArenaView *v, const ArenaSession *s, u32 roster, V3 *out);

#endif
