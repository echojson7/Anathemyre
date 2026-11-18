// Тени огней картами (шаг 5а, docs/lamp_shadows.md). Куб глубины на огонь: грань — LSM_N² D16, значение — 1 −
// расстояние до огня / LSM_RANGE (обратная глубина, как у кадра). Мест LSM_SLOTS; огни кадра с тенью получают их
// ближними первыми, место держится за огнём по его месту в мире. Грани огня рисуются, когда он получил место или
// заслонка рядом ушла или пришла (vk_sdf_moved): не больше LSM_NEW_MAX огней за кадр, ближние первыми. Огонь, чьи
// грани ждут перерисовки, читает прежние; новый без граней — тень по полю расстояний (lamps.glsl). Кадр:
//   счётчики граней с нуля → отбор (lamp_cull.comp: экземпляр × грань, кластеры в пирамиде грани) → грани
//   (lamp_shadow.vert/.frag, косвенные вызовы по счётчику грани) → свет читает (lamps.glsl, lamp_map_vis).
// Мир неподвижен — карты рисуются раз: ночь в зале станции почти бесплатна
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/sort.h"

constexpr u32 LSM_NEW_MAX = LSM_JOB_CAP / 6;  // огней, чьи грани рисуются в кадре
constexpr f64 LSM_SAME = 0.02;                // м: огонь ближе — тот же (место держится за ним)
constexpr VkDeviceSize LSM_DRAWS = (VkDeviceSize)LSM_JOB_CAP * LSM_DRAW_CAP * 20;  // вызовы граней в буфере работ

static VkDeviceAddress g_lsm_frame = 0;
static u32 g_lsm_layer[LSM_JOB_CAP];  // слой картинки грани кадра (место · 6 + грань): цель отрисовки

bool vk_lsm_init() {
    LampShadowGpu &s = g_vk.lsm;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(g_vk.gpu, VK_FORMAT_D16_UNORM, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        log_error("тени огней: видеокарта не рисует или не фильтрует глубину D16");
        return false;
    }
    // Слоёв у картинки может быть меньше LSM_LAYERS (гарантировано 256): тогда и мест меньше
    VkPhysicalDeviceProperties pp{};
    vkGetPhysicalDeviceProperties(g_vk.gpu, &pp);
    s.slots_n = pp.limits.maxImageArrayLayers / 6 < LSM_SLOTS ? pp.limits.maxImageArrayLayers / 6 : LSM_SLOTS;
    const u32 layers = s.slots_n * 6;
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_D16_UNORM;
    ii.extent = {LSM_N, LSM_N, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &s.cube.image, &s.cube.alloc, nullptr), "тени огней: кубы"))
        return false;
    vk_image_name(s.cube.image, "тени огней: кубы");
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = s.cube.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    vi.format = VK_FORMAT_D16_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, layers};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &s.cube.view), "вид кубов огней")) return false;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.subresourceRange.layerCount = 1;
    for (u32 k = 0; k < layers; ++k) {
        vi.subresourceRange.baseArrayLayer = k;
        if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &s.faces[k]), "вид грани огня")) return false;
    }
    const VkDeviceSize work = LSM_COUNTS + LSM_DRAWS + (VkDeviceSize)LSM_JOB_CAP * LSM_DRAW_CAP * 8;
    const VkBufferUsageFlags wu =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!vk_create_buffer(s.work, work, wu, true)) return false;
    vk_buffer_name(s.work, "тени огней: вызовы граней");
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.lsm_jobs, (VkDeviceSize)LSM_JOB_CAP * sizeof(LsmJobGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
        vk_buffer_name(fs.lsm_jobs, "тени огней: грани кадра");
    }
    bool ok = vk_make_compute(&s.pipe_cull, "lamp_cull.comp");
    PipeDesc d{"lamp_shadow.vert", "lamp_shadow.frag"};
    d.depth_only = true;
    d.depth = VK_FORMAT_D16_UNORM;
    d.cull = VK_CULL_MODE_NONE;  // обе стороны: тонкая стена заслоняет, с какой бы стороны ни был огонь
    ok = vk_make_pipeline(&s.pipe_draw, d) && ok;
    s.rg_cube = rg_image("тени огней: кубы", VK_IMAGE_ASPECT_DEPTH_BIT);
    rg_bind_image(s.rg_cube, s.cube.image);
    s.rg_work = rg_buffer("тени огней: вызовы граней");
    // Набор кадра мира: 14 — выборка со сравнением, 15 — глубина как есть (сэмплеры неизменные, vk_world_layout)
    VkDescriptorImageInfo di{VK_NULL_HANDLE, s.cube.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = g_vk.world.set;
        wr[i].dstBinding = 14 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[i].pImageInfo = &di;
    }
    vkUpdateDescriptorSets(g_vk.device, 2, wr, 0, nullptr);
    log_info("тени огней: %u мест по 6 граней %u², %.0f МБ", s.slots_n, LSM_N,
             (f64)((VkDeviceSize)layers * LSM_N * LSM_N * 2 + work) / (1 << 20));
    return ok;
}

void vk_lsm_shutdown() {
    LampShadowGpu &s = g_vk.lsm;
    VkPipeline ps[] = {s.pipe_cull, s.pipe_draw};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    for (VkImageView v : s.faces)
        if (v) vkDestroyImageView(g_vk.device, v, nullptr);
    if (s.cube.view) vkDestroyImageView(g_vk.device, s.cube.view, nullptr);
    if (s.cube.image) vmaDestroyImage(g_vk.vma, s.cube.image, s.cube.alloc);
    vk_destroy_buffer(s.work);
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.lsm_jobs);
    VkSampler cmp = s.cmp, raw = s.raw;  // сэмплеры живут с раскладкой набора (vk_world_shutdown)
    s = LampShadowGpu{};
    s.cmp = cmp;
    s.raw = raw;
}

// Заслонки, ушедшие и пришедшие в кадре (vk_sdf_moved, сетки заменены): места, в чью дальность они попали, ждут
// перерисовки. Место без огня в кадре — тоже: огонь может вернуться
static void lsm_moved(LampShadowGpu &s) {
    const f32 (*mv)[4] = nullptr;
    bool all = false;
    u32 n = vk_sdf_moved(&mv, &all);
    if (s.seen_mesh != g_vk.shadow.mesh_gen) {
        s.seen_mesh = g_vk.shadow.mesh_gen;
        all = true;
    }
    if (!all && !n) return;
    f64 ax = (f64)g_vk.sdf.anchor[0] * 256.0, az = (f64)g_vk.sdf.anchor[1] * 256.0;
    for (u32 k = 0; k < s.slots_n; ++k) {
        LsmSlot &l = s.slots[k];
        if (!l.ready || l.dirty) continue;
        l.dirty = all;
        f64 p[3] = {l.pos[0] - ax, l.pos[1], l.pos[2] - az};
        for (u32 i = 0; i < n && !l.dirty; ++i) {
            f64 dx = p[0] - mv[i][0], dy = p[1] - mv[i][1], dz = p[2] - mv[i][2], e = (f64)l.range + mv[i][3];
            l.dirty = dx * dx + dy * dy + dz * dz < e * e;
        }
    }
}

// Огонь кадра с тенью: номер в списке огней; ключ — квадрат расстояния до камеры битами f32 и номер (порядок один)
struct LsmCand {
    u32 light;
    u64 key;
};
static LsmCand g_lsm_cand[RENDER_MAX_LIGHTS], g_lsm_tmp[RENDER_MAX_LIGHTS];

static bool lsm_same(const LsmSlot &l, const f64 w[3]) {
    return __builtin_fabs(l.pos[0] - w[0]) <= LSM_SAME && __builtin_fabs(l.pos[1] - w[1]) <= LSM_SAME &&
           __builtin_fabs(l.pos[2] - w[2]) <= LSM_SAME;
}

void vk_lsm_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    LampShadowGpu &s = g_vk.lsm;
    s.jobs_n = 0;
    g.lsm_on = 0;
    g.lsm_jobs_n = 0;
    g.lsm_jobs = fs.lsm_jobs.address;
    g.lsm_counts = s.work.address;
    g.lsm_draws = s.work.address + LSM_COUNTS;
    g.lsm_list = s.work.address + LSM_COUNTS + LSM_DRAWS;
    // Без кадра мира (поле выключено вместе с ним) или в сравнении «тени полем» — карт нет; включились снова —
    // всё с нуля: пока их не было, изменения мира не считались
    if (!s.cube.image || !g_vk.sdf.on || !f.style.lamp_maps) {
        if (s.on)
            for (LsmSlot &l : s.slots) l = LsmSlot{};
        s.on = false;
        return;
    }
    s.on = true;
    g_lsm_frame = fs.frame.address;
    ++s.frame;
    lsm_moved(s);

    // Огни кадра с тенью, ближние первыми; мест — не больше slots_n, дальние остаются с тенью по полю
    RenderPointLight *ls = (RenderPointLight *)fs.lights.mapped;
    u32 n = g.froxel_on ? g.light_count : 0, nc = 0;
    for (u32 i = 0; i < n; ++i) {
        const RenderPointLight &l = ls[i];
        if (!(l.flags & LIGHT_SHADOW)) continue;
        f32 d2 = l.x * l.x + l.y * l.y + l.z * l.z;
        u32 bits;
        memcpy(&bits, &d2, 4);
        g_lsm_cand[nc++] = {i, (u64)bits << 32 | i};
    }
    radix_sort_u64(g_lsm_cand, g_lsm_tmp, nc, [](const LsmCand &c) { return c.key; });
    if (nc > s.slots_n) nc = s.slots_n;
    const f64 cam[3] = {(f64)f.view.cx * 256.0 + f.view.x, f.view.y, (f64)f.view.cz * 256.0 + f.view.z};
    u32 slot_of[LSM_SLOTS];
    f64 wpos[LSM_SLOTS][3];
    // Тот же огонь (место в мире) — своё место
    for (u32 c = 0; c < nc; ++c) {
        const RenderPointLight &l = ls[g_lsm_cand[c].light];
        f64 *w = wpos[c];
        w[0] = cam[0] + l.x;
        w[1] = cam[1] + l.y;
        w[2] = cam[2] + l.z;
        slot_of[c] = ~0u;
        for (u32 k = 0; k < s.slots_n; ++k) {
            LsmSlot &sl = s.slots[k];
            if (!sl.used || !lsm_same(sl, w)) continue;
            slot_of[c] = k;
            sl.used = s.frame;
            break;
        }
    }
    // Новый огонь — свободное место или то, что дольше всех не показывалось (не занятое этим кадром)
    for (u32 c = 0; c < nc; ++c) {
        if (slot_of[c] != ~0u) continue;
        u32 best = ~0u;
        for (u32 k = 0; k < s.slots_n; ++k) {
            const LsmSlot &sl = s.slots[k];
            if (sl.used == s.frame) continue;
            if (best == ~0u || sl.used < s.slots[best].used) best = k;
        }
        if (best == ~0u) break;
        LsmSlot &sl = s.slots[best];
        sl = LsmSlot{};
        for (u32 a = 0; a < 3; ++a) sl.pos[a] = wpos[c][a];
        sl.used = s.frame;
        sl.dirty = true;
        slot_of[c] = best;
    }
    // Грани к отрисовке: ждущие, ближние первыми, не больше LSM_NEW_MAX огней. Заслонки — до LSM_RANGE, а не до
    // дальности огня: она идёт за глазом (lamps.cpp), и грани не перерисовываются при каждой смене экспозиции
    LsmJobGpu *jobs = (LsmJobGpu *)fs.lsm_jobs.mapped;
    u32 drawn = 0;
    for (u32 c = 0; c < nc && drawn < LSM_NEW_MAX; ++c) {
        if (slot_of[c] == ~0u) continue;
        LsmSlot &sl = s.slots[slot_of[c]];
        if (!sl.dirty) continue;
        const RenderPointLight &l = ls[g_lsm_cand[c].light];
        sl.range = LSM_RANGE;
        for (u32 face = 0; face < 6; ++face) {
            g_lsm_layer[s.jobs_n] = slot_of[c] * 6 + face;
            jobs[s.jobs_n++] = LsmJobGpu{{l.x, l.y, l.z}, l.size + LSM_SELF, {l.ax, l.ay, l.az}, slot_of[c] * 6 + face,
                                         {l.fx, l.fy, l.fz}, 0};
        }
        sl.dirty = false;
        sl.ready = true;
        ++drawn;
    }
    // Место огня — в его flags (биты 8..15, место + 1): свет читает карту, если грани есть
    for (u32 c = 0; c < nc; ++c) {
        if (slot_of[c] == ~0u) continue;
        RenderPointLight &l = ls[g_lsm_cand[c].light];
        l.flags = (l.flags & ~0xFF00u) | (s.slots[slot_of[c]].ready ? (slot_of[c] + 1) << 8 : 0u);
    }
    if (nc) vmaFlushAllocation(g_vk.vma, fs.lights.alloc, 0, VK_WHOLE_SIZE);
    if (s.jobs_n) vmaFlushAllocation(g_vk.vma, fs.lsm_jobs.alloc, 0, VK_WHOLE_SIZE);
    g.lsm_on = 1;
    g.lsm_jobs_n = s.jobs_n;
}

static void lsm_record_clear(VkCommandBuffer cmd, void *) {
    vkCmdFillBuffer(cmd, g_vk.lsm.work.buffer, 0, LSM_COUNTS, 0);
}
// Отбор: поток — экземпляр (x) и грань (y); без экземпляров — одна группа, чтобы грань всё равно посчиталась
static void lsm_record_cull(VkCommandBuffer cmd, void *) {
    const LampShadowGpu &s = g_vk.lsm;
    u32 n = g_vk.world.inst_count;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipe_cull);
    PushConsts pc{g_lsm_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, n ? (n + 63) / 64 : 1, s.jobs_n, 1);
}
// Грани: каждая — своя цель глубины (очистка нулём — «пусто»), вызовы — по её счётчику
static void lsm_record_draw(VkCommandBuffer cmd, void *) {
    const LampShadowGpu &s = g_vk.lsm;
    auto da = vk_info<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    da.clearValue.depthStencil = {0.0f, 0};
    auto ri = vk_info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = {{0, 0}, {LSM_N, LSM_N}};
    ri.layerCount = 1;
    ri.pDepthAttachment = &da;
    const VkViewport vp{0, 0, (f32)LSM_N, (f32)LSM_N, 0, 1};
    const VkRect2D sc{{0, 0}, {LSM_N, LSM_N}};
    for (u32 j = 0; j < s.jobs_n; ++j) {
        da.imageView = s.faces[g_lsm_layer[j]];
        vkCmdBeginRendering(cmd, &ri);
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipe_draw);
        PushConsts pc{g_lsm_frame, j, 0};
        vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
        vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirectCount(cmd, s.work.buffer, LSM_COUNTS + (VkDeviceSize)j * LSM_DRAW_CAP * 20, s.work.buffer,
                                      (VkDeviceSize)j * 4, LSM_DRAW_CAP, 20);
        vkCmdEndRendering(cmd);
    }
}

void vk_lsm_passes() {
    const LampShadowGpu &s = g_vk.lsm;
    if (!s.on || !s.jobs_n) return;
    rg_pass("огни: счётчики граней с нуля", "тени огней", lsm_record_clear, nullptr);
    rg_use(s.rg_work, RG_COPY_WRITE);
    rg_pass("огни: отбор заслонок граней", "тени огней", lsm_record_cull, nullptr);
    rg_use(g_vk.world.rg_inst, RG_CS_READ);
    rg_use(s.rg_work, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_skin_clusters_read();
    rg_pass("огни: грани", "тени огней", lsm_record_draw, nullptr);
    rg_use(s.rg_work, RG_INDIRECT);
    rg_use(s.rg_work, RG_VS_READ);
    rg_use(g_vk.world.rg_inst, RG_VS_READ);
    rg_use(s.rg_cube, RG_DEPTH);
    vk_skin_vertices_read(RG_VS_READ);
}

// Чтение кубов — всегда, когда картинка есть: раскладка в наборе (GENERAL) должна совпадать и в кадре без карт
void vk_lsm_reads() {
    const LampShadowGpu &s = g_vk.lsm;
    if (s.cube.image) rg_use(s.rg_cube, RG_IMG_CS_SAMPLE);
}
