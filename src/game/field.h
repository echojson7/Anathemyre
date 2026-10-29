// Поле боя открытого мира: один CombatWorld на всех, кто ходит по миру (свой отряд, мобы, бродящие).
// У каждого тела постоянный слот. Вне боя слот спит: телом правит ходьба (Player), бой его не видит.
// Враг рядом — слот просыпается: позицию, удары и раны ведёт симуляция боя, тело только повторяет.
// Бой стих — слот засыпает, раны остаются в листе персонажа.
// Приказ атаки как в Kenshi: бежит к цели своим шагом, у цели вступает в бой и бьёт только её,
// пока та не выбыла (без сознания, сдалась, мертва). Убежала — догоняет бегом.
#ifndef ANA_FIELD_H
#define ANA_FIELD_H

#include "../ai/fighter.h"
#include "../sim/inventory.h"
#include "fight_anim.h"
#include "player.h"

#define FIELD_MAX 1024

// Как тело ведёт себя само, без приказов
enum {
    FF_ASSIST = 1,  // идёт на помощь своим, если рядом дерутся
    FF_AGGRO = 2,   // сам нападает на тех, с кем враждует
    FF_PLAYER = 4,  // свой отряд: идёт по приказу — мимо боя, не ввязываясь
};

typedef struct Field {
    CombatWorld w;
    FighterAI ai;
    f32 alpha;                       // для кадра: доля шага мира после последнего тика (field_present)
    Player    *body[FIELD_MAX];
    CharSheet *sheet[FIELD_MAX];
    u8   flags[FIELD_MAX];
    u8   soft[FIELD_MAX];            // цель выбрана самим (помощь, нападение): проснулся — выбирает заново
    u8   leave[FIELD_MAX];           // выйти из боя при первой возможности: дан приказ идти
    f32  calm[FIELD_MAX];            // сколько секунд рядом нет врага
    f32  px[FIELD_MAX], pz[FIELD_MAX], ph[FIELD_MAX];  // прошлый тик: сглаживание
    FightAnim anim[FIELD_MAX];       // вид бойца, пока слот не спит: пишет только кадр (field_present)
    f32  grudge[COMBAT_MAX_TEAMS];   // сколько ещё секунд команда помнит вражду
    u32  fights;                     // проснувшихся сейчас
} Field;

void field_init(Field *f, Arena *a, u64 seed);
// Поставить тело на поле. Лист обязателен: в нём тело, навыки и оружие. Возвращает номер слота
u32  field_add(Field *f, Player *body, CharSheet *sheet, u8 team, u8 flags);
// Приказ: слот who атакует слот target. false — нельзя (свой, мёртвый, нечем бить)
bool field_attack(Field *f, u32 who, u32 target);
// Отменить приказ атаки; leave — если дерётся, выйти из боя, как только сможет (приказ идти)
void field_cancel(Field *f, u32 who, bool leave);
// Тик поля (шаг мира): после ходьбы тел. Тело бойца в бою стоит там, где его держит симуляция
void field_tick(Field *f, GroundFn ground);
// Вид боя для кадра: alpha — доля шага после тика, dt — игровое время кадра. Мир не меняет
void field_present(Field *f, f32 alpha, f32 dt);
static inline bool field_awake(const Field *f, u32 i) { return i < f->w.count && !f->w.idle[i]; }
// Цель по приказу или -1
i32  field_target(const Field *f, u32 i);
// Сглаженная позиция бойца между тиками: alpha — доля шага после последнего тика
void field_pos(const Field *f, u32 i, f32 alpha, f32 *x, f32 *z, f32 *heading);
// Враждуют ли команды двух слотов
bool field_hostile(const Field *f, u32 a, u32 b);
// Проверка без окна: сценка атаки с журналом, затем нагрузка на bodies тел (0 — без неё)
int  field_bench(u32 bodies, u64 seed);

#endif
