// Vulkan-бэкенд слоя рендера: то, чего нет в rb.h. Остальное — rb.h после rb_select(RB_API_VULKAN)
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

#endif
