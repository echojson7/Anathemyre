// ИИ поединка (этап 11, docs/people.md «Обмен ударами»): у каждого, кто бьётся (приказ «атаковать»), раз в think
// тиков — оценка действий (utility) по состоянию мира до шага, итог — только намерение INTENT_ACT (шов 3,
// docs/invariants.md). Таймеры, позы и попадания — у исполнения (sim/action.cpp). Числа — data/duel.txt
#pragma once
#include "../core/mem.h"
#include "../core/hash.h"
#include "../sim/intent.h"
#include "../sim/world.h"

struct World;
struct GroupDesc;
struct FightView;

struct DuelDesc {
    u32 think;          // тиков между раздумьями одного бойца (свой сдвиг у каждого — по хешу номера)
    f32 notice[2];      // тиков от начала замаха противника, пока боец его заметит: при навыке 0 и 100
    f32 noise[2];       // шум оценок (±): при навыке 0 и 100
    f32 attack[2];      // доля раздумий, в которые боец начинает удар, пока противник закрыт: при навыке 0 и 100
    f32 open;           // то же, когда противник открыт (реакция, отбит, откат после удара, конец защиты)
    f32 dodge;          // оценка нырка против удара, под который он проходит (у блока — его доля отбитых)
    f32 counter;        // оценка ответного удара, который успеет раньше удара противника
    f32 none;           // оценка «ничего не делать» против замаха
    u32 margin;         // тиков запаса у ответного удара
    u64 hash;
};

// Число [0, 1) из хеша шага, бойца и вопроса: случайность ИИ без RNG мира
inline f32 ai_roll(u64 step, u32 id, u32 q) { return (f32)(hash_combine(hash_combine(step, id), q) >> 40) * (1.0f / 16777216.0f); }
// Описание действия по номеру + 1 (0 и чужие номера — нет)
inline const ActionDesc *ai_act(const World &w, u8 act) { return act && act <= w.actions.count ? &w.actions.a[act - 1] : nullptr; }

// Разобрать data/duel.txt (формат docs/text.md, все поля обязательны). false — ошибка в err
bool duel_parse(const u8 *data, usize size, DuelDesc &out, char *err, usize err_size);
// Раздумье бойца s (у него есть цель) на шаг w.step: какое действие желать. Зовёт ai_fight (group.h) в тик
// раздумий бойца; угроза — замах любого, кто бьётся с ним, удар — только при свободном жетоне на цель
void duel_think(const World &w, const DuelDesc &d, const GroupDesc &g, const FightView &v, u32 s, FixedArray<Intent> &out);
