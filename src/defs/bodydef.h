// Вид описаний body (data/defs/bodies.txt): тело — скелет, роли костей, крепления, хит-капсулы, части ragdoll.
// Разбор строк для defs.c; формат — docs/body.md
#ifndef ANA_BODYDEF_H
#define ANA_BODYDEF_H

#include "deftext.h"

// Перед чтением файлов
void bodydef_reset(void);
// Заголовок «body human». Набор тел постоянный: моды правят поля, новых не добавляют
bool bodydef_header(const DefLine *l);
bool bodydef_field(const DefLine *l, Arena *mem);
// После всех файлов: у тела есть всё, без чего оно не работает
bool bodydef_check(void);

#endif
