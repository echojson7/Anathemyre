// Действия людей и удар (этап 10, docs/people.md «Удар»): действие — запись данных (data/actions/*.txt) поверх
// клипа и его меток. Шаг мира ведёт его по запечённой дорожке клипа (.trk) без смешивания: корень, капсулы тела
// и отрезок клинка на тик. Попадание — клинок против 11 капсул другого человека в окне удара; стоп на попадании
// (hit-stop) — замершие тики у обоих; реакция задетого — отброс и замирание движения
#pragma once
#include "../anim/track.h"
#include "../core/mathf.h"
#include "../core/wpos.h"

constexpr u32 ACTIONS_MAX = 8;
constexpr u32 BODY_PARTS = 11;   // как PARTS в tools/retarget/bake.py

struct ActionDesc {
    u64 name;                // хеш имени файла без .txt
    char label[24];
    u64 clip;                // хеш имени клипа
    char clip_name[32];
    const Track *track;      // дорожка клипа на скелете game_engine
    u16 hit_a, hit_b;        // окно удара, тики 60 Гц от начала (метка hit клипа, ключи ×2), включительно
    u16 end;                 // тик конца действия (end, ключ ×2)
    u16 cancel;              // с какого тика можно отменить (метка cancel клипа); 0xFFFF — нельзя
    f32 reach_min, reach_max;  // начинать удар, когда до цели столько, м
    u8 hitstop;              // тиков стопа на попадании у обоих
    f32 push;                // отброс задетого, м
    u16 react;               // тиков реакции задетого
    Turn aim;                // куда идёт клинок в середине окна удара в осях начала действия (корень клипа
                             // поворачивается по ходу удара): бьющий встаёт так, чтобы цель была по aim
};

// Описания действий и дорожки ходьбы и бега (капсулы того, кто не в действии). Неизменны после создания мира,
// хеш — в хеше неизменного (world_static_hash)
struct ActionTable {
    ActionDesc a[ACTIONS_MAX];
    u32 count;
    u32 strike;              // номер действия, которое делает приказ «атаковать»
    const Track *walk, *run;
    u64 hash;
};

// Разобрать описание действия (формат docs/text.md, поля — docs/people.md «Удар»). Окна удара и отмены —
// из меток клипа: передаёт app (hit, cancel — тики). false — ошибка в err
bool action_parse(const u8 *data, usize size, ActionDesc &out, char *err, usize err_size);
// Направление удара (aim) по дорожке: остриё в середине окна удара с поворотом корня клипа
Turn action_aim(const ActionDesc &a);
// Хеш чисел действия (в хеш неизменного)
u64 action_hash(const ActionDesc &a);

// Капсулы тела в мире на этот тик относительно опорной точки (её чанк и смещение): начало, конец, радиус, м
struct BodyCaps {
    f32 a[BODY_PARTS][3], b[BODY_PARTS][3], r[BODY_PARTS];
    u32 n;
};
struct World;
// Капсулы человека s относительно точки ref (дорожка действия на его тике или ходьба и бег по фазе шага)
void people_body(const World &w, u32 s, const WPos &ref, BodyCaps &out);
// Отрезок клинка человека s на тике действия t относительно ref; false — не в действии или клип без оружия
bool people_blade(const World &w, u32 s, u32 t, const WPos &ref, f32 a[3], f32 b[3]);
// Имена частей тела (по порядку дорожек)
const char *body_part_name(u32 part);
