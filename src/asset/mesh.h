// Формат .mesh (docs/mesh.md): сетка детали без UV. Куски «тег, версия, длина» (core/ser.h), незнакомые
// пропускаются. Пишет tools/meshgen.py, игра только читает. Кластеры ≤ 124 треугольников — под visibility
// buffer этапа 5 (25 бит кластер + 7 бит треугольник); коллайдер читает шаг мира, грубую форму — свет
#pragma once
#include "../core/mem.h"
#include "../core/text.h"

constexpr u32 MESH_MAX_CLUSTER_TRIS = 124;
constexpr u32 MESH_MAX_MATERIALS = 255;

// Вершина детали, как её читает шейдер по адресу (data/shaders/common.glsl, Vertex), 24 байта
struct MeshVertex {
    f32 px, py, pz;
    u32 normal;  // октаэдр, два snorm16: x — младшие 16 бит
    u32 mats;    // материал 0 | материал 1 << 16 (номера в таблице материалов игры)
    u32 misc;    // доля смеси (u8, 255 — весь материал 1) | запечённое затенение << 8 (u8, 255 — без тени)
};
static_assert(sizeof(MeshVertex) == 24, "MeshVertex — 24 байта, как в шейдере");

struct MeshCluster {
    u32 first_index;   // от начала индексов сетки
    u32 tri_count;     // ≤ MESH_MAX_CLUSTER_TRIS
    f32 cx, cy, cz, radius;  // сфера вокруг кластера
};

struct MeshLod {
    u32 first_cluster, cluster_count;
    u32 first_index, index_count;
    f32 error;         // ошибка упрощения в метрах (LOD 0 — 0)
};

// Коробка коллайдера или грубой формы в осях сетки
struct MeshBox {
    f32 cx, cy, cz;    // центр
    f32 hx, hy, hz;    // половины сторон
    f32 qx, qy, qz, qw;  // поворот
};

struct MeshData {
    f32 min[3], max[3];         // границы в осях сетки
    Span<Str> material_names;   // локальные материалы вершин (номер в вершине → имя)
    Span<MeshVertex> vertices;  // mats — локальные номера, пока не вызван mesh_remap_materials
    Span<u32> indices;
    Span<MeshCluster> clusters;
    Span<MeshLod> lods;
    Span<MeshBox> collider;
    Span<MeshBox> coarse;
};

// Разобрать файл; строки и массивы — в арене (данные файла можно освободить). false — ошибка в *err
bool mesh_parse(const u8 *data, usize size, Arena &arena, MeshData &out, const char **err);
// Локальные номера материалов в вершинах → номера таблицы игры (global[локальный])
void mesh_remap_materials(MeshData &m, const u16 *global);
