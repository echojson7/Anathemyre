// Обход состояния для сохранения: одна функция xxx_ser на таблицу и пишет, и читает, и считает сумму.
// Что записано — то и прочитано, в том же порядке. Указатели не пишутся: столбцы арены, ссылки на таблицы
// и описания остаются как есть у живого мира, в файл идут только данные. Строки пишутся текстом,
// при чтении копируются в пул (Ser.pool)
#ifndef ANA_SER_H
#define ANA_SER_H

#include <stdio.h>

#include "types.h"

typedef enum SerMode { SER_HASH, SER_SAVE, SER_LOAD } SerMode;

typedef struct Ser {
    SerMode mode;
    FILE   *f;              // запись и чтение; при SER_HASH не нужен
    u64     hash, bytes;    // свёртка всего, что прошло через обход, и сколько байт
    bool    bad;            // сбой записи, чтения или испорченные числа: дальше ничего не делается
    bool    refused;        // файл цел, но не подходит к этой игре (нет описаний вещей): мир не читается
    char   *pool;           // строки при чтении
    u32     pool_used, pool_cap;
} Ser;

#define SER_HASH_START 1469598103934665603ull

void ser_bytes(Ser *s, void *p, size_t n);
// Строка или NULL. При чтении — копия в пуле
void ser_str(Ser *s, const char **str);
// Число строк таблицы; при чтении больше cap — файл испорчен (bad)
void ser_count(Ser *s, u32 *n, u32 cap);
// Проверка при чтении: не так — файл испорчен
void ser_check(Ser *s, bool ok);
u64  ser_fnv(u64 h, const void *p, size_t n);

#define SER(s, v)       ser_bytes((s), &(v), sizeof(v))
#define SER_N(s, p, n)  ser_bytes((s), (p), sizeof *(p) * (size_t)(n))

#endif
