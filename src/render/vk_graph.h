// Граф кадра (docs/graph.md): проход объявляет, какие ресурсы он читает и пишет и как. Барьеры, переходы
// картинок, метки времени F3 и метки для разбора сбоя GPU ставит граф, а не код прохода. Порядок проходов —
// порядок объявления. Состояние ресурса (кто последним писал, кто читал после, раскладка картинки) живёт между
// кадрами: буферы общие для кадров в полёте, а очередь одна, поэтому барьер кадра N+1 упорядочивает и с кадром N
#pragma once
#include "vk_state.h"

constexpr u32 RG_MAX_RES = 64, RG_MAX_PASSES = 48, RG_MAX_USES = 24;

// Как проход трогает ресурс. Запись включает чтение тем же проходом (атомики, чтение-запись)
enum RgUse : u8 {
    RG_CS_READ,        // буфер: чтение в compute
    RG_CS_WRITE,       // буфер: запись в compute
    RG_VS_READ,        // буфер: чтение в вершинном шейдере
    RG_FS_READ,        // буфер: чтение во фрагментном
    RG_INDIRECT,       // буфер: аргументы и счётчик косвенного вызова
    RG_COPY_READ,      // буфер: источник копии
    RG_COPY_WRITE,     // буфер: приёмник копии или заливки (vkCmdFillBuffer для слоёв — тоже копия)
    RG_IMG_CS_READ,    // картинка GENERAL: imageLoad в compute
    RG_IMG_CS_WRITE,   // картинка GENERAL: imageStore в compute
    RG_IMG_FS_READ,    // картинка GENERAL: imageLoad во фрагментном
    RG_COLOR,          // картинка: цель цвета (с loadOp LOAD — и чтение)
    RG_DEPTH,          // картинка: глубина, тест и запись (storeOp DONT_CARE для слоёв — тоже запись)
    RG_IMG_COPY_READ,  // картинка: источник копии
    RG_PRESENT,        // картинка swapchain: к показу
    RG_USES
};
// Старое содержимое картинки не нужно: переход из UNDEFINED
constexpr u32 RG_DISCARD = 1;

typedef u8 RgRes;  // номер ресурса; 0 — нет
// Регистрация при создании (имя — для журнала, слоёв проверки и разбора сбоя)
RgRes rg_buffer(const char *name);
RgRes rg_image(const char *name, VkImageAspectFlags aspect);
// Картинка ресурса создана заново: состояние с нуля. Буфер создан заново — rg_reset
void rg_bind_image(RgRes r, VkImage img);
void rg_reset(RgRes r);
// Картинка swapchain этого кадра: из UNDEFINED, после ожидания семафора забора на стадии вывода цвета
void rg_import_swapchain(RgRes r, VkImage img);
// Все ресурсы забыть (render_shutdown)
void rg_clear();

typedef void (*RgRecord)(VkCommandBuffer cmd, void *user);
// Кадр: rg_begin, проходы с их ресурсами, rg_execute. timer — строка F3: подряд идущие проходы с одной строкой
// меряются вместе; nullptr — проход без замера (интерфейс, снимок, показ). rec == nullptr — только барьер
void rg_begin();
void rg_pass(const char *name, const char *timer, RgRecord rec, void *user);
void rg_use(RgRes r, RgUse u, u32 flags = 0);
// Записать кадр: перед проходом — его барьеры одним vkCmdPipelineBarrier2 и метка начала для разбора сбоя,
// после — метка конца и, на границе строки F3, метка времени
void rg_execute(VkCommandBuffer cmd);
// Число барьеров последнего кадра (для docs/graph.md и проверки, что граф не ставит лишнего)
u32 rg_barriers();

// Проходы модулей кадра: объявляют себя в графе между rg_begin и rg_execute
// Счётчики GPU (vk_diag.cpp): обнулить в начале кадра, скопировать в буфер слота в конце; прочитать после забора
void vk_stats_clear_pass();
void vk_stats_copy_pass(FrameSlot &fs);
void vk_stats_read(FrameSlot &fs);
// Небо (vk_sky.cpp): таблицы пропускания и рассеяния при смене воздуха, вид и свет неба — каждый кадр
void vk_sky_passes(VkDeviceAddress frame);
// Чтение всех таблиц неба проходом (u — стадия: RG_CS_READ или RG_FS_READ)
void vk_sky_reads(RgUse u);
// Froxels (vk_froxel.cpp): среда и списки огней и декалей по клеткам; чтение их проходом
void vk_froxel_passes(const FrameSlot &fs);
void vk_froxel_reads(RgUse u);
// Карта горизонта (vk_horizon.cpp): сборка в первом кадре новой земли; чтение карты светом (u — стадия)
void vk_horizon_passes(VkDeviceAddress frame);
void vk_horizon_reads(RgUse u);
// Кадр мира (vk_world.cpp): земля, отсечение, visibility buffer, освещение, стиль с отрезками и точками.
// false — нет картинок кадра мира
bool vk_world_passes(FrameSlot &fs, const RenderFrame &f, u32 img, u32 npoints, u32 nlines);
// Кеш земли (vk_gcache.cpp): сброс и заявки с нуля до разбора плиток; что пишет разбор плиток; свободные
// страницы, счёт и запекание после него; что читает освещение
void vk_gcache_begin(VkDeviceAddress frame);
void vk_gcache_tiles_uses();
void vk_gcache_bake_passes();
void vk_gcache_reads();
// Интерфейс поверх кадра (vk_ui.cpp)
void vk_ui_pass(u32 img);
