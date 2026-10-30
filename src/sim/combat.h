// Ближний бой: бойцы (SoA), состояния, разрешение ударов, раны, мораль, рост навыков.
// Фиксированный тик, детерминированный: исход зависит только от seed и состава.
// Ничего не знает о графике.
#ifndef ANA_COMBAT_H
#define ANA_COMBAT_H

#include "../core/arena.h"
#include "../core/grid.h"
#include "../core/ser.h"
#include "items.h"

#define COMBAT_HZ 20
#define COMBAT_DT (1.0f / COMBAT_HZ)
#define COMBAT_THINK_TICKS 5   // решения ИИ раз в 0.25 с
#define COMBAT_NEAR_MAX 1024   // предел выборки соседей за один запрос
#define COMBAT_MAX_TEAMS 30

// Метки бойцов в сетке: мёртвые, выбывшие, остальные по командам
#define TAG_DEAD 0
#define TAG_OUT 1
#define TAG_BIT(tag) (1u << (tag))
#define TEAM_BIT(team) (1u << (2u + (team)))
#define TAGS_ALIVE (~TAG_BIT(TAG_DEAD))
#define TAGS_FIGHTING (~(TAG_BIT(TAG_DEAD) | TAG_BIT(TAG_OUT)))

typedef enum Skill {
    SK_ATTACK, SK_DEFENSE, SK_DODGE,
    SK_STRENGTH, SK_DEXTERITY, SK_TOUGHNESS, SK_ATHLETICS,
    SK_WEAPON,  // + WeaponClass: SK_WEAPON + WC_BLADE и т.д.
    SK_COUNT = SK_WEAPON + WC_COUNT
} Skill;

typedef enum FighterState {
    FS_READY,        // стойка, сближение, кружение
    FS_WINDUP,       // замах
    FS_STRIKE,       // удар (исход уже применён)
    FS_RECOVER,      // возврат оружия
    FS_BLOCK,
    FS_DODGE,
    FS_STAGGER,      // ошеломлён
    FS_KNOCKED,      // сбит с ног, падает
    FS_DOWN,         // лежит в сознании
    FS_GETUP,
    FS_UNCONSCIOUS,
    FS_CRAWL,        // ноги не держат
    FS_RETREAT,      // мораль упала
    FS_SURRENDER,
    FS_DEAD,
    FS_COUNT
} FighterState;

// Фигура вне боя: лежит, сдалась, мертва
static inline bool fs_out(u8 s)
{
    return s == FS_UNCONSCIOUS || s == FS_CRAWL || s == FS_SURRENDER || s == FS_DEAD;
}
static inline bool fs_lying(u8 s)
{
    return s == FS_KNOCKED || s == FS_DOWN || s == FS_GETUP || s == FS_UNCONSCIOUS || s == FS_CRAWL || s == FS_DEAD;
}

typedef enum CombatEventKind {
    EV_ATTACK,     // a начал удар b, arg = AttackKind
    EV_HIT,        // a попал по b
    EV_BLOCK,      // b заблокировал a; flags & EVF_STAGGER — ошеломлён
    EV_DODGE,      // b уклонился от a
    EV_COUNTER,    // b контратакует a
    EV_KNOCKDOWN,  // b сбит с ног ударом a
    EV_FALL,       // a упал от шока или раны ноги
    EV_KO,         // a без сознания
    EV_DEATH,      // a мёртв
    EV_DISARM,     // a выронил оружие
    EV_RETREAT,    // a отступает
    EV_SURRENDER,  // a сдаётся
    EV_GETUP,      // a поднялся
} CombatEventKind;

enum { REACT_NONE, REACT_BLOCK, REACT_DODGE };

enum { EVF_STAGGER = 1, EVF_FRACTURE = 2, EVF_SEVER = 4, EVF_ALLY = 8 };

typedef struct CombatEvent {
    u32 tick;
    u8  kind, part, flags, arg;
    u16 a, b;
    f32 cut, blunt, pierce;
} CombatEvent;

// Описание бойца для добавления в бой
typedef struct FighterDesc {
    u8  team;
    f32 skill[SK_COUNT];
    u8  weapon, weapon_q;
    u8  armor[SLOT_COUNT];  // ARMOR_NONE — пусто
    f32 mass, height;
    f32 morale_base;
    // Состояние тела на входе (раны с прошлого боя); hp == 0 и blood == 0 — здоров
    f32 hp[PART_COUNT];
    f32 blood;
    u8  fracture, severed;  // маски частей
    V3  pos;
    f32 heading;
} FighterDesc;

// Наблюдатель журнала (звук): получает события до очистки, сам бой о нём ничего не знает
struct CombatWorld;
typedef void (*CombatSink)(void *user, const struct CombatWorld *w);

typedef struct CombatWorld {
    u32 cap, count;
    u64 seed;
    u32 tick;
    bool mercy;      // выбывших не добивают, сдачу принимают
    bool halted;     // бой остановлен: ИИ больше не атакует
    f32 fence_r;     // > 0: бой внутри круга этого радиуса
    u32 foe[COMBAT_MAX_TEAMS];  // с кем враждует команда: биты команд; по умолчанию со всеми чужими

    // Горячие данные каждого тика
    f32 *x, *z, *heading;
    u8  *state, *team, *attack, *react;  // react — ответ цели на текущий удар, REACT_*
    f32 *aim_x, *aim_y, *aim_s;  // куда идёт удар: точка на цели в плоскости взгляда (м, поза привязки) и наклон следа
    f32 *timer;      // осталось в состоянии, с
    f32 *dur;        // полная длительность состояния
    f32 *pause;      // пауза до следующей атаки
    i32 *target;
    f32 *since_hit;
    f32 *mvx, *mvz;  // желаемое движение от ИИ, длина <= 1
    u8  *idle;       // спящий слот: телом правит ходьба снаружи, бой его не видит, не бьёт и не двигает
    i32 *order;      // цель по приказу: ИИ бьёт только её, пока она в бою; -1 — выбирает сам

    // Тело
    f32 *hp[PART_COUNT];
    f32 *blood, *blood_max, *bleed, *shock, *morale, *morale_base, *mass, *height;
    u8  *fracture, *severed, *disarmed;
    u8  *prone;      // лежит ничком, иначе на спине: от этого, где лежит тело

    // Навыки и снаряжение
    f32 *skill[SK_COUNT];
    u8  *weapon, *weapon_q;
    u8  *armor[SLOT_COUNT];

    // Журнал событий текущего тика и далее; читатель сам сбрасывает ev_count
    CombatEvent *ev;
    u32 ev_count, ev_cap, last_ev;
    CombatSink sink;
    void *sink_user;

    // Сетка соседей: строится по позициям в конце тика и после добавления бойцов
    Grid grid;
    bool grid_dirty;
    u8  *grid_tag;       // метка в сетке, TAG_*
    f32 *sep_x, *sep_z;  // сдвиг расталкивания за тик
    u32 *strikes;        // кто наносит удар в этом тике
} CombatWorld;

// Враждует ли боец i с бойцом j (по командам)
static inline bool combat_hostile(const CombatWorld *w, u32 i, u32 j) { return (w->foe[w->team[i]] >> w->team[j]) & 1u; }
// Метки сетки для врагов команды team, кроме выбывших
static inline u32 combat_foe_tags(const CombatWorld *w, u8 team) { return w->foe[team] << 2u; }

void combat_init(CombatWorld *w, Arena *a, u32 cap, u32 ev_cap);
void combat_reset(CombatWorld *w, u64 seed);
u32  combat_add(CombatWorld *w, const FighterDesc *d);
// Заново заполнить слот i из описания: тело, навыки, снаряжение, место; состояние — стойка
void combat_load(CombatWorld *w, u32 i, const FighterDesc *d);
// Вражда двух команд в обе стороны
void combat_set_foes(CombatWorld *w, u8 a, u8 b, bool on);
// Начать удар (вызывает ИИ): замах, бросок реакции цели
void combat_begin_attack(CombatWorld *w, u32 i, u8 kind, u32 target, bool counter);
void combat_tick(CombatWorld *w);
// Журнал прочитан: показать его наблюдателю (если есть) и очистить
void combat_events_done(CombatWorld *w);
// Забрать состояние тела и навыки обратно в описание (после боя)
void combat_store(const CombatWorld *w, u32 i, FighterDesc *d);

// Сохранение: все бойцы (count, ёмкость — от combat_init), их журнал этого тика и сетка
void combat_ser(Ser *s, CombatWorld *w);
// Свёртка событий журнала в контрольную сумму (проверка детерминизма)
u64 combat_hash_events(const CombatWorld *w, u64 h);
// Сколько бойцов команды ещё в бою
u32 combat_team_active(const CombatWorld *w, u8 team);
// Перестроить сетку, если позиции менялись вне тика
void combat_sync_grid(CombatWorld *w);
// Кандидаты с метками из mask в радиусе r (по ячейкам, расстояние проверяет вызывающий);
// out на COMBAT_NEAR_MAX. Метки сняты в конце прошлого тика
u32 combat_near(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask, u32 *out);
// Сколько бойцов каждой метки из mask ближе r (расстояние² <= r²): прибавляет в count[GRID_MAX_TAGS]
void combat_count(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask, u32 *count);
// Есть ли поблизости кто-то из меток mask (грубо, с запасом)
bool combat_any_near(const CombatWorld *w, f32 x, f32 z, f32 r, u32 mask);
f32 combat_reach(const CombatWorld *w, u32 i, u8 kind);
f32 combat_arc(const CombatWorld *w, u32 i, u8 kind);      // полная дуга, рад
f32 combat_vital_ratio(const CombatWorld *w, u32 i);       // худшая жизненная часть, 0..1
f32 combat_roll(const CombatWorld *w, u32 a, u32 b, u32 salt);
void combat_set_state(CombatWorld *w, u32 i, u8 s, f32 dur);
void combat_event(CombatWorld *w, u8 kind, u32 a, u32 b);
f32 combat_rating(const FighterDesc *d);  // грубая сила для подбора пар

#endif
