#include "defs.h"

#include <stddef.h>
#include <string.h>

#include "../sim/look.h"
#include "../core/mesh.h"
#include "../sim/combat_tune.h"
#include "deftext.h"
#include "material.h"
#include "objects.h"
#include "bodydef.h"

#define ARMOR_CAP ARMOR_NONE  // 255 — «пусто»
#define COMP_CAP 32           // свет, ёмкость, тело
#define ID_MAX 32
#define HIDES_MAX 8

static ItemDef    s_items[ITEM_CAP];
static WeaponDef  s_weapons[WEAPON_CAP];
static WeaponPart s_parts[WEAPON_CAP * WEAPON_PARTS];
static f32        s_pts[WEAPON_CAP * WEAPON_PTS];
static ArmorDef   s_armors[ARMOR_CAP];
static LightDef   s_lights[COMP_CAP];
static ContainerDef s_containers[COMP_CAP];
static PhysCompDef s_phys[COMP_CAP];
static u32        s_light_count, s_container_count, s_phys_count;
static DefText    s_text;
static MatDef     s_mats[MATDEF_CAP];
// Часть формы оружия красится материалом по имени (paint имя): цвет берётся после всех файлов, мод материала
// перекрашивает и оружие. NULL — свой цвет в строке paint
static const char *s_part_mat[WEAPON_CAP * WEAPON_PARTS];
// Имена, которые решаются после всех файлов: материал вещи и сетки, которые она закрывает
static const char *s_item_mat[ITEM_CAP];
static const char *s_item_hides[ITEM_CAP][HIDES_MAX];

MatOf g_mat;
const MatDef *g_mats = s_mats;
u32 g_mat_count;

// Слова файлов. Порядок — как перечисления в коде
static const char *const g_class_word[WC_COUNT] = { "blade", "cleaver", "blunt", "polearm", "heavy" };
static const char *const g_attack_word[ATK_COUNT] = { "overhead", "swing", "thrust" };
static const char *const g_quality_word[Q_COUNT] = { "junk", "worn", "sound", "master", "ancient" };
static const char *const g_slot_word[SLOT_COUNT] = { "head", "face", "shirt", "armor", "legs",
                                                     "feet", "hands", "back", "belt", "pants" };
static const char *const g_part_word[PART_COUNT] = { "head", "chest", "stomach", "arm_l", "arm_r", "leg_l", "leg_r" };
static const char *const g_mat_word[] = { "concrete", "rust", "sand", "cloth", "leather", "glyph", "emissive", "skin", "metal",
                                          "terrain", "rock", "water", "bark", "leaf", "grass", "steel" };
static const char *const g_impact_word[] = { "metal", "wood", "stone", "soft" };
_Static_assert(ARRAY_COUNT(g_impact_word) == IMP_COUNT, "g_impact_word по номерам IMP_*");
_Static_assert(MAT_METAL == 8 && MAT_GLYPH == 5 && MAT_STEEL == 15, "g_mat_word по номерам MAT_*");

// Имена, которые ставит сам код: свой отряд, наборы арены, тестовые сцены. Переедут в файлы с фракциями.
// Оружие и броня — вещи с этим компонентом
enum { NEED_WEAPON, NEED_ARMOR, NEED_ITEM, NEED_COUNT };
static const char *const g_need[NEED_COUNT][16] = {
    [NEED_WEAPON] = { "ressora", "sekach", "bagor", "balka" },
    [NEED_ARMOR] = { "rags", "jacket", "quilt", "plate", "bucket", "greaves", "pants" },
    [NEED_ITEM] = { "water", "rusk", "meat", "bandage", "scrap", "part", "fuel", "token", "wrap", "boots", "mitts", "pack", "belt", "torch" },
};

// Номер вещи по имени; 0 — запись «ничего», по имени не находится
static i32 find_item(const char *s, u32 n)
{
    for (u32 i = 1; i < g_item_count; i++) {
        const char *id = s_items[i].id;
        if (strlen(id) == n && !memcmp(id, s, n)) return (i32)i;
    }
    return -1;
}

static i32 find_mat(const char *s, u32 n)
{
    for (u32 i = 0; i < g_mat_count; i++)
        if (strlen(s_mats[i].id) == n && !memcmp(s_mats[i].id, s, n)) return (i32)i;
    return -1;
}

static i32 named(const char *id, u32 comp_of)
{
    i32 i = find_item(id, (u32)strlen(id));
    u8 c = i < 0 ? COMP_NONE : comp_of == NEED_WEAPON ? s_items[i].weapon : comp_of == NEED_ARMOR ? s_items[i].armor : 0;
    if (c == COMP_NONE) {
        log_error("ОПИСАНИЯ: нет записи %s%s", id, comp_of == NEED_WEAPON ? " с оружием" : comp_of == NEED_ARMOR ? " с бронёй" : "");
        return -1;
    }
    return comp_of == NEED_ITEM ? i : c;
}

u8 def_weapon(const char *id) { i32 c = named(id, NEED_WEAPON); return c < 0 ? 0 : (u8)c; }
u8 def_armor(const char *id) { i32 c = named(id, NEED_ARMOR); return c < 0 ? 0 : (u8)c; }
u16 def_item(const char *id) { i32 c = named(id, NEED_ITEM); return c < 0 ? ITEM_NONE : (u16)c; }

// Разбор одной строки поля: число слов и сами слова
typedef struct Block {
    i32 kind;   // BK_*; -1 — вне записи
    u32 id;
    u8  sect;   // SEC_*: чьи поля идут сейчас — вещи или её компонента
    u32 lines;  // строк после заголовка
    bool shape; // в этой записи уже была форма: прежняя заменена
    bool paint;
    bool tone;  // в этой записи материала уже был color: набор родителя заменён
    u8  mat;
    f32 color[3];
    const char *mat_ref;  // paint по имени материала
} Block;

enum { BK_ITEM, BK_ATTACK, BK_QUALITY, BK_MATERIAL, BK_TUNING, BK_BODY };
static const char *const g_kinds[] = { "item", "attack", "quality", "material", "tuning", "body" };
// Компоненты вещи: строка с этим словом открывает его поля
enum { SEC_ITEM, SEC_WEAPON, SEC_ARMOR, SEC_WEAR, SEC_LIGHT, SEC_CONTAINER, SEC_PHYS, SEC_COUNT };
static const char *const g_sect_word[SEC_COUNT] = { "", "weapon", "armor", "wear", "light", "container", "phys" };

// Числа боя: ключ — имя поля CombatTune. Каждое должно быть в файлах, мод правит любое
#define TUNE_KEY(n) #n,
#define TUNE_OFF(n) offsetof(CombatTune, n),
static const char *const g_tune_key[] = { COMBAT_TUNE(TUNE_KEY) };
static const size_t g_tune_off[] = { COMBAT_TUNE(TUNE_OFF) };
#undef TUNE_KEY
#undef TUNE_OFF
#define TUNE_COUNT ARRAY_COUNT(g_tune_key)
static bool s_tune_set[TUNE_COUNT];
static const char *const g_tuning_word[] = { "combat" };

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

static bool id_ok(const DefTok *t);

// Сетка на теле по имени: новое имя дописывается в таблицу сеток, файл находит вид
static bool look(const DefLine *l, Arena *mem, u8 *out)
{
    if (!args(l, 1)) return false;
    const DefTok *t = &l->tok[1];
    if (def_is(t, "none")) {
        *out = GEAR_NONE;
        return true;
    }
    if (!id_ok(t)) {
        def_error(l, "look: имя сетки — латиница, цифры и _");
        return false;
    }
    for (u32 i = 0; i < g_gear_count; i++)
        if (strlen(g_gear_file[i]) == t->n && !memcmp(g_gear_file[i], t->s, t->n)) {
            *out = (u8)i;
            return true;
        }
    if (g_gear_count == GEAR_CAP) {
        def_error(l, "сеток одежды больше %u", GEAR_CAP);
        return false;
    }
    g_gear_file[g_gear_count] = def_copy(mem, t);
    if (!g_gear_file[g_gear_count]) return false;
    *out = (u8)g_gear_count++;
    return true;
}

// Что закрывает: имена сеток, решаются после всех файлов; hair — причёска
static bool hides(const DefLine *l, Arena *mem, u32 item)
{
    if (l->n - 1 > HIDES_MAX) {
        def_error(l, "hides: не больше %u имён", HIDES_MAX);
        return false;
    }
    memset(s_item_hides[item], 0, sizeof s_item_hides[item]);
    for (u32 i = 1; i < l->n; i++) {
        if (!id_ok(&l->tok[i])) {
            def_error(l, "hides: имена сеток или hair");
            return false;
        }
        s_item_hides[item][i - 1] = def_copy(mem, &l->tok[i]);
        if (!s_item_hides[item][i - 1]) return false;
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
    u32 wi = s_items[b->id].weapon;
    WeaponDef *d = &s_weapons[wi];
    const DefTok *k = &l->tok[0];
    f32 v[6];
    u32 n;
    if (def_is(k, "model")) return text(l, mem, &d->model);
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
    if (def_is(k, "speed")) return num(l, &d->speed);
    if (def_is(k, "arc")) return num(l, &d->arc_deg);
    if (def_is(k, "knock")) return num(l, &d->knock);
    if (def_is(k, "pen")) return num(l, &d->pen);
    if (def_is(k, "sound")) {
        // Голос по имени: какой это звук, решает вид (view/sfx.c)
        if (l->n < 2 || l->n > 3 || !id_ok(&l->tok[1]) || (l->n == 3 && !id_ok(&l->tok[2]))) {
            def_error(l, "sound: одно или два имени");
            return false;
        }
        d->sound = def_copy(mem, &l->tok[1]);
        d->sound_strike = l->n == 3 ? def_copy(mem, &l->tok[2]) : d->sound;
        return d->sound && d->sound_strike;
    }
    if (def_is(k, "paint")) {
        // paint имя — материал из materials.txt; paint поверхность r g b — свой цвет
        if (l->n == 2) {
            if (!id_ok(&l->tok[1])) {
                def_error(l, "paint: имя материала или «поверхность r g b»");
                return false;
            }
            b->mat_ref = def_copy(mem, &l->tok[1]);
            b->paint = true;
            return b->mat_ref != NULL;
        }
        b->mat_ref = NULL;
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
        u32 at = wi * WEAPON_PTS;
        for (u32 i = 0; i < d->parts; i++) {
            const WeaponPart *q = &s_parts[d->part + i];
            if (q->npt && q->pt + q->npt > at) at = q->pt + q->npt;
        }
        WeaponPart *p = &s_parts[d->part + d->parts++];
        *p = (WeaponPart){ .kind = (u8)sk, .mat = b->mat, .pt = at };
        memcpy(p->color, b->color, sizeof p->color);
        s_part_mat[d->part + d->parts - 1] = b->mat_ref;
        memcpy(p->v, v, sizeof p->v);
        return true;
    }
    def_error(l, "у оружия нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool armor_field(const DefLine *l, Block *b)
{
    ArmorDef *d = &s_armors[s_items[b->id].armor];
    const DefTok *k = &l->tok[0];
    f32 v[3];
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
    if (def_is(k, "dodge_penalty")) return num(l, &d->dodge_pen);
    def_error(l, "у брони нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool light_field(const DefLine *l, Block *b)
{
    LightDef *d = &s_lights[s_items[b->id].light];
    const DefTok *k = &l->tok[0];
    f32 v[3];
    if (def_is(k, "color")) {
        if (!nums(l, 3, v)) return false;
        d->color = v3(v[0], v[1], v[2]);
        return true;
    }
    if (def_is(k, "power")) return num(l, &d->power);
    if (def_is(k, "radius")) return num(l, &d->radius);
    def_error(l, "у света нет поля %.*s", (int)k->n, k->s);
    return false;
}

static bool container_field(const DefLine *l, Block *b)
{
    ContainerDef *d = &s_containers[s_items[b->id].container];
    if (def_is(&l->tok[0], "grid")) return size2(l, &d->w, &d->h);
    def_error(l, "у ёмкости нет поля %.*s", (int)l->tok[0].n, l->tok[0].s);
    return false;
}

static bool phys_field(const DefLine *l, Block *b)
{
    PhysCompDef *d = &s_phys[s_items[b->id].phys];
    f32 v[3];
    if (def_is(&l->tok[0], "box")) {
        if (!nums(l, 3, v)) return false;
        d->half = v3(v[0], v[1], v[2]);
        return true;
    }
    def_error(l, "у тела нет поля %.*s", (int)l->tok[0].n, l->tok[0].s);
    return false;
}

// Компонент вещи: новый — пустой, свой номер в таблице компонента
static bool comp_new(const DefLine *l, u8 *slot, u32 *count, u32 cap, const char *what)
{
    if (*slot != COMP_NONE) return true;
    if (*count == cap) {
        def_error(l, "%s больше %u", what, cap);
        return false;
    }
    *slot = (u8)(*count)++;
    return true;
}

// Строка компонента: weapon, armor, wear слот, light, container, phys — дальше его поля
static bool section(const DefLine *l, Block *b, u8 sect)
{
    ItemDef *d = &s_items[b->id];
    b->sect = sect;
    switch (sect) {
    case SEC_WEAR:
        if (!slot(l, &d->slot)) return false;
        return true;
    case SEC_WEAPON:
        if (d->weapon == COMP_NONE) {
            if (!comp_new(l, &d->weapon, &g_weapon_count, WEAPON_CAP, "оружия")) return false;
            s_weapons[d->weapon] = (WeaponDef){ .id = d->id, .item = (u16)b->id, .part = (u16)(d->weapon * WEAPON_PARTS) };
        }
        return true;
    case SEC_ARMOR:
        if (d->armor == COMP_NONE) {
            if (!comp_new(l, &d->armor, &g_armor_count, ARMOR_CAP, "брони")) return false;
            s_armors[d->armor] = (ArmorDef){ .id = d->id, .item = (u16)b->id };
        }
        return true;
    case SEC_LIGHT:
        if (d->light == COMP_NONE) {
            if (!comp_new(l, &d->light, &s_light_count, COMP_CAP, "света")) return false;
            s_lights[d->light] = (LightDef){ 0 };
        }
        return true;
    case SEC_CONTAINER:
        if (d->container == COMP_NONE) {
            if (!comp_new(l, &d->container, &s_container_count, COMP_CAP, "ёмкостей")) return false;
            s_containers[d->container] = (ContainerDef){ 0 };
        }
        return true;
    default:
        if (d->phys == COMP_NONE) {
            if (!comp_new(l, &d->phys, &s_phys_count, COMP_CAP, "тел")) return false;
            s_phys[d->phys] = (PhysCompDef){ 0 };
        }
        return true;
    }
}

// Поля вещи: общие — в любом месте записи, остальные — у открытого компонента
static bool item_field(const DefLine *l, Block *b, Arena *mem)
{
    ItemDef *d = &s_items[b->id];
    const DefTok *k = &l->tok[0];
    u32 n;
    i32 sect = def_word(k, g_sect_word + 1, SEC_COUNT - 1);
    if (sect >= 0) return (sect + 1 == SEC_WEAR || args(l, 0)) && section(l, b, (u8)(sect + 1));
    if (def_is(k, "name")) return text(l, mem, &d->name);
    if (def_is(k, "desc")) return text(l, mem, &d->desc);
    if (def_is(k, "size")) return size2(l, &d->w, &d->h);
    if (def_is(k, "weight")) return num(l, &d->weight);
    if (def_is(k, "stack")) {
        if (!args(l, 1) || !def_uint(&l->tok[1], &n) || !n || n > 255) {
            def_error(l, "stack от 1 до 255");
            return false;
        }
        d->stack = (u8)n;
        return true;
    }
    if (def_is(k, "made_of")) {
        if (!args(l, 1) || !id_ok(&l->tok[1])) {
            def_error(l, "made_of: имя записи материала");
            return false;
        }
        s_item_mat[b->id] = def_copy(mem, &l->tok[1]);
        return s_item_mat[b->id] != NULL;
    }
    if (def_is(k, "look")) return look(l, mem, &d->look);
    if (def_is(k, "hides")) return hides(l, mem, b->id);
    switch (b->sect) {
    case SEC_WEAPON: return weapon_field(l, b, mem);
    case SEC_ARMOR: return armor_field(l, b);
    case SEC_WEAR:
        if (def_is(k, "carry")) return num(l, &d->carry);
        break;
    case SEC_LIGHT: return light_field(l, b);
    case SEC_CONTAINER: return container_field(l, b);
    case SEC_PHYS: return phys_field(l, b);
    default: break;
    }
    def_error(l, "у вещи нет поля %.*s%s", (int)k->n, k->s, b->sect == SEC_ITEM ? " (поля компонента — после строки weapon, armor, wear…)" : "");
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

static bool tuning_field(const DefLine *l)
{
    i32 k = def_word(&l->tok[0], g_tune_key, TUNE_COUNT);
    if (k < 0) {
        def_error(l, "у tuning combat нет числа %.*s", (int)l->tok[0].n, l->tok[0].s);
        return false;
    }
    s_tune_set[k] = true;
    return num(l, (f32 *)((char *)&g_tune + g_tune_off[k]));
}

static bool material_field(const DefLine *l, Block *b)
{
    MatDef *d = &s_mats[b->id];
    const DefTok *k = &l->tok[0];
    if (def_is(k, "surface")) return args(l, 1) && word(l, 1, g_mat_word, ARRAY_COUNT(g_mat_word), &d->surface);
    if (def_is(k, "impact")) return args(l, 1) && word(l, 1, g_impact_word, IMP_COUNT, &d->impact);
    if (def_is(k, "color")) {
        // Каждая строка — оттенок набора; первая в записи заменяет набор родителя
        f32 v[3];
        if (!nums(l, 3, v)) return false;
        if (!b->tone) d->tones = 0;
        b->tone = true;
        if (d->tones == MAT_TONES) {
            def_error(l, "оттенков больше %u", MAT_TONES);
            return false;
        }
        d->tone[d->tones++] = v3(v[0], v[1], v[2]);
        return true;
    }
    def_error(l, "у материала нет поля %.*s", (int)k->n, k->s);
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
    i32 kind = def_word(&l->tok[0], g_kinds, ARRAY_COUNT(g_kinds));
    *b = (Block){ .kind = kind };
    if (l->n != 2 || !id_ok(&l->tok[1]) || def_is(&l->tok[1], "none")) {
        def_error(l, "запись: «%s имя», имя — латиница, цифры и _ короче %u, не none", g_kinds[kind], ID_MAX);
        return false;
    }
    const DefTok *t = &l->tok[1];
    if (kind == BK_BODY) return bodydef_header(l);
    if (kind == BK_TUNING) {
        u8 w;
        if (!word(l, 1, g_tuning_word, ARRAY_COUNT(g_tuning_word), &w)) return false;
        b->id = w;
        return true;
    }
    if (kind == BK_ATTACK || kind == BK_QUALITY) {
        u8 w;
        if (!word(l, 1, kind == BK_ATTACK ? g_attack_word : g_quality_word, kind == BK_ATTACK ? ATK_COUNT : Q_COUNT, &w))
            return false;
        b->id = w;
        return true;
    }
    if (kind == BK_MATERIAL) {
        i32 at = find_mat(t->s, t->n);
        if (at >= 0) {
            b->id = (u32)at;
            return true;
        }
        if (g_mat_count == MATDEF_CAP) {
            def_error(l, "материалов больше %u", MATDEF_CAP);
            return false;
        }
        const char *id = def_copy(mem, t);
        if (!id) return false;
        b->id = g_mat_count++;
        s_mats[b->id] = (MatDef){ .id = id, .surface = 255, .impact = 255 };
        return true;
    }
    i32 at = find_item(t->s, t->n);
    if (at >= 0) {
        b->id = (u32)at;
        return true;
    }
    if (g_item_count == ITEM_CAP) {
        def_error(l, "вещей больше %u", ITEM_CAP);
        return false;
    }
    const char *id = def_copy(mem, t);
    if (!id) return false;
    b->id = g_item_count++;
    s_items[b->id] = (ItemDef){ .id = id, .slot = SLOT_NONE, .stack = 1, .look = GEAR_NONE, .weapon = COMP_NONE,
                                .armor = COMP_NONE, .light = COMP_NONE, .container = COMP_NONE, .phys = COMP_NONE };
    s_item_mat[b->id] = NULL;
    memset(s_item_hides[b->id], 0, sizeof s_item_hides[b->id]);
    return true;
}

// Оружие от родителя: свои части формы и точки в своём участке
static void weapon_copy(u32 to, u32 from)
{
    WeaponDef *d = &s_weapons[to];
    const char *id = d->id;
    u16 part = d->part, item = d->item;
    *d = s_weapons[from];
    d->id = id, d->part = part, d->item = item;
    memcpy(&s_parts[part], &s_parts[s_weapons[from].part], sizeof(WeaponPart) * d->parts);
    memcpy(&s_part_mat[part], &s_part_mat[s_weapons[from].part], sizeof(s_part_mat[0]) * d->parts);
    memcpy(&s_pts[to * WEAPON_PTS], &s_pts[from * WEAPON_PTS], sizeof(f32) * WEAPON_PTS);
    for (u32 i = 0; i < d->parts; i++) s_parts[part + i].pt = s_parts[part + i].pt - from * WEAPON_PTS + to * WEAPON_PTS;
}

// parent: запись берёт все поля другой (кроме имени) и копии её компонентов, дальше правит свои. Только первой строкой
static bool parent(const DefLine *l, Block *b)
{
    if ((b->kind != BK_ITEM && b->kind != BK_MATERIAL) || b->lines != 1) {
        def_error(l, "parent — первой строкой записи вещи или материала");
        return false;
    }
    if (!args(l, 1)) return false;
    i32 p = b->kind == BK_MATERIAL ? find_mat(l->tok[1].s, l->tok[1].n) : find_item(l->tok[1].s, l->tok[1].n);
    if (p < 0 || (u32)p == b->id) {
        def_error(l, "родителя %.*s нет выше по файлам", (int)l->tok[1].n, l->tok[1].s);
        return false;
    }
    if (b->kind == BK_MATERIAL) {
        const char *id = s_mats[b->id].id;
        s_mats[b->id] = s_mats[p];
        s_mats[b->id].id = id;
        return true;
    }
    ItemDef *d = &s_items[b->id];
    const ItemDef *pd = &s_items[p];
    ItemDef own = *d;
    *d = *pd;
    d->id = own.id;
    d->weapon = own.weapon, d->armor = own.armor, d->light = own.light, d->container = own.container, d->phys = own.phys;
    s_item_mat[b->id] = s_item_mat[p];
    memcpy(s_item_hides[b->id], s_item_hides[p], sizeof s_item_hides[p]);
    // Компоненты родителя — своими копиями
    if (pd->weapon != COMP_NONE) {
        if (!section(l, b, SEC_WEAPON)) return false;
        weapon_copy(d->weapon, pd->weapon);
    }
    if (pd->armor != COMP_NONE) {
        if (!section(l, b, SEC_ARMOR)) return false;
        const char *id = s_armors[d->armor].id;
        s_armors[d->armor] = s_armors[pd->armor];
        s_armors[d->armor].id = id, s_armors[d->armor].item = (u16)b->id;
    }
    if (pd->light != COMP_NONE) {
        if (!section(l, b, SEC_LIGHT)) return false;
        s_lights[d->light] = s_lights[pd->light];
    }
    if (pd->container != COMP_NONE) {
        if (!section(l, b, SEC_CONTAINER)) return false;
        s_containers[d->container] = s_containers[pd->container];
    }
    if (pd->phys != COMP_NONE) {
        if (!section(l, b, SEC_PHYS)) return false;
        s_phys[d->phys] = s_phys[pd->phys];
    }
    b->sect = SEC_ITEM;
    return true;
}

// После всех файлов: у каждой записи есть всё, без чего она не работает
static bool check(void)
{
    bool ok = true;
    for (u32 i = 1; i < g_item_count; i++) {
        ItemDef *d = &s_items[i];
        if (!d->name || !d->w || (!d->desc && d->weapon == COMP_NONE && d->armor == COMP_NONE)) {
            log_error("ОПИСАНИЯ: вещи %s не хватает name, size или desc (desc не нужен оружию и броне)", d->id);
            ok = false;
        }
        i32 m = s_item_mat[i] ? find_mat(s_item_mat[i], (u32)strlen(s_item_mat[i])) : -1;
        if (m < 0) {
            log_error("ОПИСАНИЯ: вещи %s нужен made_of — материал из materials.txt%s%s", d->id, s_item_mat[i] ? ", а нет " : "",
                      s_item_mat[i] ? s_item_mat[i] : "");
            ok = false;
        } else d->mat = (u8)m;
        d->hides = 0;
        for (u32 h = 0; h < HIDES_MAX && s_item_hides[i][h]; h++) {
            u8 g = gear_find(s_item_hides[i][h]);
            if (!strcmp(s_item_hides[i][h], "hair")) d->hides |= GEAR_HIDES_HAIR;
            else if (g != GEAR_NONE) d->hides |= 1u << g;
            else {
                log_error("ОПИСАНИЯ: вещь %s закрывает сетку %s, а её нет ни у одной вещи (look)", d->id, s_item_hides[i][h]);
                ok = false;
            }
        }
        // Бой читает своё в компонентах: имя, вес, слот
        if (d->weapon != COMP_NONE) {
            WeaponDef *w = &s_weapons[d->weapon];
            w->name = d->name, w->weight = d->weight;
            if (!w->attacks || !w->max_targets || !w->parts || w->length <= 0.0f || w->speed <= 0.0f || !w->sound) {
                log_error("ОПИСАНИЯ: оружию %s не хватает attacks, targets, length, speed, sound или формы", d->id);
                ok = false;
            }
        }
        if (d->armor != COMP_NONE) {
            ArmorDef *a = &s_armors[d->armor];
            a->name = d->name, a->weight = d->weight, a->slot = d->slot;
            if (d->slot == SLOT_NONE) {
                log_error("ОПИСАНИЯ: броня %s надевается в слот: нужна строка wear", d->id);
                ok = false;
            }
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
    for (u32 i = 0; i < TUNE_COUNT; i++)
        if (!s_tune_set[i]) {
            log_error("ОПИСАНИЯ: в tuning combat нет числа %s", g_tune_key[i]);
            ok = false;
        }
    for (u32 i = 0; i < g_mat_count; i++)
        if (s_mats[i].surface == 255 || s_mats[i].impact == 255 || !s_mats[i].tones) {
            log_error("ОПИСАНИЯ: материалу %s не хватает surface, impact или color", s_mats[i].id);
            ok = false;
        }
    // Материалы кода: имя поля g_mat = имя записи
#define MAT_NEED(n)                                                                                                    \
    {                                                                                                                  \
        i32 at = find_mat(#n, (u32)strlen(#n));                                                                        \
        if (at < 0) {                                                                                                  \
            log_error("ОПИСАНИЯ: нет материала %s, его берёт код", #n);                                              \
            ok = false;                                                                                                \
        } else g_mat.n = &s_mats[at];                                                                                  \
    }
    MAT_OF(MAT_NEED)
#undef MAT_NEED
    // Части оружия с материалом по имени: цвет и поверхность — после всех файлов
    for (u32 w = 0; w < g_weapon_count; w++)
        for (u32 i = 0; i < s_weapons[w].parts; i++) {
            u32 at = s_weapons[w].part + i;
            if (!s_part_mat[at]) continue;
            i32 m = find_mat(s_part_mat[at], (u32)strlen(s_part_mat[at]));
            if (m < 0) {
                log_error("ОПИСАНИЯ: оружие %s красится материалом %s, а его нет", s_weapons[w].id, s_part_mat[at]);
                ok = false;
                continue;
            }
            s_parts[at].mat = s_mats[m].surface;
            s_parts[at].color[0] = s_mats[m].tone[0].x;
            s_parts[at].color[1] = s_mats[m].tone[0].y;
            s_parts[at].color[2] = s_mats[m].tone[0].z;
        }
    for (u32 k = 0; k < NEED_COUNT; k++)
        for (u32 i = 0; i < ARRAY_COUNT(g_need[k]) && g_need[k][i]; i++)
            if (named(g_need[k][i], k) < 0) {
                log_error("ОПИСАНИЯ: записи %s ставит код (отряд, арена, сцены)", g_need[k][i]);
                ok = false;
            }
    return ok;
}

bool defs_load(Arena *mem, const char *base_dir, const char *mods_dir)
{
    g_weapon_count = g_armor_count = g_mat_count = g_gear_count = 0;
    s_light_count = s_container_count = s_phys_count = 0;
    objdefs_reset();
    memset(s_part_mat, 0, sizeof s_part_mat);
    memset(&g_mat, 0, sizeof g_mat);
    // Запись 0 — «ничего»: пустой слот, пустая стопка
    s_items[0] = (ItemDef){ .id = "none", .name = "", .slot = SLOT_NONE, .look = GEAR_NONE, .weapon = COMP_NONE,
                            .armor = COMP_NONE, .light = COMP_NONE, .container = COMP_NONE, .phys = COMP_NONE };
    s_item_mat[0] = NULL;
    g_item_count = 1;
    g_items = s_items;
    memset(s_tune_set, 0, sizeof s_tune_set);
    bodydef_reset();
    g_weapons = s_weapons;
    g_weapon_parts = s_parts;
    g_weapon_pts = s_pts;
    g_armors = s_armors;
    g_lights = s_lights;
    g_containers = s_containers;
    g_phys_comps = s_phys;
    if (!deftext_open(&s_text, mem, base_dir, mods_dir)) return false;
    DefLine l;
    Block b = { .kind = -1 };
    bool bad = false, ok = true;
    while (deftext_next(&s_text, &l, &bad)) {
        i32 obj = 0;
        if (def_word(&l.tok[0], g_kinds, ARRAY_COUNT(g_kinds)) >= 0) {
            objdefs_end();
            ok = header(&l, &b, mem);
        } else if ((obj = objdefs_line(&l, mem)) != 0) {
            // Объекты и заготовки (objects.c)
            ok = obj > 0;
            b.kind = -1;
        } else if (b.kind < 0) {
            def_error(&l, "поле вне записи: сначала item, attack, quality, material, tuning, body, object или prefab");
            ok = false;
        } else {
            b.lines++;
            if (def_is(&l.tok[0], "parent")) ok = parent(&l, &b);
            else switch (b.kind) {
                case BK_ITEM: ok = item_field(&l, &b, mem); break;
                case BK_ATTACK: ok = attack_field(&l, &b, mem); break;
                case BK_MATERIAL: ok = material_field(&l, &b); break;
                case BK_TUNING: ok = tuning_field(&l); break;
                case BK_BODY: ok = bodydef_field(&l, mem); break;
                default: ok = quality_field(&l, &b, mem); break;
                }
        }
        if (!ok) break;
    }
    if (bad || !ok || !check() || !objdefs_check() || !bodydef_check()) return false;
    g_weapon_pool_count = 0;
    for (u32 i = 0; i < g_weapon_count; i++)
        if (s_weapons[i].common) g_weapon_pool[g_weapon_pool_count++] = (u8)i;
    if (!g_weapon_pool_count) {
        log_error("ОПИСАНИЯ: нет оружия с common yes — отрядам и арене нечего дать");
        return false;
    }
    // Что закрывает сетка: от всех записей с этим видом
    memset(g_gear_hides, 0, sizeof g_gear_hides);
    for (u32 i = 1; i < g_item_count; i++)
        if (s_items[i].look < g_gear_count) g_gear_hides[s_items[i].look] |= s_items[i].hides;
    log_info("ОПИСАНИЯ: вещей %u (оружия %u, брони %u, света %u), сеток %u, материалов %u, объектов %u, заготовок %u, "
             "файлов %u, модов %u",
             g_item_count - 1, g_weapon_count, g_armor_count, s_light_count, g_gear_count, g_mat_count, g_object_count,
             g_prefab_count, s_text.files, s_text.mods);
    return true;
}

// Списки имён в сохранении: вещи, их компоненты оружия и брони (по имени вещи), материалы
static u32 count_of(u32 k)
{
    return k == DK_ITEM ? g_item_count - 1 : k == DK_WEAPON ? g_weapon_count : k == DK_ARMOR ? g_armor_count : g_mat_count;
}

static const char *id_of(u32 k, u32 i)
{
    return k == DK_ITEM ? s_items[i + 1].id : k == DK_WEAPON ? s_weapons[i].id : k == DK_ARMOR ? s_armors[i].id : s_mats[i].id;
}

static i32 find_now(u32 k, const char *s, u32 n)
{
    if (k == DK_MAT) return find_mat(s, n);
    i32 i = find_item(s, n);
    if (i < 0 || k == DK_ITEM) return i < 0 ? -1 : i - 1;
    u8 c = k == DK_WEAPON ? s_items[i].weapon : s_items[i].armor;
    return c == COMP_NONE ? -1 : c;
}

static const u32 g_map_cap[DK_COUNT] = { ITEM_CAP - 1, WEAPON_CAP, ARMOR_CAP, MATDEF_CAP };

void defs_ser(Ser *s, DefMap *m)
{
    m->same = true;
    for (u32 k = 0; k < DK_COUNT; k++)
        for (u32 i = 0; i < ITEM_CAP; i++) m->to[k][i] = (u16)i;
    // Сумма мира от имён не зависит: одинаковый мир с другим порядком описаний — та же сумма после пересчёта
    if (s->mode == SER_HASH) return;
    for (u32 k = 0; k < DK_COUNT; k++) {
        u32 n = count_of(k);
        ser_count(s, &n, g_map_cap[k]);
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
            i32 now = find_now(k, name, len);
            if (now < 0) {
                log_error("СОХРАНЕНИЕ: в мире есть %.*s, а в описаниях такой записи нет (мод убран?)", (int)len, name);
                s->refused = true;
                continue;
            }
            // Вещи — с 1: запись 0 «ничего» в списке не пишется
            u32 at = k == DK_ITEM ? i + 1 : i;
            m->to[k][at] = (u16)(k == DK_ITEM ? now + 1 : now);
            m->same &= m->to[k][at] == at;
        }
    }
}

static u8 to_armor(const DefMap *m, u8 a) { return a == ARMOR_NONE ? a : (u8)m->to[DK_ARMOR][a]; }

void defs_remap_desc(FighterDesc *d, const DefMap *m)
{
    d->weapon = (u8)m->to[DK_WEAPON][d->weapon];
    for (u32 s = 0; s < SLOT_COUNT; s++) d->armor[s] = to_armor(m, d->armor[s]);
}

void defs_remap_item(ItemStack *it, const DefMap *m)
{
    if (!item_live(*it)) return;
    it->def = m->to[DK_ITEM][it->def];
    it->mat = (u8)m->to[DK_MAT][it->mat];
}

void defs_remap_sheet(CharSheet *c, const DefMap *m)
{
    defs_remap_desc(&c->d, m);
    for (u32 s = 0; s <= SLOT_COUNT; s++) defs_remap_item(&c->equip[s], m);
    for (u32 i = 0; i < c->inv.n && i < INV_MAX; i++) defs_remap_item(&c->inv.item[i], m);
}

void defs_remap_combat(CombatWorld *w, const DefMap *m)
{
    for (u32 i = 0; i < w->count; i++) {
        w->weapon[i] = (u8)m->to[DK_WEAPON][w->weapon[i]];
        for (u32 s = 0; s < SLOT_COUNT; s++) w->armor[s][i] = to_armor(m, w->armor[s][i]);
    }
}
