// Тени солнца страницами (этап 6б-1, docs/shadow.md). Виртуальная карта теней clipmap'ом вокруг камеры: уровень k —
// решётка текселей cell_min·2^k в плоскости поперёк светила, тексели привязаны к миру. Окно уровня — SM_WIN²
// страниц по SM_PAGE² текселей, запись окна указывает на страницу общего пула. Кадр:
//   сброс страниц под изменившимися экземплярами (shadow_inval.comp) → заявки в разборе плиток (light_tiles.comp)
//   → свободные (shadow_scan.comp) → счёт (shadow_args.comp) → выдача и очистка (shadow_alloc.comp) → отбор
//   заслонок для новых страниц (shadow_cull.comp) → отрисовка в страницы (shadow.vert/.frag) → свет читает.
// Страница рисуется раз и живёт, пока её читают: статичный мир почти бесплатен. Подвижные (люди, машины, оружие) —
// слоем поверх (6б-3, «Подвижные заслонки»): прочитанная кадром страница под ними на кадр подменяется копией
// (shadow_dyn.comp), в копию поверх рисуются подвижные, следующий кадр возвращает месту прежнюю.
// Светило идёт шагами по октаэдру (~0.1–0.2°, 6б-2). Наборов страниц два: на новый шаг строится новый набор —
// в фоне, не больше SM_BUILD_PAGES страниц за кадр, пока показан старый; когда новый готов, свет перетекает со
// старого на новый за SM_BLEND. Края теней не прыгают, тяжёлый кадр перерисовки размазан на несколько
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"

constexpr u32 SM_MIN_PAGES = 1024, SM_MAX_PAGES = 8192;  // пул: 64 МБ … 512 МБ (64 КБ на страницу)
constexpr u32 SM_REQ_HEAD = 16 + 16 + 32 * 4 + 32 * 32 * 4;  // SmReq до заявок (common.glsl)
constexpr f32 SM_FAR = 3000.0f;   // дальше тени сеток нет (карта горизонта остаётся), м
constexpr f32 SM_STEP = 512.0f;   // шаг светила: доля октаэдра (~0.11–0.22°)
constexpr f32 SM_BLEND = 1.0f;    // перетекание со старого шага на новый, с
// Светило быстрее стольких рад/с (0.5°/с; обычные сутки — 0.125°/с) — ускоренное время: шаг сразу, без перетекания
constexpr f32 SM_FAST = 0.0087f;
constexpr f32 SM_FAST_WINDOW = 0.1f;  // окно замера скорости, с
constexpr u32 SM_BUILD_PAGES = 192;  // страниц нового набора за кадр, пока он строится в фоне

bool vk_shadow_init() {
    ShadowGpu &s = g_vk.shadow;
    bool ok = true;
    ok = vk_make_compute(&s.pipe_inval, "shadow_inval.comp") && ok;
    ok = vk_make_compute(&s.pipe_scan, "shadow_scan.comp") && ok;
    ok = vk_make_compute(&s.pipe_args, "shadow_args.comp") && ok;
    ok = vk_make_compute(&s.pipe_alloc, "shadow_alloc.comp") && ok;
    ok = vk_make_compute(&s.pipe_cull, "shadow_cull.comp") && ok;
    ok = vk_make_compute(&s.pipe_dyn, "shadow_dyn.comp") && ok;
    PipeDesc d{"shadow.vert", "shadow.frag"};
    d.no_attach = true;
    d.depth_test = false;
    d.depth_write = false;
    // Обе стороны (у сеток грани снаружи против часовой, как у visibility buffer): грань, повёрнутая от светила, —
    // нижняя грань заслонки; повёрнутая к светилу пишет высоту с отступом (shadow.frag) — толща тела не пропускает
    // свет на стыках, а освещённая грань не затеняет сама себя
    d.cull = VK_CULL_MODE_NONE;
    ok = vk_make_pipeline(&s.pipe_draw, d) && ok;
    s.rg_table = rg_buffer("тени: окна страниц");
    s.rg_pages = rg_buffer("тени: страницы");
    s.rg_texels = rg_buffer("тени: тексели");
    s.rg_req = rg_buffer("тени: заявки");
    s.rg_draws = rg_buffer("тени: вызовы");
    s.rg_list = rg_buffer("тени: список кластеров");
    s.rg_dyn = rg_buffer("тени: копии подвижных");
    s.rg_dyn_draws = rg_buffer("тени: вызовы подвижных");
    s.rg_dyn_list = rg_buffer("тени: список подвижных");
    s.reset = true;
    for (FrameSlot &fs : g_vk.frames) {
        if (!vk_create_buffer(fs.sm_inval, SM_INVAL_MAX * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
        vk_buffer_name(fs.sm_inval, "тени: сферы сброса");
        if (!vk_create_buffer(fs.sm_casters, SM_DYN_CASTERS * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return false;
        vk_buffer_name(fs.sm_casters, "тени: заслонки подвижных");
    }
    return ok;
}

void vk_shadow_shutdown() {
    ShadowGpu &s = g_vk.shadow;
    VkPipeline ps[] = {s.pipe_inval, s.pipe_scan, s.pipe_args, s.pipe_alloc, s.pipe_cull, s.pipe_draw, s.pipe_dyn};
    for (VkPipeline p : ps)
        if (p) vkDestroyPipeline(g_vk.device, p, nullptr);
    GpuBuffer *bs[] = {&s.table, &s.pages, &s.texels, &s.req, &s.draws, &s.list, &s.dyn, &s.dyn_draws, &s.dyn_list};
    for (GpuBuffer *b : bs) vk_destroy_buffer(*b);
    for (FrameSlot &fs : g_vk.frames) {
        vk_destroy_buffer(fs.sm_inval);
        vk_destroy_buffer(fs.sm_casters);
    }
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

// Пул под размер кадра: страниц — пикселей / 1024, не меньше SM_MIN_PAGES; не хватило — вдвое (с нуля). За пулом —
// SM_DYN_PAGES мест копий слоя подвижных (свободные их не видят: shadow_scan.comp перебирает только pages_n)
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
    const VkDeviceSize all = (VkDeviceSize)pages + SM_DYN_PAGES;
    if (!sm_make(s.table, (VkDeviceSize)SM_SLOTS * SM_WIN * SM_WIN * 4, "тени: окна страниц", s.rg_table) ||
        !sm_make(s.pages, all * 20, "тени: страницы", s.rg_pages) ||
        !sm_make(s.texels, all * SM_PAGE * SM_PAGE * 4, "тени: тексели", s.rg_texels) ||
        !sm_make(s.req, SM_REQ_HEAD + (VkDeviceSize)(4 * s.req_cap + 2 * pages) * 4, "тени: заявки", s.rg_req, ind) ||
        !sm_make(s.draws, 16 + (VkDeviceSize)SM_DRAW_CAP * 20, "тени: вызовы", s.rg_draws, ind) ||
        !sm_make(s.list, (VkDeviceSize)SM_DRAW_CAP * 8, "тени: список кластеров", s.rg_list) ||
        !sm_make(s.dyn, SM_DYN_BYTES, "тени: копии подвижных", s.rg_dyn, ind) ||
        !sm_make(s.dyn_draws, 16 + (VkDeviceSize)SM_DYN_DRAW_CAP * 20, "тени: вызовы подвижных", s.rg_dyn_draws, ind) ||
        !sm_make(s.dyn_list, (VkDeviceSize)SM_DYN_DRAW_CAP * 8, "тени: список подвижных", s.rg_dyn_list)) {
        s.pages_n = 0;
        return false;
    }
    log_info("тени: окно %u² страниц по %u² текселей × %u уровней × %u набора, пул %u страниц и %u копий подвижных (%.0f МБ)",
             SM_WIN, SM_PAGE, SM_LEVELS, SM_SETS, pages, SM_DYN_PAGES,
             (f64)(s.texels.size + s.pages.size + s.table.size + s.dyn.size) / (1 << 20));
    s.reset = true;
    return true;
}

// Октаэдр: развёртка направления (|x| + |y| + |z| = 1, нижняя половина отогнута наружу) и обратно
static void sm_oct_fold(f32 &x, f32 &z) {
    f32 ox = (1 - f_abs(z)) * (x >= 0 ? 1.0f : -1.0f), oz = (1 - f_abs(x)) * (z >= 0 ? 1.0f : -1.0f);
    x = ox;
    z = oz;
}
// Шаг светила: направление в октаэдр, клетка 1/SM_STEP — ключ
static void sm_sun_key(const f32 l[3], i32 key[2]) {
    f32 n = f_abs(l[0]) + f_abs(l[1]) + f_abs(l[2]);
    if (n <= 0) n = 1;
    f32 x = l[0] / n, z = l[2] / n;
    if (l[1] < 0) sm_oct_fold(x, z);
    key[0] = f_floor_i(x * SM_STEP + 0.5f);
    key[1] = f_floor_i(z * SM_STEP + 0.5f);
}
// Направление шага — центр клетки ключа
static void sm_key_dir(const i32 key[2], f32 out[3]) {
    f32 x = (f32)key[0] / SM_STEP, z = (f32)key[1] / SM_STEP;
    f32 y = 1 - f_abs(x) - f_abs(z);
    if (y < 0) sm_oct_fold(x, z);
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

// Сфера экземпляра относительно камеры; false — не заслонка (нет сетки; подвижная, если не moving_ok)
static bool sm_inst_sphere(const FrameGpu &g, const RenderInstance &it, f32 out[4], bool moving_ok = false) {
    if (!it.mesh || it.mesh >= RENDER_MAX_MESHES) return false;
    const MeshGpu &m = ((const MeshGpu *)g_vk.mesh_table.mapped)[it.mesh];
    if (!m.lod_count || (!moving_ok && (m.pad & MESH_DYNAMIC))) return false;
    if (m.pad & MESH_ALPHA) return false;  // карточки волос — без тени (нет альфы в проходе тени)
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

// Слой подвижных («Подвижные заслонки», docs/shadow.md): заслонки кадра — скиненные в этом кадре (люди и машины:
// vk_skin_fill берёт и тех, кто за кадром, но чья тень может попасть в кадр) и экземпляры подвижных сеток (оружие),
// чья тень может попасть в кадр, — от светила (sun) или огня (тени огней картами). Их сферы от камеры — в dyn_sph:
// по ним огни берут кубы слоя (vk_lamp_shadow.cpp). Порядок — порядок кадра: от запуска к запуску один
static u32 sm_dyn_casters(const FrameGpu &g, const RenderFrame &f, FrameSlot &fs, bool sun) {
    ShadowGpu &s = g_vk.shadow;
    if (!f.style.shadow_dyn) return 0;
    bool lamps = f.style.lamp_maps && g_vk.lsm.cube.image;
    if (!sun && !lamps) return 0;
    u32 *out = (u32 *)fs.sm_casters.mapped, n = 0;
    u64 want = 0;
    auto add = [&](u32 i, const f32 sp[4]) {
        ++want;
        if (n >= SM_DYN_CASTERS) return;
        memcpy(s.dyn_sph[n], sp, sizeof(f32) * 4);
        out[n++] = i;
    };
    const SkinGpu &k = g_vk.skin;
    for (u32 j = 0; j < k.jobs; ++j) {
        f32 sp[4];
        if (!sm_inst_sphere(g, f.instances.data[k.job_inst[j]], sp, true)) continue;
        sp[3] = sp[3] * 1.05f + 0.1f;  // как у vk_skin_fill: поза шире бинд-позы
        add(k.job_inst[j], sp);
    }
    if (g_vk.meshes_moving) {
        u32 ni = f.instances.count < RENDER_MAX_INSTANCES ? (u32)f.instances.count : RENDER_MAX_INSTANCES;
        for (u32 i = 0; i < ni; ++i) {
            const RenderInstance &it = f.instances.data[i];
            if (!it.mesh || it.mesh >= RENDER_MAX_MESHES || !g_vk.meshes[it.mesh].moving || g_vk.meshes[it.mesh].skin) continue;
            f32 sp[4];
            if (!sm_inst_sphere(g, it, sp, true) || !vk_dyn_caster_seen(g, f, sp, sp[3], sun, lamps)) continue;
            add(i, sp);
        }
    }
    vk_overflow("тени: подвижные заслонки", want, SM_DYN_CASTERS);
    if (n) vmaFlushAllocation(g_vk.vma, fs.sm_casters.alloc, 0, VK_WHOLE_SIZE);
    return n;
}

// Тексель уровня 0 по ступени теней (docs/light.md, «Ступени»): на «Низко» карта вчетверо, на «Средне» вдвое грубее
static f32 sm_texel0(const RenderFrame &f) {
    u32 q = f.style.shadow_quality;
    return f.style.cell_min * (q == RENDER_LIGHT_LOW ? 4.0f : q == RENDER_LIGHT_MEDIUM ? 2.0f : 1.0f);
}

// Оси, высота камеры и окна уровней набора i в данные кадра. sun — настоящее направление светила этого кадра:
// сдвиг выборки к нему (docs/shadow.md, «Тень без ступенек») — в r.w и s.w набора
static void sm_fill_set(FrameGpu &g, u32 i, const ShadowSet &st, const f64 cam[3], f32 t0, const f32 sun[3]) {
    f32 S[3];
    sm_key_dir(st.key, S);
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
    for (u32 c = 0; c < 3; ++c) {
        g.sm_r[i][c] = R[c];
        g.sm_u[i][c] = U[c];
        g.sm_s[i][c] = S[c];
    }
    // Луч к настоящему светилу, поднявшись над точкой на 1 м вдоль s набора, уходит по r и u на столько метров
    f32 ls = sun[0] * S[0] + sun[1] * S[1] + sun[2] * S[2];
    bool near = ls > 0.99f;  // шаг светила — доли градуса; иначе (смена солнца на луну) сдвига нет
    g.sm_r[i][3] = near ? (sun[0] * R[0] + sun[1] * R[1] + sun[2] * R[2]) / ls : 0.0f;
    g.sm_u[i][3] = (f32)(cam[0] * S[0] + cam[1] * S[1] + cam[2] * S[2] - st.anchor);
    g.sm_s[i][3] = near ? (sun[0] * U[0] + sun[1] * U[1] + sun[2] * U[2]) / ls : 0.0f;
    // Окна уровней: камера — в странице SM_WIN/2 окна, начало окна — целая страница мировой решётки уровня
    f64 cr = cam[0] * R[0] + cam[1] * R[1] + cam[2] * R[2];
    f64 cu = cam[0] * U[0] + cam[1] * U[1] + cam[2] * U[2];
    for (u32 k = 0; k < SM_LEVELS; ++k) {
        f64 page = (f64)t0 * (f64)(1u << k) * SM_PAGE;
        f64 br = __builtin_floor(cr / page) - SM_WIN / 2, bu = __builtin_floor(cu / page) - SM_WIN / 2;
        u32 slot = i * SM_LEVELS + k;
        g.sm_base[slot][0] = (i32)br;
        g.sm_base[slot][1] = (i32)bu;
        g.sm_off[slot][0] = (f32)(cr - br * page);
        g.sm_off[slot][1] = (f32)(cu - bu * page);
    }
}

// Якорь высоты к светилу — у камеры: высоты в страницах малые числа, f32 их держит до миллиметров
static f64 sm_anchor(const i32 key[2], const f64 cam[3]) {
    f32 S[3];
    sm_key_dir(key, S);
    return (f64)(i64)(cam[0] * S[0] + cam[1] * S[1] + cam[2] * S[2]);
}

void vk_shadow_fill(FrameGpu &g, const RenderFrame &f, FrameSlot &fs, bool images) {
    ShadowGpu &s = g_vk.shadow;
    s.on = images && f.style.cell_min > 0 && sm_ensure(g_vk.extent.width, g_vk.extent.height);
    s.inval_n = 0;
    s.dyn_n = 0;
    fs.sm_frame = 0;
    // Заслонки слоя подвижных — и без теней солнца: их берут огни
    s.dyn_all = sm_dyn_casters(g, f, fs, s.on);
    g.sm_dyn_inst = fs.sm_casters.address;
    g.sm_dyn_all = s.dyn_all;
    // Скорость светила: угол за окно SM_FAST_WINDOW (за один кадр не меряем: кадр бывает короче шага часов).
    // Нужна и без теней — зондам (vk_gi_fill идёт следом)
    const f32 *sl = f.light.sun_dir;
    if (s.sun_t < 0) {
        for (u32 a = 0; a < 3; ++a) s.sun_ref[a] = sl[a];
        s.sun_t = 0;
    } else {
        s.sun_t += f.style.dt > 0 ? f.style.dt : 0;
        if (s.sun_t >= SM_FAST_WINDOW) {
            f32 c = sl[0] * s.sun_ref[0] + sl[1] * s.sun_ref[1] + sl[2] * s.sun_ref[2];
            s.sun_fast = __builtin_acosf(c < -1 ? -1 : (c > 1 ? 1 : c)) > SM_FAST * s.sun_t;
            for (u32 a = 0; a < 3; ++a) s.sun_ref[a] = sl[a];
            s.sun_t = 0;
        }
    }
    if (!s.on) return;
    f64 cam[3] = {(f64)f.view.cx * 256.0 + f.view.x, f.view.y, (f64)f.view.cz * 256.0 + f.view.z};
    // Размер текселя, сетки: сменились — оба набора с нуля
    f32 t0 = sm_texel0(f);
    if (f.style.cell_min != s.cell_min || t0 != s.t0 || s.mesh_gen != s.seen_mesh) {
        s.cell_min = f.style.cell_min;
        s.t0 = t0;
        s.seen_mesh = s.mesh_gen;
        s.reset = true;
    }
    if (!sm_invalidations(g, f, fs)) {
        log_info("тени: изменилось больше %u заслонок — все страницы с нуля", SM_INVAL_MAX);
        s.reset = true;
    }
    if (++s.frame == 0) s.frame = 1;
    // Шаг светила. Сброс, «сразу» (первые кадры, снимок, --timelapse) или быстрое светило (ускоренное время) —
    // один набор, без перетекания: при быстром светиле шаг с ожиданием стройки и перетекания отставал на
    // градусы, и пятно солнца шло скачками раз в секунду. Иначе новый шаг строит новый набор, пока старый
    // показан; шаг, пришедший во время стройки или перетекания, ждёт их конца (тень отстаёт от светила не
    // больше чем на SM_BLEND и несколько кадров)
    i32 key[2];
    sm_sun_key(f.light.sun_dir, key);
    ShadowSet &c = s.set[s.cur];
    bool step = key[0] != c.key[0] || key[1] != c.key[1];
    if (s.reset || (step && (f.style.eye_snap || s.sun_fast))) {
        s.reset = true;
        s.old_on = s.building = s.clear_cur = false;
        s.mix = 1;
        c.key[0] = key[0];
        c.key[1] = key[1];
        c.anchor = sm_anchor(key, cam);
    } else if (step && !s.old_on) {
        s.cur ^= 1;
        ShadowSet &n = s.set[s.cur];
        n.key[0] = key[0];
        n.key[1] = key[1];
        n.anchor = sm_anchor(key, cam);
        s.old_on = s.building = s.clear_cur = true;
        s.mix = 0;
        s.build_frame = s.frame;
    }
    if (s.old_on && !s.building) {
        s.mix += f.style.dt / SM_BLEND;
        if (s.mix >= 1) {
            s.mix = 1;
            s.old_on = false;
        }
    }
    fs.sm_frame = s.frame;
    // Старый набор, когда он не нужен, повторяет новый: шейдер его не читает и не заявляет
    for (u32 i = 0; i < SM_SETS; ++i)
        sm_fill_set(g, i, s.old_on || i == s.cur ? s.set[i] : s.set[s.cur], cam, t0, f.light.sun_dir);
    g.sm_cur = s.cur;
    g.sm_read = (s.building ? 0u : 1u << s.cur) | (s.old_on && s.mix < 1 ? 1u << (s.cur ^ 1) : 0u);
    g.sm_bg = s.building ? s.cur + 1 : 0;
    g.sm_mix = s.mix;
    g.sm_build_cap = SM_BUILD_PAGES;
    // Полутень — по диску того светила, что сейчас главное (солнце или луна, sky.cpp)
    const f32 *ld = f.light.sun_dir, *sd = f.sky.sun_dir;
    bool sun = ld[0] * sd[0] + ld[1] * sd[1] + ld[2] * sd[2] > 0.9999f;
    g.sm_disc = __builtin_tanf(sun ? f.sky.sun_radius : f.sky.moon_radius);
    g.sm_far = SM_FAR;
    g.sm_t0 = t0;
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
    // Светило под горизонтом — тени от него нет, и слоя подвижных у солнца тоже
    s.dyn_n = f.light.sun_dir[1] > 0.0f ? s.dyn_all : 0;
    g.sm_dyn = s.dyn.address;
    g.sm_dyn_draws = s.dyn_draws.address;
    g.sm_dyn_list = s.dyn_list.address;
    g.sm_dyn_n = s.dyn_n;
    g.sm_dyn_cap = SM_DYN_PAGES;
    g.sm_dyn_draw_cap = SM_DYN_DRAW_CAP;
}

// Перетекание шага светила, % нового набора (F3); 100 — один набор
u32 vk_shadow_blend() {
    const ShadowGpu &s = g_vk.shadow;
    return s.building ? 0 : s.old_on ? (u32)(s.mix * 100) : 100;
}

// Счётчики кадра frame (0 — кадр без теней): новый набор готов, когда кадр стройки не отложил ни одной страницы
void vk_shadow_read(const u32 *st, u32 frame) {
    ShadowGpu &s = g_vk.shadow;
    if (s.building && frame != 0 && frame - s.build_frame < 0x80000000u && st[STAT_SM_WAIT] == 0) s.building = false;
    if (st[STAT_SM_OVER]) {
        s.grow = st[STAT_SM_OVER];
        vk_overflow("тени: страницы без места", (u64)st[STAT_SM_NEW] + st[STAT_SM_OVER], s.pages_n);
    }
    vk_overflow("тени: вызовы кластеров", st[STAT_SM_DRAWS], SM_DRAW_CAP);
    if (st[STAT_SM_DYN_OVER]) vk_overflow("тени: копии подвижных", (u64)st[STAT_SM_DYN] + st[STAT_SM_DYN_OVER], SM_DYN_PAGES);
    vk_overflow("тени: вызовы подвижных", st[STAT_SM_DYN_DRAWS], SM_DYN_DRAW_CAP);
}

static VkDeviceAddress g_sm_frame = 0;

static void sm_bind(VkCommandBuffer cmd, VkPipeline p, u32 a = 0) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p);
    PushConsts pc{g_sm_frame, a, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
}

static void sm_record_reset(VkCommandBuffer cmd, void *) {
    const ShadowGpu &s = g_vk.shadow;
    vkCmdFillBuffer(cmd, s.table.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, s.pages.buffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(cmd, s.dyn.buffer, 0, VK_WHOLE_SIZE, 0);  // копий нет, места окон не взяты
}
// Копии прошлого кадра (слой подвижных): местам окон — прежние страницы. До сброса набора и под изменившимся: те
// пишут в окна
static void sm_record_dyn_restore(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_dyn, 2);
    vkCmdDispatch(cmd, SM_DYN_PAGES / 64, 1, 1);
}
// Окна нового набора с нуля: прежние страницы набора (шаг назад) больше не находятся и уходят в свободные
static void sm_record_set_reset(VkCommandBuffer cmd, void *) {
    const ShadowGpu &s = g_vk.shadow;
    const VkDeviceSize half = (VkDeviceSize)SM_LEVELS * SM_WIN * SM_WIN * 4;
    vkCmdFillBuffer(cmd, s.table.buffer, s.cur * half, half, 0);
}
static void sm_record_clear(VkCommandBuffer cmd, void *) {
    const ShadowGpu &s = g_vk.shadow;
    vkCmdFillBuffer(cmd, s.req.buffer, 0, SM_REQ_HEAD, 0);
    vkCmdFillBuffer(cmd, s.draws.buffer, 0, 16, 0);
    // Голова копий слоя подвижных: dispatch копирования (0, 1, 1), места уровней без копий; куски не пересекаются —
    // без барьера между ними
    const u32 one[2] = {1, 1};
    vkCmdFillBuffer(cmd, s.dyn.buffer, 0, 4, 0);
    vkCmdUpdateBuffer(cmd, s.dyn.buffer, 4, sizeof one, one);
    vkCmdFillBuffer(cmd, s.dyn.buffer, 12, SM_DYN_HEAD - 12, 0);
    vkCmdFillBuffer(cmd, s.dyn_draws.buffer, 0, 16, 0);
}
static void sm_record_inval(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_inval);
    vkCmdDispatch(cmd, (g_vk.shadow.inval_n + 63) / 64, SM_SLOTS, 1);
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
// Слой подвижных: места (группа — заслонка × место уровня), копии текселей (по их числу), отбор кластеров заслонок
static void sm_record_dyn_place(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_dyn, 0);
    vkCmdDispatch(cmd, g_vk.shadow.dyn_n, SM_SLOTS, 1);
}
static void sm_record_dyn_copy(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_dyn, 1);
    vkCmdDispatchIndirect(cmd, g_vk.shadow.dyn.buffer, 0);
}
// Отбор слоя: группа на заслонку × место уровня, поток на кластер (shadow_cull.comp)
static void sm_record_dyn_cull(VkCommandBuffer cmd, void *) {
    sm_bind(cmd, g_vk.shadow.pipe_cull, 1);
    vkCmdDispatch(cmd, g_vk.shadow.dyn_n, SM_SLOTS, 1);
}
// Отрисовка новых страниц (dyn — копий слоя подвижных): без картинок, область вывода — окно уровня (SM_WIN·SM_PAGE
// текселей)
static void sm_record_pages(VkCommandBuffer cmd, bool dyn) {
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
    PushConsts pc{g_sm_frame, dyn ? 1u : 0u, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdBindIndexBuffer(cmd, g_vk.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    const GpuBuffer &d = dyn ? s.dyn_draws : s.draws;
    vkCmdDrawIndexedIndirectCount(cmd, d.buffer, 16, d.buffer, 0, dyn ? SM_DYN_DRAW_CAP : SM_DRAW_CAP, 20);
    vkCmdEndRendering(cmd);
}
static void sm_record_draw(VkCommandBuffer cmd, void *) { sm_record_pages(cmd, false); }
static void sm_record_dyn_draw(VkCommandBuffer cmd, void *) { sm_record_pages(cmd, true); }

void vk_shadow_begin(VkDeviceAddress frame) {
    ShadowGpu &s = g_vk.shadow;
    g_sm_frame = frame;
    if (!s.on) return;
    if (s.reset) {
        rg_pass("тени: сброс", "тени солнца", sm_record_reset, nullptr);
        rg_use(s.rg_table, RG_COPY_WRITE);
        rg_use(s.rg_pages, RG_COPY_WRITE);
        rg_use(s.rg_dyn, RG_COPY_WRITE);
        s.reset = false;
        s.clear_cur = false;
    } else {
        rg_pass("тени: подвижные — назад", "тени солнца", sm_record_dyn_restore, nullptr);
        rg_use(s.rg_dyn, RG_CS_READ);
        rg_use(s.rg_table, RG_CS_WRITE);
    }
    if (s.clear_cur) {
        rg_pass("тени: новый набор", "тени солнца", sm_record_set_reset, nullptr);
        rg_use(s.rg_table, RG_COPY_WRITE);
        s.clear_cur = false;
    }
    rg_pass("тени: заявки с нуля", "тени солнца", sm_record_clear, nullptr);
    rg_use(s.rg_req, RG_COPY_WRITE);
    rg_use(s.rg_draws, RG_COPY_WRITE);
    rg_use(s.rg_dyn, RG_COPY_WRITE);
    rg_use(s.rg_dyn_draws, RG_COPY_WRITE);
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
    if (!s.dyn_n) return;
    // Слой подвижных: свои строки времени в F3 (копии, отбор, отрисовка). Копии прочитанных страниц под заслонками, отбор кластеров заслонок, отрисовка в копии
    rg_pass("тени: подвижные — места", "тени людей: копии", sm_record_dyn_place, nullptr);
    rg_use(w.rg_inst, RG_CS_READ);
    rg_use(s.rg_table, RG_CS_WRITE);
    rg_use(s.rg_pages, RG_CS_WRITE);
    rg_use(s.rg_dyn, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    rg_pass("тени: подвижные — копия", "тени людей: копии", sm_record_dyn_copy, nullptr);
    rg_use(s.rg_dyn, RG_INDIRECT);
    rg_use(s.rg_dyn, RG_CS_READ);
    rg_use(s.rg_texels, RG_CS_WRITE);
    rg_pass("тени: подвижные — отбор", "тени людей: отбор", sm_record_dyn_cull, nullptr);
    rg_use(w.rg_inst, RG_CS_READ);
    rg_use(s.rg_dyn, RG_CS_READ);
    rg_use(s.rg_dyn_draws, RG_CS_WRITE);
    rg_use(s.rg_dyn_list, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
    vk_skin_clusters_read();
    rg_pass("тени: подвижные — отрисовка", "тени людей: отрисовка", sm_record_dyn_draw, nullptr);
    rg_use(s.rg_dyn_draws, RG_INDIRECT);
    rg_use(s.rg_dyn_list, RG_VS_READ);
    rg_use(w.rg_inst, RG_VS_READ);
    rg_use(s.rg_table, RG_FS_READ);
    rg_use(s.rg_pages, RG_FS_WRITE);
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
