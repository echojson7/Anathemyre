---
name: anathemyre-vulkan-stage2
description: Anathemyre — этап 2 Vulkan (устройство, swapchain, вывод, vk.h из vk.xml, офлайн SPIR-V): PR #82 влит 2026-10-30; как устроено и что дальше
metadata:
  type: project
  modified: 2026-10-30T06:47:04.456Z
---

Этап 2 плана [[anathemyre-vulkan]] сделан отдельным тредом «Собрать базу Vulkan» 2026-10-30: PR #82 (ветка claude/project-thread-b4f58z → stage0/test-room) влит 2026-10-30 12:40 по «да» пользователя; PR #81 (этап 1, rb_gl.c) влит тогда же, итог собирается чисто.

Ключевые решения (не видны из кода сразу):
- rb_vk.c определяет те же имена rb_*, что rb_gl.c, поэтому пока исключён из игры (filter-out в Makefile) и собирается только в `make vkcheck`. Переключатель РЕНДЕР: GL / VULKAN потребует таблицы функций или переименования — решить на этапе 3.
- Окно в Vulkan — своя картинка RGBA8 + D32F, кадр в памяти как в GL; переворот только blit'ом в swapchain. Глубина GL через VK_EXT_depth_clip_control; лицевая грань GL CCW = VK_FRONT_FACE_CLOCKWISE.
- vk.h: tools/vkgen (python, vk.xml v1.3.275 со сверкой sha256), список в want.txt; CI делает `gen.sh --check`.
- SPIR-V: tools/spv/list.txt → data/spv/*.spv + index.txt (sha256 исходника); CI сверяет хеши и spirv-val, а не байты .spv (версии glslang разные). Этап 1 склеивает шейдер как "#version 450 core" + defines + rb.glsl + файлы; для Vulkan нужен -DVULKAN.
- Проверка: vkcheck --verify на lavapipe (VK_ICD_FILENAMES=lvp_icd.json) под Xvfb и ASan: 0 ошибок validation, пиксели и переворот на экране верны.
- В контейнере (Mesa 25.2) отладочная игра под LSan даёт утечку в выгруженном GL-драйвере и на чистой stage0/test-room — не наша.

**How to apply:** этап 3 — буферы/текстуры со своим распределителем, push descriptors по сквозным номерам привязок из docs/render-backend.md, цели-текстуры и барьеры, рецепт rb_pipe_make → .spv. См. docs/vulkan.md.
