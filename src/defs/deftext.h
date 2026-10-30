// Текст описаний: встроенные файлы и моды. Строка — слова и строки в кавычках, # — комментарий до конца строки.
// Читается один раз при старте; в игровом цикле ничего не разбирается
#ifndef ANA_DEFTEXT_H
#define ANA_DEFTEXT_H

#include "../core/arena.h"
#include "../core/log.h"

#define DEF_TOKENS 16
#define DEF_FILES  256

typedef struct DefTok {
    const char *s;
    u32  n;
    bool quoted;
} DefTok;

typedef struct DefLine {
    const char *file;  // путь для сообщений
    u32 line;
    u32 n;
    DefTok tok[DEF_TOKENS];
} DefLine;

typedef struct DefText {
    u32 files, mods;
    const char *path[DEF_FILES];
    const char *text[DEF_FILES];
    u32 size[DEF_FILES];
    // Обход: файл и место в нём
    u32 at_file, at_pos, at_line;
} DefText;

// Собрать *.txt: сначала base_dir, затем каждая подпапка mods_dir (её может не быть), всё по алфавиту.
// Тексты читаются в mem. false — ошибка в журнале
bool deftext_open(DefText *t, Arena *mem, const char *base_dir, const char *mods_dir);
// Следующая непустая строка; false — конец или ошибка (*bad). Обход с начала — deftext_rewind
bool deftext_next(DefText *t, DefLine *l, bool *bad);
void deftext_rewind(DefText *t);

// Ошибка в строке: «ОПИСАНИЯ: файл:строка: текст»
void def_error(const DefLine *l, const char *fmt, ...) ANA_PRINTF(2, 3);

bool def_is(const DefTok *t, const char *word);
// Номер слова в списке или -1
i32  def_word(const DefTok *t, const char *const *words, u32 count);
// Число: -12, 0.85, .5 — без экспоненты. Округление одинаковое на всех системах
bool def_num(const DefTok *t, f32 *out);
bool def_uint(const DefTok *t, u32 *out);
// Копия слова или строки в mem, с нулём на конце
const char *def_copy(Arena *mem, const DefTok *t);

#endif
