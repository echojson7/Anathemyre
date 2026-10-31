// Файл сохранения: заголовок (метка, версия, раскладка сборки, зерно, тик, размер и сумма тела) и тело —
// обход мира (ser.h). Пишется во временный файл и переименовывается: старое сохранение не портится.
// Читается в два прохода: сначала сумма всего файла, и только если она сошлась — в мир
#ifndef ANA_SAVE_H
#define ANA_SAVE_H

#include "../core/ser.h"

#define SAVE_VERSION 6

typedef void (*SerFn)(Ser *s, void *user);

typedef struct SaveInfo {
    u64 layout;  // сумма размеров таблиц и ёмкостей: другая сборка — другой мир в памяти
    u64 seed;
    u64 tick;
    u64 bytes;   // тело
} SaveInfo;

// Записать: false — не записано (причина в журнале), *info->bytes — размер тела
bool save_write(const char *path, SaveInfo *info, SerFn fn, void *user);
// Прочитать: заголовок должен совпасть по раскладке и зерну; pool — память под строки.
// false — мир не тронут, если файл не прошёл проверку до чтения таблиц
bool save_read(const char *path, SaveInfo *info, SerFn fn, void *user, char *pool, u32 pool_cap);

#endif
