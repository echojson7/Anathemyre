---
name: anathemyre-persons
description: Шаг 2д ЗАКРЫТ 2026-10-31 18:05: #121 реестр Persons 512, #122 один мир боя, #123 позы/окно мешка/512/battle512, SAVE_VERSION 12
metadata:
  type: project
  modified: 2026-10-31T17:16:37.732Z
---
Тред шага 2д «Свести людей в один реестр», промпт /mnt/project-files/anathemyre/plan/step-2d-prompt.md.

PR 1 #121 влит в stage0/test-room 2026-10-31 17:16 (692c50e) по «да» пользователя:
- src/sim/persons.{c,h}: Persons, PERSONS_MAX 512, PersonId = место (16 бит) | поколение; persons_add (нижнее свободное), persons_free, persons_find.
- Место и курс только в столбцах x/y/z/heading; Motion = Player без места (макрос MOTION_FIELDS), person_body/person_set_body/person_walk. CombatWorld поля читает столбцы реестра (combat_bind_place, place_ext).
- Слот поля = место в реестре, field_join вместо field_add/field_reuse; FF_* и TEAM_* теперь в persons.h.
- People = роли (who[] + поведение), Squad.member[] PersonId (squad_at/squad_move/squad_pos/squad_sheet), NearBands.who[], near_show/near_hide.
- Кэш вида не сохраняется, persons_relook_all после чтения. SAVE_VERSION 10.
- Проверено бит в бит временным хешем мира (без раскладки) против базы; бенчи совпали.
- Оставлено ради бит в бит: в field_tick погоня за целью с бо́льшим номером в бою целится в px/pz (место до тика) — убрать в PR 2 (суммы сдвинутся).

PR 2 #122 влит 2026-10-31 17:34 (adef654): раны/навыки/сложение/act* в столбцах Persons (combat_bind_body), CharSheet без FighterDesc (person_fighter/person_set_fighter, person_random, person_speed), погоня в нынешнее место цели (field 400 сдвинулся: своих 194→198, чужих 32→38), SAVE_VERSION 11.

PR 3 #123 влит 2026-10-31 18:05 (577cec8): PEOPLE_MAX=512, NEAR_BODIES 256, окно мешка через item_move (вещь на курсоре остаётся на месте, Hud.held_*; GroundAsks с местом вещи; item_same), figures_bones, --bench battle512 (people 256 256; 4 потока: поле 0.47 мс, позы 0.75 мс, сумма 77175dccbc262c85), SAVE_VERSION 12. ШАГ 2д ЗАКРЫТ. Замечено: в бою слоты с меньшим номером проигрывают (256 против 29) — разобрано в шаге 3.1 пункт 0:
все думают в одни тики, удары тика считаются до применения. battle512 стало 248 и 74, перекос уехал
со слота на сторону. Явный ключ корзин рендера (performance.md п.6) — render.c, вне шага 2д.

См. [[anathemyre-master-plan]], [[anathemyre-body-def]].

В контейнере для CI-проверок нужны apt: mesa-vulkan-drivers, libclang-rt-18-dev, gcc-mingw-w64-x86-64.

Решение пользователя 2026-10-31 17:21: арену НЕ переделывать (ни отдельной сцены, ни отдельной логики) — оставить как есть у стартового зала, она для тестов боя и в игре её потом не будет. PR 2 без пункта про арену.
