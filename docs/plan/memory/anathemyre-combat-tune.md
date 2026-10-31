---
name: anathemyre-combat-tune
description: Шаг 2е (2026-10-31): числа боя в data/defs/combat.txt, блок tuning combat, PR #113; что осталось в коде для 2г
metadata:
  type: project
  modified: 2026-10-31T15:48:45.173Z
---
Шаг 2е мастер-плана, тред «Вынести числа боя в файл». PR #113 влит в stage0/test-room 2026-10-31 15:51 (f9351d7) по «да» пользователя.

- 163 числа из sim/combat.c и ai/fighter.c → блок `tuning combat` в data/defs/combat.txt. Список ключей — X-макрос COMBAT_TUNE в src/sim/combat_tune.h → структура `g_tune` (определена в combat.c), разбор и проверка «нет числа» в defs.c по тому же списку. Новое число: имя в COMBAT_TUNE + строка в файле + g_tune.имя.
- Бит в бит: polearm_close_d2 хранится квадратом, как прежний литерал (проверка показала 1.2f² == 1.44f, но docs/combat.md осторожно пишет «не всегда»); формы «min + (1−min)·x» использованы только там, где 1−min точно равно прежнему литералу (0.5, 0.7, 0.3); для 0.6/0.4 (rating) — два ключа.
- В коде ОСТАЛИСЬ (для 2г — наборы действий): длительности KNOCKED_S, GETUP_*, PICKUP_S, пошатывание 0.4+0.6·r, лёжа 1+1.5·r, возврат 0.45·(0.7+вес/20), проводка strike_time, удержание блока/уворота +0.2. Геометрия LIE_*, расталкивание, сетка — до ragdoll 3.1.
- SAVE_VERSION не поднимали: числа не в сохранении.
- Документ: docs/combat.md. См. [[anathemyre-master-plan]], [[anathemyre-defs-stage]].
