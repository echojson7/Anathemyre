// Свой отряд: люди реестра (Persons, ROLE_SQUAD); выбор и приказы как в Kenshi
#ifndef ANA_SQUAD_H
#define ANA_SQUAD_H

#include "../core/rng.h"
#include "inventory.h"
#include "persons.h"
#include "player.h"

#define SQUAD_MAX 8

typedef struct Squad {
    u32       count;
    Persons  *ps;
    PersonId  member[SQUAD_MAX];    // номера в реестре
    bool      selected[SQUAD_MAX];
    V3        order_pos;            // куда щёлкнули последним приказом: там метка
    f32       order_marker;         // сколько ещё секунд видна метка
    bool      formation;            // идущие по одному приказу держат темп самого медленного
    u16       next_group;
    i32       pick[SQUAD_MAX];      // вещь на земле (Ground), за которой идёт подобрать; -1 — нет
} Squad;

// Место i-го в реестре (оно же слот поля боя), его ходьба, лист и место в мире
static inline u32        squad_at(const Squad *s, u32 i) { return person_place(s->member[i]); }
static inline Motion    *squad_move(const Squad *s, u32 i) { return &s->ps->move[squad_at(s, i)]; }
static inline CharSheet *squad_sheet(const Squad *s, u32 i) { return &s->ps->sheet[squad_at(s, i)]; }
static inline V3         squad_pos(const Squad *s, u32 i) { return person_pos(s->ps, squad_at(s, i)); }

// Тело и лист чужого (место i реестра): случайный боец уровня tier (0 новичок .. 2 ветеран), оружие weapon
// (номер описания), лохмотья
void person_random(Persons *ps, u32 i, Rng *rng, const char *name, u32 tier, u8 weapon);
// Стартовый отряд из четырёх у точки origin: люди дописываются в реестр
void squad_init(Squad *s, Persons *ps, Rng *rng, V3 origin, f32 heading, GroundFn ground);
// Переставить весь отряд в другое место (переход между локациями): стоят строем у origin
void squad_place(Squad *s, V3 origin, f32 heading, GroundFn ground);
// Выбранные идут к точке строем: ряды по три поперёк направления движения
void squad_order_move(Squad *s, V3 target);
// Первый выбранный идёт подобрать вещь k, лежащую в at (подбирает шаг мира, loot.c)
void squad_order_pick(Squad *s, i32 k, V3 at);
// Режим передвижения выбранным
void squad_set_gait(Squad *s, Gait g);
// Предел скорости человека i в режиме g: ноги, атлетика, ноша
f32  person_speed(const Persons *ps, u32 i, Gait g);
f32  squad_member_speed(const Squad *s, u32 i, Gait g);
void squad_update(Squad *s, f32 dt, GroundFn ground);
// Сохранение: состав, выбор, метка приказа; люди — в реестре
void squad_ser(Ser *s, Squad *sq);
// Первый выбранный или -1
i32  squad_primary(const Squad *s);

#endif
