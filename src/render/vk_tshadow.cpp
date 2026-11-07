// Тень рельефа дальнего плана (этап 5в, шаг 5; docs/tshadow.md). Карта — TS_LEVELS уровней по TS_W² текселей
// вокруг камеры, тексель — рампа высоты тени для одного направления светила. Карт три: кадр показывает переход
// между двумя готовыми (a → b за TS_PIECES кадров), третья (c) тем временем собирается по куску на кадр для
// светила и места камеры на начало сборки. Собрана — карты сдвигаются (a ← b, b ← c), и переход идёт дальше.
// Светило не сдвинулось и камера стоит — ничего не считается. Скачок (первый кадр, новая земля, смена солнца на
// луну, часы клавишами, телепорт) — карта собирается целиком в этом кадре, без перехода
#include "vk_graph.h"
#include "../core/log.h"
#include "../core/mathf.h"
#include <math.h>

constexpr u32 TS_PIECES = 16;                          // кадров на сборку карты и на переход
constexpr u32 TS_TOTAL = TS_LEVELS * TS_W * TS_W;      // текселей карты
constexpr f32 TS_SUN_STEP = 0.99999962f;               // cos 0.05°: светило сдвинулось — новая карта
constexpr f32 TS_SUN_JUMP = 0.99939083f;               // cos 2° за кадр: скачок
constexpr f64 TS_CAM_STEP = 4.0, TS_CAM_JUMP = 256.0;  // м: камера ушла от окон карты; телепорт

bool vk_tshadow_init() {
    TshadowGpu &t = g_vk.tshadow;
    if (!vk_create_buffer(t.maps, (VkDeviceSize)TS_MAPS * TS_TOTAL * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true))
        return false;
    vk_buffer_name(t.maps, "тень рельефа: карты");
    t.rg_maps = rg_buffer("тень рельефа: карты");
    t.pipe = vk_make_compute("tshadow.comp");
    t.valid = false;
    return t.pipe != VK_NULL_HANDLE;
}

void vk_tshadow_shutdown() {
    TshadowGpu &t = g_vk.tshadow;
    if (t.pipe) vkDestroyPipeline(g_vk.device, t.pipe, nullptr);
    vk_destroy_buffer(t.maps);
    u32 tg = t.terrain_gen;
    t = TshadowGpu{};
    t.terrain_gen = tg;
}

// Карта m — для светила l, окна уровней вокруг камеры cam (метры карты)
static void place(u32 m, const f32 l[3], const f64 cam[2]) {
    TshadowGpu &t = g_vk.tshadow;
    for (u32 i = 0; i < 3; ++i) t.sun[m][i] = l[i];
    t.cam[m][0] = cam[0];
    t.cam[m][1] = cam[1];
    for (u32 k = 0; k < TS_LEVELS; ++k) {
        f64 s = (f64)TS_CELL0 * (f64)(1u << k);
        for (u32 a = 0; a < 2; ++a) t.org[m][k][a] = (i32)floor(cam[a] / s) - (i32)(TS_W / 2);
    }
}

static f32 dot3(const f32 *a, const f32 *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static f64 cheb(const f64 *a, const f64 *b) { return fmax(fabs(a[0] - b[0]), fabs(a[1] - b[1])); }
static u32 other(u32 a, u32 b) {
    for (u32 m = 0; m < TS_MAPS; ++m)
        if (m != a && m != b) return m;
    return 0;
}

void vk_tshadow_fill(FrameGpu &g, const RenderFrame &f) {
    TshadowGpu &t = g_vk.tshadow;
    t.count = 0;
    g.ts_maps = t.maps.address;
    g.ts_on = 0;
    if (!g_vk.terrain.on || !f.ground) return;
    f32 l[3] = {f.light.sun_dir[0], f.light.sun_dir[1], f.light.sun_dir[2]};
    f32 len = f_sqrt(dot3(l, l));
    if (len > 0) for (f32 &v : l) v /= len;
    const i32 c0 = g_vk.terrain.desc.chunk0;
    f64 cam[2] = {(f64)(f.view.cx - c0) * 256.0 + f.view.x, (f64)(f.view.cz - c0) * 256.0 + f.view.z};
    bool jump = !t.valid || t.terrain_gen != t.seen_terrain || dot3(l, t.last_sun) < TS_SUN_JUMP ||
                cheb(cam, t.last_cam) > TS_CAM_JUMP;
    for (u32 i = 0; i < 3; ++i) t.last_sun[i] = l[i];
    t.last_cam[0] = cam[0];
    t.last_cam[1] = cam[1];
    if (jump) {
        // Целиком в этом кадре, кадр показывает её же
        t.c = other(t.a, t.a);
        place(t.c, l, cam);
        t.first = 0;
        t.count = TS_TOTAL;
        t.a = t.b = t.c;
        t.c = other(t.a, t.b);
        t.done = t.blend = 0;
        t.valid = true;
        t.seen_terrain = t.terrain_gen;
    } else {
        // Переход a → b идёт сам по себе; собирается c, если светило или камера ушли от новейшей карты
        if (t.a != t.b && t.blend < TS_PIECES) t.blend++;
        if (t.done == 0) {
            u32 nb = t.b;
            if (dot3(l, t.sun[nb]) < TS_SUN_STEP || cheb(cam, t.cam[nb]) > TS_CAM_STEP) {
                t.c = other(t.a, t.b);
                place(t.c, l, cam);
                t.first = 0;
                t.count = TS_TOTAL / TS_PIECES;
                t.done = 1;
            }
        } else if (t.done < TS_PIECES) {
            t.first = (u32)((u64)t.done * TS_TOTAL / TS_PIECES);
            t.count = (u32)((u64)(t.done + 1) * TS_TOTAL / TS_PIECES) - t.first;
            t.done++;
        }
    }
    g.ts_on = 1;
    g.ts_a = t.a;
    g.ts_b = t.b;
    g.ts_c = t.c;
    g.ts_mix = t.a != t.b ? (f32)t.blend / (f32)TS_PIECES : 0.0f;
    g.ts_first = t.first;
    g.ts_count = t.count;
    for (u32 i = 0; i < 3; ++i) g.ts_sun[i] = t.sun[t.c][i];
    for (u32 m = 0; m < TS_MAPS; ++m)
        for (u32 k = 0; k < TS_LEVELS; ++k) {
            g.ts_org[m * TS_LEVELS + k][0] = t.org[m][k][0];
            g.ts_org[m * TS_LEVELS + k][1] = t.org[m][k][1];
        }
    // После кадра: переход кончился — a = b; сборка кончилась и переход тоже — карты сдвигаются
    if (t.a != t.b && t.blend >= TS_PIECES) {
        t.a = t.b;
        t.blend = 0;
    }
    if (t.done >= TS_PIECES && t.a == t.b) {
        t.b = t.c;
        t.c = other(t.a, t.b);
        t.done = 0;
        t.blend = 0;
    }
}

static VkDeviceAddress g_ts_frame = 0;

static void record_build(VkCommandBuffer cmd, void *) {
    const TshadowGpu &t = g_vk.tshadow;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, t.pipe);
    PushConsts pc{g_ts_frame, 0, 0};
    vkCmdPushConstants(cmd, g_vk.layout, PUSH_STAGES, 0, sizeof pc, &pc);
    vkCmdDispatch(cmd, (t.count + 63) / 64, 1, 1);
}

void vk_tshadow_passes(VkDeviceAddress frame) {
    const TshadowGpu &t = g_vk.tshadow;
    g_ts_frame = frame;
    if (!t.count) return;
    rg_pass("тень рельефа", "тень рельефа", record_build, nullptr);
    rg_use(t.rg_maps, RG_CS_WRITE);
    rg_use(g_vk.diag.rg_stats, RG_CS_WRITE);
}

void vk_tshadow_reads(RgUse u) { rg_use(g_vk.tshadow.rg_maps, u); }
