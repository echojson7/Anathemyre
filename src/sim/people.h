// Люди мира по ролям: бродящие, зрители арены, жители Скудели, мобы. Сами люди — в реестре (persons.h),
// здесь только номера и то, что нужно поведению роли: место, куда вернуться, ожидание, темп, узел пути.
// Порядок в таблице — порядок шага; от него зависят случайные числа.
// Свой отряд — squad.h, люди отрядов вдали — bands_near.h: в этой таблице их нет
#ifndef ANA_PEOPLE_H
#define ANA_PEOPLE_H

#include "../core/rng.h"
#include "persons.h"

#define PEOPLE_MAX PERSONS_MAX  // все роли разом не больше реестра

// Новый столбец — и в people_ser, иначе он не переживёт сохранение
typedef struct People {
    u32       count;
    Persons  *ps;
    PersonId  who[PEOPLE_MAX];
    bool      stand[PEOPLE_MAX];      // стоит на своём месте, после боя возвращается
    V3        home[PEOPLE_MAX];
    f32       home_h[PEOPLE_MAX];
    f32       wait[PEOPLE_MAX];       // постоит ещё столько секунд, потом дальше
    f32       pace[PEOPLE_MAX];       // свой темп шага, м/с
    u8        node[PEOPLE_MAX], prev[PEOPLE_MAX];  // жители: узел улиц; мобы: следующая точка круга
} People;

static inline u32 people_place(const People *p, u32 k) { return person_place(p->who[k]); }

void people_init(People *p, Persons *ps);
// Новый человек роли role в реестре и в этой таблице: его номер здесь; место в реестре — people_place
u32  people_add(People *p, Role role, V3 pos, f32 heading, u8 team, u8 fflags);
// Сохранение: номера и поведение; люди — в реестре
void people_ser(Ser *s, People *p);
// Шаг всех по ролям: rng — пустошь, town_rng — посёлок
void people_update(People *p, Rng *rng, Rng *town_rng, f32 dt, GroundFn ground);
// k-й человек с ролью role (место в реестре) или -1
i32  people_nth(const People *p, Role role, u32 k);

// Поведение ролей (world/crowd.c, world/town.c): шаг k-го в таблице
void crowd_step(People *p, u32 k, Rng *rng, f32 dt, GroundFn ground);
void mob_step(People *p, u32 k, Rng *rng, f32 dt, GroundFn ground);
void town_folk_step(People *p, u32 k, Rng *rng, f32 dt, GroundFn ground);

#endif
