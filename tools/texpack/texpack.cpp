// Упаковка текстуры (docs/textures.md): PNG → .tex версии 2 со сжатием BC и мипами до 4 × 4.
//
//     texpack <вид> <вход.png> <выход.tex>
//
//     color   RGB — цвет sRGB, A — затенение (линейно)       → BC7 sRGB; мипы — среднее 2 × 2 в линейном свете
//     linear  RGBA линейно (маски, шероховатость в каналах)   → BC7 UNORM
//     normal  RGB — нормаль OpenGL · 0.5 + 0.5 (зелёный вверх) → BC5 (x, y; z восстанавливает шейдер); мипы —
//             среднее нормалей с нормировкой
//     mask    R — одна маска (кривизна, шероховатость)        → BC4
//
// Офлайн-инструмент (шов 5): кодеры bc7enc и rgbcx (third_party/bc7enc, MIT), PNG читает stb_image (третьи
// стороны, public domain). Байты зависят только от входа и версий кодеров, CI сверяет их с data/textures в git
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb/stb_image.h"
#include "bc7enc/bc7enc.h"
#include "bc7enc/rgbcx.h"

namespace {

// Как TEX_* в src/asset/tex.h
enum Format : uint32_t { TEX_BC7_SRGB = 1, TEX_BC7_UNORM = 2, TEX_BC5_UNORM = 3, TEX_BC4_UNORM = 4 };

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

// sRGB ↔ линейный свет: прямая — таблицей на 256 значений, обратная — ближайшим в той же таблице (без pow на
// каждый тексель: одинаковые байты, пока одинакова таблица)
float g_lin[256];
void srgb_init() {
    for (int i = 0; i < 256; ++i) {
        double c = i / 255.0;
        g_lin[i] = (float)(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
    }
}
uint8_t to_srgb(float l) {
    int lo = 0, hi = 255;
    while (lo < hi) {  // первый с g_lin ≥ l
        int mid = (lo + hi) / 2;
        if (g_lin[mid] < l) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0 && l - g_lin[lo - 1] <= g_lin[lo] - l) --lo;
    return (uint8_t)lo;
}
uint8_t to_u8(float v) {
    float r = v * 255.0f + 0.5f;
    return (uint8_t)(r < 0.0f ? 0.0f : r > 255.0f ? 255.0f : r);
}

// Следующий мип: среднее 2 × 2 (размеры — степени двойки, соседи не нужны)
std::vector<uint8_t> downsample(const std::vector<uint8_t> &src, uint32_t w, uint32_t h, const char *kind) {
    uint32_t nw = w / 2, nh = h / 2;
    std::vector<uint8_t> dst((size_t)nw * nh * 4);
    bool color = !strcmp(kind, "color"), normal = !strcmp(kind, "normal");
    for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x) {
            const uint8_t *q[4] = {&src[((size_t)(2 * y) * w + 2 * x) * 4], &src[((size_t)(2 * y) * w + 2 * x + 1) * 4],
                                   &src[((size_t)(2 * y + 1) * w + 2 * x) * 4], &src[((size_t)(2 * y + 1) * w + 2 * x + 1) * 4]};
            uint8_t *o = &dst[((size_t)y * nw + x) * 4];
            if (normal) {
                float n[3] = {0, 0, 0};
                for (const uint8_t *p : q)
                    for (int c = 0; c < 3; ++c) n[c] += p[c] / 127.5f - 1.0f;
                float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l < 1e-6f) { n[0] = n[1] = 0.0f; n[2] = l = 1.0f; }
                for (int c = 0; c < 3; ++c) o[c] = to_u8((n[c] / l) * 0.5f + 0.5f);
                o[3] = 255;
                continue;
            }
            for (int c = 0; c < 4; ++c) {
                if (color && c < 3) {
                    float s = 0.0f;
                    for (const uint8_t *p : q) s += g_lin[p[c]];
                    o[c] = to_srgb(s * 0.25f);
                } else {
                    uint32_t s = 0;
                    for (const uint8_t *p : q) s += p[c];
                    o[c] = (uint8_t)((s + 2) / 4);
                }
            }
        }
    return dst;
}

void encode(const std::vector<uint8_t> &img, uint32_t w, uint32_t h, Format f, std::vector<uint8_t> &out) {
    bc7enc_compress_block_params bp;
    bc7enc_compress_block_params_init(&bp);
    if (f == TEX_BC7_UNORM) bc7enc_compress_block_params_init_linear_weights(&bp);
    bp.m_uber_level = 2;
    for (uint32_t by = 0; by < h / 4; ++by)
        for (uint32_t bx = 0; bx < w / 4; ++bx) {
            uint8_t px[16 * 4];
            for (uint32_t y = 0; y < 4; ++y) memcpy(&px[y * 16], &img[((size_t)(by * 4 + y) * w + bx * 4) * 4], 16);
            uint8_t blk[16];
            size_t n = 16;
            if (f == TEX_BC7_SRGB || f == TEX_BC7_UNORM) bc7enc_compress_block(blk, px, &bp);
            else if (f == TEX_BC5_UNORM) rgbcx::encode_bc5_hq(blk, px, 0, 1, 4);
            else { rgbcx::encode_bc4_hq(blk, px, 4); n = 8; }
            out.insert(out.end(), blk, blk + n);
        }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "использование: texpack color|linear|normal|mask <вход.png> <выход.tex>\n");
        return 2;
    }
    const char *kind = argv[1];
    Format f;
    if (!strcmp(kind, "color")) f = TEX_BC7_SRGB;
    else if (!strcmp(kind, "linear")) f = TEX_BC7_UNORM;
    else if (!strcmp(kind, "normal")) f = TEX_BC5_UNORM;
    else if (!strcmp(kind, "mask")) f = TEX_BC4_UNORM;
    else return fprintf(stderr, "texpack: неизвестный вид %s\n", kind), 2;
    int w = 0, h = 0, ch = 0;
    uint8_t *px = stbi_load(argv[2], &w, &h, &ch, 4);
    if (!px) return fprintf(stderr, "texpack: %s: не PNG\n", argv[2]), 1;
    if (w < 4 || h < 4 || (w & (w - 1)) || (h & (h - 1)) || w > 8192 || h > 8192) {
        stbi_image_free(px);
        return fprintf(stderr, "texpack: %s: стороны — степени двойки от 4 до 8192\n", argv[2]), 1;
    }
    std::vector<uint8_t> img(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    srgb_init();
    bc7enc_compress_block_init();
    rgbcx::init();
    std::vector<uint8_t> blocks;
    uint32_t mw = (uint32_t)w, mh = (uint32_t)h, mips = 0;
    for (;;) {
        encode(img, mw, mh, f, blocks);
        ++mips;
        if (mw <= 4 || mh <= 4) break;
        img = downsample(img, mw, mh, kind);
        mw /= 2;
        mh /= 2;
    }
    std::vector<uint8_t> out((const uint8_t *)"ANATEX02", (const uint8_t *)"ANATEX02" + 8), b;
    put(b, (uint32_t)f);
    put(b, (uint32_t)w);
    put(b, (uint32_t)h);
    put(b, mips);
    put_chunk(out, "HEAD", b);
    put_chunk(out, "MIPS", blocks);
    FILE *o = fopen(argv[3], "wb");
    if (!o || fwrite(out.data(), 1, out.size(), o) != out.size()) return fprintf(stderr, "texpack: %s не пишется\n", argv[3]), 1;
    fclose(o);
    printf("texpack: %s — %s %d × %d, мипов %u, %.1f КБ\n", argv[3], kind, w, h, mips, out.size() / 1024.0);
    return 0;
}
