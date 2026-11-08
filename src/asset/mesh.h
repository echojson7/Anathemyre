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

// Веса костей вершин для одного скелета (кусок SKIN, люди): кости по имени, как в .skel того же имени
constexpr u32 MESH_MAX_SKINS = 4;
constexpr u32 MESH_MAX_MORPHS = 8;
struct MeshSkin {
    Str skeleton;           // имя скелета (game_engine, mixamo): файл data/people/<сетка>.<скелет>.skel
    Span<Str> bones;        // имена костей; номер кости в вершине — место здесь
    Span<u32> bones4;       // на вершину: 4 номера костей u8 (младший байт — первая)
    Span<u32> weights4;     // на вершину: 4 веса unorm8, сумма 255
};
// Морфы (кусок MRPH): на вершину на морф 3 слова — dx | dy << 16, dz | nx << 16 | ny << 24, nz (i16 × шаг,
// нормаль i8 / 127). Слова вершины подряд по морфам: [вершина][морф][3]
struct MeshMorphs {
    u32 count = 0;
    Str names[MESH_MAX_MORPHS];
    f32 step[MESH_MAX_MORPHS];  // метров на единицу сдвига
    Span<u32> data;
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
    // Люди (tools/humanconv): веса по скелетам, морфы, вершин LOD k — префикс [0, lod_vertices[k]) (кусок LODV)
    MeshSkin skins[MESH_MAX_SKINS];
    u32 skin_count = 0;
    MeshMorphs morphs;
    Span<u32> lod_vertices;
};

// Разобрать файл; строки и массивы — в арене (данные файла можно освободить). false — ошибка в *err
bool mesh_parse(const u8 *data, usize size, Arena &arena, MeshData &out, const char **err);
// Локальные номера материалов в вершинах → номера таблицы игры (global[локальный])
void mesh_remap_materials(MeshData &m, const u16 *global);
