// Поле расстояний мира (этап 6в, docs/sdf.md). Вокруг камеры SDF_LEVELS колец по SDF_N³ вокселей, воксель уровня
// k — SDF_V0·2^k. Номер вокселя — от якоря поля (чанк), не от камеры: сдвиг камеры воксели не двигает, кольцо
// хранится тороидально. Поле собирается прямо из треугольников экземпляров, офлайн-SDF деталей нет:
//   засыпка (sdf_splat.comp) — группа на кластер экземпляра и уровень, поток на треугольник: расстояние до
//   треугольника атомарным минимумом в воксели рядом с ним (полоса SDF_BAND вокселей), знак — по стороне грани;
//   сборка (sdf_final.comp) — кирпич 8³ из засыпки в картинку R16F, засыпка снова «пусто».
// Пересобираются только грязные кирпичи: вошедшие в окно уровня при сдвиге камеры и те, где экземпляр пришёл или
// ушёл. Значение вокселя зависит только от мира (минимум не зависит от порядка), а не от того, когда кирпич собран
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr f32 SDF_V0 = 0.25f;          // воксель уровня 0, м (как sdf_cam.w в шейдерах)
constexpr f32 SDF_BAND = 4.0f;         // полоса засыпки, вокселей (SDF_BAND в common.glsl): дальше — «пусто»
constexpr f64 SDF_REANCHOR = 4096.0;   // камера дальше от якоря — всё с нуля (f32 держит воксели точно)
constexpr u32 SDF_BRICK_CAP = SDF_LEVELS * SDF_LEVEL_BRICKS;
constexpr i32 SDF_W = (i32)SDF_NB;     // окно уровня — кирпичей по оси

static f32 g_sdf_cls[GEOMETRY_MAX_CLUSTERS][4];  // сферы кластеров в осях сетки (render_mesh_create)
static VkDeviceAddress g_sdf_frame = 0;

void vk_sdf_mesh_clusters(u32 first, const ClusterGpu *cls, u32 n) {
    for (u32 i = 0; i < n && first + i < GEOMETRY_MAX_CLUSTERS; ++i)
        memcpy(g_sdf_cls[first + i], cls[i].sphere, sizeof cls[i].sphere);
}

bool vk_sdf_init() {
    SdfGpu &s = g_vk.sdf;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(g_vk.gpu, VK_FORMAT_R16_SFLOAT, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        log_error("поле расстояний: видеокарта не пишет и не фильтрует R16F в 3D-картинке");
        return false;
    }
    auto ii = vk_info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ii.imageType = VK_IMAGE_TYPE_3D;
    ii.format = VK_FORMAT_R16_SFLOAT;
    ii.extent = {SDF_N, SDF_N, SDF_SLAB * SDF_LEVELS};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VmaAllocationCreateInfo ci{};
    ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (!vk_check(vmaCreateImage(g_vk.vma, &ii, &ci, &s.field.image, &s.field.alloc, nullptr), "поле расстояний"))
        return false;
    auto vi = vk_info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = s.field.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
    vi.format = VK_FORMAT_R16_SFLOAT;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!vk_check(vkCreateImageView(g_vk.device, &vi, nullptr, &s.field.view), "вид поля расстояний")) return false;
    vk_image_name(s.field.image, "поле расстояний");
    const VkDeviceSize scratch = (VkDeviceSize)SDF_LEVELS * SDF_N * SDF_N * SDF_N * 4;
    if (!vk_create_buffer(s.scratch, scratch, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true))
        return false;
    vk_buffer_name(s.scratch, "поле расстояний: засыпка");
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.sdf_jobs, (VkDeviceSize)SDF_JOB_CAP * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !vk_create_buffer(fs.sdf_bricks, (VkDeviceSize)SDF_BRICK_CAP * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !vk_create_buffer(fs.sdf_bits, (VkDeviceSize)SDF_BRICK_CAP / 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
            return false;
        vk_buffer_name(fs.sdf_jobs, "поле расстояний: засыпка кадра");
        vk_buffer_name(fs.sdf_bricks, "поле расстояний: кирпичи кадра");
        vk_buffer_name(fs.sdf_bits, "поле расстояний: биты кирпичей");
    }
    s.pipe_splat = vk_make_compute("sdf_splat.comp");
    s.pipe_final = vk_make_compute("sdf_final.comp");
    s.rg_field = rg_image("поле расстояний", VK_IMAGE_ASPECT_COLOR_BIT);
    rg_bind_image(s.rg_field, s.field.image);
    s.rg_scratch = rg_buffer("поле расстояний: засыпка");
    // Набор кадра мира: 2 — выборка светом (сэмплер неизменный, vk_world_layout), 3 — запись сборкой
    VkDescriptorImageInfo di[2] = {{VK_NULL_HANDLE, s.field.view, VK_IMAGE_LAYOUT_GENERAL},
                                   {VK_NULL_HANDLE, s.field.view, VK_IMAGE_LAYOUT_GENERAL}};
    VkWriteDescriptorSet wr[2] = {};
    for (u32 i = 0; i < 2; ++i) {
        wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet = g_vk.world.set;
        wr[i].dstBinding = 2 + i;
        wr[i].descriptorCount = 1;
        wr[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].pImageInfo = &di[i];
    }
    vkUpdateDescriptorSets(g_vk.device, 2, wr, 0, nullptr);
    s.reset = s.clear = true;
    log_info("поле расстояний: %u колец по %u³ вокселей от %.2f м, %.0f МБ", SDF_LEVELS, SDF_N, (f64)SDF_V0,
             (f64)(scratch + (VkDeviceSize)SDF_N * SDF_N * SDF_SLAB * SDF_LEVELS * 2) / (1 << 20));
    return s.pipe_splat && s.pipe_final;
}

void vk_sdf_shutdown() {
    SdfGpu &s = g_vk.sdf;
    VkPipeline ps[] = {s.pipe_splat, s.pipe_final};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    if (s.field.view) vkDestroyImageView(g_vk.device, s.field.view, nullptr);
    if (s.field.image) vmaDestroyImage(g_vk.vma, s.field.image, s.field.alloc);
    vk_destroy_buffer(s.scratch);
    for (FrameSlot &fs : g_vk.frames) {
        vk_destroy_buffer(fs.sdf_jobs);
        vk_destroy_buffer(fs.sdf_bricks);
        vk_destroy_buffer(fs.sdf_bits);
    }
    VkSampler sm = s.sampler;  // сэмплер живёт с раскладкой набора (vk_world_shutdown)
    s = SdfGpu{};
    s.sampler = sm;
}

// Экземпляр в осях поля: кватернион, масштаб, начало от якоря. false — не заслонка (нет сетки, человек)
struct SdfXf {
    f32 q[4], s, p[3];
    const MeshGpu *m;
};
static bool sdf_xf(const RenderInstance &it, const i32 anchor[2], SdfXf &x) {
    if (!it.mesh || it.mesh >= RENDER_MAX_MESHES) return false;
    const MeshGpu &m = ((const MeshGpu *)g_vk.mesh_table.mapped)[it.mesh];
    if (!m.lod_count || (m.pad & MESH_DYNAMIC)) return false;
    auto sn = [](u32 v, u32 sh) { return f_clamp((f32)(i16)(u16)(v >> sh) / 32767.0f, -1.0f, 1.0f); };
    f32 q[4] = {sn(it.rot_xy, 0), sn(it.rot_xy, 16), sn(it.rot_zw, 0), sn(it.rot_zw, 16)};
    f32 ql = f_sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (ql <= 0) ql = 1;
    for (u32 i = 0; i < 4; ++i) x.q[i] = q[i] / ql;
    x.s = it.scale;
    x.p[0] = (f32)((it.cx - anchor[0]) * 256) + it.x;
    x.p[1] = it.y;
    x.p[2] = (f32)((it.cz - anchor[1]) * 256) + it.z;
    x.m = &m;
    return true;
}
// Точка сетки v → оси поля: p + q·(v·s)·q* (v + 2w(q × v) + 2 q × (q × v))
static void sdf_point(const SdfXf &x, const f32 *v, f32 out[3]) {
    const f32 *q = x.q;
    f32 a[3] = {v[0] * x.s, v[1] * x.s, v[2] * x.s};
    f32 t[3] = {2 * (q[1] * a[2] - q[2] * a[1]), 2 * (q[2] * a[0] - q[0] * a[2]), 2 * (q[0] * a[1] - q[1] * a[0])};
    out[0] = x.p[0] + a[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = x.p[1] + a[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = x.p[2] + a[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

struct SdfJob {
    u32 inst, cluster;  // экземпляр; кластер (номер в общей таблице) | уровень << 24
};
static f32 sdf_vox(u32 L) { return SDF_V0 * (f32)(1u << L); }
static u32 sdf_slot(const i32 b[3]) { return (u32)(b[0] & 15) + 16u * (u32)(b[1] & 15) + 256u * (u32)(b[2] & 15); }
static i32 sdf_floor(f64 x) { return (i32)__builtin_floor(x); }

// Кирпичи окна уровня L, которые задевает шар (засыпка пишет не дальше полосы от треугольника), — в lo, hi.
// false — шар вне окна
static bool sdf_range(const SdfGpu &s, u32 L, const f32 c[3], f32 r, i32 lo[3], i32 hi[3]) {
    f32 v = sdf_vox(L), e = r + (SDF_BAND + 1.0f) * v;
    for (u32 a = 0; a < 3; ++a) {
        lo[a] = sdf_floor((f64)(c[a] - e) / (8.0 * v));
        hi[a] = sdf_floor((f64)(c[a] + e) / (8.0 * v));
        if (lo[a] < s.win[L][a]) lo[a] = s.win[L][a];
        if (hi[a] > s.win[L][a] + SDF_W - 1) hi[a] = s.win[L][a] + SDF_W - 1;
        if (lo[a] > hi[a]) return false;
    }
    return true;
}
static void sdf_mark(SdfGpu &s, u32 L, const i32 lo[3], const i32 hi[3]) {
    for (i32 z = lo[2]; z <= hi[2]; ++z)
        for (i32 y = lo[1]; y <= hi[1]; ++y)
            for (i32 x = lo[0]; x <= hi[0]; ++x) {
                i32 b[3] = {x, y, z};
                u32 k = sdf_slot(b);
                s.dirty[L][k >> 5] |= 1u << (k & 31);
            }
}
static bool sdf_any(const SdfGpu &s, u32 L, const i32 lo[3], const i32 hi[3]) {
    for (i32 z = lo[2]; z <= hi[2]; ++z)
        for (i32 y = lo[1]; y <= hi[1]; ++y)
            for (i32 x = lo[0]; x <= hi[0]; ++x) {
                i32 b[3] = {x, y, z};
                u32 k = sdf_slot(b);
                if (s.dirty[L][k >> 5] & (1u << (k & 31))) return true;
            }
    return false;
}

// Шар экземпляра на уровне L: false — не заслонка или мельче полувокселя (на этом уровне его нет)
static bool sdf_sphere(const SdfXf &x, u32 L, f32 c[3], f32 &r) {
    sdf_point(x, x.m->sphere, c);
    r = x.m->sphere[3] * x.s;
    return r >= 0.5f * sdf_vox(L);
}

// Ушедшие и пришедшие экземпляры: множества прошлого и этого кадра сравниваются по содержимому (рассыпка
// перестраивается целиком при шаге камеры), их шары грязнят кирпичи
static RenderInstance g_sdf_prev[RENDER_MAX_INSTANCES];
static u32 g_sdf_prev_n = 0;
constexpr u32 SDF_SET_BITS = 18;
static u32 g_sdf_set[1u << SDF_SET_BITS];  // номер экземпляра + 1

static u32 sdf_hash(const RenderInstance &it) {
    const u32 *w = (const u32 *)&it;
    u32 h = 2166136261u;
    for (u32 i = 0; i < sizeof(RenderInstance) / 4; ++i) h = (h ^ w[i]) * 16777619u;
    return h ^ (h >> 15);
}
static void sdf_set_build(const RenderInstance *a, u32 n) {
    memset(g_sdf_set, 0, sizeof g_sdf_set);
    for (u32 i = 0; i < n; ++i) {
        u32 k = sdf_hash(a[i]) & ((1u << SDF_SET_BITS) - 1);
        while (g_sdf_set[k]) k = (k + 1) & ((1u << SDF_SET_BITS) - 1);
        g_sdf_set[k] = i + 1;
    }
}
static bool sdf_set_has(const RenderInstance *a, const RenderInstance &it) {
    u32 k = sdf_hash(it) & ((1u << SDF_SET_BITS) - 1);
    while (g_sdf_set[k]) {
        if (!memcmp(&a[g_sdf_set[k] - 1], &it, sizeof it)) return true;
        k = (k + 1) & ((1u << SDF_SET_BITS) - 1);
    }
    return false;
}
static void sdf_touch(SdfGpu &s, const RenderInstance &it) {
    SdfXf x;
    if (!sdf_xf(it, s.anchor, x)) return;
    for (u32 L = 0; L < SDF_LEVELS; ++L) {
        f32 c[3], r;
        i32 lo[3], hi[3];
        if (sdf_sphere(x, L, c, r) && sdf_range(s, L, c, r, lo, hi)) sdf_mark(s, L, lo, hi);
    }
}
// Неподвижные заслонки кадра (без людей и меча в руках: они меняются каждый кадр и в поле не идут)
static RenderInstance g_sdf_cur[RENDER_MAX_INSTANCES];
static void sdf_changes(SdfGpu &s, const RenderFrame &f) {
    u32 total = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
    u32 n = 0;
    for (u32 i = 0; i < total; ++i) {
        const RenderInstance &it = f.instances.data[i];
        if (!it.mesh || it.mesh >= RENDER_MAX_MESHES) continue;
        const MeshGpu &m = ((const MeshGpu *)g_vk.mesh_table.mapped)[it.mesh];
        if (m.lod_count && !(m.pad & MESH_DYNAMIC)) g_sdf_cur[n++] = it;
    }
    const RenderInstance *cur = g_sdf_cur;
    bool same = n == g_sdf_prev_n && (n == 0 || !memcmp(cur, g_sdf_prev, n * sizeof(RenderInstance)));
    if (same) return;
    if (!s.reset) {
        sdf_set_build(cur, n);
        for (u32 i = 0; i < g_sdf_prev_n; ++i)
            if (!sdf_set_has(cur, g_sdf_prev[i])) sdf_touch(s, g_sdf_prev[i]);
        sdf_set_build(g_sdf_prev, g_sdf_prev_n);
        for (u32 i = 0; i < n; ++i)
            if (!sdf_set_has(g_sdf_prev, cur[i])) sdf_touch(s, cur[i]);
    }
    memcpy(g_sdf_prev, cur, n * sizeof(RenderInstance));
    g_sdf_prev_n = n;
}

// Окна уровней вокруг камеры (камера — в кирпиче SDF_W/2): кирпичи, вошедшие в окно, грязнеют
static void sdf_windows(SdfGpu &s, const f64 cam[3]) {
    for (u32 L = 0; L < SDF_LEVELS; ++L) {
        f64 bm = 8.0 * sdf_vox(L);
        i32 nw[3] = {sdf_floor(cam[0] / bm) - SDF_W / 2, sdf_floor(cam[1] / bm) - SDF_W / 2,
                     sdf_floor(cam[2] / bm) - SDF_W / 2};
        if (s.reset) {
            memset(s.dirty[L], 0xFF, sizeof s.dirty[L]);
        } else if (nw[0] != s.win[L][0] || nw[1] != s.win[L][1] || nw[2] != s.win[L][2]) {
            for (i32 z = nw[2]; z < nw[2] + SDF_W; ++z)
                for (i32 y = nw[1]; y < nw[1] + SDF_W; ++y)
                    for (i32 x = nw[0]; x < nw[0] + SDF_W; ++x) {
                        bool was = x >= s.win[L][0] && x < s.win[L][0] + SDF_W && y >= s.win[L][1] &&
                                   y < s.win[L][1] + SDF_W && z >= s.win[L][2] && z < s.win[L][2] + SDF_W;
                        if (was) continue;
                        i32 b[3] = {x, y, z};
                        u32 k = sdf_slot(b);
                        s.dirty[L][k >> 5] |= 1u << (k & 31);
                    }
        }
        for (u32 a = 0; a < 3; ++a) s.win[L][a] = nw[a];
    }
}

// Засыпка уровня L: кластеры экземпляров (LOD с ошибкой не больше четверти вокселя), чей шар задевает грязный
// кирпич. false — не влезло в SDF_JOB_CAP (уровень ждёт следующего кадра)
static bool sdf_jobs(SdfGpu &s, const RenderFrame &f, u32 L, SdfJob *jobs) {
    u32 n = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
    f32 v = sdf_vox(L);
    for (u32 i = 0; i < n; ++i) {
        SdfXf x;
        f32 c[3], r;
        i32 lo[3], hi[3];
        if (!sdf_xf(f.instances.data[i], s.anchor, x) || !sdf_sphere(x, L, c, r) || !sdf_range(s, L, c, r, lo, hi) ||
            !sdf_any(s, L, lo, hi))
            continue;
        u32 lod = 0;
        while (lod + 1 < x.m->lod_count && x.m->lods[lod + 1].error * x.s <= 0.25f * v) ++lod;
        const MeshGpu::Lod &l = x.m->lods[lod];
        for (u32 k = 0; k < l.cluster_count; ++k) {
            u32 cl = l.first_cluster + k;
            if (cl >= GEOMETRY_MAX_CLUSTERS) break;
            f32 cc[3];
            sdf_point(x, g_sdf_cls[cl], cc);
            if (!sdf_range(s, L, cc, g_sdf_cls[cl][3] * x.s + 0.01f, lo, hi) || !sdf_any(s, L, lo, hi)) continue;
            if (s.jobs_n >= SDF_JOB_CAP) return false;
            jobs[s.jobs_n++] = {i, cl | L << 24};
        }
    }
    return true;
}

void vk_sdf_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs, bool images) {
    SdfGpu &s = g_vk.sdf;
    s.on = images && s.field.image;
    s.jobs_n = s.bricks_n = 0;
    g.sdf_on = 0;
    if (!s.on) return;
    g_sdf_frame = fs.frame.address;
    f64 camw[3] = {(f64)f.view.cx * 256.0 + f.view.x, f.view.y, (f64)f.view.cz * 256.0 + f.view.z};
    f64 ax = (f64)s.anchor[0] * 256.0, az = (f64)s.anchor[1] * 256.0;
    if (s.seen_mesh != g_vk.shadow.mesh_gen || __builtin_fabs(camw[0] - ax) > SDF_REANCHOR ||
        __builtin_fabs(camw[2] - az) > SDF_REANCHOR)
        s.reset = true;
    if (s.reset) {
        s.seen_mesh = g_vk.shadow.mesh_gen;
        s.anchor[0] = f.view.cx;
        s.anchor[1] = f.view.cz;
        ax = (f64)s.anchor[0] * 256.0;
        az = (f64)s.anchor[1] * 256.0;
    }
    f64 cam[3] = {camw[0] - ax, camw[1], camw[2] - az};
    sdf_windows(s, cam);
    sdf_changes(s, f);
    s.reset = false;

    // Уровни по порядку, пока засыпка влезает; кирпичи уровня, который не влез, остаются грязными до следующего кадра
    SdfJob *jobs = (SdfJob *)fs.sdf_jobs.mapped;
    i32 *bricks = (i32 *)fs.sdf_bricks.mapped;
    u32 *bits = (u32 *)fs.sdf_bits.mapped;
    memset(bits, 0, SDF_BRICK_CAP / 8);
    for (u32 L = 0; L < SDF_LEVELS; ++L) {
        for (u32 a = 0; a < 3; ++a) {
            g.sdf_lo[L][a] = 0x7FFFFFFF;
            g.sdf_hi[L][a] = -0x7FFFFFFF;
        }
        g.sdf_lo[L][3] = g.sdf_hi[L][3] = 0;
    }
    bool full = false;
    for (u32 L = 0; L < SDF_LEVELS && !full; ++L) {
        bool any = false;
        for (u32 w : s.dirty[L]) any |= w != 0;
        if (!any) continue;
        u32 first = s.jobs_n;
        if (!sdf_jobs(s, f, L, jobs)) {
            full = true;
            if (first > 0) {
                s.jobs_n = first;  // уровень целиком — в следующий кадр
                break;
            }
            vk_overflow("поле расстояний: засыпка", SDF_JOB_CAP + 1, SDF_JOB_CAP);
        }
        // Кирпичи уровня: мировой номер — из места в кольце и окна; рамка грязных вокселей — засыпке
        for (u32 k = 0; k < SDF_LEVEL_BRICKS; ++k) {
            if (!(s.dirty[L][k >> 5] & (1u << (k & 31)))) continue;
            i32 sl[3] = {(i32)(k & 15), (i32)((k >> 4) & 15), (i32)(k >> 8)};
            i32 *o = bricks + 4 * s.bricks_n++;
            for (u32 a = 0; a < 3; ++a) {
                o[a] = s.win[L][a] + ((sl[a] - s.win[L][a]) & 15);
                if (o[a] * 8 < g.sdf_lo[L][a]) g.sdf_lo[L][a] = o[a] * 8;
                if (o[a] * 8 + 7 > g.sdf_hi[L][a]) g.sdf_hi[L][a] = o[a] * 8 + 7;
            }
            o[3] = (i32)L;
        }
        memcpy(bits + L * (SDF_LEVEL_BRICKS / 32), s.dirty[L], sizeof s.dirty[L]);
        memset(s.dirty[L], 0, sizeof s.dirty[L]);
    }
    if (s.jobs_n) vmaFlushAllocation(g_vk.vma, fs.sdf_jobs.alloc, 0, VK_WHOLE_SIZE);
    if (s.bricks_n) {
        vmaFlushAllocation(g_vk.vma, fs.sdf_bricks.alloc, 0, VK_WHOLE_SIZE);
        vmaFlushAllocation(g_vk.vma, fs.sdf_bits.alloc, 0, VK_WHOLE_SIZE);
    }
    s.groups_x = s.jobs_n < 65535 ? s.jobs_n : 65535;
    g.sdf_jobs = fs.sdf_jobs.address;
    g.sdf_bricks = fs.sdf_bricks.address;
    g.sdf_bits = fs.sdf_bits.address;
    g.sdf_scratch = s.scratch.address;
    g.sdf_anchor[0] = s.anchor[0];
    g.sdf_anchor[1] = s.anchor[1];
    g.sdf_cam[0] = (f32)cam[0];
    g.sdf_cam[1] = (f32)cam[1];
    g.sdf_cam[2] = (f32)cam[2];
    g.sdf_cam[3] = SDF_V0;
    g.sdf_view = f.style.sdf_view < RENDER_SDF_VIEWS ? f.style.sdf_view : RENDER_SDF_FRAME;
    g.sdf_on = g.sdf_view != RENDER_SDF_OFF ? 1u : 0u;
    g.sdf_jobs_n = s.jobs_n;
    g.sdf_bricks_n = s.bricks_n;
}

static void sdf_record_clear(VkCommandBuffer cmd, void *) {
    vkCmdFillBuffer(cmd, g_vk.sdf.scratch.buffer, 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
}
static void sdf_record_splat(VkCommandBuffer cmd, void *) {
    const SdfGpu &s = g_vk.sdf;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipe_splat);
    PushConsts pc{g_sdf_frame, s.groups_x, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, s.groups_x, (s.jobs_n + s.groups_x - 1) / s.groups_x, 1);
}
static void sdf_record_final(VkCommandBuffer cmd, void *) {
    const SdfGpu &s = g_vk.sdf;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s.pipe_final);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_vk.layout, 0, 1, &g_vk.world.set, 0, nullptr);
    PushConsts pc{g_sdf_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, s.bricks_n, 1, 1);
}

void vk_sdf_passes() {
    SdfGpu &s = g_vk.sdf;
    if (!s.on) return;
    if (s.clear) {
        rg_pass("поле: засыпка с нуля", "поле расстояний", sdf_record_clear, nullptr);
        rg_use(s.rg_scratch, RG_COPY_WRITE);
        s.clear = false;
    }
    if (s.jobs_n) {
        rg_pass("поле: засыпка", "поле расстояний", sdf_record_splat, nullptr);
        rg_use(g_vk.world.rg_inst, RG_CS_READ);
        rg_use(s.rg_scratch, RG_CS_WRITE);
        rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
        vk_skin_vertices_read(RG_CS_READ);
        vk_skin_clusters_read();
    }
    if (s.bricks_n) {
        rg_pass("поле: сборка кирпичей", "поле расстояний", sdf_record_final, nullptr);
        rg_use(s.rg_scratch, RG_CS_WRITE);
        rg_use(s.rg_field, RG_IMG_CS_WRITE);
        rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    }
}

void vk_sdf_reads() {
    const SdfGpu &s = g_vk.sdf;
    if (s.on) rg_use(s.rg_field, RG_IMG_CS_SAMPLE);
}
