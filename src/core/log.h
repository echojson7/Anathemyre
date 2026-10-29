// Журнал: сухие записи с отметкой времени.
// В игре консоль получает только замеры (log_stat) и сбои; остальное — в журнал на экране (кольцо строк).
#ifndef ANA_LOG_H
#define ANA_LOG_H

#include <stdbool.h>

#if defined(__MINGW32__)
#define ANA_PRINTF(a, b) __attribute__((format(gnu_printf, a, b)))
#elif defined(__GNUC__)
#define ANA_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ANA_PRINTF(a, b)
#endif

#define LOG_LINES 128
#define LOG_LINE_MAX 192

void log_info(const char *fmt, ...) ANA_PRINTF(1, 2);   // в журнал; в консоль, пока включено эхо
void log_error(const char *fmt, ...) ANA_PRINTF(1, 2);  // в журнал и всегда в консоль
void log_stat(const char *fmt, ...) ANA_PRINTF(1, 2);   // только в консоль: к/с и замеры
// Эхо log_info в консоль: включено, пока нет окна и в режимах без окна
void log_set_echo(bool on);
// Строки журнала: 0 — самая старая из хранимых
unsigned    log_count(void);
const char *log_line(unsigned i);

#endif
