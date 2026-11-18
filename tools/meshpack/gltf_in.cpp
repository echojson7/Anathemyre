// Вход glTF 2.0 для meshpack (шов 5, docs/invariants.md): узлы с сетками в мировых осях файла (метры, y вверх —
// как у .mesh), треугольники, нормали, TEXCOORD_0 и TANGENT. Материал примитива — имя материала glTF, в игре оно
// ищется в data/materials.txt. Касательной нет — считается по развёртке (Ленгьел, сумма по треугольникам).
// Соглашение карт нормалей — как у glTF и запекания Blender: v растёт вниз по картинке, зелёный — к верху картинки,
// битангенс B = cross(N, T) · w смотрит против dP/dv (docs/mesh.md, VUV0)
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#include "meshpack.h"

namespace {

struct Corner {
    float p[3], n[3], uv[2], t[4];
    uint8_t mat;
};

void xform_point(const float m[16], const float in[3], float out[3]) {
    for (int r = 0; r < 3; ++r) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
}
void xform_dir(const float m[16], const float in[3], float out[3]) {
    for (int r = 0; r < 3; ++r) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2];
    float l = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (l > 0.0f)
        for (int r = 0; r < 3; ++r) out[r] /= l;
}

// Касательная по развёртке: T = dP/du, B = −dP/dv (v вниз), сумма по треугольникам вершины, ортогонально к N
void tangents_from_uv(std::vector<Corner> &v, const std::vector<uint32_t> &idx, size_t first_vertex, size_t first_index) {
    size_t nv = v.size() - first_vertex;
    std::vector<float> tu(nv * 3, 0.0f), tv(nv * 3, 0.0f);
    for (size_t i = first_index; i + 2 < idx.size(); i += 3) {
        const Corner &a = v[idx[i]], &b = v[idx[i + 1]], &c = v[idx[i + 2]];
        float e1[3], e2[3];
        for (int k = 0; k < 3; ++k) {
            e1[k] = b.p[k] - a.p[k];
            e2[k] = c.p[k] - a.p[k];
        }
        float du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1], du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
        float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        float r = 1.0f / det;
        for (uint32_t j = 0; j < 3; ++j) {
            size_t q = (idx[i + j] - first_vertex) * 3;
            for (int k = 0; k < 3; ++k) {
                tu[q + k] += (e1[k] * dv2 - e2[k] * dv1) * r;
                tv[q + k] += (e2[k] * du1 - e1[k] * du2) * r;
            }
        }
    }
    for (size_t i = 0; i < nv; ++i) {
        Corner &c = v[first_vertex + i];
        const float *t = &tu[i * 3], *bv = &tv[i * 3];
        float d = t[0] * c.n[0] + t[1] * c.n[1] + t[2] * c.n[2];
        float o[3] = {t[0] - c.n[0] * d, t[1] - c.n[1] * d, t[2] - c.n[2] * d};
        float l = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        if (l < 1e-8f) {
            // Развёртки нет или вырождена: любая касательная поперёк нормали
            float ax[3] = {std::fabs(c.n[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(c.n[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
            float dd = ax[0] * c.n[0] + ax[1] * c.n[1];
            for (int k = 0; k < 3; ++k) o[k] = ax[k] - c.n[k] * dd;
            l = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
        }
        for (int k = 0; k < 3; ++k) c.t[k] = o[k] / l;
        // w: cross(N, T) · w должен смотреть против dP/dv
        float cr[3] = {c.n[1] * c.t[2] - c.n[2] * c.t[1], c.n[2] * c.t[0] - c.n[0] * c.t[2], c.n[0] * c.t[1] - c.n[1] * c.t[0]};
        c.t[3] = cr[0] * bv[0] + cr[1] * bv[1] + cr[2] * bv[2] > 0.0f ? -1.0f : 1.0f;
    }
}

}  // namespace

bool gltf_read(const char *path, std::vector<Chunk> &chunks, std::string &why) {
    cgltf_options opt{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&opt, path, &data) != cgltf_result_success) return why = "не glTF", false;
    struct Free {
        cgltf_data *d;
        ~Free() { cgltf_free(d); }
    } guard{data};
    if (cgltf_load_buffers(&opt, data, path) != cgltf_result_success) return why = "буферы glTF не читаются", false;
    if (cgltf_validate(data) != cgltf_result_success) return why = "glTF не прошёл проверку", false;

    std::vector<std::string> mats;
    std::vector<Corner> v;
    std::vector<uint32_t> idx;
    bool any_uv = false;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node &node = data->nodes[ni];
        if (!node.mesh) continue;
        float m[16];
        cgltf_node_transform_world(&node, m);
        float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive &pr = node.mesh->primitives[pi];
            if (pr.type != cgltf_primitive_type_triangles) return why = "примитив не из треугольников", false;
            const cgltf_accessor *ap = nullptr, *an = nullptr, *auv = nullptr, *at = nullptr;
            for (cgltf_size k = 0; k < pr.attributes_count; ++k) {
                const cgltf_attribute &a = pr.attributes[k];
                if (a.type == cgltf_attribute_type_position) ap = a.data;
                else if (a.type == cgltf_attribute_type_normal) an = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) auv = a.data;
                else if (a.type == cgltf_attribute_type_tangent) at = a.data;
            }
            if (!ap || !an) return why = "у примитива нет POSITION или NORMAL", false;
            std::string mname = pr.material && pr.material->name ? pr.material->name : "missing";
            size_t mi = 0;
            while (mi < mats.size() && mats[mi] != mname) ++mi;
            if (mi == mats.size()) mats.push_back(mname);
            if (mats.size() > 255) return why = "материалов больше 255", false;
            any_uv |= auv != nullptr;
            size_t base = v.size(), ibase = idx.size();
            for (cgltf_size i = 0; i < ap->count; ++i) {
                Corner c{};
                float p[3], n[3], t[4] = {1, 0, 0, 1};
                cgltf_accessor_read_float(ap, i, p, 3);
                cgltf_accessor_read_float(an, i, n, 3);
                if (auv) cgltf_accessor_read_float(auv, i, c.uv, 2);
                if (at) cgltf_accessor_read_float(at, i, t, 4);
                xform_point(m, p, c.p);
                xform_dir(m, n, c.n);  // поворот и равномерный масштаб: обратная транспонированная не нужна
                xform_dir(m, t, c.t);
                c.t[3] = det < 0.0f ? -t[3] : t[3];
                c.mat = (uint8_t)mi;
                v.push_back(c);
            }
            cgltf_size ic = pr.indices ? pr.indices->count : ap->count;
            if (ic % 3) return why = "индексов не кратно 3", false;
            for (cgltf_size i = 0; i < ic; i += 3) {
                uint32_t t3[3];
                for (int k = 0; k < 3; ++k) t3[k] = (uint32_t)(pr.indices ? cgltf_accessor_read_index(pr.indices, i + k) : i + k);
                if (det < 0.0f) std::swap(t3[1], t3[2]);  // зеркальный узел: обход наоборот
                for (int k = 0; k < 3; ++k) {
                    if (t3[k] >= ap->count) return why = "индекс за вершинами", false;
                    idx.push_back((uint32_t)base + t3[k]);
                }
            }
            if (auv && !at) tangents_from_uv(v, idx, base, ibase);
        }
    }
    if (v.empty() || idx.empty()) return why = "в glTF нет сеток", false;

    auto add = [&](const char *tag, std::vector<uint8_t> &&body) {
        Chunk c;
        memcpy(c.tag, tag, 4);
        c.version = 1;
        c.body = std::move(body);
        chunks.push_back(std::move(c));
    };
    std::vector<uint8_t> b;
    put(b, (uint32_t)mats.size());
    for (const std::string &s : mats) {
        if (s.size() > 255) return why = "имя материала длиннее 255", false;
        put(b, (uint8_t)s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
    add("MATS", std::move(b));
    b.clear();
    for (const Corner &c : v) {
        Vert q{};
        memcpy(q.p, c.p, sizeof q.p);
        oct_encode_y(c.n, q.n);
        q.m0 = q.m1 = c.mat;
        q.blend = 0;
        q.ao = 255;
        b.insert(b.end(), (const uint8_t *)&q, (const uint8_t *)&q + sizeof q);
    }
    add("VERT", std::move(b));
    b.assign((const uint8_t *)idx.data(), (const uint8_t *)idx.data() + idx.size() * 4);
    add("INDX", std::move(b));
    if (any_uv) {
        b.clear();
        for (const Corner &c : v) {
            put(b, float_to_half(c.uv[0]));
            put(b, float_to_half(c.uv[1]));
            put(b, tangent_pack(c.t, c.t[3]));
        }
        add("VUV0", std::move(b));
    }
    return true;
}
