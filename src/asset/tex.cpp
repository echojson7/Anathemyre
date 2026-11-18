#include "tex.h"
#include "../core/ser.h"

static const u8 TEX_MAGIC[8] = {'A', 'N', 'A', 'T', 'E', 'X', '0', '2'};

bool tex_parse(const u8 *data, usize size, TexData &out, const char **err) {
    out = TexData{};
    if (size < sizeof TEX_MAGIC || memcmp(data, TEX_MAGIC, sizeof TEX_MAGIC) != 0) { *err = "не файл .tex версии 2"; return false; }
    Reader r{data + sizeof TEX_MAGIC, size - sizeof TEX_MAGIC, 0, false};
    bool head = false;
    Chunk c;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (chunk_is(c, "HEAD") && c.version == 1) {
            u32 f = get_u32(b);
            out.width = get_u32(b);
            out.height = get_u32(b);
            out.mips = get_u32(b);
            if (b.bad || f < TEX_BC7_SRGB || f > TEX_BC4_UNORM) { *err = "кусок HEAD испорчен"; return false; }
            out.format = (TexFormat)f;
            head = true;
        } else if (chunk_is(c, "MIPS") && c.version == 1) {
            if (!head) { *err = "MIPS раньше HEAD"; return false; }
            out.blocks = b.data;
            out.bytes = b.size;
        }
        // незнакомый кусок пропускается
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!head || !out.blocks) { *err = "нет HEAD или MIPS"; return false; }
    u32 w = out.width, h = out.height;
    if (w < 4 || h < 4 || w > 8192 || h > 8192 || (w & (w - 1)) || (h & (h - 1)) || out.mips == 0 || out.mips > TEX_MAX_MIPS) {
        *err = "размер или число мипов не те";
        return false;
    }
    usize off = 0;
    for (u32 m = 0; m < out.mips; ++m) {
        if (w < 4 || h < 4) { *err = "мип меньше 4 × 4"; return false; }
        out.mip_offset[m] = off;
        off += (usize)(w / 4) * (h / 4) * tex_block_bytes(out.format);
        w /= 2;
        h /= 2;
    }
    if (off != out.bytes) { *err = "размер MIPS не сходится с HEAD"; return false; }
    return true;
}
