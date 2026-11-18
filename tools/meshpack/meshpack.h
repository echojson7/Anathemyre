// Общее для meshpack.cpp и gltf_in.cpp: вершина и кусок .mesh (docs/mesh.md), упаковка развёртки (VUV0)
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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

template <class T> inline void put(std::vector<uint8_t> &b, const T &v) {
    const uint8_t *p = (const uint8_t *)&v;
    b.insert(b.end(), p, p + sizeof v);
}

// f32 → f16 с округлением к ближайшему чётному, без FPU-флагов и библиотек: байты одни на любой машине
inline uint16_t float_to_half(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u, e = (x >> 23) & 0xFFu, m = x & 0x7FFFFFu;
    if (e == 0xFF) return (uint16_t)(sign | 0x7C00u | (m ? 0x200u : 0u));
    int32_t he = (int32_t)e - 127 + 15;
    if (he >= 31) return (uint16_t)(sign | 0x7C00u);
    if (he <= 0) {
        if (he < -10) return (uint16_t)sign;
        m |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - he);
        uint32_t h = m >> shift, rem = m & ((1u << shift) - 1u), half = 1u << (shift - 1u);
        if (rem > half || (rem == half && (h & 1u))) ++h;
        return (uint16_t)(sign | h);
    }
    uint32_t h = ((uint32_t)he << 10) | (m >> 13), rem = m & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) ++h;  // перенос в порядок — верное округление
    return (uint16_t)(sign | h);
}

inline float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu, x;
    if (e == 0) {
        if (!m) {
            x = sign;
        } else {
            e = 113;
            while (!(m & 0x400u)) { m <<= 1; --e; }
            x = sign | (e << 23) | ((m & 0x3FFu) << 13);
        }
    } else if (e == 31) {
        x = sign | 0x7F800000u | (m << 13);
    } else {
        x = sign | ((e + 112) << 23) | (m << 13);
    }
    float f;
    memcpy(&f, &x, 4);
    return f;
}

// Нормаль VERT: октаэдр с осью y, два snorm16 (как oct_decode в data/shaders/common.glsl)
inline void oct_decode_y(const uint16_t n[2], float out[3]) {
    float ex = (float)(int16_t)n[0] / 32767.0f, ez = (float)(int16_t)n[1] / 32767.0f;
    ex = ex < -1.0f ? -1.0f : ex;
    ez = ez < -1.0f ? -1.0f : ez;
    float x = ex, y = 1.0f - std::fabs(ex) - std::fabs(ez), z = ez;
    if (y < 0.0f) {
        float ox = x;
        x = (1.0f - std::fabs(z)) * (ox >= 0.0f ? 1.0f : -1.0f);
        z = (1.0f - std::fabs(ox)) * (z >= 0.0f ? 1.0f : -1.0f);
    }
    float l = std::sqrt(x * x + y * y + z * z);
    out[0] = x / l;
    out[1] = y / l;
    out[2] = z / l;
}

inline int16_t snorm16(float v) {
    float r = v * 32767.0f;
    r = r > 32767.0f ? 32767.0f : r < -32767.0f ? -32767.0f : r;
    return (int16_t)(r >= 0.0f ? (int32_t)(r + 0.5f) : -(int32_t)(-r + 0.5f));
}

inline void oct_encode_y(const float n[3], uint16_t out[2]) {
    float s = std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]);
    float px = n[0] / s, pz = n[2] / s;
    if (n[1] < 0.0f) {
        float ox = px;
        px = (1.0f - std::fabs(pz)) * (ox >= 0.0f ? 1.0f : -1.0f);
        pz = (1.0f - std::fabs(ox)) * (pz >= 0.0f ? 1.0f : -1.0f);
    }
    out[0] = (uint16_t)snorm16(px);
    out[1] = (uint16_t)snorm16(pz);
}

// Касательная VUV0 (docs/mesh.md): обычный октаэдр с осью z, по 15 бит unorm (x — биты 0–14, y — 15–29), бит 31 —
// знак битангенса (1 — w = −1). Шейдер — tan_decode в common.glsl
inline uint32_t tangent_pack(const float t[3], float w) {
    float s = std::fabs(t[0]) + std::fabs(t[1]) + std::fabs(t[2]);
    float x = t[0] / s, y = t[1] / s;
    if (t[2] < 0.0f) {
        float ox = x;
        x = (1.0f - std::fabs(y)) * (ox >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - std::fabs(ox)) * (y >= 0.0f ? 1.0f : -1.0f);
    }
    auto q = [](float v) {
        float r = (v * 0.5f + 0.5f) * 32767.0f;
        r = r < 0.0f ? 0.0f : r > 32767.0f ? 32767.0f : r;
        return (uint32_t)(r + 0.5f);
    };
    return q(x) | (q(y) << 15) | (w < 0.0f ? 0x80000000u : 0u);
}

// glTF 2.0 (.gltf с .bin или .glb) → куски MATS, VERT, INDX, VUV0, как их пишет tools/meshgen.py. false — причина в why
bool gltf_read(const char *path, std::vector<Chunk> &chunks, std::string &why);
