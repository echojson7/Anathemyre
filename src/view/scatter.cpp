#include "scatter.h"
#include "../core/math3.h"
#include "../sim/ground.h"

static constexpr i32 CELL_M = 2;
static constexpr f32 RADIUS_M = 80.0f;
static constexpr f32 FADE_M = 16.0f;  // у края камни уменьшаются до нуля: без хлопка при движении

static inline u32 sh(u32 v) {
    u32 s = v * 747796405u + 2891336453u;
    u32 w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
static inline f32 u01(u32 h) { return (f32)(h >> 8) * (1.0f / 16777216.0f); }

void scatter_init(ScatterCache &c, Arena &arena) {
    c = ScatterCache{};
    c.items = arena_push<RenderInstance>(arena, SCATTER_MAX);
}

// Кватернион → два u32 по два snorm16 (как в view.cpp)
static u32 scatter_pack_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

void scatter_update(ScatterCache &c, const Terrain &t, const WPos &eye, const u32 meshes[SCATTER_MESHES]) {
    // Камера в метрах карты (от юго-западного угла)
    f32 ex = (f32)((eye.cx - t.chunk0) * 256) + eye.x;
    f32 ez = (f32)((eye.cz - t.chunk0) * 256) + eye.z;
    i32 cx = f_floor_i(ex / (f32)CELL_M), cz = f_floor_i(ez / (f32)CELL_M);
    if (c.valid && cx == c.cell_x && cz == c.cell_z) return;
    c.valid = true;
    c.cell_x = cx;
    c.cell_z = cz;
    c.count = 0;
    if (!meshes[0]) return;
    // Высоко над землёй камней не видно
    if (eye.y - ground_at(t, eye).height > RADIUS_M) return;
    const i32 r = (i32)(RADIUS_M / (f32)CELL_M) + 1;
    const u32 seed = t.detail_seed ^ 0x5CA77E2u;
    // Доля клеток с камнем по виду земли: песок, сухая земля, щебень, камень, трава, вода
    static const f32 DENSITY[GROUND_KINDS] = {0.004f, 0.10f, 0.45f, 0.30f, 0.02f, 0.0f};
    for (i32 iz = cz - r; iz <= cz + r; ++iz)
        for (i32 ix = cx - r; ix <= cx + r && c.count < SCATTER_MAX; ++ix) {
            u32 h = sh((u32)ix ^ sh((u32)iz ^ sh(seed)));
            i32 gx = ix * CELL_M + 1, gz = iz * CELL_M + 1;
            if (gx < 0 || gz < 0 || gx >= t.size_x || gz >= t.size_z) continue;
            u32 kind = ground_kind(terrain_masks(t, gx, gz), terrain_base_slope(t, gx, gz));
            // Камни кучками: плотность гуляет по клеткам 16 м
            u32 hc = sh((u32)(ix >> 3) ^ sh((u32)(iz >> 3) ^ sh(seed + 1)));
            f32 dens = DENSITY[kind] * (0.3f + 1.4f * u01(hc));
            if (u01(h) >= dens) continue;
            f32 x = (f32)(ix * CELL_M) + u01(sh(h + 1)) * (f32)CELL_M;
            f32 z = (f32)(iz * CELL_M) + u01(sh(h + 2)) * (f32)CELL_M;
            f32 dx = x - ex, dz = z - ez;
            f32 d = f_sqrt(dx * dx + dz * dz);
            if (d > RADIUS_M) continue;
            // Размер: большей частью мелочь, редко камень с полметра
            f32 a = u01(sh(h + 3));
            f32 size = 0.08f + 0.5f * a * a * a;
            if (kind == GROUND_ROCK) size *= 1.4f;
            size *= f_clamp((RADIUS_M - d) / FADE_M, 0.0f, 1.0f);
            if (size < 0.03f) continue;
            WPos p;
            p.cx = t.chunk0;
            p.cz = t.chunk0;
            p.x = x;
            p.z = z;
            wpos_rebase(p);
            GroundSample g = ground_at(t, p);
            if (g.kind == GROUND_WATER) continue;
            RenderInstance &it = c.items[c.count];
            it.cx = p.cx;
            it.cz = p.cz;
            it.x = p.x;
            it.z = p.z;
            it.y = g.height - size * 0.22f;  // утоплен в землю
            Quat q = quat_yaw(sh(h + 4));
            it.rot_xy = scatter_pack_snorm2(q.x, q.y);
            it.rot_zw = scatter_pack_snorm2(q.z, q.w);
            it.scale = size;
            it.mesh = meshes[sh(h + 5) % SCATTER_MESHES];
            if (!it.mesh) continue;
            it.material = RENDER_NO_MATERIAL;
            it.seed = h;
            it.lod_min = 0;
            c.count++;
        }
}
