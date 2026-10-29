#include "log.h"

#include <stdarg.h>
#include <stdio.h>

#include "clock.h"

static void log_write(FILE *f, const char *tag, const char *fmt, va_list ap)
{
    fprintf(f, "[%09.3f] %s ", time_now(), tag);
    vfprintf(f, fmt, ap);
    fputc('\n', f);
    fflush(f);
}

void log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_write(stdout, "ЗАП", fmt, ap);
    va_end(ap);
}

void log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_write(stderr, "СБОЙ", fmt, ap);
    va_end(ap);
}
