// Тени солнца страницами (этап 6б-1, docs/shadow.md). Виртуальная карта теней clipmap'ом вокруг камеры: уровень k —
// решётка текселей cell_min·2^k в плоскости поперёк светила, тексели привязаны к миру. Окно уровня — SM_WIN²
// страниц по SM_PAGE² текселей, запись окна указывает на страницу общего пула. Кадр:
//   сброс страниц под изменившимися экземплярами (shadow_inval.comp) → заявки в разборе плиток (light_tiles.comp)
//   → свободные (shadow_scan.comp) → счёт (shadow_args.comp) → выдача и очистка (shadow_alloc.comp) → отбор
//   заслонок для новых страниц (shadow_cull.comp) → отрисовка в страницы (shadow.vert/.frag) → свет читает.
// Страница рисуется раз и живёт, пока её читают: статичный мир почти бесплатен. Светило идёт шагами по октаэдру
// (~0.1°): новый шаг — все страницы с нуля (перетекание старой и новой карты — 6б-2)
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr u32 SM_MIN_PAGES = 1024, SM_MAX_PAGES = 8192;  // пул: 64 МБ … 512 МБ (64 КБ на страницу)
constexpr u32 SM_REQ_HEAD = 16 + 16 + 16 * 4 + 16 * 32 * 4;  // SmReq до заявок (common.glsl)
constexpr f32 SM_FAR = 3000.0f;   // дальше тени сеток нет (карта горизонта остаётся), м
constexpr f32 SM_STEP = 1024.0f;  // шаг светила: доля октаэдра (~0.06–0.11°)

bool vk_shadow_init() {
    ShadowGpu &s = g_vk.shadow;
    s.pipe_inval = vk_make_compute("shadow_inval.comp");
    s.pipe_scan = vk_make_compute("shadow_scan.comp");
    s.pipe_args = vk_make_compute("shadow_args.comp");
    s.pipe_alloc = vk_make_compute("shadow_alloc.comp");
    s.pipe_cull = vk_make_compute("shadow_cull.comp");
    PipeDesc d{"shadow.vert", "shadow.frag"};
    d.no_attach = true;
    d.depth_test = false;
    d.depth_write = false;
    // Только грани, повёрнутые от светила (у сеток они снаружи против часовой, как у visibility buffer): тексель
    // хранит нижнюю грань заслонки, и освещённая грань не затеняет сама себя
    d.cull = VK_CULL_MODE_FRONT_BIT;
    s.pipe_draw = vk_make_pipeline(d);
    s.rg_table = rg_buffer("тени: окна страниц");
    s.rg_pages = rg_buffer("тени: страницы");
    s.rg_texels = rg_buffer("тени: тексели");
    s.rg_req = rg_buffer("тени: заявки");
    s.rg_draws = rg_buffer("тени: вызовы");
    s.rg_list = rg_buffer("тени: список кластеров");
    s.reset = true;
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.sm_inval, SM_INVAL_MAX * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
        vk_buffer_name(fs.sm_inval, "тени: сферы сброса");
    }
    return s.pipe_inval && s.pipe_scan && s.pipe_args && s.pipe_alloc && s.pipe_cull && s.pipe_draw;
}

void vk_shadow_shutdown() {
    ShadowGpu &s = g_vk.shadow;
    VkPipeline ps[] = {s.pipe_inval, s.pipe_scan, s.pipe_args, s.pipe_alloc, s.pipe_cull, s.pipe_draw};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    GpuBuffer *bs[] = {&s.table, &s.pages, &s.texels, &s.req, &s.draws, &s.list};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    for (FrameSlot &fs : g_vk.frames) vk_destroy_buffer(fs.sm_inval);
    u32 mg = s.mesh_gen;
    s = ShadowGpu{};
    s.mesh_gen = mg;
}

static bool sm_make(GpuBuffer &b, VkDeviceSize size, const char *name, u8 rg, VkBufferUsageFlags extra = 0) {
    vk_destroy_buffer(b);
    if (!vk_create_buffer(b, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra, true))
        return false;
    vk_buffer_name(b, name);
    rg_reset(rg);
    return true;
}

// Пул под размер кадра: страниц — пикселей / 1024, не меньше SM_MIN_PAGES; не хватило — вдвое (с нуля)
static bool sm_ensure(u32 width, u32 height) {
    ShadowGpu &s = g_vk.shadow;
    u32 pages = s.pages_n ? s.pages_n : (width * height / 1024 < SM_MIN_PAGES ? SM_MIN_PAGES : width * height / 1024);
    if (s.grow && pages < SM_MAX_PAGES) {
        log_info("тени: пул %u → %u страниц (%u заявок без места)", pages, pages * 2, s.grow);
        pages *= 2;
    }
    s.grow = 0;
    if (pages > SM_MAX_PAGES) pages = SM_MAX_PAGES;
    if (pages == s.pages_n && s.table.buffer) return true;
    vkDeviceWaitIdle(g_vk.device);
    s.pages_n = pages;
    s.req_cap = pages;
    const VkBufferUsageFlags ind = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (!sm_make(s.table, (VkDeviceSize)SM_LEVELS * SM_WIN * SM_WIN * 4, "тени: окна страниц", s.rg_table) ||
        !sm_make(s.pages, (VkDeviceSize)pages * 16, "тени: страницы", s.rg_pages) ||
        !sm_make(s.texels, (VkDeviceSize)pages * SM_PAGE * SM_PAGE * 4, "тени: тексели", s.rg_texels) ||
        !sm_make(s.req, SM_REQ_HEAD + (VkDeviceSize)(2 * s.req_cap + 2 * pages) * 4, "тени: заявки", s.rg_req, ind) ||
        !sm_make(s.draws, 16 + (VkDeviceSize)SM_DRAW_CAP * 20, "тени: вызовы", s.rg_draws, ind) ||
        !sm_make(s.list, (VkDeviceSize)SM_DRAW_CAP * 8, "тени: список кластеров", s.rg_list)) {
        s.pages_n = 0;
        return false;
    }
    log_info("тени: окно %u² страниц по %u² текселей × %u уровней, пул %u страниц (%.0f МБ)", SM_WIN, SM_PAGE,
             SM_LEVELS, pages, (f64)(s.texels.size + s.pages.size + s.table.size) / (1 << 20));
    s.reset = true;
    return true;
}

// Шаг светила: направление в октаэдр, клетка 1/SM_STEP. Ключ — номер клетки, направление — её центр
static void sm_sun_step(const f32 l[3], i32 key[2], f32 out[3]) {
    f32 n = f_abs(l[0]) + f_abs(l[1]) + f_abs(l[2]);
    if (n <= 0) n = 1;
    f32 x = l[0] / n, y = l[1] / n, z = l[2] / n;
    if (y < 0) {
        f32 ox = (1 - f_abs(z)) * (x >= 0 ? 1.0f : -1.0f), oz = (1 - f_abs(x)) * (z >= 0 ? 1.0f : -1.0f);
        x = ox;
        z = oz;
    }
    key[0] = f_floor_i(x * SM_STEP + 0.5f);
    key[1] = f_floor_i(z * SM_STEP + 0.5f);
    x = (f32)key[0] / SM_STEP;
    z = (f32)key[1] / SM_STEP;
    y = 1 - f_abs(x) - f_abs(z);
    if (y < 0) {
        f32 ox = (1 - f_abs(z)) * (x >= 0 ? 1.0f : -1.0f), oz = (1 - f_abs(x)) * (z >= 0 ? 1.0f : -1.0f);
        x = ox;
        z = oz;
    }
    f32 m = f_sqrt(x * x + y * y + z * z);
    out[0] = x / m;
    out[1] = y / m;
    out[2] = z / m;
}

// Сброс страниц под изменившимися экземплярами: множества экземпляров прошлого и этого кадра сравниваются по
// содержимому (рассыпка перестраивается целиком при шаге камеры, и по номерам сдвинулась бы вся), разница —
// сферы прежнего и нового места
static RenderInstance g_sm_prev[RENDER_MAX_INSTANCES];
static u32 g_sm_prev_n = 0;
constexpr u32 SM_SET_BITS = 18;
static u32 g_sm_set[1u << SM_SET_BITS];  // номер экземпляра + 1

static u32 sm_inst_hash(const RenderInstance &it) {
    const u32 *w = (const u32 *)&it;
    u32 h = 2166136261u;
    for (u32 i = 0; i < sizeof(RenderInstance) / 4; ++i) h = (h ^ w[i]) * 16777619u;
    return h ^ (h >> 15);
}
static void sm_set_build(const RenderInstance *a, u32 n) {
    memset(g_sm_set, 0, sizeof g_sm_set);
    for (u32 i = 0; i < n; ++i) {
        u32 s = sm_inst_hash(a[i]) & ((1u << SM_SET_BITS) - 1);
        while (g_sm_set[s]) s = (s + 1) & ((1u << SM_SET_BITS) - 1);
        g_sm_set[s] = i + 1;
    }
}
static bool sm_set_has(const RenderInstance *a, const RenderInstance &it) {
    u32 s = sm_inst_hash(it) & ((1u << SM_SET_BITS) - 1);
    while (g_sm_set[s]) {
        if (!memcmp(&a[g_sm_set[s] - 1], &it, sizeof it)) return true;
        s = (s + 1) & ((1u << SM_SET_BITS) - 1);
    }
    return false;
}

// Сфера экземпляра относительно камеры; false — не заслонка (нет сетки, человек)
static bool sm_inst_sphere(const FrameGpu &g, const RenderInstance &it, f32 out[4]) {
    if (!it.mesh || it.mesh >= RENDER_MAX_MESHES) return false;
    const MeshGpu &m = ((const MeshGpu *)g_vk.mesh_table.mapped)[it.mesh];
    if (!m.lod_count || (m.pad & MESH_DYNAMIC)) return false;
    auto sn = [](u32 v, u32 sh) { return f_clamp((f32)(i16)(u16)(v >> sh) / 32767.0f, -1.0f, 1.0f); };
    f32 q[4] = {sn(it.rot_xy, 0), sn(it.rot_xy, 16), sn(it.rot_zw, 0), sn(it.rot_zw, 16)};
    f32 ql = f_sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (ql <= 0) ql = 1;
    for (f32 &c : q) c /= ql;
    f32 v[3] = {m.sphere[0] * it.scale, m.sphere[1] * it.scale, m.sphere[2] * it.scale};
    // v + 2w(q × v) + 2 q × (q × v)
    f32 t[3] = {2 * (q[1] * v[2] - q[2] * v[1]), 2 * (q[2] * v[0] - q[0] * v[2]), 2 * (q[0] * v[1] - q[1] * v[0])};
    f32 r[3] = {v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]), v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]),
                v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0])};
    out[0] = (f32)((it.cx - g.cam_cx) * 256) + (it.x - g.cam_x) + r[0];
    out[1] = it.y - g.cam_y + r[1];
    out[2] = (f32)((it.cz - g.cam_cz) * 256) + (it.z - g.cam_z) + r[2];
    out[3] = m.sphere[3] * it.scale;
    return true;
}

// Сферы сброса этого кадра в буфер слота; false — их больше SM_INVAL_MAX (сброс всего)
static bool sm_invalidations(const FrameGpu &g, const RenderFrame &f, FrameSlot &fs) {
    ShadowGpu &s = g_vk.shadow;
    s.inval_n = 0;
    u32 n = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
    const RenderInstance *cur = f.instances.data;
    bool same = n == g_sm_prev_n && (n == 0 || !memcmp(cur, g_sm_prev, n * sizeof(RenderInstance)));
    if (same) return true;
    f32 *out = (f32 *)fs.sm_inval.mapped;
    bool ok = true;
    auto add = [&](const RenderInstance &it) {
        f32 sp[4];
        if (!sm_inst_sphere(g, it, sp)) return;
        if (s.inval_n >= SM_INVAL_MAX) { ok = false; return; }
        memcpy(out + 4 * s.inval_n++, sp, sizeof sp);
    };
    if (!s.reset) {
        sm_set_build(cur, n);  // ушедшие: были, а теперь нет
        for (u32 i = 0; i < g_sm_prev_n && ok; ++i)
            if (!sm_set_has(cur, g_sm_prev[i])) add(g_sm_prev[i]);
        sm_set_build(g_sm_prev, g_sm_prev_n);  // пришедшие: есть, а раньше не было
        for (u32 i = 0; i < n && ok; ++i)
            if (!sm_set_has(g_sm_prev, cur[i])) add(cur[i]);
    }
    memcpy(g_sm_prev, cur, n * sizeof(RenderInstance));
    g_sm_prev_n = n;
    if (s.inval_n) vmaFlushAllocation(g_vk.vma, fs.sm_inval.alloc, 0, VK_WHOLE_SIZE);
    if (!ok) s.inval_n = 0;
    return ok;
}

void vk_shadow_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs, bool images) {
    ShadowGpu &s = g_vk.shadow;
    s.on = images && f.style.cell_min > 0 && sm_ensure(g_vk.extent.width, g_vk.extent.height);
    s.inval_n = 0;
    if (!s.on) return;
    // Шаг светила, размер текселя, сетки: сменилось — все страницы с нуля
    i32 key[2];
    f32 S[3];
    sm_sun_step(f.light.sun_dir, key, S);
    f64 cam[3] = {(f64)f.view.cx * 256.0 + f.view.x, f.view.y, (f64)f.view.cz * 256.0 + f.view.z};
    if (key[0] != s.key[0] || key[1] != s.key[1] || f.style.cell_min != s.cell_min || s.mesh_gen != s.seen_mesh) {
        s.key[0] = key[0];
        s.key[1] = key[1];
        s.cell_min = f.style.cell_min;
        s.seen_mesh = s.mesh_gen;
        s.reset = true;
    }
    if (s.reset) {
        // Якорь высоты к светилу — у камеры: высоты в страницах малые числа, f32 их держит до миллиметров
        s.anchor = (f64)(i64)(cam[0] * S[0] + cam[1] * S[1] + cam[2] * S[2]);
    }
    if (!sm_invalidations(g, f, fs)) {
        log_info("тени: изменилось больше %u заслонок — все страницы с нуля", SM_INVAL_MAX);
        s.reset = true;
    }
    // Оси карты: r — горизонтальная поперёк светила, u = s × r (у светила в зените — r по x)
    f32 R[3] = {S[2], 0.0f, -S[0]};
    f32 rl = f_sqrt(R[0] * R[0] + R[2] * R[2]);
    if (rl < 1e-4f) {
        R[0] = 1.0f;
        R[2] = 0.0f;
    } else {
        R[0] /= rl;
        R[2] /= rl;
    }
    f32 U[3] = {S[1] * R[2] - S[2] * R[1], S[2] * R[0] - S[0] * R[2], S[0] * R[1] - S[1] * R[0]};
    for (u32 i = 0; i < 3; ++i) {
        g.sm_r[i] = R[i];
        g.sm_u[i] = U[i];
        g.sm_s[i] = S[i];
    }
    g.sm_r[3] = f.style.cell_min;
    g.sm_u[3] = (f32)(cam[0] * S[0] + cam[1] * S[1] + cam[2] * S[2] - s.anchor);
    g.sm_s[3] = SM_FAR;
    // Окна уровней: камера — в странице SM_WIN/2 окна, начало окна — целая страница мировой решётки уровня
    f64 cr = cam[0] * R[0] + cam[1] * R[1] + cam[2] * R[2];
    f64 cu = cam[0] * U[0] + cam[1] * U[1] + cam[2] * U[2];
    for (u32 k = 0; k < SM_LEVELS; ++k) {
        f64 page = (f64)f.style.cell_min * (f64)(1u << k) * SM_PAGE;
        f64 br = __builtin_floor(cr / page) - SM_WIN / 2, bu = __builtin_floor(cu / page) - SM_WIN / 2;
        g.sm_base[k][0] = (i32)br;
        g.sm_base[k][1] = (i32)bu;
        g.sm_off[k][0] = (f32)(cr - br * page);
        g.sm_off[k][1] = (f32)(cu - bu * page);
    }
    if (++s.frame == 0) s.frame = 1;
    g.sm_table = s.table.address;
    g.sm_pages = s.pages.address;
    g.sm_texels = s.texels.address;
    g.sm_req = s.req.address;
    g.sm_draws = s.draws.address;
    g.sm_list = s.list.address;
    g.sm_inval = fs.sm_inval.address;
    g.sm_on = 1;
    g.sm_frame = s.frame;
    g.sm_pages_n = s.pages_n;
    g.sm_req_cap = s.req_cap;
    g.sm_draw_cap = SM_DRAW_CAP;
    g.sm_inval_n = s.inval_n;
    g.sm_debug = f.style.shadow_pages;
}

void vk_shadow_read(const u32 *st) {
    ShadowGpu &s = g_vk.shadow;
    if (st[STAT_SM_OVER]) {
        s.grow = st[STAT_SM_OVER];
        vk_overflow("тени: страницы без места", (u64)st[STAT_SM_NEW] + st[STAT_SM_OVER], s.pages_n);
    }
    vk_overflow("тени: вызовы кластеров", st[STAT_SM_DRAWS], SM_DRAW_CAP);
}

static VkDeviceAddress g_sm_frame = 0;

static void sm_bind(VkCommandBuffer cmd, VkPipeline p) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{g_sm_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
}

static void sm_record_reset(VkCommandBuffer cmd, void *) {
    const ShadowGpu &s = g_vk.shadow;
    vkCmdFillBuffer(cmd, s.table.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, s.pages.buffer, 0, VK_WHOLE_SIZE, 0);
}
static void sm_record_clear(VkCommandBuffer cmd, void *) {
    vkCmdFillBuffer(cmd, g_vk.shadow.req.buffer, 0, SM_REQ_HEAD, 0);
    vkCmdFillBuffer(cmd, g_vk.shadow.draws.buffer, 0, 16, 0);
}
static void sm_record_inval(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_inval);
    vkCmdDispatch(cmd, (g_vk.shadow.inval_n + 63) / 64, SM_LEVELS, 1);
}
static void sm_record_scan(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_scan);
    vkCmdDispatch(cmd, (g_vk.shadow.pages_n + 63) / 64, 1, 1);
}
static void sm_record_args(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_args);
    vkCmdDispatch(cmd, 1, 1, 1);
}
static void sm_record_alloc(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_alloc);
    vkCmdDispatchIndirect(cmd, g_vk.shadow.req.buffer, 16);
}
static void sm_record_cull(VkCommandBuffer cmd, void *) {
    u32 n = g_vk.world.inst_count;
    if (!n) return;
    sm_bind(cmd, g_vk.shadow.pipe_cull);
    vkCmdDispatch(cmd, (n + 63) / 64, 1, 1);
}
// Отрисовка новых страниц: без картинок, область вывода — окно уровня (SM_WIN·SM_PAGE текселей)
static void sm_record_draw(VkCommandBuffer cmd, void *) {
    const ShadowGpu &s = g_vk.shadow;
    if (!g_vk.world.inst_count) return;
    const u32 side = SM_WIN * SM_PAGE;
    auto ri = vk_info<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    ri.renderArea = {{0, 0}, {side, side}};
    ri.layerCount = 1;
    vkCmdBeginRendering(cmd, &ri);
    VkViewport vp{0, 0, (f32)side, (f32)side, 0, 1};
    VkRect2D sc{{0, 0}, {side, side}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.pipe_draw);
    PushConsts pc{g_sm_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirectCount(cmd, s.draws.buffer, 16, s.draws.buffer, 0, SM_DRAW_CAP, 20);
    vkCmdEndRendering(cmd);
}

void vk_shadow_begin(VkDeviceAddress frame) {
    ShadowGpu &s = g_vk.shadow;
    g_sm_frame = frame;
    if (!s.on) return;
    if (s.reset) {
        rg_pass("тени: сброс", "тени солнца", sm_record_reset, nullptr);
        rg_use(s.rg_table, RG_COPY_WRITE);
        rg_use(s.rg_pages, RG_COPY_WRITE);
        s.reset = false;
    }
    rg_pass("тени: заявки с нуля", "тени солнца", sm_record_clear, nullptr);
    rg_use(s.rg_req, RG_COPY_WRITE);
    rg_use(s.rg_draws, RG_COPY_WRITE);
    if (s.inval_n) {
        rg_pass("тени: сброс под изменившимся", "тени солнца", sm_record_inval, nullptr);
        rg_use(s.rg_table, RG_CS_WRITE);
        rg_use(s.rg_pages, RG_CS_WRITE);
    }
}

void vk_shadow_tiles_uses() {
    const ShadowGpu &s = g_vk.shadow;
    if (!s.on) return;
    rg_use(s.rg_table, RG_CS_WRITE);
    rg_use(s.rg_pages, RG_CS_WRITE);
    rg_use(s.rg_req, RG_CS_WRITE);
}

void vk_shadow_passes() {
    const ShadowGpu &s = g_vk.shadow;
    const WorldGpu &w = g_vk.world;
    if (!s.on) return;
    rg_pass("тени: свободные", "тени солнца", sm_record_scan, nullptr);
    rg_use(s.rg_pages, RG_CS_READ);
    rg_use(s.rg_req, RG_CS_WRITE);
    rg_pass("тени: счёт", "тени солнца", sm_record_args, nullptr);
    rg_use(s.rg_req, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("тени: выдача", "тени солнца", sm_record_alloc, nullptr);
    rg_use(s.rg_req, RG_INDIRECT);
    rg_use(s.rg_req, RG_CS_WRITE);
    rg_use(s.rg_table, RG_CS_WRITE);
    rg_use(s.rg_pages, RG_CS_WRITE);
    rg_use(s.rg_texels, RG_CS_WRITE);
    rg_pass("тени: отбор", "тени солнца", sm_record_cull, nullptr);
    rg_use(s.rg_req, RG_CS_READ);
    rg_use(w.rg_inst, RG_CS_READ);
    rg_use(s.rg_draws, RG_CS_WRITE);
    rg_use(s.rg_list, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_skin_clusters_read();
    rg_pass("тени: страницы", "тени солнца", sm_record_draw, nullptr);
    rg_use(s.rg_draws, RG_INDIRECT);
    rg_use(s.rg_list, RG_VS_READ);
    rg_use(w.rg_inst, RG_VS_READ);
    rg_use(s.rg_table, RG_FS_READ);
    rg_use(s.rg_pages, RG_FS_READ);
    rg_use(s.rg_texels, RG_FS_WRITE);
    vk_skin_vertices_read(RG_VS_READ);
}

void vk_shadow_reads() {
    const ShadowGpu &s = g_vk.shadow;
    if (!s.on) return;
    rg_use(s.rg_table, RG_CS_READ);
    rg_use(s.rg_pages, RG_CS_READ);
    rg_use(s.rg_texels, RG_CS_READ);
}
