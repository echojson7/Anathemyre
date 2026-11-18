// Кадр для шейдеров (FrameData в data/shaders/common.glsl) и номера счётчиков GPU (Stats там же): зеркала
// раскладки шейдеров, вынесены из vk_state.h, чтобы тот читался целиком. Включается только из vk_state.h
#pragma once

// Данные кадра для шейдеров (FrameData в data/shaders/common.glsl), пишутся раз за кадр
struct FrameGpu {
    VkDeviceAddress vertices, materials, lines, points;
    VkDeviceAddress t_height, t_mask;
    VkDeviceAddress t_water, t_mask2, t_dune, t_fine;
    i32 cam_cx, cam_cz;
    f32 cam_x, cam_y, cam_z;
    f32 tan_half_fov;
    f32 right[4], up[4], forward[4];   // .w не используется
    f32 proj_x, proj_y, near_m, pixel_angle;  // pixel_angle — 2·tan(fov/2)/высота: метров на пиксель на 1 м глубины
    f32 cell_min, cell_pixels;
    u32 pixel, scans;  // scans — поверхности из сканов CC0 (docs/scans.md)
    f32 sun_dir[4], sun_color[4];
    f32 viewport[2], ndc_per_pixel[2];
    i32 t_chunk0, t_size_x, t_size_z, t_nx, t_nz, t_nmx, t_nmz;
    u32 t_dune_seed;
    i32 t_fine_off[4], t_fine_n, t_loc_m;  // карты дюн 2 м (RenderTerrain::fine): начало локации, −1 — нет
    i32 t_oct_lp[5], t_oct_amp[5];
    u32 t_oct_seed[5], t_on;
    i32 clip_n, clip_levels, clip_morph, clip_pad;
    i32 clip[12][4];   // начало кольца в полуметрах от угла карты (x, z), дыра кольца (x, z)
    u32 ground_layers[8];  // материалы слоёв земли (RENDER_GROUND_LAYERS), остаток — 0
    i32 t_mip[8];      // начало уровня k сглаженной сетки высот (шаг 8·2^k м) в буфере высот, номера u16
    VkDeviceAddress sky_trans, sky_ms, sky_sun, sky_moon, sky_out;  // таблицы неба (vk_sky.cpp)
    RenderSky sky;
    VkDeviceAddress decals;  // RenderDecal подряд
    u32 decal_count, decal_pad;
    // Кадр мира (этап 5, vk_world.cpp): таблицы сеток и кластеров, экземпляры на GPU, отсечение, индексы
    VkDeviceAddress meshes, clusters, vis_instances, vis, cull, draws, indices, g_indices;
    u32 instance_count, vis_max;
    u32 g_first[12];  // начало индексов кольца земли L в этом кадре
    // Froxels (vk_froxel.cpp): огни кадра и списки огней и декалей по клеткам
    VkDeviceAddress lights, froxels;
    u32 light_count, froxel_x, froxel_y, froxel_z;
    f32 froxel_near, froxel_far;
    u32 froxel_on, froxel_heat;
    VkDeviceAddress g_verts;  // вершины колец земли кадра (ground_bake.comp), 32 байта на вершину
    VkDeviceAddress stats;    // счётчики GPU кадра (STAT_*), vk_diag.cpp
    VkDeviceAddress tiles;    // плитки по видам (Tiles в common.glsl, docs/tiles.md)
    u32 tile_cap, tile_pad;   // мест в списке одного вида
    VkDeviceAddress view_light;  // свет мест плиток прошлого кадра для глаза (vec4 на плитку, common.glsl)
    // Кеш поверхности земли (vk_gcache.cpp, docs/gcache.md)
    VkDeviceAddress gc_table, gc_pages, gc_texels, gc_req, gc_cells, gc_light;
    u32 gc_w, gc_pages_n, gc_req_cap, gc_frame;
    i32 gc_tpc0;
    u32 gc_on, gc_lmode, gc_lperiod;
    u32 gc_lit_min;          // свет страниц, посчитанный раньше этого кадра кеша, устарел (резкая смена света)
    u32 gc_lturn;            // круг пересчёта в этом кадре (≤ gc_lperiod: камера быстро летит — чаще)
    // Карта горизонта земли (vk_horizon.cpp, docs/horizon.md)
    VkDeviceAddress h_map;
    u32 h_on;
    f32 h_max;
    // Скиннинг людей (vk_skin.cpp, docs/people.md): работы кадра и палитры костей
    VkDeviceAddress skin_jobs, bones;
    VkDeviceAddress vertices_w, clusters_w;  // те же общие вершины и кластеры — для записи (skin*.comp)
    u32 skin_count, skin_pad;
    // Свет (docs/light.md): время кадра, сразу ли привыкать глазу, тонмаппинг, качество света
    f32 dt;
    u32 eye_snap, tonemap, light_q;
    f32 local_tone[4];  // местная экспозиция: колено, наклон в свет, наклон в тень, включена
    f32 eye_meter[4];   // замер глаза: доля по кадру, альфа HDR годна, баланс белого, время привыкания к цвету, с
    // Тени солнца страницами (vk_shadow.cpp, docs/shadow.md): окна страниц уровней, пул страниц и их тексели,
    // заявки, вызовы и список отрисовки страниц, сферы сброса кадра
    VkDeviceAddress sm_table, sm_pages, sm_texels, sm_req, sm_draws, sm_list, sm_inval;
    u32 sm_on, sm_frame, sm_pages_n, sm_req_cap;
    u32 sm_draw_cap, sm_inval_n, sm_debug, sm_cur;  // sm_cur — набор нового шага светила
    u32 sm_read, sm_bg, sm_build_cap, sm_pad;       // читаемые наборы (биты); набор в фоне + 1; его страниц за кадр
    f32 sm_mix, sm_disc, sm_far, sm_t0;  // вес нового набора; тангенс радиуса светила; дальность, м; тексель уровня 0, м
    f32 sm_r[2][4], sm_u[2][4], sm_s[2][4];  // оси наборов: r, u — плоскость карты, s — к светилу; u.w — высота
                                             // камеры к светилу от якоря набора
    i32 sm_base[24][2];             // окно места уровня (набор · SM_LEVELS + уровень): первая страница на решётке
    f32 sm_off[24][2];              // камера от начала окна по r, u, м
    // Слой подвижных (docs/shadow.md, «Подвижные заслонки»): копии страниц кадра, вызовы и список их отрисовки,
    // экземпляры-заслонки кадра (люди, машины, оружие)
    VkDeviceAddress sm_dyn, sm_dyn_draws, sm_dyn_list, sm_dyn_inst;
    u32 sm_dyn_n, sm_dyn_cap, sm_dyn_draw_cap, sm_dyn_all;  // sm_dyn_n — для солнца (0 — светило под горизонтом), all — в списке
    // Поле расстояний мира (vk_sdf.cpp, docs/sdf.md): засыпка — кластеры экземпляров на уровнях, кирпичи кадра,
    // их биты по месту в кольце; общий буфер засыпки (минимум расстояния и знак на воксель)
    VkDeviceAddress sdf_jobs, sdf_bricks, sdf_bits, sdf_scratch;
    i32 sdf_anchor[2];           // чанк якоря поля: воксели и экземпляры считаются от него
    u32 sdf_on, sdf_view;        // поле есть и свет его читает; режим F3 (RENDER_SDF_*)
    u32 sdf_jobs_n, sdf_bricks_n, sdf_pad0, sdf_pad1;
    f32 sdf_cam[4];              // камера от якоря, м; w — воксель уровня 0, м
    i32 sdf_lo[8][4], sdf_hi[8][4];  // рамка грязных вокселей уровня (засыпка пишет только в неё)
    // Зонды непрямого света (vk_gi.cpp, docs/gi.md): таблица зондов, работы кадра, лучи работ, место и состояние
    // зонда работы; окна уровней; режим F3, лучей на зонд, доля старого при смешении
    VkDeviceAddress gi_probes, gi_jobs, gi_rays, gi_pos;
    i32 gi_lo[4][4];
    u32 gi_on, gi_view, gi_rays_n, gi_pad0;
    f32 gi_hyst, gi_pad2, glow_gain, gi_pad3;
    // Ячейки света (vk_lcache.cpp, docs/lcache.md): ключи (они же по 4), места, показанный свет, сумма круга,
    // работы кадра; ступень
    VkDeviceAddress lc_keys, lc_keys4, lc_meta, lc_show, lc_filt, lc_acc, lc_jobs;
    u32 lc_on, lc_frame, lc_rays_n, lc_period;
    u32 lc_cap_new, lc_cap_ref, lc_snap, lc_ease;
    f32 lc_angle, lc_far, lc_hyst, lc_pad1;
    // Проёмы земли: x0, z0, x1, z1 от камеры, м (vis_ground.frag в них землю не рисует); t_hole_n — сколько
    f32 t_hole[4][4];
    u32 t_hole_n, t_hole_pad[3];
    // Сглаживание (vk_aa.cpp, docs/aa.md): режим, кадр (фаза дрожания, годность прошлых экземпляров), дрожание,
    // прошлый вид камеры: оси, проекция, сдвиг камеры (сейчас − тогда, м); прошлые вершины людей и экземпляры
    u32 aa_mode, aa_frame, aa_hist, aa_pad;  // aa_hist: 1 — прошлый кадр годен как история
    f32 jitter[2], prev_proj[2];
    f32 prev_right[4], prev_up[4], prev_forward[4];
    f32 prev_delta[4];
    VkDeviceAddress skin_prev, mesh_prev;
    // Облака (vk_clouds.cpp)
    VkDeviceAddress cl_map, cl_noise, cl_shadow, cl_info;
    u32 cl_on, cl_size, cl_slot_a, cl_slot_b;
    f32 cl_mix, cl_mean_t;
    u32 cl_steps, cl_pad;
    f32 cl_off_a[4], cl_off_b[4];
    f32 cl_build[4], cl_build_light[4], cl_cam[4];
    // Блеск (vk_glare.cpp)
    VkDeviceAddress gr_buf;
    u32 gr_on, gr_n, gr_pad0, gr_pad1;
    i32 gr_lv[8][4];
    // Тени огней картами (vk_lamp_shadow.cpp, docs/lamp_shadows.md): грани кадра, их вызовы, счётчики вызовов,
    // список отрисовки (экземпляр, кластер)
    VkDeviceAddress lsm_jobs, lsm_draws, lsm_counts, lsm_list;
    u32 lsm_on, lsm_jobs_n, lsm_pad0, lsm_pad1;
    // Воздух (vk_air.cpp, docs/air.md): коробки пыли залов, расстояния половины кадра, свет солнца в воздухе и его
    // размытие (rgb половинками), дальний край плиток воздуха (биты f32)
    VkDeviceAddress air_boxes, air_depth, air_ray, air_tmp, air_tmax;
    u32 air_on, air_boxes_n, air_steps, air_w2;  // air_w2, air_h2 — размер половины кадра
    u32 air_h2, air_nx, air_ny, air_pad0;        // клеток по x и y (слоёв — AIR_NZ)
    f32 air_far, air_lnk, air_dust, air_dust_h;  // дальность, м; ln(1 + far / AIR_A); пыль у нуля карты, 1/м; высота слоя
    f32 air_tint[3], air_g;                      // доля рассеяния по каналам; лепесток вперёд
    f32 air_iso, air_pad1, air_pad2, air_pad3;   // доля рассеяния во все стороны
    // Объектив (vk_glare.cpp, docs/lens.md): солнце в кадре (видимая доля диска, середина в пикселях, отступ от края);
    // биты LENS_*, номер кадра; блики, аберрация, виньетка, зерно
    VkDeviceAddress lens_sun;
    u32 lens_on, lens_frame, lens_pad0, lens_pad1;
    f32 lens_flare, lens_ca, lens_vignette, lens_grain;
};

// Счётчики GPU (Stats в common.glsl): номера — как STAT_* там
constexpr u32 STAT_VIS_WANT = 0;       // сколько видимых кластеров нашло отсечение (до обрезки по VIS_MAX_CLUSTERS)
constexpr u32 STAT_FROXEL_LIGHTS = 1;  // клеток froxels, где огни не влезли
constexpr u32 STAT_FROXEL_DECALS = 2;  // клеток froxels, где декали не влезли
// Счётчики работы освещения (docs/tiles.md)
constexpr u32 STAT_TILES = 3;          // плитки по видам, TILE_KINDS подряд
constexpr u32 STAT_PX_SKY = 8, STAT_PX_GROUND = 9, STAT_PX_MESH = 10;  // пиксели по видам
constexpr u32 STAT_SURF_GROUND = 11, STAT_SURF_MESH = 12;             // поверхности: surface() и свет раз на ячейку
constexpr u32 STAT_SHADOW_TAPS = 13;  // выборки карты горизонта (тень земли, docs/horizon.md)
// Кеш поверхности земли (docs/gcache.md): страницы, прочитанные кадром; запечённые; не получившие места;
// поверхности земли, посчитанные освещением мимо кеша (крутые склоны, нет места)
constexpr u32 STAT_GC_USED = 14, STAT_GC_BAKED = 15, STAT_GC_OVER = 16, STAT_GC_PX_DIRECT = 17;
constexpr u32 STAT_SKIN_VERTS = 18;  // вершин скинено (skin.comp)
// Тени солнца страницами (docs/shadow.md): страницы, прочитанные кадром; нарисованные в нём; заявки без места;
// выборки без страницы; вызовы кластеров в страницы (до обрезки); выборки текселей карты
constexpr u32 STAT_SM_USED = 19, STAT_SM_NEW = 20, STAT_SM_OVER = 21, STAT_SM_MISS = 22, STAT_SM_DRAWS = 23,
              STAT_SM_TAPS = 24;
constexpr u32 STAT_SM_WAIT = 25;  // страниц нового шага светила, отложенных на следующие кадры (стройка в фоне)
// Поле расстояний мира (docs/sdf.md): выборки поля светом, треугольники засыпки, кирпичи, собранные в кадре
constexpr u32 STAT_SDF_TAPS = 26, STAT_SDF_TRIS = 27, STAT_SDF_BRICKS = 28;
// Зонды непрямого света (docs/gi.md): зондов с лучами, лучей, выборок поля лучами и сдвигом зондов
constexpr u32 STAT_GI_PROBES = 29, STAT_GI_RAYS = 30, STAT_GI_TAPS = 31;
// Ячейки света (docs/lcache.md): живых, заявленных заново, посчитанных, их лучей, заявок без места, выборок поля
// лучами и поиском поверхности, молодых, отложенных до следующего кадра
constexpr u32 STAT_LC_LIVE = 32, STAT_LC_NEW = 33, STAT_LC_JOBS = 34, STAT_LC_RAYS = 35, STAT_LC_OVER = 36,
              STAT_LC_TAPS = 37, STAT_LC_WAIT = 38;
// Кеш земли (docs/gcache.md, «Отражённый свет в кеше»): текселей, чей свет посчитан в кадре
constexpr u32 STAT_GC_LIT = 39;
constexpr u32 GC_LIGHT_ON = 1, GC_LIGHT_ALL = 2;  // gc_lmode (common.glsl): свет в кеше читается; все страницы кадра заново
// Тени огней картами (docs/lamp_shadows.md): граней нарисовано, вызовов кластеров в них (до обрезки), граней, где
// вызовы не влезли, выборок карт светом
constexpr u32 STAT_LSM_FACES = 40, STAT_LSM_DRAWS = 41, STAT_LSM_OVER = 42, STAT_LSM_TAPS = 43;
// Воздух (docs/air.md): клеток с пылью, шагов лучей солнца, выборок страниц теней ими
constexpr u32 STAT_AIR_CELLS = 44, STAT_AIR_STEPS = 45, STAT_AIR_TAPS = 46;
// Слой подвижных (docs/shadow.md, «Подвижные заслонки»): копий страниц, вызовов кластеров в них (до обрезки), копий
// без места
constexpr u32 STAT_SM_DYN = 47, STAT_SM_DYN_DRAWS = 48, STAT_SM_DYN_OVER = 49;
constexpr u32 STAT_LSM_DYN_FACES = 50, STAT_LSM_DYN_DRAWS = 51;
constexpr u32 STAT_COUNT = 52;
// Виды плиток кадра мира (TILE_* в common.glsl): небо, земля из кеша (и небо), сетки (и небо), смесь земли и
// сеток, земля на пиксель (и небо)
constexpr u32 TILE_KINDS = 5;
constexpr u32 TILE_KIND_GROUND = 1, TILE_KIND_MIX = 3, TILE_KIND_GROUND_PX = 4;
constexpr u32 TILE_HEADER = TILE_KINDS * 16;  // косвенные dispatch видов перед списками
// Буфер чтения кадра: число видимых кластеров, с READBACK_STATS — счётчики, с READBACK_EYE — глаз (sky_out [4]…[8])
constexpr VkDeviceSize READBACK_STATS = 16, READBACK_EYE = READBACK_STATS + STAT_COUNT * 4;
constexpr VkDeviceSize READBACK_BYTES = READBACK_EYE + 80;
constexpr u32 SKY_OUT_EYE = 4;  // sky_out: [4], [5] — экспозиция и освещённость (sky_sum.comp, common.glsl)
