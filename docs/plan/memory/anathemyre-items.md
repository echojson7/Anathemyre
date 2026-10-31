---
name: anathemyre-items
description: Шаг 2в «вещи одной записью»: item с компонентами, ItemStack 8 байт, item_move — статус PR и решения (2026-10-31)
metadata:
  type: project
  modified: 2026-10-31T15:51:38.067Z
---
Тред «Свести вещи в одну запись» (cmsg_01KiBbXAPgB2yUMeJuD6Zt8DQkaoBJaCHKBGc8d8rWbJ2r), промпт /mnt/project-files/anathemyre/plan/step-2v-prompt.md. План одобрен пользователем «да» 2026-10-31 15:37.

PR 1 — #114 (ветка claude/project-thread-w96qgp → stage0/test-room), открыт 2026-10-31:
- data/defs: только `item`; общие поля (name, size, weight, stack, made_of, look, hides, desc), компоненты строкой: weapon, armor, wear <слот> (carry), light, container (grid), phys (box). Поле материала названо `made_of`, потому что `material` — заголовок записи.
- Бой видит прежние g_weapons/g_armors по тем же номерам (компоненты нумеруются в порядке вещей) — combat.c не тронут, бой бит в бит.
- ItemStack {def u16, seed u16, mat u8, quality u8, count u8, pad}; запись 0 = «ничего».
- GEAR_* убраны: g_gear_file собирается из look, GEAR_CAP 24, причёски с GEAR_CAP.
- Звон брони по материалу (impact metal), голос оружия — `sound имя`, sfx.c по имени.
- Факел — item torch с light; torch_light в figures.c не тронут (сбора огней 2б нет), мастер-тред уведомлён.
- SAVE_VERSION 7.

Часть 2 положена в тот же PR #114 (одна ветка): ItemPlace (IP_NONE/GROUND/HAND/BODY/BAG/BOX; префикс IP_, потому что PLACE_NONE занят в world/places.h) и item_move(g,p,from,to) в sim/items.c; журнал CH_ITEM через Ground.chron. gitem и Squad.pick — номера на земле. Выроненное уходит из руки листа, field armed() учитывает gitem>=0. HUD внутри листа по-старому (inv_*/sheet_equip). IP_BOX — только место для 2б.
#114 ВЛИТ 2026-10-31 16:01 (389dee2), мастер-тред уведомлён. Шаг 2в закрыт.

Проверка «бит в бит» делалась сравнением со сборкой origin в /tmp/old (git worktree); общая сумма мира меняется из-за раскладки, сравнивали подсумму без листов людей и стопок на земле. Локально lavapipe ставится `sudo apt-get install mesa-vulkan-drivers`.

См. [[anathemyre-master-plan]], [[anathemyre-reuse]], [[anathemyre-materials-impact]].
