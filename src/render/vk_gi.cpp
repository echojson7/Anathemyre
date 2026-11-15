// Зонды непрямого света (этап 6г, docs/gi.md): сетка зондов кольцами вокруг камеры, как DDGI (Majercik и др.
// 2019), лучи — по полю расстояний мира (vk_sdf.cpp) и карте высот земли. Номер зонда — мировой (от якоря поля),
// место — по модулю окна уровня: сдвиг камеры зонды не двигает, вошедшие в окно места — «новые» и считаются в
// том же кадре. Остальные обновляются по очереди, сколько велит ступень «Качества света». Кадр:
//   лучи (gi_trace.comp) — работа на зонд, яркость попаданий с солнцем и светом зондов прошлого смешения;
//   смешение (gi_blend.comp) — октаэдры освещённости и расстояний, место и состояние зонда в таблицу.
// Лучи зонда — решётка Фибоначчи, повёрнутая от мирового номера и номера в круге из GI_K поворотов; освещённость
// копится средним за круг и показывается в его конце: в покое показанное значение не меняется. Снимок (первый
// кадр, сброс, --shot и --timelapse, если светило сдвинулось) считает все зонды заново полным кругом — это и
// GI_K отскоков
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr f32 GI_HYST = 0.3f;       // доля старого показанного значения в конце круга
constexpr f32 GI_SNAP_COS = 0.99999f;  // светило сдвинулось с последнего снимка больше ~0.25°

// Ступени «Качества света»: лучей на зонд и зондов за кадр (docs/gi.md, «Ступени»)
static const u32 GI_Q_RAYS[RENDER_LIGHT_QUALITIES] = {128, 192, 256, 256};
static const u32 GI_Q_PROBES[RENDER_LIGHT_QUALITIES] = {768, 1024, 1536, 3072};

struct GiChunk {
    u32 first, n;
};
static GiChunk g_gi_chunks[GI_MAX_CHUNKS];
static VkDeviceAddress g_gi_frame = 0;

static bool gi_image(GpuImage &im, VkFormat fmt, u32 tile, const char *name) {
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(g_vk.gpu, fmt, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        log_error("зонды: видеокарта не пишет и не фильтрует %s", name);
        return false;
    }
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = fmt;
    ii.extent = {GI_NX * tile, GI_NZ * tile, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = GI_LEVELS * GI_NY;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &im.image, &im.alloc, nullptr), name)) return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = im.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = fmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, GI_LEVELS * GI_NY};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &im.view), name)) return false;
    vk_image_name(im.image, name);
    return true;
}

bool vk_gi_init() {
    GiGpu &s = g_vk.gi;
    if (!gi_image(s.irr, VK_FORMAT_R16G16B16A16_SFLOAT, GI_IRR, "зонды: освещённость") ||
        !gi_image(s.dep, VK_FORMAT_R16G16_SFLOAT, GI_DEP, "зонды: расстояния") ||
        !gi_image(s.acc, VK_FORMAT_R16G16B16A16_SFLOAT, GI_IRR, "зонды: среднее круга"))
        return false;
    const VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (!vk_create_buffer(s.probes, (VkDeviceSize)GI_PROBES * 32, ssbo | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true) ||
        !vk_create_buffer(s.rays, (VkDeviceSize)GI_CHUNK * GI_RAYS_MAX * 8, ssbo, true) ||
        !vk_create_buffer(s.pos, (VkDeviceSize)GI_CHUNK * 16, ssbo, true))
        return false;
    vk_buffer_name(s.probes, "зонды: таблица");
    vk_buffer_name(s.rays, "зонды: лучи");
    vk_buffer_name(s.pos, "зонды: место работ");
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.gi_jobs, (VkDeviceSize)GI_PROBES * GI_K * 4, ssbo)) return false;
        vk_buffer_name(fs.gi_jobs, "зонды: работы кадра");
    }
    s.pipe_trace = vk_make_compute("gi_trace.comp");
    s.pipe_blend = vk_make_compute("gi_blend.comp");
    s.rg_irr = rg_image("зонды: освещённость", VK_IMAGE_ASPECT_COLOR_BIT);
    s.rg_dep = rg_image("зонды: расстояния", VK_IMAGE_ASPECT_COLOR_BIT);
    s.rg_acc = rg_image("зонды: среднее круга", VK_IMAGE_ASPECT_COLOR_BIT);
    rg_bind_image(s.rg_irr, s.irr.image);
    rg_bind_image(s.rg_dep, s.dep.image);
    rg_bind_image(s.rg_acc, s.acc.image);
    s.rg_probes = rg_buffer("зонды: таблица");
    s.rg_rays = rg_buffer("зонды: лучи");
    s.rg_pos = rg_buffer("зонды: место работ");
    // Набор кадра мира: 4, 5 — выборка (сэмплер неизменный, vk_world_layout), 6, 7, 8 — запись смешением
    VkDescriptorImageInfo di[5] = {{VK_NULL_HANDLE, s.irr.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, s.dep.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, s.irr.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, s.dep.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, s.acc.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[5] = {};
    for (u32 i = 0; i < 5; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = g_vk.world.set;
        wr[i].dstBinding = 4 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = i < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].pImageInfo = &di[i];
    }
    vkUpdateDescriptorSets(g_vk.device, 5, wr, 0, nullptr);
    s.reset = s.clear = true;
    VkDeviceSize bytes = (VkDeviceSize)GI_PROBES * (GI_IRR * GI_IRR * 16 + GI_DEP * GI_DEP * 4 + 32) +
                         (VkDeviceSize)GI_CHUNK * (GI_RAYS_MAX * 8 + 16);
    log_info("зонды: %u уровня по %u×%u×%u от %.0f м, %.0f МБ", GI_LEVELS, GI_NX, GI_NY, GI_NZ, 1.0,
             (f64)bytes / (1 << 20));
    return s.pipe_trace && s.pipe_blend;
}

static void gi_destroy_image(GpuImage &im) {
    if (im.view) vkDestroyImageView(g_vk.device, im.view, nullptr);
    if (im.image) vmaDestroyImage(g_vk.vma, im.image, im.alloc);
    im = GpuImage{};
}

void vk_gi_shutdown() {
    GiGpu &s = g_vk.gi;
    if (s.pipe_trace) vkDestroyPipeline(g_vk.device, s.pipe_trace, nullptr);
    if (s.pipe_blend) vkDestroyPipeline(g_vk.device, s.pipe_blend, nullptr);
    s.pipe_trace = s.pipe_blend = VK_NULL_HANDLE;
    gi_destroy_image(s.irr);
    gi_destroy_image(s.dep);
    gi_destroy_image(s.acc);
    vk_destroy_buffer(s.probes);
    vk_destroy_buffer(s.rays);
    vk_destroy_buffer(s.pos);
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.gi_jobs);
    // Сэмплер живёт с раскладкой набора (vk_world_shutdown); остальное — с нуля при следующем запуске
    s.on = false;
    s.reset = s.clear = true;
    s.cursor = s.jobs_n = s.chunks = 0;
}

static i32 gi_floor(f64 x) { return (i32)__builtin_floor(x); }
static u32 gi_slot(u32 L, i32 x, i32 y, i32 z) {
    return ((L * GI_NY + (u32)(y & (GI_NY - 1))) * GI_NZ + (u32)(z & (GI_NZ - 1))) * GI_NX + (u32)(x & (GI_NX - 1));
}

// Окна уровней вокруг камеры (камера — в зонде GI_NX/2 по x, z и GI_CAM_Y по высоте): места вошедших зондов — новые
static void gi_windows(GiGpu &s, const f64 cam[3]) {
    const i32 c[3] = {(i32)GI_NX / 2, GI_CAM_Y, (i32)GI_NZ / 2};
    const i32 n[3] = {(i32)GI_NX, (i32)GI_NY, (i32)GI_NZ};
    for (u32 L = 0; L < GI_LEVELS; ++L) {
        f64 st = (f64)(1u << L);
        i32 nw[3];
        for (u32 a = 0; a < 3; ++a) nw[a] = gi_floor(cam[a] / st) - c[a];
        if (!s.reset && (nw[0] != s.lo[L][0] || nw[1] != s.lo[L][1] || nw[2] != s.lo[L][2])) {
            for (i32 z = nw[2]; z < nw[2] + n[2]; ++z)
                for (i32 y = nw[1]; y < nw[1] + n[1]; ++y)
                    for (i32 x = nw[0]; x < nw[0] + n[0]; ++x) {
                        bool was = x >= s.lo[L][0] && x < s.lo[L][0] + n[0] && y >= s.lo[L][1] &&
                                   y < s.lo[L][1] + n[1] && z >= s.lo[L][2] && z < s.lo[L][2] + n[2];
                        if (!was) s.fresh[gi_slot(L, x, y, z)] = 1;
                    }
        }
        for (u32 a = 0; a < 3; ++a) s.lo[L][a] = nw[a];
    }
}

void vk_gi_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    GiGpu &s = g_vk.gi;
    const SdfGpu &sd = g_vk.sdf;
    g_gi_frame = fs.frame.address;
    s.jobs_n = s.chunks = 0;
    g.gi_on = 0;
    u32 view = f.style.gi_view < RENDER_GI_VIEWS ? f.style.gi_view : RENDER_GI_FRAME;
    g.gi_view = view;
    s.on = sd.on && g.sdf_on && s.irr.image && s.pipe_trace && s.pipe_blend && view != RENDER_GI_OFF;
    if (!s.on) {
        s.reset = true;  // включили снова — всё с нуля: зонды могли устареть
        return;
    }
    if (sd.anchor[0] != s.anchor[0] || sd.anchor[1] != s.anchor[1]) s.reset = true;
    f64 cam[3] = {(f64)g.sdf_cam[0], (f64)g.sdf_cam[1], (f64)g.sdf_cam[2]};
    f32 sun[3] = {g.sun_dir[0], g.sun_dir[1], g.sun_dir[2]};
    f32 sl = f_sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
    if (sl > 0)
        for (f32 &v : sun) v /= sl;
    bool moved = sun[0] * s.snap_sun[0] + sun[1] * s.snap_sun[1] + sun[2] * s.snap_sun[2] < GI_SNAP_COS;
    bool snap = s.reset || (f.style.eye_snap && moved);
    if (s.reset) {
        s.anchor[0] = sd.anchor[0];
        s.anchor[1] = sd.anchor[1];
        s.clear = true;  // таблица с нуля: мировые номера старого якоря значат другие места
    }
    gi_windows(s, cam);
    s.reset = false;

    u32 q = g.light_q < RENDER_LIGHT_QUALITIES ? g.light_q : RENDER_LIGHT_HIGH;
    s.rays_n = GI_Q_RAYS[q];
    u32 *jobs = (u32 *)fs.gi_jobs.mapped;
    u32 n = 0;
    if (snap) {
        // Все места полным кругом: проход k — поворот k, первый — без старого, каждый показывает среднее (это и
        // GI_K отскоков подряд). Части по GI_CHUNK, части и проходы по порядку
        for (u32 k = 0; k < GI_K; ++k) {
            for (u32 i = 0; i < GI_PROBES; ++i) jobs[n++] = i | (k == 0 ? GI_FRESH : 0) | k << 17 | GI_SHOW;
            for (u32 first = k * GI_PROBES; first < n && s.chunks < GI_MAX_CHUNKS; first += GI_CHUNK)
                g_gi_chunks[s.chunks++] = {first, n - first < GI_CHUNK ? n - first : GI_CHUNK};
        }
        memset(s.fresh, 0, sizeof s.fresh);
        memset(s.turn, 0, sizeof s.turn);
        memset(s.age, GI_K, sizeof s.age);
        for (u32 a = 0; a < 3; ++a) s.snap_sun[a] = sun[a];
    } else {
        // Очередь: GI_Q_PROBES мест по кругу (новые пропускаются — они ниже, без повтора в кадре), потом новые
        auto job = [&](u32 i, bool fresh) {
            if (fresh) s.turn[i] = s.age[i] = 0;
            u32 k = s.turn[i];
            bool show = k == GI_K - 1 || s.age[i] < GI_K;
            jobs[n++] = i | (fresh ? GI_FRESH : 0) | k << 17 | (show ? GI_SHOW : 0);
            s.turn[i] = (u8)((k + 1) % GI_K);
            if (s.age[i] < GI_K) ++s.age[i];
        };
        u32 want = GI_Q_PROBES[q];
        for (u32 k = 0; k < want; ++k) {
            u32 i = s.cursor;
            s.cursor = (s.cursor + 1) % GI_PROBES;
            if (!s.fresh[i]) job(i, false);
        }
        u32 left = 0;
        for (u32 i = 0; i < GI_PROBES; ++i) {
            if (!s.fresh[i]) continue;
            if (n < GI_CHUNK) {
                job(i, true);
                s.fresh[i] = 0;
            } else {
                ++left;  // не влезли — в следующий кадр; до тех пор свет их не читает (чужое место в таблице)
            }
        }
        if (left) vk_overflow("зонды: новые места кадра", (u64)n + left, GI_CHUNK);
        if (n) g_gi_chunks[s.chunks++] = {0, n};
    }
    s.jobs_n = n;
    if (n) vmaFlushAllocation(g_vk.vma, fs.gi_jobs.alloc, 0, VK_WHOLE_SIZE);
    g.gi_probes = s.probes.address;
    g.gi_jobs = fs.gi_jobs.address;
    g.gi_rays = s.rays.address;
    g.gi_pos = s.pos.address;
    for (u32 L = 0; L < 4; ++L)
        for (u32 a = 0; a < 4; ++a) g.gi_lo[L][a] = L < GI_LEVELS && a < 3 ? s.lo[L][a] : 0;
    g.gi_on = 1;
    g.gi_rays_n = s.rays_n;
    g.gi_hyst = GI_HYST;
}

static void gi_record_clear(VkCommandBuffer cmd, void *) { vkCmdFillBuffer(cmd, g_vk.gi.probes.buffer, 0, VK_WHOLE_SIZE, 0); }
static void gi_record(VkCommandBuffer cmd, VkPipeline p, const GiChunk &c) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{g_gi_frame, c.first, c.n};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, c.n, 1, 1);
}
static void gi_record_trace(VkCommandBuffer cmd, void *user) { gi_record(cmd, g_vk.gi.pipe_trace, *(const GiChunk *)user); }
static void gi_record_blend(VkCommandBuffer cmd, void *user) { gi_record(cmd, g_vk.gi.pipe_blend, *(const GiChunk *)user); }

void vk_gi_passes() {
    GiGpu &s = g_vk.gi;
    if (!s.on) return;
    if (s.clear) {
        rg_pass("зонды: таблица с нуля", "непрямой свет", gi_record_clear, nullptr);
        rg_use(s.rg_probes, RG_COPY_WRITE);
        s.clear = false;
    }
    // Часть — лучи, потом смешение: лучи читают атлас и таблицу прошлого смешения, смешение их пишет
    for (u32 k = 0; k < s.chunks; ++k) {
        rg_pass("зонды: лучи", "непрямой свет", gi_record_trace, &g_gi_chunks[k]);
        rg_use(g_vk.sdf.rg_field, RG_IMG_CS_SAMPLE);
        rg_use(s.rg_irr, RG_IMG_CS_SAMPLE);
        rg_use(s.rg_dep, RG_IMG_CS_SAMPLE);
        rg_use(s.rg_probes, RG_CS_READ);
        rg_use(s.rg_rays, RG_CS_WRITE);
        rg_use(s.rg_pos, RG_CS_WRITE);
        rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
        vk_sky_reads(RG_CS_READ);
        vk_horizon_reads(RG_CS_READ);
        rg_pass("зонды: смешение", "непрямой свет", gi_record_blend, &g_gi_chunks[k]);
        rg_use(s.rg_rays, RG_CS_READ);
        rg_use(s.rg_pos, RG_CS_READ);
        rg_use(s.rg_probes, RG_CS_WRITE);
        rg_use(s.rg_irr, RG_IMG_CS_WRITE);
        rg_use(s.rg_dep, RG_IMG_CS_WRITE);
        rg_use(s.rg_acc, RG_IMG_CS_WRITE);
    }
}

// Атласы — в набор кадра всегда (и при выключенных зондах): граф переведёт их в GENERAL, как ждёт набор
void vk_gi_reads() {
    const GiGpu &s = g_vk.gi;
    if (!s.irr.image) return;
    rg_use(s.rg_irr, RG_IMG_CS_SAMPLE);
    rg_use(s.rg_dep, RG_IMG_CS_SAMPLE);
    rg_use(s.rg_probes, RG_CS_READ);
}
