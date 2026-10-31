// Поле боя открытого мира: один CombatWorld на всех людей реестра (persons.h). Слот боя — место в реестре:
// место и курс бой читает и пишет прямо в столбцах реестра, копий нет.
// Вне боя слот спит: телом правит ходьба (Motion), бой его не видит.
// Враг рядом — слот просыпается: позицию, удары и раны ведёт симуляция боя, тело только повторяет.
// Бой стих — слот засыпает, раны остаются в листе персонажа.
// Приказ атаки как в Kenshi: бежит к цели своим шагом, у цели вступает в бой и бьёт только её,
// пока та не выбыла (без сознания, сдалась, мертва). Убежала — догоняет бегом.
#ifndef ANA_FIELD_H
#define ANA_FIELD_H

#include "../ai/fighter.h"
#include "../io/chronicle.h"
#include "inventory.h"
#include "fight_anim.h"
#include "persons.h"

#define FIELD_MAX PERSONS_MAX


#define FIELD_TEAM_NOBODY TEAM_FREE  // команда свободных слотов: ни с кем не враждует

typedef struct Field {
    CombatWorld w;
    FighterAI ai;
    f32 alpha;                       // для кадра: доля шага мира после последнего тика (field_present)
    Persons *ps;                     // люди: слот i — место i в реестре
    u8   flags[FIELD_MAX];
    u8   soft[FIELD_MAX];            // цель выбрана самим (помощь, нападение): проснулся — выбирает заново
    u8   leave[FIELD_MAX];           // выйти из боя при первой возможности: дан приказ идти
    f32  calm[FIELD_MAX];            // сколько секунд рядом нет врага
    f32  px[FIELD_MAX], pz[FIELD_MAX], ph[FIELD_MAX];  // прошлый тик: сглаживание
    FightAnim anim[FIELD_MAX];       // вид бойца, пока слот не спит: пишет только кадр (field_present)
    f32  grudge[COMBAT_MAX_TEAMS];   // сколько ещё секунд команда помнит вражду
    u32  enmity[COMBAT_MAX_TEAMS];   // вражда навсегда (field_enmity): не забывается
    u32  fights;                     // проснувшихся сейчас
    Chronicle *chron;                // куда писать выбывших; NULL — некуда
} Field;

void field_init(Field *f, Arena *a, Persons *ps, u64 seed);
// Человек на месте i реестра на поле: новый слот (i — следующий) или прежний слот заново. Спит.
// Тело, навыки и оружие — из листа реестра
void field_join(Field *f, u32 i, u8 team, u8 flags);
// Слот i свободен: спит, «мёртв» для всех, команды нет, интерфейс его не видит
void field_free(Field *f, u32 i);
// Команды a и b враждуют всегда (отряды вдали)
void field_enmity(Field *f, u8 a, u8 b);
// Приказ: слот who атакует слот target. false — нельзя (свой, мёртвый, нечем бить)
bool field_attack(Field *f, u32 who, u32 target);
// Отменить приказ атаки; leave — если дерётся, выйти из боя, как только сможет (приказ идти)
void field_cancel(Field *f, u32 who, bool leave);
// Сохранение: бой, решения, правила поля. Тела и листы — в реестре, вид бойцов не пишется
void field_ser(Ser *s, Field *f);
// После чтения: вид дерущихся заново, сразу в нынешнем состоянии боя
void field_view_restore(Field *f);
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

#endif
