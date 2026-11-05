// Базовые типы и макросы. Включается всеми; сам ничего не включает, кроме стандартных заголовков C
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float    f32;
typedef double   f64;
typedef size_t   usize;

#define ANA_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))

// Проверка, которая остаётся и в релизе: падение с местом в журнале
[[noreturn]] void ana_fail(const char *file, int line, const char *what);
#define ANA_CHECK(cond) do { if (!(cond)) ana_fail(__FILE__, __LINE__, #cond); } while (0)

#ifdef ANA_DEBUG
#define ANA_ASSERT(cond) ANA_CHECK(cond)
#else
#define ANA_ASSERT(cond) do { (void)sizeof(cond); } while (0)
#endif
