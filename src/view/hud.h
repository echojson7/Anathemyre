// Интерфейс игры в духе Kenshi, оформление в духе Peripeteia (панели старых машин).
// Внизу слева — выбранный: портрет, схема тела в Т-позе с ранами, режим шага.
// Внизу — отряд миниатюрами. Внизу справа — кнопки окон. Вверху справа — сутки и скорость.
// Окна: инвентарь (I), персонаж (P), внешность (K). Чужих (бойцов арены) можно осмотреть, но не трогать вещи.
#ifndef ANA_HUD_H
#define ANA_HUD_H

#include "../platform/platform.h"
#include "../sim/arena_ring.h"
#include "../sim/ground.h"
#include "../sim/field.h"
#include "../sim/squad.h"

// LOOK_FIELD — чужой на поле (бродящий, моб): номер слота поля
typedef enum LookKind { LOOK_NONE, LOOK_SQUAD, LOOK_ARENA, LOOK_FIELD } LookKind;

// Ячейки атласа портретов
#define HUD_PORTRAIT_FOCUS 8        // выбранный: в панели и в окне персонажа
#define HUD_PORTRAIT_FOCUS_PX 80
#define HUD_PORTRAIT_CARD_PX 54     // миниатюры отряда: ячейка = номер в отряде

// Окно со строками виджетов (отладочное меню, редактор внешности): виджет рисуется и читает мышь в одном вызове
typedef struct HudForm {
    bool open, drag, over;
    f32  x, y, w, grab_x, grab_y;
    f32  mx, my, row;      // мышь в слое окон; низ последней строки
    f32  h;                // высота окна по прошлому кадру
    bool lmb, down;
    i32  slider, id;       // какой ползунок тянут; номер текущего виджета
} HudForm;

typedef struct Hud {
    bool lmb_was;
    bool dragging, drag_live;   // нажата ЛКМ в мире; рамка уже тянется
    f32  drag_x, drag_y;        // начало рамки, логические координаты
    bool win_inv, win_char;     // окна инвентаря и персонажа
    u8   look_kind;             // кого показывает панель выбранного и окна
    u32  look;                  // номер в отряде или в ростере арены
    ItemStack held;                  // вещь на курсоре
    f32  last_click_t;
    i32  last_click_card;
    i32  focus;                 // запрос камере: навестись на члена отряда, -1 — нет
    u8   speed, speed_before;   // 0 пауза, 1..3 — скорость времени g_speed_scale
    u8   win_drag;              // окно тянут за шапку: 1 инвентарь, 2 персонаж
    f32  grab_x, grab_y;        // где взяли относительно угла окна
    f32  inv_x, inv_y;          // углы окон; < 0 — место по умолчанию
    f32  char_x, char_y;
    bool win_look;              // окно ВНЕШНОСТЬ (K): редактор, debug/editor.c
    bool can_look;              // редактор есть в сборке (без отладки окна нет)
    // Отладочное меню (кнопка слева вверху)
    struct {
        HudForm f;
        bool journal;          // журнал справа от меню
        bool fps_on;           // счётчик кадров сверху по центру
        u8   tab;              // раздел меню: 0 ОТЛАДКА, 1 СВЕТ
        f32  fps, fps_ms;      // что показывает счётчик
    } dbg;
} Hud;

// Что интерфейс видит в мире на этом кадре
typedef struct HudWorld {
    Squad              *squad;
    const ArenaSession *arena;      // бои арены (отладка, debug/); NULL — арены нет
    const ArenaRing    *ring;
    const Field        *field;
    const Player       *view;       // люди такими, какими их показал кадр (Figures.view), по номеру в таблице
    M4                  view_proj;
    f32                 time;       // реальное время, с
    f32                 tod;        // время суток 0..1
    u32                 day;
    const Ground       *ground;     // вещи на земле: имя под курсором, подбор
    const Phys         *phys;
    GroundAsks         *ground_ask; // бросить вещь с курсора на землю
    f32                 alpha;      // доля шага мира: где кадр показывает вещи
} HudWorld;

// Скорость времени по кнопкам часов: пауза, ×1, ×2, ×5 (шагов мира за секунду — в столько раз больше)
extern const u8 g_speed_scale[4];

void hud_init(Hud *h);
// Между ui_begin и отрисовкой кадра. true — мышь над интерфейсом или занята им: ПКМ не приказ
bool hud_input(Hud *h, const Input *in, HudWorld *w);
// После render_frame и портретов, перед ui_end
void hud_draw(const Hud *h, const Input *in, const HudWorld *w);
// Показать персонажа в панели выбранного (для снимков и горячих клавиш)
void hud_look(Hud *h, u8 kind, u32 index);
bool hud_window_open(const Hud *h);
// Кто под курсором в мире (свои, бойцы арены, чужие на поле); false — никого или курсор над интерфейсом
bool hud_pick(const Hud *h, const Input *in, const HudWorld *w, u8 *kind, u32 *index);
// Вещь на земле под курсором (Ground) или -1
i32  hud_pick_item(const Hud *h, const Input *in, const HudWorld *w);

// Окно с виджетами. Между ui_begin и hud_input: begin (false — окно закрыто), виджеты, end.
// end возвращает true, если мышь занята окном; тогда клики в мир и HUD не передавать.
bool hud_form_begin(HudForm *f, const Input *in, const char *title, const char *right);
bool hud_form_check(HudForm *f, const char *label, bool *v);       // true — переключили
bool hud_form_button(HudForm *f, const char *label);               // true — нажали
bool hud_form_slider(HudForm *f, const char *label, f32 *v, f32 lo, f32 hi, const char *value);
void hud_form_text(HudForm *f, const char *label, const char *value);
// Выбор из списка стрелками: −1 или +1 — щёлкнули влево или вправо, 0 — нет
i32  hud_form_step(HudForm *f, const char *label, const char *value);
// Вкладки в одну строку: *cur — выбранная; true — переключили
bool hud_form_tabs(HudForm *f, const char *const *names, u32 n, u8 *cur);
// Заголовок раздела с чертой
void hud_form_head(HudForm *f, const char *label);
// Образец цвета у правого края прошлой строки (вместо числа ползунка)
void hud_form_swatch(HudForm *f, u32 rgba);
// Портрет из атласа по центру строки
void hud_form_portrait(HudForm *f, u32 slot, i32 px);
bool hud_form_end(HudForm *f);

// Отладочное меню: кнопка слева вверху и окно НАСТРОЙКИ; виджеты — как у окна выше
bool hud_debug_begin(Hud *h, const Input *in);
bool hud_debug_check(Hud *h, const char *label, bool *v);
bool hud_debug_button(Hud *h, const char *label);
bool hud_debug_slider(Hud *h, const char *label, f32 *v, f32 lo, f32 hi, const char *value);
void hud_debug_text(Hud *h, const char *label, const char *value);
bool hud_debug_end(Hud *h);
// Боец арены по номеру в ростере: номер в текущем бою или -1, позиция на ринге
i32  hud_arena_fighter(const ArenaRing *v, const ArenaSession *s, u32 roster);
bool hud_arena_pos(const ArenaRing *v, const ArenaSession *s, u32 roster, V3 *out);

#endif
