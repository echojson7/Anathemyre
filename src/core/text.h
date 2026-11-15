// Свой текстовый формат описаний (docs/text.md): строки из слов через пробелы, «#» — комментарий до конца строки.
// Числа разбираются своим кодом, а не strtof/scanf: CRT Windows и glibc могут округлить по-разному,
// а описания читает и шаг мира (docs/invariants.md)
#pragma once
#include "types.h"

struct Str {
    const char *p = nullptr;
    u32 n = 0;
};
bool str_eq(Str s, const char *lit);
// Устойчивый хеш имени — как hash_str у строки C (содержимое по имени)
u64 str_hash(Str s);

constexpr u32 TEXT_MAX_WORDS = 16;
struct TextLine {
    u32 line = 0;   // номер строки в файле, с 1
    u32 count = 0;  // слов
    Str w[TEXT_MAX_WORDS];
};

struct TextReader {
    const char *p = nullptr;
    const char *end = nullptr;
    u32 line = 0;
};
inline TextReader text_reader(const u8 *data, usize size) {
    return {(const char *)data, (const char *)data + size, 0};
}
// Следующая непустая строка (пустые и комментарии пропускаются); false — конец текста.
// Слов больше TEXT_MAX_WORDS — лишние отбрасываются, count = TEXT_MAX_WORDS + 1 (ошибку пишет вызывающий)
bool text_next(TextReader &r, TextLine &out);

// Десятичное число: [-]цифры[.цифры], до 18 значащих цифр. Результат одинаков на всех целях:
// целая мантисса делится на точную степень 10 в f64 и округляется в f32
bool parse_f32(Str s, f32 &out);
bool parse_i32(Str s, i32 &out);
bool parse_u32(Str s, u32 &out);
// Сообщение об ошибке разбора без printf (запрет в шаге мира): «строка N: что», при line 0 — только «что»
void text_err(char *err, usize cap, u32 line, const char *what);
// Хеш текста описания без '\r': файл с окончаниями строк Windows даёт тот же хеш
u64 text_hash(const u8 *data, usize size);
