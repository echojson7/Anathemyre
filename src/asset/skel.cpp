#include "skel.h"
#include "../core/ser.h"

static const u8 SKEL_MAGIC[8] = {'A', 'N', 'A', 'S', 'K', 'E', 'L', '1'};

bool skel_parse(const u8 *data, usize size, Arena &arena, Skel &out, const char **err) {
    out = Skel{};
    if (size < sizeof SKEL_MAGIC || memcmp(data, SKEL_MAGIC, sizeof SKEL_MAGIC) != 0) { *err = "не файл .skel"; return false; }
    Reader r{data + sizeof SKEL_MAGIC, size - sizeof SKEL_MAGIC, 0, false};
    Chunk c;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (!chunk_is(c, "BONE") || c.version != 1) continue;  // незнакомый кусок пропускается
        u32 n = get_u32(b);
        if (b.bad || n == 0 || n > SKEL_MAX_BONES) { *err = "костей 0 или больше 255"; return false; }
        out.bones = {arena_push<SkelBone>(arena, n), n};
        for (u32 i = 0; i < n; ++i) {
            SkelBone &k = out.bones.data[i];
            u8 len = get_u8(b);
            char *s = arena_push<char>(arena, (usize)len + 1);
            get_bytes(b, s, len);
            s[b.bad ? 0 : len] = 0;
            k.name = {s, b.bad ? 0u : len};
            k.hash = str_hash(k.name);
            k.parent = get_i32(b);
            k.length = get_f32(b);
            for (f32 &v : k.pos) v = get_f32(b);
            for (f32 &v : k.rot) v = get_f32(b);
            for (f32 &v : k.inv) v = get_f32(b);
            if (k.parent >= (i32)i || k.parent < -1) { *err = "родитель кости не раньше неё"; return false; }
        }
        if (b.bad) { *err = "кусок BONE испорчен"; return false; }
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!out.bones.count) { *err = "нет костей (BONE)"; return false; }
    return true;
}

i32 skel_find(const Skel &s, u64 name_hash) {
    for (usize i = 0; i < s.bones.count; ++i)
        if (s.bones.data[i].hash == name_hash) return (i32)i;
    return -1;
}
