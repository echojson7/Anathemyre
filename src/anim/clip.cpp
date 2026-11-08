#include "clip.h"
#include "../core/ser.h"
#include "../core/text.h"
#include <stdio.h>

static const u8 ANIM_MAGIC[8] = {'A', 'N', 'A', 'A', 'N', 'I', 'M', '1'};

bool anim_parse(const u8 *data, usize size, Arena &arena, AnimClip &out, const char **err) {
    out = AnimClip{};
    if (size < sizeof ANIM_MAGIC || memcmp(data, ANIM_MAGIC, sizeof ANIM_MAGIC) != 0) { *err = "не файл .anim"; return false; }
    Reader r{data + sizeof ANIM_MAGIC, size - sizeof ANIM_MAGIC, 0, false};
    Chunk c;
    u64 trans_hash = 0;
    bool head = false;
    while (next_chunk(r, c)) {
        Reader &b = c.body;
        if (c.version != 1) continue;
        if (chunk_is(c, "HEAD")) {
            out.skel = get_name_hash(b);
            u32 kps = get_u32(b);
            out.keys = get_u32(b);
            out.bones = get_u32(b);
            out.flags = get_u32(b);
            trans_hash = get_name_hash(b);
            if (b.bad || kps != ANIM_KEYS_PER_S || out.keys < 2 || !out.bones || out.bones > SKEL_MAX_BONES) {
                *err = "HEAD испорчен (ключей < 2, костей 0 или не 30 ключей/с)";
                return false;
            }
            head = true;
        } else if (!head) {
            continue;  // куски до HEAD не понять
        } else if (chunk_is(c, "BONE")) {
            out.bone_hash = arena_push<u64>(arena, out.bones);
            for (u32 i = 0; i < out.bones; ++i) out.bone_hash[i] = get_name_hash(b);
            if (b.bad) { *err = "BONE испорчен"; return false; }
        } else if (chunk_is(c, "ROOT")) {
            out.root = arena_push<i32>(arena, (usize)out.keys * 3);
            if (!get_bytes(b, out.root, (usize)out.keys * 3 * sizeof(i32))) { *err = "ROOT короче ключей"; return false; }
        } else if (chunk_is(c, "TRNS")) {
            out.trans = arena_push<i16>(arena, (usize)out.keys * 3);
            if (!get_bytes(b, out.trans, (usize)out.keys * 3 * sizeof(i16))) { *err = "TRNS короче ключей"; return false; }
        } else if (chunk_is(c, "ROTS")) {
            usize n = (usize)out.keys * out.bones * 6;
            out.rots = arena_push<u8>(arena, n);
            if (!get_bytes(b, out.rots, n)) { *err = "ROTS короче ключей"; return false; }
        }
    }
    if (r.bad) { *err = "файл обрезан"; return false; }
    if (!head || !out.bone_hash || !out.root || !out.trans || !out.rots) { *err = "нет HEAD, BONE, ROOT, TRNS или ROTS"; return false; }
    out.trans_bone = out.bones;
    for (u32 i = 0; i < out.bones; ++i)
        if (out.bone_hash[i] == trans_hash) out.trans_bone = i;
    if (out.trans_bone == out.bones) { *err = "кости переноса нет в BONE"; return false; }
    return true;
}

f32 anim_duration(const AnimClip &c) { return (f32)(c.keys - 1) / (f32)ANIM_KEYS_PER_S; }

bool anim_bind(const AnimClip &c, const Skel &s, i16 *map, i32 *trans_bone) {
    *trans_bone = -1;
    for (usize i = 0; i < s.bones.count; ++i) {
        map[i] = -1;
        for (u32 k = 0; k < c.bones; ++k)
            if (c.bone_hash[k] == s.bones.data[i].hash) map[i] = (i16)k;
        if (map[i] == (i16)c.trans_bone) *trans_bone = (i32)i;
    }
    return *trans_bone >= 0;
}

// smallest-three: 2 бита — номер наибольшей компоненты, три остальные по 15 бит в [−1/√2, 1/√2]
static Quat unpack(const u8 *p) {
    u64 v = (u64)p[0] | (u64)p[1] << 8 | (u64)p[2] << 16 | (u64)p[3] << 24 | (u64)p[4] << 32 | (u64)p[5] << 40;
    u32 k = (u32)(v & 3);
    f32 q[4];
    f32 s = 0;
    u32 sh = 2;
    for (u32 i = 0; i < 4; ++i) {
        if (i == k) continue;
        u32 u = (u32)(v >> sh) & 0x7fff;
        sh += 15;
        q[i] = ((f32)u * (2.0f / 32767.0f) - 1.0f) * 0.70710678f;
        s += q[i] * q[i];
    }
    q[k] = f_sqrt(f_max(0.0f, 1.0f - s));
    return {q[0], q[1], q[2], q[3]};
}

static Quat nlerp(Quat a, Quat b, f32 w) {
    f32 d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    f32 wb = d < 0 ? -w : w, wa = 1.0f - w;
    Quat q{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
    f32 n = f_sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    f32 k = n > 0 ? 1.0f / n : 0.0f;
    return {q.x * k, q.y * k, q.z * k, q.w * k};
}

void anim_sample(const AnimClip &c, const i16 *map, u32 nbones, f32 t, AnimPose &out) {
    f32 dur = anim_duration(c);
    if (c.flags & ANIM_LOOP) {
        t -= dur * (f32)f_floor_i(t / dur);
        if (t >= dur) t = 0;
    }
    f32 kf = f_clamp(t, 0.0f, dur) * (f32)ANIM_KEYS_PER_S;
    u32 k0 = (u32)f_floor_i(kf);
    if (k0 > c.keys - 2) k0 = c.keys - 2;
    f32 w = f_clamp(kf - (f32)k0, 0.0f, 1.0f);
    const u8 *r0 = c.rots + (usize)k0 * c.bones * 6, *r1 = r0 + (usize)c.bones * 6;
    for (u32 i = 0; i < nbones; ++i) {
        i32 m = map[i];
        out.rot[i] = m < 0 ? Quat{} : nlerp(unpack(r0 + m * 6), unpack(r1 + m * 6), w);
    }
    const i16 *a = c.trans + k0 * 3, *b = a + 3;
    out.trans = Vec3{(f32)a[0], (f32)a[1], (f32)a[2]} * ((1.0f - w) * 0.001f) + Vec3{(f32)b[0], (f32)b[1], (f32)b[2]} * (w * 0.001f);
    const i32 *ra = c.root + k0 * 3, *rb = ra + 3;
    out.root_x = ((f32)ra[0] * (1.0f - w) + (f32)rb[0] * w) * 0.001f;
    out.root_z = ((f32)ra[1] * (1.0f - w) + (f32)rb[1] * w) * 0.001f;
    i32 dy = (i32)((u32)rb[2] - (u32)ra[2]);  // курс — по короткой дуге
    out.root_yaw = (u32)ra[2] + (u32)(i32)((f32)dy * w);
}

void anim_blend(AnimPose &a, const AnimPose &b, u32 nbones, f32 w) {
    for (u32 i = 0; i < nbones; ++i) a.rot[i] = nlerp(a.rot[i], b.rot[i], w);
    a.trans = a.trans * (1.0f - w) + b.trans * w;
    a.root_x = a.root_x * (1.0f - w) + b.root_x * w;
    a.root_z = a.root_z * (1.0f - w) + b.root_z * w;
    i32 dy = (i32)(b.root_yaw - a.root_yaw);
    a.root_yaw += (u32)(i32)((f32)dy * w);
}

static bool add_span(AnimSpan *s, u32 &n, const TextLine &l, u32 first, char *err, usize err_size) {
    u32 a = 0, b = 0;
    if (l.count < first + 2 || !parse_u32(l.w[first], a) || !parse_u32(l.w[first + 1], b) || b < a || b > 0xffff) {
        snprintf(err, err_size, "строка %u: нужны ключи от и до", l.line);
        return false;
    }
    if (n >= ANIM_MAX_SPANS) {
        snprintf(err, err_size, "строка %u: отрезков больше %u", l.line, ANIM_MAX_SPANS);
        return false;
    }
    s[n++] = {(u16)a, (u16)b};
    return true;
}

bool anim_labels_parse(const u8 *data, usize size, AnimLabels &out, char *err, usize err_size) {
    out = AnimLabels{};
    out.cancel = -1;
    TextReader r = text_reader(data, size);
    TextLine l;
    while (text_next(r, l)) {
        Str w = l.w[0];
        bool ok = true;
        if (str_eq(w, "foot")) {
            u32 side = l.count > 1 && str_eq(l.w[1], "r") ? 1 : 0;
            ok = add_span(out.foot[side], out.foot_count[side], l, 2, err, err_size);
        } else if (str_eq(w, "hit")) {
            ok = add_span(out.hit, out.hit_count, l, 1, err, err_size);
        } else if (str_eq(w, "block")) {
            ok = add_span(out.block, out.block_count, l, 1, err, err_size);
        } else if (str_eq(w, "cancel")) {
            u32 k = 0;
            ok = l.count > 1 && parse_u32(l.w[1], k);
            if (!ok) snprintf(err, err_size, "строка %u: cancel <ключ>", l.line);
            out.cancel = (i32)k;
        } else if (str_eq(w, "weapon")) {
            out.weapon = true;
        }
        if (!ok) return false;
    }
    return true;
}

bool anim_span_has(const AnimSpan *s, u32 n, u32 k) {
    for (u32 i = 0; i < n; ++i)
        if (k >= s[i].a && k <= s[i].b) return true;
    return false;
}
