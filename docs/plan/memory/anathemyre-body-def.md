---
name: anathemyre-body-def
description: Шаг 2г ЗАКРЫТ 2026-10-31 16:50 (#116 тело, #118 .anim, #120 actionset + действие в мире боя, SAVE_VERSION 9)
metadata:
  type: project
  modified: 2026-10-31T16:12:19.227Z
---
Шаг 2г мастер-плана, тред «Описать тело, клипы и действия данными». PR 1 #116 влит в stage0/test-room 2026-10-31 16:12 по «да» пользователя (61f2949).

- `data/defs/bodies.txt`, запись `body human` (набор тел постоянный, моды правят поля; первая строка hit/ragdoll в записи заменяет весь список). Разбор — src/defs/bodydef.c (отдельно, чтобы не конфликтовать в defs.c).
- В мире: `g_body` (sim/body.h): height 1.80, skeleton mh_male, bone_name по 20 ролям, socket[grip, grip2, sheath, back, torch], hit[11], rag[11] (RagPart: bone, to, parent, joint RAG_ROOT/BALL/HINGE по номерам PhysJointKind, r, mass, lo/hi/cone в радианах). `g_part_hp`/`g_part_name` остались глобальными массивами, их заполняет описание.
- `body_load(data_dir)` после defs_load и look_load_skeletons: общий скелет + капсулы. `body_scale(рост)` вместо `/1.8f`.
- Пределы ragdoll придуманы мной (колено 0..150, локоть 0..145, плечо конус 90): знак сгиба и оси ставит 3.1.
- Проверка бит в бит: временный хеш поз (все клипы × 4 оружия × 8 вариантов × 16 точек) /tmp/posehash — в PR 2 сделать постоянным в tools/animbake.
- PR 2 #118 влит 2026-10-31 16:38: 33 клипа в data/anims/*.anim (AANM v1, ряды f32), список ANIM_CLIPS в sim/anim_keys.h, исходник ключей tools/animbake/keys.c, `make anims`, CI `animbake check` (эталон tools/animbake/poses.hash = 626368b31374a7a6). Правка позы — keys.c, make anims, новый хеш.
- PR 3 #120 влит 2026-10-31 16:50 (313327d): data/defs/actionsets.txt, вид actionset (src/defs/actiondef.c), sim/action.h: ActionId (падения, подъём, удар, блок, смерть...), наборы human + семьи оружия (parent human; стойка key у polearm 1, heavy 2), action_time(). В CombatWorld столбцы act, act_flags, act_t, act_t0, act_dur — сохраняются, SAVE_VERSION 9. Бои/позы/отряды бит в бит, сдвинута только сумма мира. Длительности правятся только в actionsets.txt. См. [[anathemyre-master-plan]], [[anathemyre-combat-tune]].
