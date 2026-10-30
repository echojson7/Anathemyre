// Vulkan-бэкенд слоя рендера: то, чего нет в rb.h. Остальное — функции rb.h из rb_vk.c
#ifndef ANA_RB_VK_H
#define ANA_RB_VK_H

#include "rb.h"

// Как кадр уходит в окно. FIFO — по кадровой развёртке; MAILBOX — без ожидания и без разрывов,
// лишние кадры выбрасываются; IMMEDIATE — без ожидания, возможны разрывы. Нет у драйвера — FIFO
typedef enum RbPresent { RB_PRESENT_FIFO, RB_PRESENT_MAILBOX, RB_PRESENT_IMMEDIATE } RbPresent;

// Применяется со следующего кадра (swapchain пересобирается)
void        rb_vk_set_present(RbPresent mode);
RbPresent   rb_vk_present(void);   // что выбрано на деле
const char *rb_vk_device_name(void);
// Ошибок validation с начала работы (отладочная сборка или ANA_VK_VALIDATION=1)
u32         rb_vk_validation_errors(void);

// Конвейер из готовых .spv (пути от data_dir). rb_pipe_make будет собирать путь по рецепту шейдера
RbPipe rb_vk_pipe_spv(const RbPipeDesc *d, const char *vs_spv, const char *fs_spv);

#endif
