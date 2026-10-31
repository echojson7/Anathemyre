// Отряды вдали рядом с игроком: отряд (bands.h) ближе NEAR_R к кому-то из своего отряда или ближе
// NEAR_CAM_R к точке взгляда камеры становится телами — людьми реестра (ROLE_BAND) на поле боя. Тела идут
// строем за точкой отряда, дерутся на поле боя по-настоящему. Отряд ушёл дальше — снова точка, в нём те,
// кто выжил; убитые остаются лежать, пока отряд рядом.
// Проявление и растворение — одна пара: near_show (точка → люди) и near_hide (люди → точка).
// Места в реестре и на поле боя заняты при старте (NEAR_BODIES) и освобождены: отряд занимает их и отдаёт
#ifndef ANA_BANDS_NEAR_H
#define ANA_BANDS_NEAR_H

#include "bands.h"
#include "field.h"
#include "persons.h"
#include "squad.h"

#define NEAR_BODIES 256    // мест под людей отрядов вдали: половина реестра (бой 256 на 256)
_Static_assert(NEAR_BODIES <= PERSONS_MAX / 2, "людям отрядов вдали — не больше половины реестра");
#define NEAR_PER_BAND 16   // больше тел от одного отряда не бывает; остальные идут числом
#define NEAR_R 200.0f      // м от своего человека
#define NEAR_CAM_R 400.0f  // м от точки взгляда камеры
#define NEAR_KEEP 1.25f    // уйти — дальше в столько раз

typedef struct NearBands {
    PersonId who[NEAR_BODIES];       // человек на месте; PERSON_NONE — свободно
    i16 owner[NEAR_BODIES];          // чей отряд на месте, -1 — свободно
    u8  tier[NEAR_BODIES], rank[NEAR_BODIES];  // выучка и место в строю
    u8  shown[BANDS_MAX][BAND_TIERS];  // сколько людей отряда сейчас телами
    f32 heading[BANDS_MAX];          // куда смотрит строй
} NearBands;

// Занять места: свободные места в реестре и на поле боя (после всех людей мира)
bool near_init(NearBands *n, Persons *ps, Field *f);
// Сохранение: чьи места заняты и строй первых bands отрядов
void near_ser(Ser *s, NearBands *n, u32 bands);
// Отряд bi стал людьми рядом: ветераны впереди, новички сзади, числа отряда — в тела
void near_show(NearBands *n, Bands *b, Persons *ps, Field *f, u32 bi, GroundFn ground);
// Отряд bi снова точка: живые возвращаются в его числа, места свободны
void near_hide(NearBands *n, Bands *b, Persons *ps, Field *f, u32 bi);
// Шаг мира, после ходьбы людей и отряда: тела идут за своими точками; раз в секунду (перед шагом отрядов
// с первым куском) — кто стал телами (near_show), кто снова точкой (near_hide). focus — точка взгляда камеры, dt — шаг мира
void near_tick(NearBands *n, Bands *b, Persons *ps, Field *f, const Squad *sq, V3 focus, f32 dt, GroundFn ground);

#endif
