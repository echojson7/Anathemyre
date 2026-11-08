// Упаковка детали (docs/mesh.md): читает .mesh из tools/meshgen.py (вся сетка одним LOD, кластеры подряд) и пишет
// тот же .mesh с LOD и кластерами от meshoptimizer: склейка одинаковых вершин, порядок под кеш вершин, цепочка
// LOD упрощением с закреплённой границей (стыки соседних деталей не расходятся), кластеры по соседству
// ≤ 124 треугольников со сферой. Коллайдер и грубая форма переписываются как есть.
//
//     meshpack [--lods n1,n2,…] <вход.mesh> <выход.mesh>
//
// Сетка с весами костей (куски SKIN, MRPH — люди, tools/humanconv): потоки вершин SKIN и MRPH идут вместе с VERT
// (склейка по всем, тот же порядок), граница не закрепляется (это не стык деталей), --lods задаёт треугольники
// LOD 1… (LOD 0 — вся сетка), а вершины упорядочены по LOD: вершины LOD k — префикс, длины пишутся в LODV.
// Скиннинг тогда считает только префикс выбранного LOD (docs/mesh.md)
//
// Офлайн-инструмент: в игру не входит, правила шага мира на него не распространяются. Байты результата зависят
// только от входа и версии meshoptimizer (third_party/meshoptimizer), CI сверяет их с data/meshes в git.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "meshoptimizer/meshoptimizer.h"

namespace {

constexpr unsigned MAX_TRIS = 124;      // кластер visibility buffer: 7 бит треугольника
constexpr unsigned MAX_VERTS = 128;     // вершин на кластер (форма .mesh их не ограничивает)
constexpr unsigned MAX_LODS = 6;
constexpr float LOD_STEP = 0.5f;        // каждый LOD — вдвое меньше треугольников
constexpr float LOD_MAX_ERROR = 0.25f;  // дальше 25 см ошибки LOD не нужен: деталь уже в несколько пикселей
constexpr float LOD_MIN_GAIN = 0.85f;   // упростилось меньше чем на 15% — цепочка кончилась

// Вершина файла, 20 байт (docs/mesh.md, кусок VERT)
struct Vert {
    float p[3];
    uint16_t n[2];
    uint8_t m0, m1, blend, ao;
};
static_assert(sizeof(Vert) == 20, "вершина .mesh — 20 байт");

struct Chunk {
    char tag[4];
    uint32_t version;
    std::vector<uint8_t> body;
};

bool die(const char *what, const char *path) {
    fprintf(stderr, "meshpack: %s: %s\n", path, what);
    return false;
}

bool read_file(const char *path, std::vector<uint8_t> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return true;
}

template <class T> void put(std::vector<uint8_t> &b, const T &v) {
    const uint8_t *p = (const uint8_t *)&v;
    b.insert(b.end(), p, p + sizeof v);
}

void put_chunk(std::vector<uint8_t> &out, const char *tag, const std::vector<uint8_t> &body) {
    out.insert(out.end(), tag, tag + 4);
    put(out, (uint32_t)1);
    put(out, (uint64_t)body.size());
    out.insert(out.end(), body.begin(), body.end());
}

struct Lod {
    uint32_t first_cluster, cluster_count, first_index, index_count;
    float error;
};
struct Cluster {
    uint32_t first_index, tri_count;
    float c[4];
};


// Поток вершин сверх VERT (SKIN, MRPH): шапка куска как есть, дальше записи по stride байт на вершину
struct Stream {
    const Chunk *chunk;
    size_t head, stride;
    std::vector<uint8_t> data;  // записи вершин
};

// Длина строки «u8 + байты» в куске с места pos; 0 — за концом
size_t skip_name(const std::vector<uint8_t> &b, size_t pos) { return pos < b.size() && pos + 1 + b[pos] <= b.size() ? 1 + b[pos] : 0; }

// Шапка SKIN: имя скелета, число костей u32, имена костей; дальше 8 байт на вершину (4 кости u8, 4 веса u8)
bool skin_stream(const Chunk &c, size_t nv, Stream &s) {
    size_t pos = skip_name(c.body, 0);
    if (!pos || pos + 4 > c.body.size()) return false;
    uint32_t nb;
    memcpy(&nb, &c.body[pos], 4);
    pos += 4;
    for (uint32_t i = 0; i < nb; ++i) {
        size_t k = skip_name(c.body, pos);
        if (!k) return false;
        pos += k;
    }
    s = Stream{&c, pos, 8, {}};
    return c.body.size() - pos == nv * 8;
}

// Шапка MRPH: число морфов u32, на морф — имя и шаг f32; дальше на вершину на морф 9 байт (i16×3, i8×3)
bool morph_stream(const Chunk &c, size_t nv, Stream &s) {
    if (c.body.size() < 4) return false;
    uint32_t n;
    memcpy(&n, c.body.data(), 4);
    size_t pos = 4;
    for (uint32_t i = 0; i < n; ++i) {
        size_t k = skip_name(c.body, pos);
        if (!k || pos + k + 4 > c.body.size()) return false;
        pos += k + 4;
    }
    s = Stream{&c, pos, 9ull * n, {}};
    return s.stride && c.body.size() - pos == nv * s.stride;
}

}  // namespace

int main(int argc, char **argv) {
    std::vector<size_t> lod_tris;  // --lods: треугольников LOD 1…
    int arg = 1;
    if (argc == 5 && !strcmp(argv[1], "--lods")) {
        for (const char *p = argv[2]; *p;) {
            char *end;
            unsigned long n = strtoul(p, &end, 10);
            if (end == p || n < 12) return fprintf(stderr, "meshpack: --lods — числа через запятую, не меньше 12\n"), 2;
            lod_tris.push_back(n);
            p = *end == ',' ? end + 1 : end;
            if (*end && *end != ',') return fprintf(stderr, "meshpack: --lods — числа через запятую\n"), 2;
        }
        arg = 3;
    } else if (argc != 3) {
        fprintf(stderr, "использование: meshpack [--lods n1,n2,…] <вход.mesh> <выход.mesh>\n");
        return 2;
    }
    const char *in_path = argv[arg], *out_path = argv[arg + 1];
    std::vector<uint8_t> data;
    if (!read_file(in_path, data)) return die("не читается", in_path), 1;
    if (data.size() < 8 || memcmp(data.data(), "ANAMESH1", 8) != 0) return die("не .mesh", in_path), 1;

    std::vector<Chunk> chunks;
    for (size_t pos = 8; pos < data.size();) {
        if (data.size() - pos < 16) return die("обрезан кусок", in_path), 1;
        Chunk c;
        memcpy(c.tag, &data[pos], 4);
        memcpy(&c.version, &data[pos + 4], 4);
        uint64_t len;
        memcpy(&len, &data[pos + 8], 8);
        pos += 16;
        if (len > data.size() - pos) return die("длина куска за концом файла", in_path), 1;
        c.body.assign(data.begin() + (long)pos, data.begin() + (long)(pos + len));
        pos += len;
        chunks.push_back(std::move(c));
    }
    auto find = [&](const char *tag) -> const Chunk * {
        for (const Chunk &c : chunks)
            if (!memcmp(c.tag, tag, 4)) return &c;
        return nullptr;
    };
    const Chunk *cv = find("VERT"), *ci = find("INDX"), *cm = find("MATS");
    if (!cv || !ci || !cm) return die("нет VERT, INDX или MATS", in_path), 1;
    if (cv->body.size() % sizeof(Vert) || ci->body.size() % 12) return die("размер VERT или INDX", in_path), 1;

    std::vector<Vert> verts(cv->body.size() / sizeof(Vert));
    memcpy(verts.data(), cv->body.data(), cv->body.size());
    std::vector<uint32_t> idx(ci->body.size() / 4);
    memcpy(idx.data(), ci->body.data(), ci->body.size());
    for (uint32_t i : idx)
        if (i >= verts.size()) return die("индекс за вершинами", in_path), 1;

    // Потоки вершин людей: веса костей (по куску на скелет) и сдвиги морфов
    std::vector<Stream> streams;
    for (const Chunk &c : chunks) {
        Stream s;
        if (!memcmp(c.tag, "SKIN", 4)) {
            if (!skin_stream(c, verts.size(), s)) return die("кусок SKIN не сходится с VERT", in_path), 1;
        } else if (!memcmp(c.tag, "MRPH", 4)) {
            if (!morph_stream(c, verts.size(), s)) return die("кусок MRPH не сходится с VERT", in_path), 1;
        } else {
            continue;
        }
        s.data.assign(c.body.begin() + (long)s.head, c.body.end());
        streams.push_back(std::move(s));
    }
    const bool skinned = !streams.empty();
    if (skinned != !lod_tris.empty()) return die("--lods — только для сетки с весами костей (SKIN) и для неё обязателен", in_path), 1;

    // Склейка одинаковых вершин (все 20 байт и все потоки), порядок треугольников под кеш, вершины — по первому
    // обращению (у сетки с весами — потом по LOD)
    std::vector<uint32_t> remap(verts.size());
    std::vector<meshopt_Stream> ms{{verts.data(), sizeof(Vert), sizeof(Vert)}};
    for (const Stream &s : streams) ms.push_back({s.data.data(), s.stride, s.stride});
    size_t nv = meshopt_generateVertexRemapMulti(remap.data(), idx.data(), idx.size(), verts.size(), ms.data(), ms.size());
    std::vector<Vert> v(nv);
    meshopt_remapVertexBuffer(v.data(), verts.data(), verts.size(), sizeof(Vert), remap.data());
    for (Stream &s : streams) {
        std::vector<uint8_t> d(nv * s.stride);
        meshopt_remapVertexBuffer(d.data(), s.data.data(), verts.size(), s.stride, remap.data());
        s.data.swap(d);
    }
    meshopt_remapIndexBuffer(idx.data(), idx.data(), idx.size(), remap.data());
    meshopt_optimizeVertexCache(idx.data(), idx.data(), idx.size(), nv);
    {
        std::vector<uint32_t> r(nv);
        nv = meshopt_optimizeVertexFetchRemap(r.data(), idx.data(), idx.size(), nv);
        std::vector<Vert> f(nv);
        meshopt_remapVertexBuffer(f.data(), v.data(), v.size(), sizeof(Vert), r.data());
        v.swap(f);
        for (Stream &s : streams) {
            std::vector<uint8_t> d(nv * s.stride);
            meshopt_remapVertexBuffer(d.data(), s.data.data(), s.data.size() / s.stride, s.stride, r.data());
            s.data.swap(d);
        }
        meshopt_remapIndexBuffer(idx.data(), idx.data(), idx.size(), r.data());
    }

    // Цепочка LOD: каждый следующий из предыдущего. У деталей граница закреплена (стыки соседних деталей не
    // расходятся), LOD — вдвое меньше; у сетки с весами граница свободна, число треугольников — из --lods
    const float scale = meshopt_simplifyScale(&v[0].p[0], nv, sizeof(Vert));
    std::vector<std::vector<uint32_t>> lods{idx};
    std::vector<float> lod_err{0.0f};
    while (skinned ? lods.size() <= lod_tris.size() : lods.size() < MAX_LODS) {
        const std::vector<uint32_t> &src = lods.back();
        size_t target = skinned ? lod_tris[lods.size() - 1] * 3 : (size_t)((float)src.size() * LOD_STEP) / 3 * 3;
        if (target < 36) break;
        std::vector<uint32_t> dst(src.size());
        float err = 0.0f;
        size_t n = meshopt_simplify(dst.data(), src.data(), src.size(), &v[0].p[0], nv, sizeof(Vert), target,
                                    LOD_MAX_ERROR / scale, skinned ? 0u : (unsigned)meshopt_SimplifyLockBorder, &err);
        if (n == 0 || (float)n > (float)src.size() * LOD_MIN_GAIN) {
            if (skinned) return die("LOD из --lods не вышел: упрощение остановилось", in_path), 1;
            break;
        }
        dst.resize(n);
        meshopt_optimizeVertexCache(dst.data(), dst.data(), n, nv);
        float e = lod_err.back() + err * scale;  // ошибка каждого шага — от предыдущего LOD, копится
        lods.push_back(std::move(dst));
        lod_err.push_back(e);
    }

    // Сетка с весами: вершины по LOD — сначала вершины самого грубого, потом добавленные следующим и т.д. (в каждом
    // — по первому обращению). Вершины LOD k+1 — часть вершин LOD k (упрощение их не создаёт), поэтому вершины
    // LOD k — префикс [0, lod_verts[k]), и скиннинг считает только его
    std::vector<uint32_t> lod_verts;
    if (skinned) {
        std::vector<uint32_t> r(nv, ~0u);
        uint32_t next = 0;
        lod_verts.assign(lods.size(), 0);
        for (size_t l = lods.size(); l-- > 0;) {
            for (uint32_t i : lods[l])
                if (r[i] == ~0u) r[i] = next++;
            lod_verts[l] = next;
        }
        if (next != nv) return die("вершина вне треугольников", in_path), 1;
        std::vector<Vert> f(nv);
        meshopt_remapVertexBuffer(f.data(), v.data(), nv, sizeof(Vert), r.data());
        v.swap(f);
        for (Stream &s : streams) {
            std::vector<uint8_t> d(nv * s.stride);
            meshopt_remapVertexBuffer(d.data(), s.data.data(), nv, s.stride, r.data());
            s.data.swap(d);
        }
        for (std::vector<uint32_t> &l : lods) meshopt_remapIndexBuffer(l.data(), l.data(), l.size(), r.data());
    }

    // Кластеры по соседству на каждый LOD; индексы кластера идут подряд (кусок CLUS: первый индекс + число)
    std::vector<uint32_t> out_idx;
    std::vector<Cluster> clusters;
    std::vector<Lod> out_lods;
    for (size_t l = 0; l < lods.size(); ++l) {
        const std::vector<uint32_t> &li = lods[l];
        size_t maxm = meshopt_buildMeshletsBound(li.size(), MAX_VERTS, MAX_TRIS);
        std::vector<meshopt_Meshlet> ml(maxm);
        std::vector<uint32_t> mv(maxm * MAX_VERTS);
        std::vector<uint8_t> mt(maxm * MAX_TRIS * 3);
        size_t nm = meshopt_buildMeshlets(ml.data(), mv.data(), mt.data(), li.data(), li.size(), &v[0].p[0], nv,
                                          sizeof(Vert), MAX_VERTS, MAX_TRIS, 0.0f);
        Lod lod{(uint32_t)clusters.size(), (uint32_t)nm, (uint32_t)out_idx.size(), 0, lod_err[l]};
        for (size_t k = 0; k < nm; ++k) {
            const meshopt_Meshlet &m = ml[k];
            meshopt_Bounds b = meshopt_computeMeshletBounds(&mv[m.vertex_offset], &mt[m.triangle_offset], m.triangle_count,
                                                            &v[0].p[0], nv, sizeof(Vert));
            Cluster c{(uint32_t)out_idx.size(), m.triangle_count, {b.center[0], b.center[1], b.center[2], b.radius}};
            for (unsigned t = 0; t < m.triangle_count * 3; ++t) out_idx.push_back(mv[m.vertex_offset + mt[m.triangle_offset + t]]);
            clusters.push_back(c);
        }
        lod.index_count = (uint32_t)out_idx.size() - lod.first_index;
        out_lods.push_back(lod);
    }

    // Запись: HEAD, MATS, VERT, INDX, CLUS, LODS, затем остальные куски входа как есть (COLL, COAR, …)
    float mn[3] = {v[0].p[0], v[0].p[1], v[0].p[2]}, mx[3] = {mn[0], mn[1], mn[2]};
    for (const Vert &q : v)
        for (int a = 0; a < 3; ++a) {
            if (q.p[a] < mn[a]) mn[a] = q.p[a];
            if (q.p[a] > mx[a]) mx[a] = q.p[a];
        }
    std::vector<uint8_t> out(data.begin(), data.begin() + 8), b;
    put(b, (uint32_t)nv);
    put(b, (uint32_t)out_idx.size());
    for (float f : mn) put(b, f);
    for (float f : mx) put(b, f);
    put_chunk(out, "HEAD", b);
    put_chunk(out, "MATS", cm->body);
    b.assign((const uint8_t *)v.data(), (const uint8_t *)v.data() + v.size() * sizeof(Vert));
    put_chunk(out, "VERT", b);
    b.assign((const uint8_t *)out_idx.data(), (const uint8_t *)out_idx.data() + out_idx.size() * 4);
    put_chunk(out, "INDX", b);
    b.clear();
    put(b, (uint32_t)clusters.size());
    for (const Cluster &c : clusters) {
        put(b, c.first_index);
        put(b, c.tri_count);
        for (float f : c.c) put(b, f);
    }
    put_chunk(out, "CLUS", b);
    b.clear();
    put(b, (uint32_t)out_lods.size());
    for (const Lod &l : out_lods) {
        put(b, l.first_cluster);
        put(b, l.cluster_count);
        put(b, l.first_index);
        put(b, l.index_count);
        put(b, l.error);
    }
    put_chunk(out, "LODS", b);
    // Потоки вершин людей — в новом порядке вершин, со своими шапками; длины префиксов LOD — в LODV
    for (const Stream &s : streams) {
        b.assign(s.chunk->body.begin(), s.chunk->body.begin() + (long)s.head);
        b.insert(b.end(), s.data.begin(), s.data.end());
        out.insert(out.end(), s.chunk->tag, s.chunk->tag + 4);
        put(out, s.chunk->version);
        put(out, (uint64_t)b.size());
        out.insert(out.end(), b.begin(), b.end());
    }
    if (skinned) {
        b.clear();
        put(b, (uint32_t)lod_verts.size());
        for (uint32_t n : lod_verts) put(b, n);
        put_chunk(out, "LODV", b);
    }
    static const char *const known[] = {"HEAD", "MATS", "VERT", "INDX", "CLUS", "LODS", "SKIN", "MRPH", "LODV"};
    for (const Chunk &c : chunks) {
        bool skip = false;
        for (const char *k : known) skip |= !memcmp(c.tag, k, 4);
        if (skip) continue;
        out.insert(out.end(), c.tag, c.tag + 4);
        put(out, c.version);
        put(out, (uint64_t)c.body.size());
        out.insert(out.end(), c.body.begin(), c.body.end());
    }

    FILE *f = fopen(out_path, "wb");
    if (!f || fwrite(out.data(), 1, out.size(), f) != out.size()) return die("не пишется", out_path), 1;
    fclose(f);
    printf("meshpack: %s — вершин %zu, треугольников %zu, LOD %zu (", out_path, nv, idx.size() / 3, lods.size());
    for (size_t l = 0; l < lods.size(); ++l) printf("%s%zu/%.3f м", l ? ", " : "", lods[l].size() / 3, (double)lod_err[l]);
    printf("), кластеров %zu\n", clusters.size());
    return 0;
}
