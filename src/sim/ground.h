// Земля для шага мира — только через ground_at() (docs/invariants.md, docs/terrain.md).
// Высота — билинейная смесь четырёх узлов решётки 1 м (terrain_node) в float без FMA, нормаль — её наклон.
// Вызывающие (размещение объектов, точки, потом ноги, путь и бой) сетку напрямую не читают
#pragma once
#include "../core/math3.h"
#include "../core/wpos.h"
#include "terrain.h"

// Вид земли в точке — главный слой. Номер = место материала слоя у кадра (песок, сухая земля, щебень, камень)
enum GroundKind : u32 {
    GROUND_SAND = 0,
    GROUND_DIRT = 1,
    GROUND_GRAVEL = 2,
    GROUND_ROCK = 3,
    GROUND_KINDS = 4,
};

struct GroundSample {
    f32 height = 0;          // метры от нуля карты
    Vec3 normal{0, 1, 0};
    u32 kind = GROUND_DIRT;
};

// Вид слоя по маскам и наклону сетки 8 м (единицы 1/1280 м на метр). Те же пороги — у шейдера (ground.frag)
inline u32 ground_kind(u32 rough, u32 sand, i32 slope_u) {
    if (sand > 110 && slope_u < 900) return GROUND_SAND;  // дюна песчаная и на крутом склоне
    if (slope_u > 830) return GROUND_ROCK;                // круче ~33°
    if (rough > 165) return GROUND_GRAVEL;
    return GROUND_DIRT;
}

inline GroundSample ground_at(const Terrain &t, i32 cx, i32 cz, f32 x, f32 z) {
    i32 ix = f_floor_i(x), iz = f_floor_i(z);
    f32 fx = x - (f32)ix, fz = z - (f32)iz;
    i32 gx = (cx - t.chunk0) * (i32)CHUNK_M + ix;
    i32 gz = (cz - t.chunk0) * (i32)CHUNK_M + iz;
    if (gx < 0) { gx = 0; fx = 0.0f; } else if (gx >= t.size_m) { gx = t.size_m - 1; fx = 1.0f; }
    if (gz < 0) { gz = 0; fz = 0.0f; } else if (gz >= t.size_m) { gz = t.size_m - 1; fz = 1.0f; }
    i32 h00 = terrain_node(t, gx, gz), h10 = terrain_node(t, gx + 1, gz);
    i32 h01 = terrain_node(t, gx, gz + 1), h11 = terrain_node(t, gx + 1, gz + 1);
    i32 dx0 = h10 - h00, dx1 = h11 - h01, dz0 = h01 - h00, dz1 = h11 - h10;
    f32 a = (f32)h00 + (f32)dx0 * fx;
    f32 b = (f32)h01 + (f32)dx1 * fx;
    GroundSample s;
    s.height = (a + (b - a) * fz) * TERRAIN_M_PER_U;
    f32 gxu = (f32)dx0 + (f32)(dx1 - dx0) * fz;  // наклон, единицы на метр
    f32 gzu = (f32)dz0 + (f32)(dz1 - dz0) * fx;
    s.normal = normalize(Vec3{-gxu * TERRAIN_M_PER_U, 1.0f, -gzu * TERRAIN_M_PER_U});
    u32 rough = 0, sand = 0;
    terrain_masks(t, gx, gz, &rough, &sand);
    s.kind = ground_kind(rough, sand, terrain_base_slope(t, gx, gz));
    return s;
}
inline GroundSample ground_at(const Terrain &t, const WPos &p) { return ground_at(t, p.cx, p.cz, p.x, p.z); }
