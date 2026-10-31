// Отряды вдали рядом с игроком: отряд (bands.h) ближе NEAR_R к кому-то из своего отряда или ближе
// NEAR_CAM_R к точке взгляда камеры становится телами — людьми таблицы (ROLE_BAND) на поле боя. Тела идут
// строем за точкой отряда, дерутся на поле боя по-настоящему. Отряд ушёл дальше — снова точка, в нём те,
// кто выжил; убитые остаются лежать, пока отряд рядом.
// Места в таблице людей и на поле боя заняты при старте (NEAR_BODIES) и освобождаются: из таблицы никто не уходит
#ifndef ANA_BANDS_NEAR_H
#define ANA_BANDS_NEAR_H

#include "bands.h"
#include "field.h"
#include "people.h"
#include "squad.h"

#define NEAR_BODIES 48     // мест под людей отрядов вдали
#define NEAR_PER_BAND 16   // больше тел от одного отряда не бывает; остальные идут числом
#define NEAR_R 200.0f      // м от своего человека
#define NEAR_CAM_R 400.0f  // м от точки взгляда камеры
#define NEAR_KEEP 1.25f    // уйти — дальше в столько раз

typedef struct NearBands {
    u32 first;                       // первое место в таблице людей
    i16 owner[NEAR_BODIES];          // чей отряд на месте, -1 — свободно
    u8  tier[NEAR_BODIES], rank[NEAR_BODIES];  // выучка и место в строю
    u8  shown[BANDS_MAX][BAND_TIERS];  // сколько людей отряда сейчас телами
    f32 heading[BANDS_MAX];          // куда смотрит строй
} NearBands;

// Занять места: дописать свободных людей в таблицу и на поле боя (после people_join_field)
bool near_init(NearBands *n, People *p, Field *f);
// Сохранение: чьи места заняты и строй первых bands отрядов
void near_ser(Ser *s, NearBands *n, u32 bands);
// Шаг мира, после ходьбы людей и отряда: тела идут за своими точками; раз в секунду (перед шагом отрядов
// с первым куском) — кто стал телами, кто снова точкой. focus — точка взгляда камеры, dt — шаг мира
void near_tick(NearBands *n, Bands *b, People *p, Field *f, const Squad *sq, V3 focus, f32 dt, GroundFn ground);

#endif
