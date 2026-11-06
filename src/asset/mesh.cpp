#include "mesh.h"
#include "../core/ser.h"

static const u8 MESH_MAGIC[8] = {'A', 'N', 'A', 'M', 'E', 'S', 'H', '1'};

static bool read_boxes(Reader &b, Arena &arena, Span<MeshBox> &out) {
    u32 n = get_u32(b);
    if ((u64)n * 40 > b.size - b.pos) return false;
    out = {arena_push<MeshBox>(arena, n), n};
    for (u32 i = 0; i < n; ++i) {
        MeshBox &x = out.data[i];
        x.cx = get_f32(b); x.cy = get_f32(b); x.cz = get_f32(b);
        x.hx = get_f32(b); x.hy = get_f32(b); x.hz = get_f32(b);
        x.qx = get_f32(b); x.qy = get_f32(b); x.qz = get_f32(b); x.qw = get_f32(b);
    }
    return !b.bad;
}

bool mesh_parse(const u8 *data, usize size, Arena &arena, MeshData &out, const char **err) {
    out = MeshData{};
    if (size < sizeof MESH_MAGIC || memcmp(data, MESH_MAGIC, sizeof MESH_MAGIC) != 0) { *err = "не файл .mesh"; return false; }
    Reader r{data + sizeof MESH_MAGIC, size - sizeof MESH_MAGIC, 0, false};
    u32 nverts = 0, nindices = 0;
    bool head = false, verts = false, inds = false;
    Chunk c;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (chunk_is(c, "HEAD") && c.version == 1) {
            nverts = get_u32(b);
            nindices = get_u32(b);
            for (f32 &v : out.min) v = get_f32(b);
            for (f32 &v : out.max) v = get_f32(b);
            head = !b.bad && nindices % 3 == 0;
        } else if (chunk_is(c, "MATS") && c.version == 1) {
            u32 n = get_u32(b);
            if (n > MESH_MAX_MATERIALS) { *err = "слишком много материалов"; return false; }
            out.material_names = {arena_push<Str>(arena, n), n};
            for (u32 i = 0; i < n; ++i) {
                u8 len = get_u8(b);
                char *s = arena_push<char>(arena, (usize)len + 1);
                get_bytes(b, s, len);
                out.material_names.data[i] = {s, len};
            }
            if (b.bad) { *err = "кусок MATS испорчен"; return false; }
        } else if (chunk_is(c, "VERT") && c.version == 1) {
            if (!head) { *err = "VERT раньше HEAD"; return false; }
            // На вершину: 3 f32 положение, 2 i16 нормаль (октаэдр), 4 u8: материал 0, 1, доля смеси, затенение
            if ((u64)nverts * 20 != b.size) { *err = "размер VERT не сходится с HEAD"; return false; }
            out.vertices = {arena_push<MeshVertex>(arena, nverts), nverts};
            for (u32 i = 0; i < nverts; ++i) {
                MeshVertex &v = out.vertices.data[i];
                v.px = get_f32(b); v.py = get_f32(b); v.pz = get_f32(b);
                u32 nx = get_u16(b), ny = get_u16(b);
                v.normal = nx | (ny << 16);
                u32 m0 = get_u8(b), m1 = get_u8(b), blend = get_u8(b), ao = get_u8(b);
                v.mats = m0 | (m1 << 16);
                v.misc = blend | (ao << 8);
            }
            verts = !b.bad;
        } else if (chunk_is(c, "INDX") && c.version == 1) {
            if (!head) { *err = "INDX раньше HEAD"; return false; }
            if ((u64)nindices * 4 != b.size) { *err = "размер INDX не сходится с HEAD"; return false; }
            out.indices = {arena_push<u32>(arena, nindices), nindices};
            for (u32 i = 0; i < nindices; ++i) out.indices.data[i] = get_u32(b);
            inds = !b.bad;
        } else if (chunk_is(c, "CLUS") && c.version == 1) {
            u32 n = get_u32(b);
            if ((u64)n * 24 > b.size - b.pos) { *err = "кусок CLUS испорчен"; return false; }
            out.clusters = {arena_push<MeshCluster>(arena, n), n};
            for (u32 i = 0; i < n; ++i) {
                MeshCluster &k = out.clusters.data[i];
                k.first_index = get_u32(b);
                k.tri_count = get_u32(b);
                k.cx = get_f32(b); k.cy = get_f32(b); k.cz = get_f32(b); k.radius = get_f32(b);
            }
            if (b.bad) { *err = "кусок CLUS испорчен"; return false; }
        } else if (chunk_is(c, "LODS") && c.version == 1) {
            u32 n = get_u32(b);
            if ((u64)n * 20 > b.size - b.pos) { *err = "кусок LODS испорчен"; return false; }
            out.lods = {arena_push<MeshLod>(arena, n), n};
            for (u32 i = 0; i < n; ++i) {
                MeshLod &l = out.lods.data[i];
                l.first_cluster = get_u32(b);
                l.cluster_count = get_u32(b);
                l.first_index = get_u32(b);
                l.index_count = get_u32(b);
                l.error = get_f32(b);
            }
            if (b.bad) { *err = "кусок LODS испорчен"; return false; }
        } else if (chunk_is(c, "COLL") && c.version == 1) {
            if (!read_boxes(b, arena, out.collider)) { *err = "кусок COLL испорчен"; return false; }
        } else if (chunk_is(c, "COAR") && c.version == 1) {
            if (!read_boxes(b, arena, out.coarse)) { *err = "кусок COAR испорчен"; return false; }
        }
        // незнакомый кусок пропускается
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!head || !verts || !inds) { *err = "нет HEAD, VERT или INDX"; return false; }
    if (out.material_names.count == 0) { *err = "нет материалов (MATS)"; return false; }
    for (u32 i : out.indices)
        if (i >= nverts) { *err = "индекс за пределами вершин"; return false; }
    u32 nm = (u32)out.material_names.count;
    for (const MeshVertex &v : out.vertices)
        if ((v.mats & 0xFFFF) >= nm || (v.mats >> 16) >= nm) { *err = "номер материала вершины вне MATS"; return false; }
    for (const MeshCluster &k : out.clusters)
        if (k.tri_count > MESH_MAX_CLUSTER_TRIS || (u64)k.first_index + k.tri_count * 3 > nindices) {
            *err = "кластер больше 124 треугольников или вне индексов";
            return false;
        }
    for (const MeshLod &l : out.lods)
        if ((u64)l.first_cluster + l.cluster_count > out.clusters.count || (u64)l.first_index + l.index_count > nindices) {
            *err = "LOD вне кластеров или индексов";
            return false;
        }
    if (out.lods.count == 0) { *err = "нет LOD (LODS)"; return false; }
    return true;
}

void mesh_remap_materials(MeshData &m, const u16 *global) {
    for (MeshVertex &v : m.vertices) v.mats = (u32)global[v.mats & 0xFFFF] | ((u32)global[v.mats >> 16] << 16);
}
