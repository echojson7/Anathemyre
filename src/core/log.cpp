#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static LogSink g_sink = nullptr;
static void *g_sink_user = nullptr;

void log_set_sink(LogSink sink, void *user) {
    g_sink = sink;
    g_sink_user = user;
}

void log_write(LogLevel level, const char *fmt, ...) {
    static const char *tags[] = {"", "внимание: ", "ошибка: "};
    char line[2048];
    int n = snprintf(line, sizeof line, "%s", tags[level]);
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (usize)n - 1, fmt, ap);
    va_end(ap);
    usize len = (usize)n + (m < 0 ? 0 : (usize)m);
    if (len > sizeof line - 2) len = sizeof line - 2;
    line[len++] = '\n';
    line[len] = 0;
    fwrite(line, 1, len, stderr);
    if (g_sink) g_sink(line, len, g_sink_user);
}

char *hex_u64(u64 v, char *buf) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 15; i >= 0; --i) {
        buf[i] = digits[v & 15];
        v >>= 4;
    }
    buf[16] = 0;
    return buf;
}

void ana_fail(const char *file, int line, const char *what) {
    log_error("проверка не прошла: %s (%s:%d)", what, file, line);
    fflush(stderr);
    abort();
}
