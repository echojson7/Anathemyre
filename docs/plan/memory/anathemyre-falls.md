---
name: anathemyre-falls
description: Падение, подъём, смерть (2026-10-30): оба этапа сделаны, PR #89 влит в stage0/test-room 14:26; длительности, LIE_*, FS_PICKUP, SAVE_VERSION 2
metadata:
  type: project
---
Тред «Разобрать анимации падения, вставания и смерти», 2026-10-30. Пользователь 13:37: «все этапы выполняй».
Док разбора: https://claude.ai/artifact/UR33SCXC6kzeVGFHu2mMz2. Кадры до: /mnt/project-files/anathemyre/shots/falls/, после: .../falls/new/ (листы n_*.jpg).

Сделано (PR #89 влит 2026-10-30 14:26 по «да» пользователя, слит с #86 обхода лежащих):
- Длительности: падение 1.1 с, подъём со спины 2.2, ничком 2.0 (одинаково для всех, к навыкам НЕ привязано — решение пользователя), обморок 1.1, смерть 0.8, истёк кровью 2.0, оседает 1.1, переворот 1.2, подбор оружия 0.9, сдача 1.6.
- Симуляция: CombatWorld.fall (FALL_BACK/FRONT/LEFT/RIGHT по углу к бьющему), prone = LIE_BACK/FRONT/SIDE_L/SIDE_R (на боку только убитый сбоку и истёкший кровью; ставят go_down/begin_getup/check_body до смены состояния; around_bodies обходит бок поперёк), loose (оружие выронено с шансом 15–50%), FS_PICKUP «ПОДНИМАЕТ»; SAVE_VERSION 2.
- Вид: клипы CLIP_DIE/BLEED/SINK/TURN/PICKUP, кроссфейд 0.35 с, settle_limbs, наклон по склону (CharPose.slope_x/z, Figures.slope), разброс трупов по seed, BODYFALL по касанию (fight_anim_contact).
- docs/animations.md раздел «Падение, подъём, смерть».

Правило пользователя 2026-10-30 13:03: длительности всех анимаций реалистичны относительно друг друга ([[anathemyre-anim-timing]]).

Снимки клипов: временный патч стенда falls_sheet.patch (env ANA_CLIP/ANA_T/ANA_PRONE/ANA_LIMP/ANA_FALL/ANA_DEAD/ANA_SIDE/ANA_SEED/ANA_W/ANA_SLX, камера ANA_DIST/PITCH/CYAW/TY), скрипты clip.sh/one.sh/sheet.py рядом; в коммит не идёт. ANA_PRONE и ANA_DEAD срабатывают по наличию переменной, а не по значению.

Витрина: PR #93 влит 2026-10-31 11:39 — 8 кругов вдоль северной стены Мерильни (z −71.4, x 12…−16), таблица g_loops/ClipLoop, шаг ОБРАТНО = тот же клип назад; 14-я лампа у входа. Кадры: /mnt/project-files/anathemyre/shots/falls/hall/. Камера на ряд: --cam X -72 3.1416 0.28 8 --hall-t S.
После ragdoll (тред «Перезаложить физику боя и ragdoll», этапы 1–2) площадки CL_DIE_* и CL_KNOCK заменить на ragdoll + подъём из позы; передано треду физики 2026-10-31.
