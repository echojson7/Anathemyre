// Отряды вдали: отряд — точка на графе мест и дорог (places.h), люди в нём — числом по выучке, без тел.
// Шаг отряда раз в секунду, кусками: на каждом шаге мира — каждый phases-й отряд, так за секунду каждый
// ровно один раз. Враждебные отряды сходятся — бой решает формула: урон по силе врага, гибнут сначала
// новички; потеряв половину, отряд бежит. Отряд рядом с игроком (near) — тела в игре: формула его не трогает,
// точка идёт по дороге впереди тел и ждёт их (hold)
#ifndef ANA_BANDS_H
#define ANA_BANDS_H

#include "../core/grid.h"
#include "../io/chronicle.h"
#include "../world/places.h"

#define BANDS_MAX 4096
#define BAND_TIERS 3      // выучка: новички, бойцы, ветераны (как tier у person_random)
#define BAND_TEAMS 31     // команды; метка сетки 31 — рассеянные
#define BAND_STEP_S 1.0f  // шаг отряда, с

typedef enum BandState { BAND_GONE, BAND_REST, BAND_TRAVEL, BAND_FIGHT } BandState;

typedef struct Bands {
    u32           count;          // занятых номеров; рассеянные остаются BAND_GONE
    u32           phases, phase;  // кусков в секунде и какой следующий
    u64           seed, sec;      // секунд дальнего мира с начала
    const Places *places;
    // Где: на месте from (to == from) или на дороге from → to, along м от from
    f32 x[BANDS_MAX], z[BANDS_MAX];
    f32 along[BANDS_MAX];
    f32 wait[BANDS_MAX];          // на месте: сколько ещё стоит; в пути после бегства: сколько не ввязывается, с
    f32 hurt[BANDS_MAX];          // урон в бою, ещё не ставший потерями (в новичках)
    u16 men[BANDS_MAX][BAND_TIERS];
    u16 men0[BANDS_MAX];          // людей в начале боя
    u16 foe[BANDS_MAX];
    u8  from[BANDS_MAX], to[BANDS_MAX], goal[BANDS_MAX];
    u8  team[BANDS_MAX], state[BANDS_MAX], tag[BANDS_MAX];
    u8  near[BANDS_MAX];          // рядом: люди — тела в игре, бой — на поле боя
    u8  hold[BANDS_MAX];          // рядом и тела отстали или дерутся: точка ждёт
    u32 hostile[BAND_TEAMS];      // с кем команда враждует: маска команд
    Grid grid;
    // Итоги для журнала и замера
    u32 fights, routs, wiped;
    u64 dead;
    u32 log_left;                 // сколько ещё событий писать в журнал
    Chronicle *chron;             // журнал событий мира; NULL — некуда (замер)
} Bands;

void bands_init(Bands *b, Arena *perm, const Places *places, u64 seed, u32 phases);
// Новый отряд на месте place; men — людей по выучке. -1 — нет места в таблице
i32  bands_add(Bands *b, u8 team, u32 place, const u16 men[BAND_TIERS]);
void bands_hostile(Bands *b, u8 a, u8 c);
// Шаг мира: шаг каждого phases-го отряда
void bands_tick(Bands *b);
// Стоящий на месте отряд идёт к месту goal. false — идёт или дерётся, или нет дороги
bool bands_send(Bands *b, u32 i, u32 goal);
// Отряд стал телами или снова точкой. Бой формулой при этом прекращается
void bands_set_near(Bands *b, u32 i, bool on);
// Люди отряда после тел: кто вернулся. Никого — отряд истреблён
void bands_set_men(Bands *b, u32 i, const u16 men[BAND_TIERS]);
u32  bands_men(const Bands *b, u32 i);
// Сохранение: все отряды и сетка; места и дороги — у мира
void bands_ser(Ser *s, Bands *b);
// Сумма состояния: одинаковый мир — одинаковая сумма
u64  bands_checksum(const Bands *b);

#endif
