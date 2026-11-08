#include "track.h"
#include "../core/ser.h"
#include "../core/text.h"

static const u8 TRACK_MAGIC[8] = {'A', 'N', 'A', 'T', 'R', 'A', 'K', '1'};

bool track_parse(const u8 *data, usize size, Arena &arena, Track &out, const char **err) {
    out = Track{};
    if (size < sizeof TRACK_MAGIC || memcmp(data, TRACK_MAGIC, sizeof TRACK_MAGIC) != 0) { *err = "не файл .trk"; return false; }
    Reader r{data + sizeof TRACK_MAGIC, size - sizeof TRACK_MAGIC, 0, false};
    Chunk c;
    bool head = false;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (c.version != 1) continue;
        if (chunk_is(c, "HEAD")) {
            out.skel = get_name_hash(b);
            u32 tps = get_u32(b);
            out.ticks = get_u32(b);
            out.parts = get_u32(b);
            out.flags = get_u32(b);
            if (b.bad || tps != TRACK_TICKS_PER_S || out.ticks < 2 || out.parts > TRACK_MAX_PARTS) {
                *err = "HEAD испорчен (тиков < 2, частей больше 16 или не 60 тиков/с)";
                return false;
            }
            head = true;
        } else if (!head) {
            continue;
        } else if (chunk_is(c, "PART")) {
            for (u32 i = 0; i < out.parts; ++i) {
                out.part[i].name = get_name_hash(b);
                out.part[i].radius = get_u16(b);
            }
            if (b.bad) { *err = "PART испорчен"; return false; }
        } else if (chunk_is(c, "ROOT")) {
            out.root = arena_push<i32>(arena, (usize)out.ticks * 3);
            if (!get_bytes(b, out.root, (usize)out.ticks * 3 * sizeof(i32))) { *err = "ROOT короче тиков"; return false; }
        } else if (chunk_is(c, "CAPS")) {
            usize n = (usize)out.ticks * out.parts * 6;
            out.caps = arena_push<i16>(arena, n);
            if (!get_bytes(b, out.caps, n * sizeof(i16))) { *err = "CAPS короче тиков"; return false; }
        } else if (chunk_is(c, "WEAP")) {
            out.weapon = arena_push<i16>(arena, (usize)out.ticks * 6);
            if (!get_bytes(b, out.weapon, (usize)out.ticks * 6 * sizeof(i16))) { *err = "WEAP короче тиков"; return false; }
        }
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!head || !out.root || !out.caps) { *err = "нет HEAD, ROOT или CAPS"; return false; }
    if ((out.flags & TRACK_WEAPON) && !out.weapon) { *err = "флаг оружия без WEAP"; return false; }
    return true;
}
