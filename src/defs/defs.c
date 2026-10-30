#include "defs.h"

#include <string.h>

#include "../audio/audio.h"
#include "../game/look.h"
#include "../render/mesh.h"
#include "deftext.h"

#define ARMOR_CAP ARMOR_NONE  // 255 — «пусто»
#define GOODS_CAP 255
#define ID_MAX 32

static WeaponDef  s_weapons[WEAPON_CAP];
static WeaponPart s_parts[WEAPON_CAP * WEAPON_PARTS];
static f32        s_pts[WEAPON_CAP * WEAPON_PTS];
static ArmorDef   s_armors[ARMOR_CAP];
static GoodsDef   s_goods[GOODS_CAP];
static DefText    s_text;

// Слова файлов. Порядок — как перечисления в коде
static const char *const g_class_word[WC_COUNT] = { "blade", "cleaver", "blunt", "polearm", "heavy" };
static const char *const g_attack_word[ATK_COUNT] = { "overhead", "swing", "thrust" };
static const char *const g_quality_word[Q_COUNT] = { "junk", "worn", "sound", "master", "ancient" };
static const char *const g_slot_word[SLOT_COUNT] = { "head", "face", "shirt", "armor", "legs",
                                                     "feet", "hands", "back", "belt", "pants" };
static const char *const g_part_word[PART_COUNT] = { "head", "chest", "stomach", "arm_l", "arm_r", "leg_l", "leg_r" };
static const char *const g_mat_word[] = { "concrete", "rust", "sand", "cloth", "leather", "glyph", "emissive", "skin", "metal",
                                          "terrain", "rock", "water", "bark", "leaf", "grass", "steel" };
static const char *const g_voice_word[] = { "ressora", "sekach", "armatura", "bagor", "hook", "balka" };
_Static_assert(MAT_METAL == 8 && MAT_GLYPH == 5 && MAT_STEEL == 15, "g_mat_word по номерам MAT_*");
_Static_assert(SFX_WPN_BALKA - SFX_WPN_RESSORA == 5 && SFX_WPN_HOOK - SFX_WPN_RESSORA == 4, "g_voice_word по SFX_WPN_*");

// Имена, которые ставит сам код: свой отряд, наборы арены, тестовые сцены. Переедут в файлы с фракциями
static const char *const g_need[DK_COUNT][16] = {
    [DK_WEAPON] = { "ressora", "sekach", "armatura", "bagor", "balka" },
    [DK_ARMOR] = { "rags", "jacket", "quilt", "plate", "bucket", "greaves", "pants" },
    [DK_GOODS] = { "water", "rusk", "meat", "bandage", "scrap", "part", "fuel", "token", "wrap", "boots", "mitts", "pack", "belt" },
};

static u32 count_of(u32 k) { return k == DK_WEAPON ? g_weapon_count : k == DK_ARMOR ? g_armor_count : g_goods_count; }

static const char *id_of(u32 k, u32 i)
{
    return k == DK_WEAPON ? s_weapons[i].id : k == DK_ARMOR ? s_armors[i].id : s_goods[i].id;
}

static i32 find(u32 k, const char *s, u32 n)
{
    for (u32 i = 0; i < count_of(k); i++) {
        const char *id = id_of(k, i);
        if (strlen(id) == n && !memcmp(id, s, n)) return (i32)i;
    }
    return -1;
}

static u8 by_name(u32 k, const char *id)
{
    i32 i = find(k, id, (u32)strlen(id));
    if (i < 0) {
        log_error("ОПИСАНИЯ: нет записи %s", id);
        return 0;
    }
    return (u8)i;
}

u8 def_weapon(const char *id) { return by_name(DK_WEAPON, id); }
u8 def_armor(const char *id) { return by_name(DK_ARMOR, id); }
u8 def_goods(const char *id) { return by_name(DK_GOODS, id); }

// Разбор одной строки поля: число слов и сами слова
typedef struct Block {
    i32 kind;   // DK_*, BK_ATTACK, BK_QUALITY; -1 — вне записи
    u32 id;
    u32 lines;  // строк после заголовка
    bool shape; // в этой записи уже была форма: прежняя заменена
    bool paint;
    u8  mat;
    f32 color[3];
} Block;

enum { BK_ATTACK = DK_COUNT, BK_QUALITY };

static bool args(const DefLine *l, u32 n)
{
    if (l->n == n + 1) return true;
    def_error(l, "у %.*s нужно значений: %u", (int)l->tok[0].n, l->tok[0].s, n);
    return false;
}

static bool nums(const DefLine *l, u32 n, f32 *out)
{
    if (!args(l, n)) return false;
    for (u32 i = 0; i < n; i++)
        if (!def_num(&l->tok[1 + i], &out[i])) {
            def_error(l, "не число: %.*s", (int)l->tok[1 + i].n, l->tok[1 + i].s);
            return false;
        }
    return true;
}

static bool num(const DefLine *l, f32 *out) { return nums(l, 1, out); }

static bool word(const DefLine *l, u32 at, const char *const *words, u32 count, u8 *out)
{
    i32 w = def_word(&l->tok[at], words, count);
    if (w < 0) {
        def_error(l, "неизвестное слово: %.*s", (int)l->tok[at].n, l->tok[at].s);
        return false;
    }
    *out = (u8)w;
    return true;
}

static bool text(const DefLine *l, Arena *mem, const char **out)
{
    if (!args(l, 1)) return false;
    if (!l->tok[1].quoted) {
        def_error(l, "строка пишется в кавычках");
        return false;
    }
    *out = def_copy(mem, &l->tok[1]);
    return *out != NULL;
}

static bool size2(const DefLine *l, u8 *w, u8 *h)
{
    u32 a, b;
    if (!args(l, 2)) return false;
    if (!def_uint(&l->tok[1], &a) || !def_uint(&l->tok[2], &b) || !a || !b || a > INV_W || b > INV_H) {
        def_error(l, "размер — две клетки от 1 до %u и %u", INV_W, INV_H);
        return false;
    }
    *w = (u8)a;
    *h = (u8)b;
    return true;
}

static bool look(const DefLine *l, u8 *out)
{
    if (!args(l, 1)) return false;
    if (def_is(&l->tok[1], "none")) {
        *out = GEAR_NONE;
        return true;
    }
    return word(l, 1, g_gear_file, GEAR_COUNT, out);
}

static bool hides(const DefLine *l, u32 *out)
{
    *out = 0;
    for (u32 i = 1; i < l->n; i++) {
        u8 g;
        if (def_is(&l->tok[i], "hair")) *out |= GEAR_HIDES_HAIR;
        else if (word(l, i, g_gear_file, GEAR_COUNT, &g)) *out |= 1u << g;
        else return false;
    }
    return true;
}

static bool flag(const DefLine *l, bool *out)
{
    if (l->n == 1 || (l->n == 2 && def_is(&l->tok[1], "yes"))) *out = true;
    else if (l->n == 2 && def_is(&l->tok[1], "no")) *out = false;
    else {
        def_error(l, "после %.*s — ничего, yes или no", (int)l->tok[0].n, l->tok[0].s);
        return false;
    }
    return true;
}

static bool slot(const DefLine *l, u8 *out)
{
    if (!args(l, 1)) return false;
    if (def_is(&l->tok[1], "none")) {
        *out = SLOT_NONE;
        return true;
    }
    return word(l, 1, g_slot_word, SLOT_COUNT, out);
}

static bool weapon_field(const DefLine *l, Block *b, Arena *mem)
{
    WeaponDef *d = &s_weapons[b->id];
    const DefTok *k = &l->tok[0];
    f32 v[6];
    u32 n;
    if (def_is(k, "name")) return text(l, mem, &d->name);
    if (def_is(k, "class")) return args(l, 1) && word(l, 1, g_class_word, WC_COUNT, &d->wclass);
    if (def_is(k, "attacks")) {
        d->attacks = 0;
        for (u32 i = 1; i < l->n; i++) {
            u8 a;
            if (!word(l, i, g_attack_word, ATK_COUNT, &a)) return false;
            d->attacks |= (u8)(1u << a);
        }
        return true;
    }
    if (def_is(k, "targets")) {
        if (!args(l, 1) || !def_uint(&l->tok[1], &n) || !n || n > 8) {
            def_error(l, "целей от 1 до 8");
            return false;
        }
        d->max_targets = (u8)n;
        return true;
    }
    if (def_is(k, "damage")) {
        if (!nums(l, 3, v)) return false;
        d->cut = v[0], d->blunt = v[1], d->pierce = v[2];
        return true;
    }
    if (def_is(k, "length")) return num(l, &d->length);
    if (def_is(k, "weight")) return num(l, &d->weight);
    if (def_is(k, "speed")) return num(l, &d->speed);
    if (def_is(k, "arc")) return num(l, &d->arc_deg);
    if (def_is(k, "knock")) return num(l, &d->knock);
    if (def_is(k, "pen")) return num(l, &d->pen);
    if (def_is(k, "size")) return size2(l, &d->size_w, &d->size_h);
    if (def_is(k, "voice")) {
        if (l->n < 2 || l->n > 3) {
            def_error(l, "voice: одно или два слова");
            return false;
        }
        if (!word(l, 1, g_voice_word, ARRAY_COUNT(g_voice_word), &d->voice)) return false;
        d->voice_strike = d->voice;
        return l->n == 2 || word(l, 2, g_voice_word, ARRAY_COUNT(g_voice_word), &d->voice_strike);
    }
    if (def_is(k, "paint")) {
        if (!args(l, 4) || !word(l, 1, g_mat_word, ARRAY_COUNT(g_mat_word), &b->mat)) return false;
        for (u32 i = 0; i < 3; i++)
            if (!def_num(&l->tok[2 + i], &b->color[i])) {
                def_error(l, "цвет — три числа");
                return false;
            }
        b->paint = true;
        return true;
    }
    if (def_is(k, "common")) return flag(l, &d->common);
    if (def_is(k, "unique")) return flag(l, &d->unique);
    // Точки клинка (st) и контура пластины (pt, bumps) — к последней части формы
    bool st = def_is(k, "st"), pt = def_is(k, "pt"), bumps = def_is(k, "bumps");
    if (st || pt || bumps) {
        WeaponPart *p = d->parts && b->shape ? &s_parts[d->part + d->parts - 1] : NULL;
        if (!p || p->kind != (st ? WP_BLADE : WP_PLATE)) {
            def_error(l, st ? "st — после blade" : "pt и bumps — после plate");
            return false;
        }
        f32 a[DEF_TOKENS];
        u32 na = l->n - 1;
        if (st ? na != 6 : pt ? !na || na % 2 : na != 5) {
            def_error(l, st ? "st: ось, обух, лезвие, толщина, толщина обуха, ребро" :
                         pt ? "pt: пары «ось к_лезвию»" : "bumps: от до к_лезвию сколько выступ");
            return false;
        }
        for (u32 i = 0; i < na; i++)
            if (!def_num(&l->tok[1 + i], &a[i])) {
                def_error(l, "не число: %.*s", (int)l->tok[1 + i].n, l->tok[1 + i].s);
                return false;
            }
        // Зубцы: n полукругов от y0 до y1 по линии z, выступ r (меньше нуля — выемки)
        f32 gen[2 * 8 * 5];
        if (bumps) {
            u32 nb = (u32)a[3], m = 0;
            if (nb < 1 || nb > 8) {
                def_error(l, "зубцов от 1 до 8");
                return false;
            }
            f32 step = (a[1] - a[0]) / (f32)nb;
            for (u32 i = 0; i < nb; i++)
                for (u32 j = 0; j < 5; j++) {
                    f32 ang = PI_F * (1.0f - (f32)j / 4.0f), c = a[0] + ((f32)i + 0.5f) * step;
                    gen[m++] = c + cosf(ang) * step * 0.5f;
                    gen[m++] = a[2] + sinf(ang) * a[4];
                }
            na = m;
        }
        const f32 *src = bumps ? gen : a;
        if ((u32)(p->pt - d->part / WEAPON_PARTS * WEAPON_PTS) + p->npt + na > WEAPON_PTS) {
            def_error(l, "точек формы больше %u", WEAPON_PTS);
            return false;
        }
        memcpy(&s_pts[p->pt + p->npt], src, sizeof(f32) * na);
        p->npt = (u16)(p->npt + na);
        return true;
    }
    // Дол клинка: к последней части blade
    if (def_is(k, "fuller")) {
        WeaponPart *p = d->parts && b->shape ? &s_parts[d->part + d->parts - 1] : NULL;
        if (!p || p->kind != WP_BLADE) {
            def_error(l, "fuller — после blade");
            return false;
        }
        if (!nums(l, 4, v)) return false;
        memcpy(&p->v[2], v, sizeof(f32) * 4);
        return true;
    }
    // Части формы: слово и от shape_min до shape_max чисел, недостающие — 0
    static const char *const shape[] = { "rod", "box", "blade", "plate", "hole", "wrap", "knob" };
    static const u32 shape_min[] = { 4, 6, 0, 1, 4, 4, 4 }, shape_n[] = { 4, 6, 2, 1, 4, 6, 6 };
    i32 sk = def_word(k, shape, ARRAY_COUNT(shape));
    if (sk >= 0) {
        u32 na = l->n - 1;
        if (na < shape_min[sk] || na > shape_n[sk]) {
            def_error(l, "у %.*s нужно значений: от %u до %u", (int)k->n, k->s, shape_min[sk], shape_n[sk]);
            return false;
        }
        memset(v, 0, sizeof v);
        for (u32 i = 0; i < na; i++)
            if (!def_num(&l->tok[1 + i], &v[i])) {
                def_error(l, "не число: %.*s", (int)l->tok[1 + i].n, l->tok[1 + i].s);
                return false;
            }
        if (!b->paint) {
            def_error(l, "перед частями формы нужен paint");
            return false;
        }
        // Первая часть в записи заменяет форму родителя или встроенную целиком
        if (!b->shape) d->parts = 0;
        b->shape = true;
        if (d->parts == WEAPON_PARTS) {
            def_error(l, "частей формы больше %u", WEAPON_PARTS);
            return false;
        }
        if (sk == WP_HOLE && (!d->parts || s_parts[d->part + d->parts - 1].kind < WP_PLATE ||
                              s_parts[d->part + d->parts - 1].kind > WP_HOLE)) {
            def_error(l, "hole — сразу после plate или другой hole");
            return false;
        }
        // Точки идут подряд за точками прежних частей этого оружия
        u32 at = b->id * WEAPON_PTS;
        for (u32 i = 0; i < d->parts; i++) {
            const WeaponPart *q = &s_parts[d->part + i];
            if (q->npt && q->pt + q->npt > at) at = q->pt + q->npt;
        }
        WeaponPart *p = &s_parts[d->part + d->parts++];
        *p = (WeaponPart){ .kind = (u8)sk, .mat = b->mat, .pt = at };
        memcpy(p->color, b->color, sizeof p->color);
        memcpy(p->v, v, sizeof p->v);
        return true;
    }
    def_error(l, "у оружия нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool armor_field(const DefLine *l, Block *b, Arena *mem)
{
    ArmorDef *d = &s_armors[b->id];
    const DefTok *k = &l->tok[0];
    f32 v[3];
    if (def_is(k, "name")) return text(l, mem, &d->name);
    if (def_is(k, "slot")) {
        if (!slot(l, &d->slot)) return false;
        if (d->slot != SLOT_NONE) return true;
        def_error(l, "броня надевается в слот");
        return false;
    }
    if (def_is(k, "cover")) {
        // Список целиком: не названные части не закрыты
        if (l->n % 2 == 0) {
            def_error(l, "cover: пары «часть доля»");
            return false;
        }
        memset(d->cover, 0, sizeof d->cover);
        for (u32 i = 1; i < l->n; i += 2) {
            u8 p;
            if (!word(l, i, g_part_word, PART_COUNT, &p)) return false;
            if (!def_num(&l->tok[i + 1], &d->cover[p])) {
                def_error(l, "не число: %.*s", (int)l->tok[i + 1].n, l->tok[i + 1].s);
                return false;
            }
        }
        return true;
    }
    if (def_is(k, "resist")) {
        if (!nums(l, 3, v)) return false;
        d->res_cut = v[0], d->res_blunt = v[1], d->res_pierce = v[2];
        return true;
    }
    if (def_is(k, "weight")) return num(l, &d->weight);
    if (def_is(k, "dodge_penalty")) return num(l, &d->dodge_pen);
    if (def_is(k, "size")) return size2(l, &d->size_w, &d->size_h);
    if (def_is(k, "look")) return look(l, &d->look);
    if (def_is(k, "hides")) return hides(l, &d->hides);
    if (def_is(k, "metal")) return flag(l, &d->metal);
    def_error(l, "у брони нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool goods_field(const DefLine *l, Block *b, Arena *mem)
{
    GoodsDef *d = &s_goods[b->id];
    const DefTok *k = &l->tok[0];
    u32 n;
    if (def_is(k, "name")) return text(l, mem, &d->name);
    if (def_is(k, "desc")) return text(l, mem, &d->desc);
    if (def_is(k, "size")) return size2(l, &d->w, &d->h);
    if (def_is(k, "stack")) {
        if (!args(l, 1) || !def_uint(&l->tok[1], &n) || !n || n > 255) {
            def_error(l, "stack от 1 до 255");
            return false;
        }
        d->stack = (u8)n;
        return true;
    }
    if (def_is(k, "slot")) return slot(l, &d->slot);
    if (def_is(k, "weight")) return num(l, &d->weight);
    if (def_is(k, "carry")) return num(l, &d->carry);
    if (def_is(k, "look")) return look(l, &d->look);
    if (def_is(k, "hides")) return hides(l, &d->hides);
    def_error(l, "у вещи нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool attack_field(const DefLine *l, Block *b, Arena *mem)
{
    AttackDef *d = &g_attacks[b->id];
    const DefTok *k = &l->tok[0];
    if (def_is(k, "name")) return text(l, mem, &g_attack_name[b->id]);
    if (def_is(k, "damage")) return num(l, &d->dmg_mul);
    if (def_is(k, "windup")) return num(l, &d->windup_mul);
    if (def_is(k, "reach")) return num(l, &d->reach_add);
    if (def_is(k, "arc")) return num(l, &d->arc_deg);
    def_error(l, "у удара нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool quality_field(const DefLine *l, Block *b, Arena *mem)
{
    const DefTok *k = &l->tok[0];
    if (def_is(k, "name")) return text(l, mem, &g_quality_name[b->id]);
    if (def_is(k, "mul")) return num(l, &g_quality_mul[b->id]);
    def_error(l, "у качества нет поля %.*s", (int)k->n, k->s);
    return false;
}

// Имя записи: латиница в нижнем регистре, цифры и _
static bool id_ok(const DefTok *t)
{
    if (t->quoted || !t->n || t->n >= ID_MAX) return false;
    for (u32 i = 0; i < t->n; i++) {
        char c = t->s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

// Заголовок записи: новая — с пустыми полями, прежняя — правится дальше по полям
static bool header(const DefLine *l, Block *b, Arena *mem)
{
    static const char *const kinds[] = { "weapon", "armor", "goods", "attack", "quality" };
    i32 kind = def_word(&l->tok[0], kinds, ARRAY_COUNT(kinds));
    *b = (Block){ .kind = kind };
    if (l->n != 2 || !id_ok(&l->tok[1])) {
        def_error(l, "запись: «%s имя», имя — латиница, цифры и _ короче %u", kinds[kind], ID_MAX);
        return false;
    }
    const DefTok *t = &l->tok[1];
    if (kind == BK_ATTACK || kind == BK_QUALITY) {
        u8 w;
        if (!word(l, 1, kind == BK_ATTACK ? g_attack_word : g_quality_word, kind == BK_ATTACK ? ATK_COUNT : Q_COUNT, &w))
            return false;
        b->id = w;
        return true;
    }
    i32 at = find((u32)kind, t->s, t->n);
    if (at >= 0) {
        b->id = (u32)at;
        return true;
    }
    static const u32 cap[DK_COUNT] = { WEAPON_CAP, ARMOR_CAP, GOODS_CAP };
    if (count_of((u32)kind) == cap[kind]) {
        def_error(l, "записей %s больше %u", kinds[kind], cap[kind]);
        return false;
    }
    const char *id = def_copy(mem, t);
    if (!id) return false;
    switch (kind) {
    case DK_WEAPON:
        b->id = g_weapon_count++;
        s_weapons[b->id] = (WeaponDef){ .id = id, .part = (u16)(b->id * WEAPON_PARTS) };
        break;
    case DK_ARMOR:
        b->id = g_armor_count++;
        s_armors[b->id] = (ArmorDef){ .id = id, .slot = SLOT_NONE, .look = GEAR_NONE };
        break;
    default:
        b->id = g_goods_count++;
        s_goods[b->id] = (GoodsDef){ .id = id, .slot = SLOT_NONE, .stack = 1, .look = GEAR_NONE };
        break;
    }
    return true;
}

// parent: запись берёт все поля другой (кроме имени), дальше правит свои. Только первой строкой записи
static bool parent(const DefLine *l, Block *b)
{
    if (b->kind >= DK_COUNT || b->lines != 1) {
        def_error(l, "parent — первой строкой записи оружия, брони или вещи");
        return false;
    }
    if (!args(l, 1)) return false;
    i32 p = find((u32)b->kind, l->tok[1].s, l->tok[1].n);
    if (p < 0 || (u32)p == b->id) {
        def_error(l, "родителя %.*s нет выше по файлам", (int)l->tok[1].n, l->tok[1].s);
        return false;
    }
    switch (b->kind) {
    case DK_WEAPON: {
        WeaponDef *d = &s_weapons[b->id];
        const char *id = d->id;
        u16 part = d->part;
        *d = s_weapons[p];
        d->id = id, d->part = part;
        memcpy(&s_parts[part], &s_parts[s_weapons[p].part], sizeof(WeaponPart) * d->parts);
        // Точки родителя — в свой участок, номера частей сдвигаются туда же
        memcpy(&s_pts[b->id * WEAPON_PTS], &s_pts[(u32)p * WEAPON_PTS], sizeof(f32) * WEAPON_PTS);
        for (u32 i = 0; i < d->parts; i++) s_parts[part + i].pt = s_parts[part + i].pt - (u32)p * WEAPON_PTS + b->id * WEAPON_PTS;
        break;
    }
    case DK_ARMOR: {
        const char *id = s_armors[b->id].id;
        s_armors[b->id] = s_armors[p];
        s_armors[b->id].id = id;
        break;
    }
    default: {
        const char *id = s_goods[b->id].id;
        s_goods[b->id] = s_goods[p];
        s_goods[b->id].id = id;
        break;
    }
    }
    return true;
}

// После всех файлов: у каждой записи есть всё, без чего она не работает
static bool check(void)
{
    bool ok = true;
    for (u32 i = 0; i < g_weapon_count; i++) {
        const WeaponDef *d = &s_weapons[i];
        if (!d->name || !d->attacks || !d->max_targets || !d->size_w || !d->parts || d->length <= 0.0f ||
            d->speed <= 0.0f) {
            log_error("ОПИСАНИЯ: оружию %s не хватает name, attacks, targets, size, length, speed или формы", d->id);
            ok = false;
        }
    }
    for (u32 i = 0; i < g_armor_count; i++) {
        const ArmorDef *d = &s_armors[i];
        if (!d->name || d->slot == SLOT_NONE || !d->size_w) {
            log_error("ОПИСАНИЯ: броне %s не хватает name, slot или size", d->id);
            ok = false;
        }
    }
    for (u32 i = 0; i < g_goods_count; i++) {
        const GoodsDef *d = &s_goods[i];
        if (!d->name || !d->w || !d->desc) {
            log_error("ОПИСАНИЯ: вещи %s не хватает name, size или desc", d->id);
            ok = false;
        }
    }
    for (u32 i = 0; i < ATK_COUNT; i++)
        if (!g_attack_name[i] || g_attacks[i].windup_mul <= 0.0f) {
            log_error("ОПИСАНИЯ: удара %s нет или нет windup", g_attack_word[i]);
            ok = false;
        }
    for (u32 i = 0; i < Q_COUNT; i++)
        if (!g_quality_name[i]) {
            log_error("ОПИСАНИЯ: качества %s нет", g_quality_word[i]);
            ok = false;
        }
    for (u32 k = 0; k < DK_COUNT; k++)
        for (u32 i = 0; i < ARRAY_COUNT(g_need[k]) && g_need[k][i]; i++)
            if (find(k, g_need[k][i], (u32)strlen(g_need[k][i])) < 0) {
                log_error("ОПИСАНИЯ: нет записи %s, её ставит код (отряд, арена, сцены)", g_need[k][i]);
                ok = false;
            }
    return ok;
}

bool defs_load(Arena *mem, const char *base_dir, const char *mods_dir)
{
    g_weapon_count = g_armor_count = g_goods_count = 0;
    g_weapons = s_weapons;
    g_weapon_parts = s_parts;
    g_weapon_pts = s_pts;
    g_armors = s_armors;
    g_goods = s_goods;
    if (!deftext_open(&s_text, mem, base_dir, mods_dir)) return false;
    DefLine l;
    Block b = { .kind = -1 };
    bool bad = false, ok = true;
    while (deftext_next(&s_text, &l, &bad)) {
        static const char *const kinds[] = { "weapon", "armor", "goods", "attack", "quality" };
        if (def_word(&l.tok[0], kinds, ARRAY_COUNT(kinds)) >= 0) {
            ok = header(&l, &b, mem);
        } else if (b.kind < 0) {
            def_error(&l, "поле вне записи: сначала weapon, armor, goods, attack или quality");
            ok = false;
        } else {
            b.lines++;
            if (def_is(&l.tok[0], "parent")) ok = parent(&l, &b);
            else switch (b.kind) {
                case DK_WEAPON: ok = weapon_field(&l, &b, mem); break;
                case DK_ARMOR: ok = armor_field(&l, &b, mem); break;
                case DK_GOODS: ok = goods_field(&l, &b, mem); break;
                case BK_ATTACK: ok = attack_field(&l, &b, mem); break;
                default: ok = quality_field(&l, &b, mem); break;
                }
        }
        if (!ok) break;
    }
    if (bad || !ok || !check()) return false;
    g_weapon_pool_count = 0;
    for (u32 i = 0; i < g_weapon_count; i++)
        if (s_weapons[i].common) g_weapon_pool[g_weapon_pool_count++] = (u8)i;
    if (!g_weapon_pool_count) {
        log_error("ОПИСАНИЯ: нет оружия с common yes — отрядам и арене нечего дать");
        return false;
    }
    // Что закрывает сетка: от всех записей с этим видом
    memset(g_gear_hides, 0, sizeof g_gear_hides);
    for (u32 i = 0; i < g_armor_count; i++)
        if (s_armors[i].look < GEAR_COUNT) g_gear_hides[s_armors[i].look] |= s_armors[i].hides;
    for (u32 i = 0; i < g_goods_count; i++)
        if (s_goods[i].look < GEAR_COUNT) g_gear_hides[s_goods[i].look] |= s_goods[i].hides;
    log_info("ОПИСАНИЯ: оружия %u, брони %u, вещей %u, файлов %u, модов %u", g_weapon_count, g_armor_count, g_goods_count,
             s_text.files, s_text.mods);
    return true;
}

void defs_ser(Ser *s, DefMap *m)
{
    m->same = true;
    for (u32 k = 0; k < DK_COUNT; k++)
        for (u32 i = 0; i < 256; i++) m->to[k][i] = (u8)i;
    // Сумма мира от имён не зависит: одинаковый мир с другим порядком описаний — та же сумма после пересчёта
    if (s->mode == SER_HASH) return;
    for (u32 k = 0; k < DK_COUNT; k++) {
        u32 n = count_of(k);
        ser_count(s, &n, 255);
        for (u32 i = 0; i < n && !s->bad; i++) {
            char name[ID_MAX];
            u8 len = 0;
            if (s->mode == SER_SAVE) {
                len = (u8)strlen(id_of(k, i));
                memcpy(name, id_of(k, i), len);
            }
            SER(s, len);
            ser_check(s, len < ID_MAX);
            if (s->bad) return;
            ser_bytes(s, name, len);
            if (s->mode != SER_LOAD || s->bad) continue;
            i32 now = find(k, name, len);
            if (now < 0) {
                log_error("СОХРАНЕНИЕ: в мире есть %.*s, а в описаниях такой записи нет (мод убран?)", (int)len, name);
                s->refused = true;
                continue;
            }
            m->to[k][i] = (u8)now;
            m->same &= (u32)now == i;
        }
    }
}

static u8 to_armor(const DefMap *m, u8 a) { return a == ARMOR_NONE ? a : m->to[DK_ARMOR][a]; }

void defs_remap_desc(FighterDesc *d, const DefMap *m)
{
    d->weapon = m->to[DK_WEAPON][d->weapon];
    for (u32 s = 0; s < SLOT_COUNT; s++) d->armor[s] = to_armor(m, d->armor[s]);
}

static void remap_item(Item *it, const DefMap *m)
{
    if (it->kind == IK_WEAPON) it->id = m->to[DK_WEAPON][it->id];
    else if (it->kind == IK_ARMOR) it->id = m->to[DK_ARMOR][it->id];
    else if (it->kind == IK_GOODS) it->id = m->to[DK_GOODS][it->id];
}

void defs_remap_sheet(CharSheet *c, const DefMap *m)
{
    defs_remap_desc(&c->d, m);
    for (u32 s = 0; s <= SLOT_COUNT; s++) remap_item(&c->equip[s], m);
    for (u32 i = 0; i < c->inv.n && i < INV_MAX; i++) remap_item(&c->inv.item[i], m);
}

void defs_remap_combat(CombatWorld *w, const DefMap *m)
{
    for (u32 i = 0; i < w->count; i++) {
        w->weapon[i] = m->to[DK_WEAPON][w->weapon[i]];
        for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s][i] = to_armor(m, w->armor[s][i]);
    }
}
