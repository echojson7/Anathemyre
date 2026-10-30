// Свой отряд: люди из общей таблицы (People, ROLE_SQUAD); выбор и приказы как в Kenshi
#ifndef ANA_SQUAD_H
#define ANA_SQUAD_H

#include "../core/rng.h"
#include "../sim/inventory.h"
#include "people.h"
#include "player.h"

#define SQUAD_MAX 8

typedef struct Squad {
    u32       count;
    People   *people;
    u32       member[SQUAD_MAX];    // номера в таблице людей
    bool      selected[SQUAD_MAX];
    V3        order_pos;            // куда щёлкнули последним приказом: там метка
    f32       order_marker;         // сколько ещё секунд видна метка
    bool      formation;            // идущие по одному приказу держат темп самого медленного
    u16       next_group;
    i32       pick[SQUAD_MAX];      // вещь на земле (Ground), за которой идёт подобрать; -1 — нет
} Squad;

static inline Player    *squad_body(const Squad *s, u32 i) { return &s->people->body[s->member[i]]; }
static inline CharSheet *squad_sheet(const Squad *s, u32 i) { return &s->people->sheet[s->member[i]]; }

// Лист чужого: случайный боец уровня tier (0 новичок .. 2 ветеран), оружие weapon (номер описания), лохмотья
void sheet_random(CharSheet *c, Rng *rng, const char *name, u32 tier, u8 weapon);
// Стартовый отряд из четырёх у точки origin: люди дописываются в таблицу p
void squad_init(Squad *s, People *p, Rng *rng, V3 origin, f32 heading, GroundFn ground);
// Переставить весь отряд в другое место (переход между локациями): стоят строем у origin
void squad_place(Squad *s, V3 origin, f32 heading, GroundFn ground);
// Выбранные идут к точке строем: ряды по три поперёк направления движения
void squad_order_move(Squad *s, V3 target);
// Первый выбранный идёт подобрать вещь k, лежащую в at (подбирает шаг мира, loot.c)
void squad_order_pick(Squad *s, i32 k, V3 at);
// Режим передвижения выбранным
void squad_set_gait(Squad *s, Gait g);
// Предел скорости персонажа в режиме g: ноги, атлетика, ноша
f32  sheet_speed(const CharSheet *c, Gait g);
f32  squad_member_speed(const Squad *s, u32 i, Gait g);
void squad_update(Squad *s, f32 dt, GroundFn ground);
// Сохранение: состав, выбор, метка приказа; люди — в таблице людей
void squad_ser(Ser *s, Squad *sq);
// Первый выбранный или -1
i32  squad_primary(const Squad *s);

#endif
