#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#include "clock.h"

static struct {
    char     line[LOG_LINES][LOG_LINE_MAX];
    unsigned total;   // сколько строк записано за всё время
    bool     quiet;
} L;

static void log_write(FILE *f, const char *tag, const char *fmt, va_list ap)
{
    fprintf(f, "[%09.3f] %s ", time_now(), tag);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
}

static void log_keep(const char *fmt, va_list ap)
{
    vsnprintf(L.line[L.total % LOG_LINES], LOG_LINE_MAX, fmt, ap);
    L.total++;
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_keep(fmt, ap);
    va_end(ap);
    if (L.quiet) return;
    va_start(ap, fmt);
    log_write(stdout, "ЗАП", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_keep(fmt, ap);
    va_end(ap);
    va_start(ap, fmt);
    log_write(stderr, "СБОЙ", fmt, ap);
    va_end(ap);
}

void log_stat(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_write(stdout, "ЗАП", fmt, ap);
    va_end(ap);
}

void log_set_echo(bool on) { L.quiet = !on; }

unsigned log_count(void) { return L.total < LOG_LINES ? L.total : LOG_LINES; }

const char *log_line(unsigned i)
{
    unsigned first = L.total < LOG_LINES ? 0 : L.total - LOG_LINES;
    return L.line[(first + i) % LOG_LINES];
}
