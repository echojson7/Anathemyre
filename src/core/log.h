// Журнал: сухие записи с отметкой времени
#ifndef ANA_LOG_H
#define ANA_LOG_H

#if defined(__MINGW32__)
#define ANA_PRINTF(a, b) __attribute__((format(gnu_printf, a, b)))
#elif defined(__GNUC__)
#define ANA_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ANA_PRINTF(a, b)
#endif

void log_info(const char *fmt, ...) ANA_PRINTF(1, 2);
void log_error(const char *fmt, ...) ANA_PRINTF(1, 2);

#endif
