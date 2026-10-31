// Описания из текстовых файлов: вещи (item с компонентами), удары, качество, материалы. Формат и ключи — docs/content.md.
// Встроенные — data/defs, моды — mods/<имя>; мод добавляет записи или правит поля чужих по имени
#ifndef ANA_DEFS_H
#define ANA_DEFS_H

#include "../core/arena.h"
#include "../core/ser.h"
#include "../sim/combat.h"
#include "../sim/inventory.h"

// Списки имён в сохранении
typedef enum DefKind { DK_ITEM, DK_WEAPON, DK_ARMOR, DK_MAT, DK_COUNT } DefKind;

#define DEFS_MEM MB(2)  // тексты файлов и строки описаний

// Прочитать при старте, до всего, что берёт оружие или вещи. false — ошибка в журнале, игра не запускается
bool defs_load(Arena *mem, const char *base_dir, const char *mods_dir);
// Номер по имени вещи: её компонент оружия или брони, сама вещь. Имена, которые ставит сам код, проверены при загрузке
u8  def_weapon(const char *id);
u8  def_armor(const char *id);
u16 def_item(const char *id);

// Сохранение пишет имена описаний; при чтении — какой номер у того же имени теперь.
// Имени больше нет — Ser.refused, мир не читается
typedef struct DefMap {
    bool same;
    u16  to[DK_COUNT][ITEM_CAP];
} DefMap;
void defs_ser(Ser *s, DefMap *map);
// Номера в прочитанном мире под нынешние описания
void defs_remap_desc(FighterDesc *d, const DefMap *m);
void defs_remap_sheet(CharSheet *c, const DefMap *m);
void defs_remap_combat(CombatWorld *w, const DefMap *m);
void defs_remap_item(ItemStack *it, const DefMap *m);

#endif
