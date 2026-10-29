// Люди мира одной таблицей: свой отряд, бродящие, зрители арены, жители Скудели, мобы.
// Номер человека — место в таблице; он же слот на поле боя (people_join_field).
// Поведение — по роли (people_update), позы и отрисовка — одним циклом для всех (figures.c).
// Поколение номера (чтобы старый номер не находил нового человека на том же месте) появится,
// когда люди начнут уходить из таблицы; сейчас никто не уходит.
#ifndef ANA_PEOPLE_H
#define ANA_PEOPLE_H

#include "../core/rng.h"
#include "../sim/inventory.h"
#include "field.h"
#include "look.h"
#include "player.h"

#define PEOPLE_MAX 256

typedef enum Role { ROLE_SQUAD, ROLE_WANDER, ROLE_WATCHER, ROLE_TOWN, ROLE_MOB, ROLE_COUNT } Role;

// Команды на поле боя: свой отряд, шайка мобов, бродящие (каждый сам за себя: TEAM_WANDER + номер),
// за ними зрители и посёлок
enum { TEAM_SQUAD, TEAM_MOBS, TEAM_WANDER };

typedef struct People {
    u32       count;
    Player    body[PEOPLE_MAX];
    Player    last[PEOPLE_MAX];       // тело на прошлом шаге мира: кадр сглаживает между ним и body
    CharSheet sheet[PEOPLE_MAX];
    u8        role[PEOPLE_MAX];
    u8        team[PEOPLE_MAX];       // команда на поле боя
    u8        fflags[PEOPLE_MAX];     // FF_*: как ведёт себя в бою сам
    // Внешность: тело (BodyKind), зерно позы, рост и ширина (множители, 0 — как есть), лицо, вещи и причёска
    u8        variant[PEOPLE_MAX];
    f32       seed[PEOPLE_MAX];
    f32       height[PEOPLE_MAX], width[PEOPLE_MAX];
    u32       look_seed[PEOPLE_MAX];
    M4        look[PEOPLE_MAX];
    u8        gear[PEOPLE_MAX][GEAR_MAX_WORN];
    u8        ngear[PEOPLE_MAX];
    bool      torch[PEOPLE_MAX];      // с темнотой зажигает факел
    // Поведение по роли
    bool      stand[PEOPLE_MAX];      // стоит на своём месте, после боя возвращается
    V3        home[PEOPLE_MAX];
    f32       home_h[PEOPLE_MAX];
    f32       wait[PEOPLE_MAX];       // постоит ещё столько секунд, потом дальше
    f32       pace[PEOPLE_MAX];       // свой темп шага, м/с
    u8        node[PEOPLE_MAX], prev[PEOPLE_MAX];  // жители: узел улиц; мобы: следующая точка круга
} People;

// Новый человек с ролью и местом на поле боя: тело в pos лицом к heading, остальное заполняет роль
u32  people_add(People *p, Role role, V3 pos, f32 heading, u8 team, u8 fflags);
// Внешность прохожего: сложение из зерна позы, лицо из look_seed, одежда случайная по gear_seed
void people_look_random(People *p, u32 i, u32 look_seed, u32 gear_seed);
// Внешность из листа: надетые вещи, сложение и сила из листа. Отряд и мобы — каждый кадр (переодеваются)
void people_dress(People *p, u32 i);
void people_dress_live(People *p);
// Всех на поле боя по порядку: слот = номер в таблице
void people_join_field(People *p, Field *f);
// Шаг всех, кроме своего отряда (им правит squad_update): rng — пустошь, town_rng — посёлок
void people_update(People *p, Rng *rng, Rng *town_rng, f32 dt, GroundFn ground);
// k-й человек с ролью role или -1
i32  people_nth(const People *p, Role role, u32 k);

// Поведение ролей (testroom.c, town.c): шаг одного
void crowd_step(People *p, u32 i, Rng *rng, f32 dt, GroundFn ground);
void mob_step(People *p, u32 i, Rng *rng, f32 dt, GroundFn ground);
void town_folk_step(People *p, u32 i, Rng *rng, f32 dt, GroundFn ground);

#endif
