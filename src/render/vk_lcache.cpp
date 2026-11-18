// Ячейки света на поверхностях (docs/lcache.md, lcache.glsl): отражённый свет в маленьких мировых ячейках прямо на
// полу и стенах, вместо интерполяции зондов, висящих в воздухе через 1–4 м. Кадр:
//   заявки — разбор плиток (light_tiles.comp): ячейки под точками пикселей, новые места таблицы;
//   работы (lcache_list.comp, lcache_args.comp): освободить старые места, молодые и очередь обновления;
//   лучи (lcache_trace.comp): полусфера над ячейкой тем же лучом, что у зондов, гармоники L1 средним за круг.
// Значение ячейки зависит только от её мирового номера: камера ячейки не двигает, поворот не меняет уровни
#include "vk_graph.h"
#include "../core/log.h"

constexpr f32 LC_HYST = 0.3f;  // доля старого показанного значения в конце круга (как у зондов)

// Ступени «Качества света» (docs/lcache.md, «Ступени»): угол ячейки, лучей на счёт, кадров между счётами видимой
// ячейки, пределы молодых и обновлений за кадр
static const f32 LC_Q_TAN[RENDER_LIGHT_QUALITIES] = {0.043661f, 0.034921f, 0.026186f, 0.017455f};  // 2.5°, 2°, 1.5°, 1°
static const u32 LC_Q_RAYS[RENDER_LIGHT_QUALITIES] = {32, 48, 64, 64};
static const u32 LC_Q_PERIOD[RENDER_LIGHT_QUALITIES] = {16, 12, 8, 6};
static const u32 LC_Q_NEW[RENDER_LIGHT_QUALITIES] = {512, 768, 1024, 2048};
static const u32 LC_Q_REF[RENDER_LIGHT_QUALITIES] = {256, 512, 1024, 2048};
constexpr f32 LC_FAR = 64.0f;  // дальше — свет зондов
constexpr u32 LC_JOBS_HEAD = 176;   // счётчики, dispatch, корзины очереди, срок жизни (LcJobs, common.glsl)
constexpr u32 LC_JOBS_CLEAR = 160;  // каждый кадр чистится всё, кроме срока жизни

static VkDeviceAddress g_lc_frame = 0;

bool vk_lcache_init() {
    LcGpu &s = g_vk.lc;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vk_create_buffer(s.keys, (VkDeviceSize)LC_CAP * 4, ssbo, true) ||
        !vk_create_buffer(s.meta, (VkDeviceSize)LC_CAP * 40, ssbo, true) ||
        !vk_create_buffer(s.show, (VkDeviceSize)LC_CAP * 24, ssbo, true) ||
        !vk_create_buffer(s.filt, (VkDeviceSize)LC_CAP * 24, ssbo, true) ||
        !vk_create_buffer(s.acc, (VkDeviceSize)LC_CAP * 48, ssbo, true) ||
        !vk_create_buffer(s.jobs, LC_JOBS_HEAD + (VkDeviceSize)LC_CAP * 2 * 4, ssbo | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, true))
        return false;
    vk_buffer_name(s.keys, "ячейки света: ключи");
    vk_buffer_name(s.meta, "ячейки света: места");
    vk_buffer_name(s.show, "ячейки света: показанный свет");
    vk_buffer_name(s.filt, "ячейки света: сглаженный свет");
    vk_buffer_name(s.acc, "ячейки света: сумма круга");
    vk_buffer_name(s.jobs, "ячейки света: работы");
    bool ok = true;
    ok = vk_make_compute(&s.pipe_list, "lcache_list.comp") && ok;
    ok = vk_make_compute(&s.pipe_args, "lcache_args.comp") && ok;
    ok = vk_make_compute(&s.pipe_trace, "lcache_trace.comp") && ok;
    ok = vk_make_compute(&s.pipe_filter, "lcache_filter.comp") && ok;
    s.rg_keys = rg_buffer("ячейки света: ключи");
    s.rg_meta = rg_buffer("ячейки света: места");
    s.rg_show = rg_buffer("ячейки света: показанный свет");
    s.rg_filt = rg_buffer("ячейки света: сглаженный свет");
    s.rg_acc = rg_buffer("ячейки света: сумма круга");
    s.rg_jobs = rg_buffer("ячейки света: работы");
    s.clear = true;
    log_info("ячейки света: %u мест, %.1f МБ", LC_CAP, (f64)(LC_CAP * (4 + 40 + 24 + 24 + 48 + 8) + 32) / (1 << 20));
    return ok;
}

void vk_lcache_shutdown() {
    LcGpu &s = g_vk.lc;
    for (VkPipeline *p : {&s.pipe_list, &s.pipe_args, &s.pipe_trace, &s.pipe_filter}) {
        if (*p) vkDestroyPipeline(g_vk.device, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    vk_destroy_buffer(s.keys);
    vk_destroy_buffer(s.meta);
    vk_destroy_buffer(s.show);
    vk_destroy_buffer(s.filt);
    vk_destroy_buffer(s.acc);
    vk_destroy_buffer(s.jobs);
    s.on = false;
    s.clear = true;
    s.frame = 0;
}

void vk_lcache_fill(FrameGpu &g, const RenderFrame &f) {
    LcGpu &s = g_vk.lc;
    const GiGpu &gi = g_vk.gi;
    g_lc_frame = 0;
    g.lc_on = 0;
    // Без зондов (выключены, поле выключено) ячеек нет: второй отскок лучей ячеек — свет зондов
    bool on = gi.on && g.gi_on && s.pipe_trace && f.style.gi_view != RENDER_GI_OFF;
    if (!on) {
        s.on = false;
        s.clear = true;
        return;
    }
    if (!s.on || gi.anchor[0] != s.anchor[0] || gi.anchor[1] != s.anchor[1]) s.clear = true;
    // Снимок зондов (скачок часов, первый кадр, --shot) — и ячейки с нуля: невидимые сейчас ячейки иначе хранили бы
    // свет прошлого часа и показали бы его, вернувшись в кадр (docs/light.md, «Резкая смена света»)
    if (gi.snapped) s.clear = true;
    s.on = true;
    if (s.clear) {
        s.anchor[0] = gi.anchor[0];
        s.anchor[1] = gi.anchor[1];
        s.frame = 0;
    }
    ++s.frame;
    u32 q = g.gi_q < RENDER_LIGHT_QUALITIES ? g.gi_q : RENDER_LIGHT_HIGH;
    g.lc_keys = g.lc_keys4 = s.keys.address;
    g.lc_meta = s.meta.address;
    g.lc_show = s.show.address;
    g.lc_filt = s.filt.address;
    g.lc_acc = s.acc.address;
    g.lc_jobs = s.jobs.address;
    g.lc_on = 1;
    g.lc_frame = s.frame;
    g.lc_rays_n = LC_Q_RAYS[q];
    g.lc_period = LC_Q_PERIOD[q];
    g.lc_cap_new = LC_Q_NEW[q];
    g.lc_cap_ref = LC_Q_REF[q];
    g.lc_snap = gi.snapped || s.clear ? 1 : 0;
    g.lc_ease = g_vk.shadow.sun_fast ? 1 : 0;  // ускоренное время: как зонды (vk_gi.cpp, GI_EASE)
    g.lc_angle = LC_Q_TAN[q];
    g.lc_far = LC_FAR;
    g.lc_hyst = LC_HYST;
}

static void lc_record_clear(VkCommandBuffer cmd, void *) {
    vkCmdFillBuffer(cmd, g_vk.lc.keys.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, g_vk.lc.meta.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, g_vk.lc.jobs.buffer, 0, LC_JOBS_HEAD, 0);  // и срок жизни — с начала
}
static void lc_record_jobs_clear(VkCommandBuffer cmd, void *) { vkCmdFillBuffer(cmd, g_vk.lc.jobs.buffer, 0, LC_JOBS_CLEAR, 0); }
static void lc_bind(VkCommandBuffer cmd, VkPipeline p) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{g_lc_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
}
static void lc_record_list(VkCommandBuffer cmd, void *) {
    lc_bind(cmd, g_vk.lc.pipe_list);
    vkCmdDispatch(cmd, LC_CAP / 256, 1, 1);
}
static void lc_record_args(VkCommandBuffer cmd, void *) {
    lc_bind(cmd, g_vk.lc.pipe_args);
    vkCmdDispatch(cmd, 1, 1, 1);
}
static void lc_record_trace(VkCommandBuffer cmd, void *) {
    lc_bind(cmd, g_vk.lc.pipe_trace);
    vkCmdDispatchIndirect(cmd, g_vk.lc.jobs.buffer, 16);
}
static void lc_record_filter(VkCommandBuffer cmd, void *) {
    lc_bind(cmd, g_vk.lc.pipe_filter);
    vkCmdDispatchIndirect(cmd, g_vk.lc.jobs.buffer, 16);  // группа на работу, как у лучей: работают первые
}

// До разбора плиток: таблица с нуля, если надо (заявки пишут в неё)
void vk_lcache_begin(VkDeviceAddress frame) {
    LcGpu &s = g_vk.lc;
    g_lc_frame = frame;
    if (!s.on || !s.clear) return;
    rg_pass("ячейки света: с нуля", "ячейки света", lc_record_clear, nullptr);
    rg_use(s.rg_keys, RG_COPY_WRITE);
    rg_use(s.rg_meta, RG_COPY_WRITE);
    rg_use(s.rg_jobs, RG_COPY_WRITE);
    s.clear = false;
}

// Разбор плиток заявляет ячейки: ключи и места
void vk_lcache_tiles_uses() {
    const LcGpu &s = g_vk.lc;
    if (!s.on) return;
    rg_use(s.rg_keys, RG_CS_WRITE);
    rg_use(s.rg_meta, RG_CS_WRITE);
}

// После разбора плиток, до освещения: работы и лучи. Лучи читают зонды (второй отскок) — их проходы уже были
void vk_lcache_passes() {
    const LcGpu &s = g_vk.lc;
    if (!s.on) return;
    rg_pass("ячейки света: работы с нуля", "ячейки света", lc_record_jobs_clear, nullptr);
    rg_use(s.rg_jobs, RG_COPY_WRITE);
    rg_pass("ячейки света: работы", "ячейки света", lc_record_list, nullptr);
    rg_use(s.rg_keys, RG_CS_WRITE);
    rg_use(s.rg_meta, RG_CS_WRITE);
    rg_use(s.rg_jobs, RG_CS_WRITE);
    rg_pass("ячейки света: счёт", "ячейки света", lc_record_args, nullptr);
    rg_use(s.rg_jobs, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("ячейки света: лучи", "ячейки света", lc_record_trace, nullptr);
    rg_use(s.rg_jobs, RG_INDIRECT);
    rg_use(s.rg_jobs, RG_CS_READ);
    rg_use(s.rg_meta, RG_CS_WRITE);
    rg_use(s.rg_show, RG_CS_WRITE);
    rg_use(s.rg_acc, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_sdf_reads();
    vk_lsm_reads();
    vk_gi_reads();
    vk_sky_reads(RG_CS_READ);
    vk_horizon_reads(RG_CS_READ);
    rg_pass("ячейки света: сглаживание", "ячейки света", lc_record_filter, nullptr);
    rg_use(s.rg_jobs, RG_INDIRECT);
    rg_use(s.rg_jobs, RG_CS_READ);
    rg_use(s.rg_keys, RG_CS_READ);
    rg_use(s.rg_meta, RG_CS_WRITE);  // пишет сглаженное затенение у точки (LcMeta.ao)
    rg_use(s.rg_show, RG_CS_READ);
    rg_use(s.rg_filt, RG_CS_WRITE);
    vk_sdf_reads();
}

// Освещение читает сглаженный свет и точки лучей: сбор на пиксель (lc_gather) и F3 «ячейки»
void vk_lcache_reads() {
    const LcGpu &s = g_vk.lc;
    if (!s.on) return;
    rg_use(s.rg_keys, RG_CS_READ);
    rg_use(s.rg_meta, RG_CS_READ);
    rg_use(s.rg_filt, RG_CS_READ);
}
