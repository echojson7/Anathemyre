// Вид описаний actionset (data/defs/actionsets.txt): наборы действий. Разбор строк для defs.c; формат — docs/combat.md
#ifndef ANA_ACTIONDEF_H
#define ANA_ACTIONDEF_H

#include "deftext.h"

void actiondef_reset(void);
// Заголовок «actionset имя»: human или класс оружия. Набор постоянный: моды правят действия, новых наборов нет
bool actiondef_header(const DefLine *l);
// parent — первой строкой: набор берёт все действия родителя
bool actiondef_parent(const DefLine *l, u32 lines);
bool actiondef_field(const DefLine *l);
bool actiondef_check(void);

#endif
