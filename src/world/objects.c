#include "objects.h"

#include <string.h>

#include "../core/hash.h"
#include "../core/log.h"
#include "map.h"

// Раскладка: строки заготовок в порядке файла, вложенные — на месте строки
typedef struct Spread {
    Objects *o;
    u32      n;     // уже разложено (до сортировки по чанкам)
    u32     *chunk_of;
    bool     full;
} Spread;

u32 obj_chunk_of(f32 x, f32 z)
{
    i32 cx = (i32)((x + MAP_HALF) * (1.0f / OBJ_CHUNK)), cz = (i32)((z + MAP_HALF) * (1.0f / OBJ_CHUNK));
    cx = cx < 0 ? 0 : cx >= (i32)OBJ_SIDE ? (i32)OBJ_SIDE - 1 : cx;
    cz = cz < 0 ? 0 : cz >= (i32)OBJ_SIDE ? (i32)OBJ_SIDE - 1 : cz;
    return (u32)cz * OBJ_SIDE + (u32)cx;
}

static i16 cm(f32 v)
{
    f32 c = v * 100.0f + (v < 0.0f ? -0.5f : 0.5f);
    return (i16)(c > 32767.0f ? 32767.0f : c < -32767.0f ? -32767.0f : c);
}

static void spread(Spread *s, u32 pre, V3 at, f32 yaw, u32 seed, const MatDef *const *mat, u8 group, u32 depth)
{
    const PrefabDef *d = &g_prefabs[pre];
    M4 R = m4_rot_y(yaw);
    for (u32 i = 0; i < d->count; i++) {
        const PrefabPut *p = &g_prefab_puts[d->first + i];
        u32 sd = hash_u32(seed + (i + 1) * 0x9E3779B9u);
        u16 what = p->what[p->picks > 1 ? hash_u32(sd ^ 0x51u) % p->picks : 0];
        V3 pos = v3_add(at, m4_point(R, p->pos));
        f32 y = yaw + p->yaw;
        const MatDef *m[OBJ_MATS];
        for (u32 k = 0; k < OBJ_MATS; k++) m[k] = p->mat[k] ? p->mat[k] : mat[k];
        u8 g = depth ? group : (u8)(i < OBJ_GROUPS ? i : OBJ_GROUPS - 1);
        if (!depth && i < OBJ_GROUPS) s->o->group_name[i] = p->ref, s->o->groups = i + 1;
        if (what & PUT_PREFAB) {
            spread(s, what & ~PUT_PREFAB, pos, y, sd, m, g, depth + 1);
            continue;
        }
        if (s->n == OBJ_MAX) {
            s->full = true;
            return;
        }
        Objects *o = s->o;
        const ObjectDef *od = &g_objects[what];
        u32 k = s->n++;
        o->def[k] = what;
        o->flags[k] = (u8)((od->lit ? OBJF_LIT : 0) | (od->cast ? 0 : OBJF_NO_SHADOW));
        o->group[k] = g;
        o->seed[k] = sd;
        o->pos[k] = pos;
        f32 turn = y * (65536.0f / (2.0f * PI_F));
        turn -= 65536.0f * floorf(turn / 65536.0f);
        o->yaw[k] = (u16)((u32)(turn + 0.5f) & 0xFFFFu);
        o->size[k][0] = cm(p->size.x), o->size[k][1] = cm(p->size.y), o->size[k][2] = cm(p->size.z);
        for (u32 j = 0; j < OBJ_MATS; j++) o->mat[k][j] = m[j] ? (u8)(m[j] - g_mats) : 255;
        s->chunk_of[k] = obj_chunk_of(pos.x, pos.z);
    }
}

bool objects_build(Objects *o, Arena *perm, u64 seed)
{
    memset(o, 0, sizeof *o);
    i32 world = prefab_find("world");
    if (world < 0) {
        log_error("ОБЪЕКТЫ: нет заготовки world (data/defs/prefabs.txt)");
        return false;
    }
    // Сначала во временные массивы по порядку раскладки, потом по чанкам подсчётом: порядок внутри чанка тот же
    Objects t = { 0 };
    size_t mark = perm->used;
    t.def = ARENA_PUSH_ARRAY(perm, u16, OBJ_MAX);
    t.flags = ARENA_PUSH_ARRAY(perm, u8, OBJ_MAX);
    t.group = ARENA_PUSH_ARRAY(perm, u8, OBJ_MAX);
    t.seed = ARENA_PUSH_ARRAY(perm, u32, OBJ_MAX);
    t.pos = ARENA_PUSH_ARRAY(perm, V3, OBJ_MAX);
    t.yaw = ARENA_PUSH_ARRAY(perm, u16, OBJ_MAX);
    t.size = (i16(*)[3])ARENA_PUSH_ARRAY(perm, i16, OBJ_MAX * 3);
    t.mat = (u8(*)[OBJ_MATS])ARENA_PUSH_ARRAY(perm, u8, OBJ_MAX * OBJ_MATS);
    Spread s = { .o = &t, .chunk_of = ARENA_PUSH_ARRAY(perm, u32, OBJ_MAX) };
    const MatDef *none[OBJ_MATS] = { 0 };
    spread(&s, (u32)world, v3(0, 0, 0), 0.0f, hash_u32((u32)seed ^ (u32)(seed >> 32) ^ 0x0B1EC7u), none, 0, 0);
    if (s.full) {
        log_error("ОБЪЕКТЫ: экземпляров больше %u", OBJ_MAX);
        return false;
    }
    u32 n = s.n;
    // Постоянные массивы — над временными, потом временные снимаются копированием вниз
    static u32 count[OBJ_SIDE * OBJ_SIDE + 1], at_chunk[OBJ_SIDE * OBJ_SIDE];
    memset(count, 0, sizeof count);
    for (u32 i = 0; i < n; i++) count[s.chunk_of[i] + 1]++;
    for (u32 c = 0; c < OBJ_SIDE * OBJ_SIDE; c++) count[c + 1] += count[c];
    u32 *to = ARENA_PUSH_ARRAY(perm, u32, n ? n : 1);
    memcpy(at_chunk, count, sizeof at_chunk);
    for (u32 i = 0; i < n; i++) to[i] = at_chunk[s.chunk_of[i]]++;
    // Сортированная копия во временный буфер выше, затем на место временных массивов
    Objects f = t;
    f.def = ARENA_PUSH_ARRAY(perm, u16, n + 1);
    f.flags = ARENA_PUSH_ARRAY(perm, u8, n + 1);
    f.group = ARENA_PUSH_ARRAY(perm, u8, n + 1);
    f.seed = ARENA_PUSH_ARRAY(perm, u32, n + 1);
    f.pos = ARENA_PUSH_ARRAY(perm, V3, n + 1);
    f.yaw = ARENA_PUSH_ARRAY(perm, u16, n + 1);
    f.size = (i16(*)[3])ARENA_PUSH_ARRAY(perm, i16, (n + 1) * 3);
    f.mat = (u8(*)[OBJ_MATS])ARENA_PUSH_ARRAY(perm, u8, (n + 1) * OBJ_MATS);
    u32 lit = 0;
    for (u32 i = 0; i < n; i++) {
        u32 j = to[i];
        f.def[j] = t.def[i], f.flags[j] = t.flags[i], f.group[j] = t.group[i], f.seed[j] = t.seed[i];
        f.pos[j] = t.pos[i], f.yaw[j] = t.yaw[i];
        memcpy(f.size[j], t.size[i], sizeof f.size[j]);
        memcpy(f.mat[j], t.mat[i], sizeof f.mat[j]);
        lit += (t.flags[i] & OBJF_LIT) != 0;
    }
    // Всё нужное — в начало занятой памяти: временное снимается
    u8 *keep = (u8 *)perm->base + mark;
    size_t at = 0;
#define KEEP(field, type, cnt)                                                                                         \
    {                                                                                                                  \
        size_t bytes = sizeof(type) * (cnt);                                                                           \
        at = (at + 15) & ~(size_t)15;                                                                                  \
        memmove(keep + at, (const void *)f.field, bytes);                                                              \
        o->field = (void *)(keep + at);                                                                                \
        at += bytes;                                                                                                   \
    }
    KEEP(def, u16, n + 1)
    KEEP(flags, u8, n + 1)
    KEEP(group, u8, n + 1)
    KEEP(seed, u32, n + 1)
    KEEP(pos, V3, n + 1)
    KEEP(yaw, u16, n + 1)
    KEEP(size, i16, (n + 1) * 3)
    KEEP(mat, u8, (n + 1) * OBJ_MATS)
#undef KEEP
    // Сдвиг вниз безопасен: источник всегда выше приёмника (временные массивы раньше и не меньше)
    perm->used = mark + ((at + 15) & ~(size_t)15);
    u32 *chunk = ARENA_PUSH_ARRAY(perm, u32, OBJ_SIDE * OBJ_SIDE + 1);
    memcpy(chunk, count, sizeof(u32) * (OBJ_SIDE * OBJ_SIDE + 1));
    o->chunk = chunk;
    o->count = n;
    o->groups = t.groups;
    memcpy(o->group_name, t.group_name, sizeof o->group_name);
    o->lit = ARENA_PUSH_ARRAY(perm, u32, lit + 1);
    for (u32 i = 0; i < n; i++)
        if (o->flags[i] & OBJF_LIT) o->lit[o->lit_count++] = i;
    log_info("ОБЪЕКТЫ: экземпляров %u (%u Б), со светом %u, локаций %u", n, n * 30u, o->lit_count, o->groups);
    return true;
}

V3 obj_size(const Objects *o, u32 i)
{
    const i16 *s = o->size[i];
    if (!s[0] && !s[1] && !s[2]) return g_objects[o->def[i]].size;
    return v3((f32)s[0] * 0.01f, (f32)s[1] * 0.01f, (f32)s[2] * 0.01f);
}

const MatDef *obj_mat(const Objects *o, u32 i, u32 slot)
{
    u8 m = o->mat[i][slot];
    return m != 255 ? &g_mats[m] : g_objects[o->def[i]].mat[slot];
}

M4 obj_frame(const Objects *o, u32 i) { return m4_mul(m4_translate(o->pos[i]), m4_rot_y(obj_yaw(o, i))); }

void objects_solids(const Objects *o, Phys *p)
{
    u32 n = 0;
    for (u32 i = 0; i < o->count; i++) {
        const ObjectDef *d = &g_objects[o->def[i]];
        if (d->place != PHYS_FIXED || (!d->solids && !d->solid_full)) continue;
        V3 sz = obj_size(o, i);
        f32 yaw = obj_yaw(o, i);
        M4 L = obj_frame(o, i);
        if (d->solid_full) n += phys_static_add(p, o->pos[i], v3_mul(sz, 0.5f), yaw);
        // Коробки описания растягиваются вместе с размером экземпляра
        // Ось без размера (у провода, забора) не растягивает
        V3 k = v3(d->size.x != 0.0f && sz.x != 0.0f ? sz.x / d->size.x : 1.0f, d->size.y != 0.0f && sz.y != 0.0f ? sz.y / d->size.y : 1.0f,
                  d->size.z != 0.0f && sz.z != 0.0f ? sz.z / d->size.z : 1.0f);
        for (u32 b = 0; b < d->solids; b++) {
            const ObjSolid *s = &d->solid[b];
            n += phys_static_add(p, m4_point(L, v3_hadamard(s->c, k)), v3_hadamard(s->half, k), yaw);
        }
    }
    log_info("ОБЪЕКТЫ: коробок физики %u", n);
}
