// ИИ группы (этап 12, docs/people.md «Группа»): выбор и смена цели у бойцов со стороной, места вокруг цели
// кольцом и очередь на удар (жетоны: бить разом могут не больше tokens нападающих на одного). Итог — только
// намерения INTENT_ATTACK (цель) и INTENT_PLACE (место), шов 3 docs/invariants.md. Числа — data/group.txt
#pragma once
#include "../core/mem.h"
#include "../sim/intent.h"
#include "../sim/people_grid.h"

struct World;
struct DuelDesc;

struct GroupDesc {
    u32 retarget;   // тиков между выбором цели (свой сдвиг у каждого)
    f32 aggro;      // м: врагов дальше не замечает (нынешнюю цель держит до aggro · 1.5)
    u32 tokens;     // сколько нападающих на одного бьют разом; остальные ждут очереди
    f32 crowd;      // штраф цели за каждого, кто уже нападает на неё (сверх жетонов — вдвое)
    f32 near;       // м: штраф 1 за столько расстояния до цели
    f32 keep;       // надбавка нынешней цели
    f32 threat;     // надбавка тому, кто замахнулся на бойца
    f32 open;       // надбавка тому, кто открыт (реакция, отбит)
    f32 wait;       // м: ждущие очереди держатся дальше на столько
    f32 gap;        // оборота: наибольший шаг мест вокруг цели
    u64 hash;
};

// Разобрать data/group.txt (формат docs/text.md, все поля обязательны). false — ошибка в err
bool group_parse(const u8 *data, usize size, GroupDesc &out, char *err, usize err_size);

// Срез мира для решений одного шага: кто на кого нападает (списком по цели), кто держит жетон, чей замах ближе
// всех к удару по каждому. Строится раз на шаг из мира до шага
struct FightView {
    u16 first[PEOPLE_CAPACITY + 1];  // нападающие на место v — list[first[v] .. first[v + 1]) по номеру места
    u16 list[PEOPLE_CAPACITY];
    u16 holders[PEOPLE_CAPACITY];    // сколько из них держат жетон (бьют до конца окна удара или хотят бить)
    u16 urgent[PEOPLE_CAPACITY];     // чей удар по месту ближе всех к окну, 0xFFFF — никто не замахнулся
    PeopleGrid grid;                 // сетка людей 8 м: кого выбрать целью, кто из своих рядом
};
void fight_view(const World &w, FightView &v);
// Держит ли s жетон: бьёт (до конца окна удара) или ИИ уже велел бить
bool fight_holds(const World &w, u32 s);
// Может ли s начать удар по t: держит жетон или жетон свободен и s среди первых в очереди (дольше всех без удара)
bool fight_may_strike(const World &w, const GroupDesc &g, const FightView &v, u32 s, u32 t);

// Намерения боя на шаг w.step: выбор цели (группа), места, действия (поединок, duel.cpp). Читает только мир;
// порядок бойцов на исход не влияет
void ai_fight(const World &w, const DuelDesc &d, const GroupDesc &g, FixedArray<Intent> &out);
