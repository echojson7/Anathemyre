// Кеш поверхности земли (этап 5в, шаг 4; docs/gcache.md). Виртуальная текстура в мировых координатах: уровень k —
// решётка ячеек правила пиксельности (cell_min·2^k), страница — 8×8 текселей, окно страниц уровня вокруг камеры
// указывает на страницу общего пула. Кадр: разбор плиток (light_tiles.comp) пишет пикселю земли его тексель и
// заявляет недостающие страницы → gcache_scan.comp собирает свободные (старые, потом недавние) →
// gcache_args.comp — сколько заявок получат место → gcache_bake.comp запекает их → освещение читает тексели.
// Тексель — функция только своего уровня и номера, поэтому кадр не зависит от того, когда страница запеклась.
// Пул растёт вдвое, если в прочитанном кадре не хватило места (пиксели без места считались на пиксель)
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr u32 GC_MIN_PAGES = 4096, GC_MAX_PAGES = 1u << 18;  // пул: 4 МБ … 256 МБ
constexpr u32 GC_MAX_W = 1024;                               // окно уровня: до 1024² страниц
constexpr u32 GC_LIGHT_BYTES = 16;                           // свет текселя: высота, E/π, доли (gcache_light.comp)
constexpr u32 GC_LIGHT_PERIOD = 8;                           // страница пересчитывает свет раз в столько кадров
constexpr f32 GC_LIGHT_MOVE = 2.0f;  // при полёте круг короче: камера проходит за круг не больше, м (не меньше 2 кадров)

bool vk_gcache_init() {
    GcacheGpu &c = g_vk.gcache;
    bool ok = true;
    ok = vk_make_compute(&c.pipe_scan, "gcache_scan.comp") && ok;
    ok = vk_make_compute(&c.pipe_args, "gcache_args.comp") && ok;
    ok = vk_make_compute(&c.pipe_bake, "gcache_bake.comp") && ok;
    ok = vk_make_compute(&c.pipe_light, "gcache_light.comp") && ok;
    c.rg_table = rg_buffer("земля: кеш, окна страниц");
    c.rg_pages = rg_buffer("земля: кеш, страницы");
    c.rg_texels = rg_buffer("земля: кеш, тексели");
    c.rg_req = rg_buffer("земля: кеш, заявки");
    c.rg_cells = rg_buffer("земля: ячейки пикселей");
    c.rg_light = rg_buffer("земля: кеш, свет");
    c.reset = true;
    return ok;
}

void vk_gcache_shutdown() {
    GcacheGpu &c = g_vk.gcache;
    VkPipeline ps[] = {c.pipe_scan, c.pipe_args, c.pipe_bake, c.pipe_light};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    GpuBuffer *bs[] = {&c.table, &c.pages, &c.texels, &c.req, &c.cells, &c.light};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    u32 mg = c.materials_gen, tg = c.terrain_gen;
    c = GcacheGpu{};
    c.materials_gen = mg;
    c.terrain_gen = tg;
}

static bool make(GpuBuffer &b, VkDeviceSize size, const char *name, u8 rg, VkBufferUsageFlags extra = 0) {
    vk_destroy_buffer(b);
    if (!vk_create_buffer(b, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | extra, true))
        return false;
    vk_buffer_name(b, name);
    rg_reset(rg);
    return true;
}

// Окно уровня в страницах: видимая земля уровня k лежит не дальше E = r / (pixel_angle · cell_pixels) текселей от
// камеры по xz (r — длина луча угла кадра при глубине 1), окно — степень двойки не меньше 2E/8 + 3
static u32 window_pages(const RenderFrame &f, u32 width, u32 height) {
    f32 t = f.view.tan_half_fov, aspect = height ? (f32)width / (f32)height : 1.0f;
    f32 pa = 2.0f * t / (f32)(height ? height : 1);
    f32 r = f_sqrt(1.0f + t * aspect * t * aspect + t * t);
    f32 e = r / (pa * f.style.cell_pixels);
    u32 need = (u32)(2.0f * e / 8.0f) + 3, w = 64;
    while (w < need && w < GC_MAX_W) w *= 2;
    return w;
}

// Буферы под окно, пул и размер кадра; всё, что меняет содержимое текселей, — сброс кеша
static bool ensure(const RenderFrame &f, u32 width, u32 height) {
    GcacheGpu &c = g_vk.gcache;
    u32 w = window_pages(f, width, height);
    u32 pages = c.pages_n ? c.pages_n : (width * height / 24 < GC_MIN_PAGES ? GC_MIN_PAGES : width * height / 24);
    if (c.grow && pages < GC_MAX_PAGES) {
        log_info("кеш земли: пул %u → %u страниц (%u заявок без места)", pages, pages * 2, c.grow);
        pages *= 2;
    }
    c.grow = 0;
    if (pages > GC_MAX_PAGES) pages = GC_MAX_PAGES;
    VkDeviceSize cells = (VkDeviceSize)width * height * 12;
    bool remake = w != c.w || pages != c.pages_n || !c.table.buffer;
    if (!remake && c.cells.size >= cells) return true;
    vkDeviceWaitIdle(g_vk.device);
    if (c.cells.size < cells && !make(c.cells, cells, "земля: ячейки пикселей", c.rg_cells)) return false;
    if (!remake) return true;
    c.w = w;
    c.pages_n = pages;
    c.req_cap = pages;
    if (!make(c.table, (VkDeviceSize)GC_LEVELS * w * w * 4, "земля: кеш, окна страниц", c.rg_table) ||
        !make(c.pages, (VkDeviceSize)pages * 16, "земля: кеш, страницы", c.rg_pages) ||
        !make(c.texels, (VkDeviceSize)pages * GC_PAGE_TEXELS * GC_TEXEL_BYTES, "земля: кеш, тексели", c.rg_texels) ||
        !make(c.light, (VkDeviceSize)pages * GC_PAGE_TEXELS * GC_LIGHT_BYTES, "земля: кеш, свет", c.rg_light) ||
        !make(c.req, 32 + (VkDeviceSize)(2 * c.req_cap + 2 * pages) * 4, "земля: кеш, заявки", c.rg_req,
              VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)) {
        c.w = c.pages_n = 0;
        return false;
    }
    log_info("кеш земли: окно %u² страниц × %u уровней, пул %u страниц (%.0f МБ)", w, GC_LEVELS, pages,
             (f64)(c.pages.size + c.texels.size + c.light.size + c.table.size) / (1 << 20));
    c.reset = true;
    return true;
}

void vk_gcache_fill(FrameGpu &g, const RenderFrame &f, bool images) {
    GcacheGpu &c = g_vk.gcache;
    // Тексели — от размера ячейки, правила, материалов слоёв земли и самой земли: изменилось — с нуля
    i32 tpc0 = (i32)(256.0f / f.style.cell_min + 0.5f);
    bool grid = f.style.cell_min > 0 && (f32)tpc0 * f.style.cell_min == 256.0f && tpc0 % (1 << (GC_LEVELS - 1)) == 0;
    // Почему кеша нет — вслух (F3, журнал): выключенный кеш молча уводит всю землю на пиксель, и кадр просто
    // медленнее (docs/gcache.md)
    const char *off = !f.style.gcache ? "без кеша земли (F3, --nogcache)"
                      : !grid         ? "ячейка не делит чанк"
                                      : nullptr;
    bool ground = images && g_vk.terrain.on && f.ground;
    c.on = ground && !off && ensure(f, g_vk.extent.width, g_vk.extent.height);
    if (ground && !off && !c.on) off = "нет видеопамяти";
    if (off != c.off) {
        if (off) log_info("кеш земли выключен: %s — вся земля считается на пиксель", off);
        else if (c.on) log_info("кеш земли включён");
    }
    c.off = off;
    if (!c.on) return;
    u32 layers[8];
    for (u32 i = 0; i < 8; ++i) layers[i] = g.ground_layers[i];
    if (f.style.cell_min != c.cell_min || f.style.cell_pixels != c.cell_pixels || memcmp(layers, c.layers, sizeof layers) ||
        c.materials_gen != c.seen_materials || c.terrain_gen != c.seen_terrain) {
        c.cell_min = f.style.cell_min;
        c.cell_pixels = f.style.cell_pixels;
        memcpy(c.layers, layers, sizeof layers);
        c.seen_materials = c.materials_gen;
        c.seen_terrain = c.terrain_gen;
        c.reset = true;
    }
    c.tpc0 = tpc0;
    if (++c.frame == 0) {
        c.frame = 1;
        c.lit_min = 0;  // счёт по кругу: старый порог иначе держал бы всё «устаревшим»
    }
    g.gc_table = c.table.address;
    g.gc_pages = c.pages.address;
    g.gc_texels = c.texels.address;
    g.gc_req = c.req.address;
    g.gc_cells = c.cells.address;
    g.gc_light = c.light.address;
    g.gc_w = c.w;
    g.gc_pages_n = c.pages_n;
    g.gc_req_cap = c.req_cap;
    g.gc_frame = c.frame;
    g.gc_tpc0 = c.tpc0;
    g.gc_on = 1;
}

// Отражённый свет в кеше (docs/gcache.md, «Отражённый свет в кеше»): после зондов и ячеек — снимок зондов, сброс
// ячеек, ускоренное время и смена того, от чего свет текселя, пересчитывают все страницы кадра разом
void vk_gcache_light_fill(FrameGpu &g, const RenderFrame &f) {
    GcacheGpu &c = g_vk.gcache;
    bool lit = c.on && f.style.gclight && !f.style.pixel && c.pipe_light;
    if (!lit) {
        c.lit = false;
        c.lit_all = true;
        g.gc_lmode = 0;
        return;
    }
    u32 sig = g.light_q | g.gi_view << 4 | g.sdf_view << 8 | g.gi_on << 12 | g.sdf_on << 13 | g.lc_on << 14;
    if (sig != c.lit_sig || !c.lit || c.reset || g_vk.gi.snapped || g.lc_snap || g_vk.shadow.sun_fast) c.lit_all = true;
    c.lit_sig = sig;
    c.lit = true;
    // «Все заново» касается и страниц, которых этот кадр не читает: их свет — от прошлых зондов и ячеек (до скачка
    // часов — дневной), а «свежим» он считался ещё круг; вернувшись в кадр, страница показала бы его белым
    // прямоугольником на ночной земле (разбор 17.11). Свет старше lit_min пересчитывается при первом чтении
    if (c.lit_all) c.lit_min = c.frame;
    g.gc_lit_min = c.lit_min;
    g.gc_lmode = GC_LIGHT_ON | (c.lit_all ? GC_LIGHT_ALL : 0);
    g.gc_lperiod = GC_LIGHT_PERIOD;
    // Свет текселя от камеры чуть зависит (окна зондов, уровни ячеек, второй отскок по зондам), и при быстром полёте
    // страницы, пересчитанные в разные кадры круга, расходились — по освещённому песку шли квадраты страниц (разбор
    // 18.11). Круг — столько кадров, чтобы камера прошла за него не больше GC_LIGHT_MOVE, но не меньше 2
    f32 dx = (f32)(g.cam_cx - c.lit_cx) * 256.0f + (g.cam_x - c.lit_x), dy = g.cam_y - c.lit_y,
        dz = (f32)(g.cam_cz - c.lit_cz) * 256.0f + (g.cam_z - c.lit_z);
    f32 step = sqrtf(dx * dx + dy * dy + dz * dz);
    c.lit_cx = g.cam_cx;
    c.lit_cz = g.cam_cz;
    c.lit_x = g.cam_x;
    c.lit_y = g.cam_y;
    c.lit_z = g.cam_z;
    u32 turn = GC_LIGHT_PERIOD;
    if (step * (f32)GC_LIGHT_PERIOD > GC_LIGHT_MOVE) turn = (u32)(GC_LIGHT_MOVE / step);
    g.gc_lturn = turn < 2 ? 2 : turn > GC_LIGHT_PERIOD ? GC_LIGHT_PERIOD : turn;
    c.lit_all = false;
}

void vk_gcache_read(const u32 *st) {
    GcacheGpu &c = g_vk.gcache;
    // Не хватило места — пул растёт перед следующим кадром (список заявок не длиннее пула)
    if (st[STAT_GC_OVER]) {
        c.grow = st[STAT_GC_OVER];
        vk_overflow("кеш земли: страницы без места", (u64)st[STAT_GC_BAKED] + st[STAT_GC_OVER], c.pages_n);
    }
}

static void record_reset(VkCommandBuffer cmd, void *) {
    const GcacheGpu &c = g_vk.gcache;
    vkCmdFillBuffer(cmd, c.table.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, c.pages.buffer, 0, VK_WHOLE_SIZE, 0);
}

static void record_req_clear(VkCommandBuffer cmd, void *) { vkCmdFillBuffer(cmd, g_vk.gcache.req.buffer, 0, 16, 0); }

static VkDeviceAddress g_gc_frame = 0;

static void gc_bind(VkCommandBuffer cmd, VkPipeline p) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{g_gc_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
}
static void record_scan(VkCommandBuffer cmd, void *) {
    gc_bind(cmd, g_vk.gcache.pipe_scan);
    vkCmdDispatch(cmd, (g_vk.gcache.pages_n + 63) / 64, 1, 1);
}
static void record_args(VkCommandBuffer cmd, void *) {
    gc_bind(cmd, g_vk.gcache.pipe_args);
    vkCmdDispatch(cmd, 1, 1, 1);
}
static void record_bake(VkCommandBuffer cmd, void *) {
    gc_bind(cmd, g_vk.gcache.pipe_bake);
    vkCmdDispatchIndirect(cmd, g_vk.gcache.req.buffer, 16);
}

void vk_gcache_begin(VkDeviceAddress frame) {
    GcacheGpu &c = g_vk.gcache;
    g_gc_frame = frame;
    if (!c.on) return;
    if (c.reset) {
        rg_pass("земля: кеш, сброс", "плитки", record_reset, nullptr);
        rg_use(c.rg_table, RG_COPY_WRITE);
        rg_use(c.rg_pages, RG_COPY_WRITE);
        c.reset = false;
    }
    rg_pass("земля: кеш, заявки с нуля", "плитки", record_req_clear, nullptr);
    rg_use(c.rg_req, RG_COPY_WRITE);
}

void vk_gcache_tiles_uses() {
    const GcacheGpu &c = g_vk.gcache;
    if (!c.on) return;
    rg_use(c.rg_table, RG_CS_WRITE);
    rg_use(c.rg_pages, RG_CS_WRITE);
    rg_use(c.rg_req, RG_CS_WRITE);
    rg_use(c.rg_cells, RG_CS_WRITE);
}

void vk_gcache_bake_passes() {
    const GcacheGpu &c = g_vk.gcache;
    if (!c.on) return;
    rg_pass("земля: кеш, свободные", "земля: кеш", record_scan, nullptr);
    rg_use(c.rg_pages, RG_CS_READ);
    rg_use(c.rg_req, RG_CS_WRITE);
    rg_pass("земля: кеш, счёт", "земля: кеш", record_args, nullptr);
    rg_use(c.rg_req, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("земля: кеш, запекание", "земля: кеш", record_bake, nullptr);
    rg_use(c.rg_req, RG_INDIRECT);
    rg_use(c.rg_req, RG_CS_READ);
    rg_use(c.rg_table, RG_CS_WRITE);
    rg_use(c.rg_pages, RG_CS_WRITE);
    rg_use(c.rg_texels, RG_CS_WRITE);
    rg_use(c.rg_light, RG_CS_WRITE);
}

static void record_gc_light(VkCommandBuffer cmd, void *) {
    gc_bind(cmd, g_vk.gcache.pipe_light);
    u32 n = g_vk.gcache.pages_n, gx = n < 65535 ? n : 65535;
    vkCmdDispatch(cmd, gx, (n + gx - 1) / gx, 1);
}

void vk_gcache_light_pass() {
    const GcacheGpu &c = g_vk.gcache;
    if (!c.on || !c.lit) return;
    rg_pass("земля: кеш, свет", "земля: свет кеша", record_gc_light, nullptr);
    rg_use(c.rg_pages, RG_CS_WRITE);
    rg_use(c.rg_texels, RG_CS_READ);
    rg_use(c.rg_light, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_sky_reads(RG_CS_READ);
    vk_horizon_reads(RG_CS_READ);
    vk_sdf_reads();
    vk_gi_reads();
    vk_lcache_reads();
}

void vk_gcache_reads() {
    const GcacheGpu &c = g_vk.gcache;
    if (!c.on) return;
    rg_use(c.rg_table, RG_CS_READ);
    rg_use(c.rg_pages, RG_CS_READ);
    rg_use(c.rg_texels, RG_CS_READ);
    rg_use(c.rg_cells, RG_CS_READ);
    if (c.lit) rg_use(c.rg_light, RG_CS_READ);
}
