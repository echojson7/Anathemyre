// Журнал событий мира (летопись): заметные события короткими записями одного размера — когда, что, кто,
// кого, где, сколько. Из него потом читают память, отношения и слухи. Это история мира, а не связь систем:
// системы вызываются напрямую, а сюда только пишут. Кольцо: самые старые затираются.
// Текстовый журнал на экране (log.h) — отдельно, его строки пишут сами системы
#ifndef ANA_CHRONICLE_H
#define ANA_CHRONICLE_H

#include "../core/ser.h"

#define CHRON_CAP 16384        // записей в кольце (~0.5 МБ)
#define CHRON_NOBODY 0xFFFFFFFFu

typedef enum ChronKind {
    CH_BAND_CLASH,    // отряды a и b сошлись вдали: n против m
    CH_BAND_ROUT,     // отряд a бежит, осталось n из m
    CH_BAND_WIPED,    // отряд a истреблён
    CH_BAND_NEAR,     // отряд a рядом: n тел из m людей
    CH_BAND_FAR,      // отряд a снова вдали: вернулось n тел из m
    CH_KO,            // человек a без сознания
    CH_DEATH,         // человек a мёртв
    CH_SURRENDER,     // человек a сдался
    CH_ITEM,          // вещь n перешла от a к b (CHRON_NOBODY — земля или никто); m = место откуда × 16 + куда (PlaceKind)
    CH_KIND_COUNT
} ChronKind;

typedef struct ChronRec {
    u64 tick;
    u8  kind;
    u8  from, to;     // место или дорога отрядов вдали (places.h); 255 — не на графе
    u8  pad;
    u32 a, b;         // номер отряда или человека; CHRON_NOBODY — никого
    i32 n, m;
    f32 x, z;         // где на карте
} ChronRec;

typedef struct Chronicle {
    u64      tick;    // шаг мира: ставит мир перед шагом систем
    u64      total;   // записей с начала; последняя — rec[(total - 1) % CHRON_CAP]
    u32      counts[CH_KIND_COUNT];  // сколько каждого вида с начала (для сводок)
    ChronRec rec[CHRON_CAP];
} Chronicle;

// Записать событие с текущим шагом. c == NULL — некуда (замеры без мира)
void chron_add(Chronicle *c, ChronRec r);
// k-я с конца: 0 — последняя; NULL — столько нет
const ChronRec *chron_back(const Chronicle *c, u32 k);
void chron_ser(Ser *s, Chronicle *c);

#endif
