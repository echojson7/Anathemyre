// Журнал: строки с уровнем в stderr и в приёмник (файл ставит app через platform).
// Не для шага мира: форматирование чисел зависит от CRT.
#pragma once
#include "types.h"

enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR };

typedef void (*LogSink)(const char *line, usize len, void *user);
void log_set_sink(LogSink sink, void *user);
void log_write(LogLevel level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define log_info(...)  log_write(LOG_INFO, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN, __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __VA_ARGS__)

// u64 в 16 шестнадцатеричных цифр без CRT: хеши печатаются одинаково на всех целях. buf ≥ 17
char *hex_u64(u64 v, char *buf);
