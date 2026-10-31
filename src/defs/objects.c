#include "objects.h"

#include <string.h>

#include "../sim/phys.h"

#define ID_MAX 32

static ObjectDef s_obj[OBJDEF_CAP];
static PrefabDef s_pre[PREFAB_CAP];
static PrefabPut s_puts[PREFAB_PUTS];
static u32 s_nputs;

const ObjectDef *g_objects = s_obj;
u32 g_object_count;
const PrefabDef *g_prefabs = s_pre;
u32 g_prefab_count;
const PrefabPut *g_prefab_puts = s_puts;

// Открытая запись: объект или заготовка; -1 — нет
static struct {
    i32  kind;     // 0 объект, 1 заготовка
    u32  id;
    u32  lines;
    bool solid;    // в записи уже была строка solid: коробки родителя заменены
    bool puts;     // в записи уже был put: список прежней записи с этим именем заменён
} s_at = { .kind = -1 };

static const char *const g_place_word[] = { "settle", "hang", "fixed" };
static const char *const g_flick_word[FLICK_COUNT] = { "none", "hum" };
_Static_assert(PHYS_SETTLE == 0 && PHYS_HANG == 1 && PHYS_FIXED == 2, "g_place_word по номерам PhysMode");

static bool tok_eq(const DefTok *t, const char *s) { return strlen(s) == t->n && !memcmp(t->s, s, t->n); }

i32 object_find(const char *id)
{
    for (u32 i = 0; i < g_object_count; i++)
        if (!strcmp(s_obj[i].id, id)) return (i32)i;
    return -1;
}

i32 prefab_find(const char *id)
{
    for (u32 i = 0; i < g_prefab_count; i++)
        if (!strcmp(s_pre[i].id, id)) return (i32)i;
    return -1;
}

static i32 find_tok(bool prefab, const DefTok *t)
{
    u32 n = prefab ? g_prefab_count : g_object_count;
    for (u32 i = 0; i < n; i++)
        if (tok_eq(t, prefab ? s_pre[i].id : s_obj[i].id)) return (i32)i;
    return -1;
}

static bool nums_at(const DefLine *l, u32 at, u32 n, f32 *out)
{
    if (l->n < at + n) {
        def_error(l, "у %.*s нужно чисел: %u", (int)l->tok[0].n, l->tok[0].s, n);
        return false;
    }
    for (u32 i = 0; i < n; i++)
        if (!def_num(&l->tok[at + i], &out[i])) {
            def_error(l, "не число: %.*s", (int)l->tok[at + i].n, l->tok[at + i].s);
            return false;
        }
    return true;
}

static bool exact(const DefLine *l, u32 n)
{
    if (l->n == n + 1) return true;
    def_error(l, "у %.*s нужно значений: %u", (int)l->tok[0].n, l->tok[0].s, n);
    return false;
}

static bool nums(const DefLine *l, u32 n, f32 *out) { return exact(l, n) && nums_at(l, 1, n, out); }

static bool v3_field(const DefLine *l, V3 *out)
{
    f32 v[3];
    if (!nums(l, 3, v)) return false;
    *out = v3(v[0], v[1], v[2]);
    return true;
}

static bool yes_no(const DefLine *l, bool *out)
{
    if (!exact(l, 1)) return false;
    if (def_is(&l->tok[1], "yes") || def_is(&l->tok[1], "no")) {
        *out = def_is(&l->tok[1], "yes");
        return true;
    }
    def_error(l, "нужно yes или no");
    return false;
}

static bool word(const DefLine *l, const char *const *words, u32 count, u8 *out)
{
    if (!exact(l, 1)) return false;
    i32 w = def_word(&l->tok[1], words, count);
    if (w < 0) {
        def_error(l, "неизвестное слово: %.*s", (int)l->tok[1].n, l->tok[1].s);
        return false;
    }
    *out = (u8)w;
    return true;
}

static bool name_ok(const DefTok *t)
{
    if (t->quoted || !t->n || t->n >= ID_MAX) return false;
    for (u32 i = 0; i < t->n; i++) {
        char c = t->s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static bool header(const DefLine *l, bool prefab, Arena *mem)
{
    if (l->n != 2 || !name_ok(&l->tok[1])) {
        def_error(l, "запись: «%s имя», имя — латиница, цифры и _ короче %u", prefab ? "prefab" : "object", ID_MAX);
        return false;
    }
    s_at.kind = prefab;
    s_at.lines = 0;
    s_at.solid = s_at.puts = false;
    i32 at = find_tok(prefab, &l->tok[1]);
    if (at >= 0) {
        s_at.id = (u32)at;
        return true;
    }
    if ((prefab ? g_prefab_count : g_object_count) == (prefab ? PREFAB_CAP : OBJDEF_CAP)) {
        def_error(l, "записей %s больше %u", prefab ? "prefab" : "object", prefab ? PREFAB_CAP : OBJDEF_CAP);
        return false;
    }
    const char *id = def_copy(mem, &l->tok[1]);
    if (!id) return false;
    if (prefab) {
        s_at.id = g_prefab_count++;
        s_pre[s_at.id] = (PrefabDef){ .id = id };
    } else {
        s_at.id = g_object_count++;
        s_obj[s_at.id] = (ObjectDef){ .id = id, .size = v3(1, 1, 1), .place = PHYS_FIXED, .cast = true,
                                      .light = { .cone = -1.0f, .night = -1.0f, .dir = v3(0, -1, 0) } };
    }
    return true;
}

static bool object_field(const DefLine *l, Arena *mem)
{
    ObjectDef *d = &s_obj[s_at.id];
    const DefTok *k = &l->tok[0];
    if (def_is(k, "parent")) {
        if (s_at.lines != 1 || !exact(l, 1)) {
            def_error(l, "parent — первой строкой записи");
            return false;
        }
        i32 p = find_tok(false, &l->tok[1]);
        if (p < 0 || (u32)p == s_at.id) {
            def_error(l, "родителя %.*s нет выше по файлам", (int)l->tok[1].n, l->tok[1].s);
            return false;
        }
        const char *id = d->id;
        *d = s_obj[p];
        d->id = id;
        return true;
    }
    if (def_is(k, "shape")) {
        if (!exact(l, 1)) return false;
        return (d->shape = def_copy(mem, &l->tok[1])) != NULL;
    }
    if (def_is(k, "size")) return v3_field(l, &d->size);
    if (def_is(k, "mat") || def_is(k, "mat2")) {
        if (!exact(l, 1)) return false;
        u32 s = def_is(k, "mat2");
        return (d->mat_ref[s] = def_copy(mem, &l->tok[1])) != NULL;
    }
    if (def_is(k, "tint")) return nums(l, 1, &d->tint);
    if (def_is(k, "place")) return word(l, g_place_word, ARRAY_COUNT(g_place_word), &d->place);
    if (def_is(k, "shadow")) return yes_no(l, &d->cast);
    if (def_is(k, "solid")) {
        // Первая строка solid в записи заменяет коробки родителя
        if (!s_at.solid) d->solids = 0, d->solid_full = false;
        s_at.solid = true;
        if (l->n == 2 && def_is(&l->tok[1], "full")) return d->solid_full = true;
        if (l->n == 2 && def_is(&l->tok[1], "none")) return true;
        f32 v[6];
        if (!nums(l, 6, v)) return false;
        if (d->solids == OBJ_SOLIDS) {
            def_error(l, "коробок больше %u", OBJ_SOLIDS);
            return false;
        }
        d->solid[d->solids++] = (ObjSolid){ v3(v[0], v[1], v[2]), v3(v[3], v[4], v[5]) };
        return true;
    }
    if (def_is(k, "act")) {
        // act что x y z поворот: точка действия на будущее
        f32 v[4];
        if (l->n != 6 || !nums_at(l, 2, 4, v)) {
            def_error(l, "act что x y z поворот");
            return false;
        }
        if (d->acts == OBJ_ACTS) {
            def_error(l, "точек действия больше %u", OBJ_ACTS);
            return false;
        }
        d->act[d->acts++] = (ObjAct){ v3(v[0], v[1], v[2]), v[3] * (PI_F / 180.0f), def_copy(mem, &l->tok[1]) };
        return true;
    }
    ObjLight *g = &d->light;
    if (def_is(k, "light")) {
        // light r g b — цвет с яркостью; light материал сила — цвет лампы из materials.txt
        d->lit = true;
        f32 v;
        if (l->n == 3 && !def_num(&l->tok[1], &v)) {
            if (!def_num(&l->tok[2], &d->light_power)) {
                def_error(l, "light материал сила");
                return false;
            }
            return (d->light_mat = def_copy(mem, &l->tok[1])) != NULL;
        }
        d->light_mat = NULL;
        return v3_field(l, &g->color);
    }
    if (def_is(k, "light_at")) return v3_field(l, &g->at);
    if (def_is(k, "light_dir")) {
        if (!v3_field(l, &g->dir)) return false;
        if (v3_dot(g->dir, g->dir) < 1e-6f) {
            def_error(l, "нулевая ось света");
            return false;
        }
        g->dir = v3_norm(g->dir);
        return true;
    }
    if (def_is(k, "light_radius")) return nums(l, 1, &g->radius);
    if (def_is(k, "light_cone")) return nums(l, 1, &g->cone);
    if (def_is(k, "light_glow")) return nums(l, 1, &g->glow);
    if (def_is(k, "light_night")) {
        f32 v[2];
        if (!nums(l, 2, v)) return false;
        g->night = v[0], g->spread = v[1];
        return true;
    }
    if (def_is(k, "light_flicker")) return word(l, g_flick_word, FLICK_COUNT, &g->flicker);
    if (def_is(k, "light_shadow")) return yes_no(l, &g->shadow);
    def_error(l, "у объекта нет поля %.*s", (int)k->n, k->s);
    return false;
}

// put что x y z [yaw градусы] [size w h d] [mat имя] [mat2 имя]; что — объект, заготовка или «a|b|c» на выбор по зерну
static bool prefab_field(const DefLine *l, Arena *mem)
{
    PrefabDef *d = &s_pre[s_at.id];
    if (!def_is(&l->tok[0], "put")) {
        def_error(l, "у заготовки есть только put");
        return false;
    }
    if (s_nputs == PREFAB_PUTS) {
        def_error(l, "строк put больше %u", PREFAB_PUTS);
        return false;
    }
    // Прежний список той же заготовки заменяется целиком; строки одной заготовки идут подряд
    if (!s_at.puts) d->first = s_nputs, d->count = 0;
    s_at.puts = true;
    if (d->first + d->count != s_nputs) {
        def_error(l, "строки put заготовки %s прерваны другой записью", d->id);
        return false;
    }
    PrefabPut *p = &s_puts[s_nputs];
    *p = (PrefabPut){ .file = l->file, .line = l->line };
    f32 v[3];
    if (l->n < 5 || l->tok[1].quoted || !nums_at(l, 2, 3, v)) {
        def_error(l, "put что x y z [yaw d] [size w h d] [var a b] [mat имя] [mat2 имя]");
        return false;
    }
    p->ref = def_copy(mem, &l->tok[1]);
    p->pos = v3(v[0], v[1], v[2]);
    for (u32 i = 5; i < l->n;) {
        const DefTok *k = &l->tok[i];
        if (def_is(k, "yaw") && nums_at(l, i + 1, 1, v)) {
            p->yaw = v[0] * (PI_F / 180.0f);
            i += 2;
        } else if (def_is(k, "size") && nums_at(l, i + 1, 3, v)) {
            p->size = v3(v[0], v[1], v[2]);
            i += 4;
        } else if (def_is(k, "var") && nums_at(l, i + 1, 2, v)) {
            // Целые −128..255: знаковые (наклон в градусах) хранятся как u8
            for (u32 j = 0; j < 2; j++) {
                if (v[j] < -128.0f || v[j] > 255.0f || v[j] != floorf(v[j])) {
                    def_error(l, "var — целые от -128 до 255");
                    return false;
                }
                p->var[j] = (u8)(i32)v[j];
            }
            i += 3;
        } else if ((def_is(k, "mat") || def_is(k, "mat2")) && i + 1 < l->n) {
            p->mat_ref[def_is(k, "mat2")] = def_copy(mem, &l->tok[i + 1]);
            i += 2;
        } else {
            def_error(l, "в put непонятно: %.*s", (int)k->n, k->s);
            return false;
        }
    }
    s_nputs++;
    d->count++;
    return true;
}

void objdefs_reset(void)
{
    g_object_count = g_prefab_count = s_nputs = 0;
    s_at.kind = -1;
}

void objdefs_end(void) { s_at.kind = -1; }

i32 objdefs_line(const DefLine *l, Arena *mem)
{
    const DefTok *k = &l->tok[0];
    if (def_is(k, "object") || def_is(k, "prefab")) return header(l, def_is(k, "prefab"), mem) ? 1 : -1;
    if (s_at.kind < 0) return 0;
    s_at.lines++;
    return (s_at.kind ? prefab_field(l, mem) : object_field(l, mem)) ? 1 : -1;
}

static const MatDef *mat_named(const char *name)
{
    for (u32 i = 0; i < g_mat_count; i++)
        if (!strcmp(g_mats[i].id, name)) return &g_mats[i];
    return NULL;
}

// Заготовка не включает саму себя ни прямо, ни через другие
static bool loops(u32 pre, u32 depth)
{
    if (depth > 16) return true;
    const PrefabDef *d = &s_pre[pre];
    for (u32 i = 0; i < d->count; i++) {
        const PrefabPut *p = &s_puts[d->first + i];
        for (u32 k = 0; k < p->picks; k++)
            if ((p->what[k] & PUT_PREFAB) && loops(p->what[k] & ~PUT_PREFAB, depth + 1)) return true;
    }
    return false;
}

bool objdefs_check(void)
{
    bool ok = true;
    for (u32 i = 0; i < g_object_count; i++) {
        ObjectDef *d = &s_obj[i];
        if (!d->shape && !d->lit) {
            log_error("ОПИСАНИЯ: у объекта %s нет ни shape, ни light", d->id);
            ok = false;
        }
        for (u32 s = 0; s < OBJ_MATS; s++) {
            if (!d->mat_ref[s]) continue;
            if (!(d->mat[s] = mat_named(d->mat_ref[s]))) {
                log_error("ОПИСАНИЯ: объект %s красится материалом %s, а его нет", d->id, d->mat_ref[s]);
                ok = false;
            }
        }
        if (d->lit && d->light_mat) {
            const MatDef *m = mat_named(d->light_mat);
            if (!m) {
                log_error("ОПИСАНИЯ: свет объекта %s — материал %s, а его нет", d->id, d->light_mat);
                ok = false;
            } else
                d->light.color = v3_mul(m->tone[0], d->light_power);
        }
        if (d->lit && d->light.radius <= 0.0f) {
            log_error("ОПИСАНИЯ: у света объекта %s нет light_radius", d->id);
            ok = false;
        }
    }
    for (u32 i = 0; i < s_nputs; i++) {
        PrefabPut *p = &s_puts[i];
        // Варианты через |: имя объекта, иначе заготовки
        const char *s = p->ref;
        while (*s && ok) {
            const char *e = strchr(s, '|');
            u32 n = e ? (u32)(e - s) : (u32)strlen(s);
            DefTok t = { s, n, false };
            i32 o = find_tok(false, &t), f = o < 0 ? find_tok(true, &t) : -1;
            if ((o < 0 && f < 0) || p->picks == PUT_PICK) {
                log_error("ОПИСАНИЯ: %s:%u: нет объекта или заготовки %.*s (или вариантов больше %u)", p->file, p->line,
                          (int)n, s, PUT_PICK);
                ok = false;
                break;
            }
            p->what[p->picks++] = o >= 0 ? (u16)o : (u16)(PUT_PREFAB | (u32)f);
            if (f >= 0 && (p->size.x != 0.0f || p->size.y != 0.0f || p->size.z != 0.0f)) {
                log_error("ОПИСАНИЯ: %s:%u: у заготовки %.*s нет размера", p->file, p->line, (int)n, s);
                ok = false;
            }
            s = e ? e + 1 : s + n;
        }
        for (u32 k = 0; k < OBJ_MATS; k++) {
            if (!p->mat_ref[k]) continue;
            if (!(p->mat[k] = mat_named(p->mat_ref[k]))) {
                log_error("ОПИСАНИЯ: %s:%u: нет материала %s", p->file, p->line, p->mat_ref[k]);
                ok = false;
            }
        }
    }
    for (u32 i = 0; ok && i < g_prefab_count; i++)
        if (loops(i, 0)) {
            log_error("ОПИСАНИЯ: заготовка %s включает саму себя (или вложена глубже 16)", s_pre[i].id);
            ok = false;
        }
    return ok;
}
