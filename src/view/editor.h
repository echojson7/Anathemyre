// Редактор внешности (окно ВНЕШНОСТЬ, K): тело, сложение, лицо, цвета и причёска выбранного человека.
// Правит ровно то, из чего генератор собирает внешность (look.c), и пишет в таблицу людей: видно сразу в мире.
// Правят своих из отряда и прохожих на поле; бойцов арены нет (их внешность считается из ростера)
#ifndef ANA_EDITOR_H
#define ANA_EDITOR_H

#include "camera.h"
#include "hud.h"
#include "../world/world.h"

typedef struct Editor {
    HudForm  f;
    i32      who;       // номер в таблице людей, -1 — править некого
    u32      rolls;     // сколько раз жали СЛУЧАЙНО: зерно новой внешности
    // Как было, когда редактор открылся на этом человеке (кнопка ВЕРНУТЬ)
    CharLook lk;
    bool     lk_own;
    u8       variant;
    f32      height, width, sheet_h;
} Editor;

void editor_init(Editor *e);
// Между menu_input и hud_input. При открытии камера встаёт вплотную лицом к тому, кого правят.
// Если мышь занята окном, клики из *in убраны
void editor_input(Editor *e, Hud *h, Input *in, World *w, RtsCamera *cam, bool humans_on);
// За кем держать камеру (номер в таблице людей), -1 — окно закрыто или править некого
i32  editor_follow(const Editor *e);

#endif
