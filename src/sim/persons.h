// Реестр людей: единственная таблица людей с телами (и место под заметных личностей). Структура массивов,
// PERSONS_MAX мест, пул при старте. Номер человека (PersonId) — место и поколение: место освободили
// и заняли другим — старый номер больше никого не находит (persons_find).
// Место в реестре — оно же слот поля боя (field.h): позиция и курс живут только здесь, бой читает и пишет
// те же столбцы. People, Squad, NearBands и Field хранят номера, а не копии.
// Столбцы группами: кто, тело, режим тела, лист со снаряжением, внешность (зерно) и кэш вида.
// Кэш вида (матрица внешности, сетки одежды) в сохранение не идёт: его заново считает persons_relook_all
#ifndef ANA_PERSONS_H
#define ANA_PERSONS_H

#include "../core/ser.h"
#include <string.h>

#include "inventory.h"
#include "look.h"
#include "player.h"

#define PERSONS_MAX 512

typedef u32 PersonId;
#define PERSON_NONE 0xFFFFFFFFu
#define PERSON_PLACE_BITS 16

// Кто он в мире: свой отряд, бродящие, зрители арены, жители Скудели, мобы, люди отрядов вдали.
// ROLE_FREE — свободное место: не рисуется, не ходит
typedef enum Role { ROLE_SQUAD, ROLE_WANDER, ROLE_WATCHER, ROLE_TOWN, ROLE_MOB, ROLE_BAND, ROLE_FREE, ROLE_COUNT } Role;

// Команды на поле боя: свой отряд, шайка мобов, бродящие (каждый сам за себя: TEAM_WANDER + номер),
// за ними зрители и посёлок. Отряды вдали — TEAM_BANDS + команда отряда; свободные места — ничья команда
enum { TEAM_SQUAD, TEAM_MOBS, TEAM_WANDER, TEAM_BANDS = 16, TEAM_FREE = COMBAT_MAX_TEAMS - 1 };

// Как тело ведёт себя в бою само, без приказов (Persons.fflags, Field.flags)
enum {
    FF_ASSIST = 1,  // идёт на помощь своим, если рядом дерутся
    FF_AGGRO = 2,   // сам нападает на тех, с кем враждует
    FF_PLAYER = 4,  // свой отряд: идёт по приказу — мимо боя, не ввязываясь
    FF_ABSENT = 8,  // свободный слот: никого нет (field_free)
};

// Кто правит телом. Пока только анимация; ragdoll и запечённая поза — шаг 3.1
typedef enum BodyMode { BODY_ANIM, BODY_RAGDOLL, BODY_BAKED, BODY_MODE_COUNT } BodyMode;

// Откуда внешность: из зерна (прохожие) или из листа (сложение и надетое; отряд, мобы, люди отрядов)
enum { LOOK_FROM_SEED, LOOK_FROM_SHEET };

// Новый столбец — и в persons_ser (если это не кэш вида), иначе он не переживёт сохранение
typedef struct Persons {
    u32       count;                  // мест в ходу: 0..count-1; свободные внутри — live 0
    u16       gen[PERSONS_MAX];       // поколение места: растёт, когда место освобождают
    bool      live[PERSONS_MAX];
    // Кто
    u8        role[PERSONS_MAX];
    u8        team[PERSONS_MAX];      // команда на поле боя
    u8        fflags[PERSONS_MAX];    // FF_*: как ведёт себя в бою сам
    u8        variant[PERSONS_MAX];   // тело, BodyKind
    // Тело: место и курс — единственные; бой (field.h) читает и пишет их же
    f32       x[PERSONS_MAX], y[PERSONS_MAX], z[PERSONS_MAX], heading[PERSONS_MAX];
    Motion    move[PERSONS_MAX];      // ходьба: скорость, походка, приказ, поза отдыха
    Player    last[PERSONS_MAX];      // тело на прошлом шаге мира: кадр сглаживает между ним и нынешним
    u8        mode[PERSONS_MAX];      // BodyMode
    // Лист: навыки, части тела, кровь; надетое и мешок
    CharSheet sheet[PERSONS_MAX];
    // Внешность: зерно позы, рост и ширина (множители), зёрна лица и одежды, откуда она; заданная редактором
    f32       seed[PERSONS_MAX];
    f32       height[PERSONS_MAX], width[PERSONS_MAX];
    u32       look_seed[PERSONS_MAX], gear_seed[PERSONS_MAX];
    u8        look_from[PERSONS_MAX];
    bool      lk_own[PERSONS_MAX];    // внешность задал редактор: lk в сохранении, из зерна не пересчитывается
    bool      torch[PERSONS_MAX];     // с темнотой зажигает факел
    // Кэш вида: из зерна, листа или lk; не сохраняется
    M4        look[PERSONS_MAX];
    CharLook  lk[PERSONS_MAX];        // из чего собрана look: морфы, тона, причёска
    u8        gear[PERSONS_MAX][GEAR_MAX_WORN];
    u8        ngear[PERSONS_MAX];
} Persons;

static inline u32      person_place(PersonId id) { return id & ((1u << PERSON_PLACE_BITS) - 1u); }
static inline PersonId person_id(const Persons *ps, u32 i) { return (u32)ps->gen[i] << PERSON_PLACE_BITS | i; }
// Место живого человека с этим номером или -1 (место свободно или занято уже другим)
static inline i32 persons_find(const Persons *ps, PersonId id)
{
    u32 i = person_place(id);
    return id != PERSON_NONE && i < ps->count && ps->live[i] && person_id(ps, i) == id ? (i32)i : -1;
}

static inline V3 person_pos(const Persons *ps, u32 i) { return v3(ps->x[i], ps->y[i], ps->z[i]); }
// Тело целиком значением (для ходьбы и кадра) и обратно в столбцы
static inline Player person_body(const Persons *ps, u32 i)
{
    Player b;
    b.pos = person_pos(ps, i);
    b.heading = ps->heading[i];
    memcpy((u8 *)&b + PLAYER_MOTION_AT, &ps->move[i], sizeof(Motion));
    return b;
}
static inline void person_set_body(Persons *ps, u32 i, const Player *b)
{
    ps->x[i] = b->pos.x, ps->y[i] = b->pos.y, ps->z[i] = b->pos.z;
    ps->heading[i] = b->heading;
    memcpy(&ps->move[i], (const u8 *)b + PLAYER_MOTION_AT, sizeof(Motion));
}
static inline void person_set_pos(Persons *ps, u32 i, V3 p) { ps->x[i] = p.x, ps->y[i] = p.y, ps->z[i] = p.z; }

void persons_init(Persons *ps);
// Новый человек на нижнем свободном месте: тело в pos лицом к heading, лист пуст, внешность нулевая.
// PERSON_NONE — мест нет. Место на поле боя даёт field_join
PersonId persons_add(Persons *ps, Role role, V3 pos, f32 heading, u8 team, u8 fflags);
// Место свободно: поколение растёт, старые номера его больше не находят
void persons_free(Persons *ps, u32 i);
// Шаг ходьбы (player.c) телом из столбцов
void person_walk(Persons *ps, u32 i, f32 dt, GroundFn ground);
// Прошлый шаг = нынешний: перед шагом мира и после переноса (кадр не тянет тело через карту)
void persons_keep_last(Persons *ps);
// Внешность из зерна: сложение из зерна позы, лицо из look_seed, одежда случайная по gear_seed
void persons_look_random(Persons *ps, u32 i, u32 look_seed, u32 gear_seed);
// Внешность из листа: надетые вещи, сложение и сила из листа. Отряд и мобы — каждый кадр (переодеваются)
void persons_dress(Persons *ps, u32 i);
void persons_dress_live(Persons *ps);
// Внешность из lk[i] (после правки в редакторе): матрица и причёска в списке вещей
void persons_relook(Persons *ps, u32 i);
// Кэш вида всех заново (после чтения сохранения)
void persons_relook_all(Persons *ps);
// Сохранение: столбцы мест 0..count-1 без кэша вида; имена и роли — текстом
void persons_ser(Ser *s, Persons *ps);

#endif
