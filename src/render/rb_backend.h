// Внутреннее для слоя rb: рецепты SPIR-V (rb.c) и сборка конвейера в rb_vk.c
#ifndef ANA_RB_BACKEND_H
#define ANA_RB_BACKEND_H

#include "rb.h"

// Стадии шейдера в рецепте SPIR-V
typedef enum RbStage { RB_STAGE_VERT, RB_STAGE_FRAG, RB_STAGE_COMP } RbStage;

// Рецепт SPIR-V стадии: «стадия [-DИМЯ[=ЗНАЧ] ...] файлы» — строка tools/spv/list.txt.
// Имя .spv из рецепта: файлы через «+», определения через «~», стадия: frame.glsl+scene.vert~SHADOW_PASS.vert.spv
bool rb_spv_recipe(const RbPipeDesc *d, RbStage stage, char *line, size_t line_size, char *name, size_t name_size);

// rb_pipe_make (rb.c) пишет рецепт в ANA_SPV_LIST и зовёт сборку Vulkan
RbPipe rb_vk_pipe_make(const RbPipeDesc *d);

#endif
