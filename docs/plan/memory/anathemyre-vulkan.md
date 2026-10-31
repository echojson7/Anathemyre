---
name: anathemyre-vulkan
description: Anathemyre — Vulkan вторым бэкендом: этапы 1–3 влиты (PR #81, #82, #90), GL запасной, как добавлять шейдеры (рецепты SPIR-V), что осталось в этапах 4–5
metadata:
  type: project
  modified: 2026-10-30T15:03:25.366Z
---

Тред «Перевести рендер на Vulkan» (cmsg_01KiBbXAPgB2yUMeJuD6Zt8DVUbk2DvBSNTMiytGGByFwT). План (Claude Doc): https://claude.ai/artifact/Gyxz71gChHGV8MbB3dnuQT, «да» 2026-10-30 06:13.

Суть: Vulkan вторым бэкендом за слоем rb (src/render/rb.h). Решение пользователя 2026-10-30 13:41: GL ОСТАЁТСЯ запасным и эталоном кадров; Vulkan станет основным только после проверки на RTX. Не предлагать удалить GL без новой причины. Аппаратных лучей (RTX) и DLSS не будет никогда.
Решения: Vulkan 1.3 (dynamic rendering, push descriptors, robustness2 nullDescriptor), свой vk.h из vk.xml, свой распределитель без VMA, SPIR-V офлайн (glslang), .spv в репо. Точность: GL бит в бит, Vulkan против GL ≤ 2/255.

Сделано:
- Этап 1 слой rb — PR #81 влит 12:40. Этап 2 остов — PR #82 влит.
- Этап 3 вся сцена — PR #90 влит 2026-10-30 15:03 по «да». Бэкенды за таблицей RB_FUNCS (rb_backend.h, rb.c). НАСТРОЙКИ «РЕНДЕР: VULKAN (ПОСЛЕ ПЕРЕЗАПУСКА)» → anathemyre.cfg рядом с data (`render vulkan|gl`), ключи --vk/--gl, при сбое Vulkan откат на GL. CI снимает кадр Vulkan на lavapipe под ASan.

Шейдеры на Vulkan: каждый вариант = строка в tools/spv/list.txt (`stage [-DNAME] файлы`). `ANA_SPV_LIST=путь` — игра дописывает недостающие, затем ./tools/spv/build.sh. Устаревшие рецепты убирать руками, иначе --check в CI падает. Новый шейдер света без рецепта = на Vulkan проход не строится.

2026-10-31 шаг 2.0: PR 1 (#106) Vulkan по умолчанию влит. PR 2а (#108): GL удалён целиком (rb_gl.c, gl.c, GLX/WGL, rb_select, cfg, --gl/--vk); rb_* прямо в rb_vk.c; сравнения убраны (шум хешем, AO вполовину, MDI, кеш поз выкл, u_skinned теперь 0 статика/1 люди из кеша/2/3); skin_prepare зовут сцена, портреты и тени огней, смещения SSBO кратны 256 Б; CI на lavapipe, 4 задачи. Дальше PR 2б: VRS через VK_KHR_fragment_shading_rate (+3.5–6 %) и дорогая отправка кадра на CPU (0.67 мс против 0.32 у GL). lavapipe и glslang в контейнере — apt (sudo есть).

Осталось после 2б: точечные барьеры и раскладки вместо полного барьера, выбор показа в меню.

Свет идёт параллельно через слой rb, тред света cmsg_01KiBbXAPgB2yUMeJuD6Zt8D79KxguyTvckcqT8ejVGQNR. См. [[anathemyre-lighting-plan]], [[anathemyre-perf-stages]].

Перед слиянием любого PR в stage0/test-room, который трогает data/shaders: если test-room сдвинулась после последнего прогона CI этого PR, сначала влить её в ветку PR и дождаться зелёного CI. **Why:** 2026-10-30 #88 (frame.glsl) влили сразу после #90 по старому прогону CI, и .spv в test-room устарели: CI красный у всех, Мерильня под Vulkan светлая. Проверка `build.sh --check` в CI есть, ловит, но только на свежей базе.
