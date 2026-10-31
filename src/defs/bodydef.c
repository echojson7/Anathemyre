#include "bodydef.h"

#include <string.h>

#include "../sim/body.h"

static const char *const g_body_word[BODY_CLASS_COUNT] = { "human" };
static const char *const g_role_word[BONE_COUNT] = {
    "root", "pelvis", "spine", "chest", "neck", "head",
    "clavicle_l", "upper_arm_l", "forearm_l", "hand_l", "clavicle_r", "upper_arm_r", "forearm_r", "hand_r",
    "thigh_l", "shin_l", "foot_l", "thigh_r", "shin_r", "foot_r",
};
static const char *const g_socket_word[SOCKET_COUNT] = { "grip", "grip2", "sheath", "back", "torch" };
static const char *const g_bpart_word[PART_COUNT] = { "head", "chest", "stomach", "arm_l", "arm_r", "leg_l", "leg_r" };
static const char *const g_joint_word[] = { "root", "ball", "hinge" };

// Что задано: поле, роль, крепление, часть. Списки hit и ragdoll: первая строка в записи заменяет список
static struct {
    bool height, bone[BONE_COUNT], socket[SOCKET_COUNT], part[PART_COUNT];
    bool hit_new, rag_new;  // в этой записи списка ещё не было
} s_set;

void bodydef_reset(void)
{
    memset(&s_set, 0, sizeof s_set);
    memset(&g_body, 0, sizeof g_body);
    memset(g_part_hp, 0, sizeof(f32) * PART_COUNT);
    memset(g_part_name, 0, sizeof(g_part_name[0]) * PART_COUNT);
}

bool bodydef_header(const DefLine *l)
{
    if (l->n != 2 || def_word(&l->tok[1], g_body_word, BODY_CLASS_COUNT) < 0) {
        def_error(l, "тело: «body human», другого пока нет");
        return false;
    }
    s_set.hit_new = s_set.rag_new = true;
    return true;
}

static bool want(const DefLine *l, u32 n)
{
    if (l->n == n + 1) return true;
    def_error(l, "у %.*s нужно значений: %u", (int)l->tok[0].n, l->tok[0].s, n);
    return false;
}

static bool nums_at(const DefLine *l, u32 at, u32 n, f32 *out)
{
    for (u32 i = 0; i < n; i++)
        if (!def_num(&l->tok[at + i], &out[i])) {
            def_error(l, "не число: %.*s", (int)l->tok[at + i].n, l->tok[at + i].s);
            return false;
        }
    return true;
}

static bool word_at(const DefLine *l, u32 at, const char *const *words, u32 count, u8 *out)
{
    i32 w = def_word(&l->tok[at], words, count);
    if (w < 0) {
        def_error(l, "неизвестное слово: %.*s", (int)l->tok[at].n, l->tok[at].s);
        return false;
    }
    *out = (u8)w;
    return true;
}

#define DEG_TO_RAD (PI_F / 180.0f)

bool bodydef_field(const DefLine *l, Arena *mem)
{
    const DefTok *k = &l->tok[0];
    BodyDef *b = &g_body;
    if (def_is(k, "height")) {
        s_set.height = true;
        return want(l, 1) && nums_at(l, 1, 1, &b->height);
    }
    if (def_is(k, "skeleton")) {
        if (!want(l, 1)) return false;
        b->skeleton = def_copy(mem, &l->tok[1]);
        return b->skeleton != NULL;
    }
    if (def_is(k, "bone")) {
        // bone <роль> <имя кости в .skel>
        u8 r;
        if (!want(l, 2) || !word_at(l, 1, g_role_word, BONE_COUNT, &r)) return false;
        if (l->tok[2].n >= sizeof b->bone_name[0]) {
            def_error(l, "имя кости длиннее %zu", sizeof b->bone_name[0] - 1);
            return false;
        }
        memset(b->bone_name[r], 0, sizeof b->bone_name[r]);
        memcpy(b->bone_name[r], l->tok[2].s, l->tok[2].n);
        s_set.bone[r] = true;
        return true;
    }
    if (def_is(k, "part")) {
        // part <часть> <HP> "<название>"
        u8 p;
        if (!want(l, 3) || !word_at(l, 1, g_bpart_word, PART_COUNT, &p) || !nums_at(l, 2, 1, &g_part_hp[p])) return false;
        if (!l->tok[3].quoted) {
            def_error(l, "название части — в кавычках");
            return false;
        }
        g_part_name[p] = def_copy(mem, &l->tok[3]);
        s_set.part[p] = true;
        return g_part_name[p] != NULL;
    }
    if (def_is(k, "socket")) {
        // socket <крепление> <кость> x y z — точка в позе привязки
        u8 s, bone;
        f32 v[3];
        if (!want(l, 5) || !word_at(l, 1, g_socket_word, SOCKET_COUNT, &s) || !word_at(l, 2, g_role_word, BONE_COUNT, &bone) ||
            !nums_at(l, 3, 3, v))
            return false;
        b->socket[s] = (SocketDef){ bone, v3(v[0], v[1], v[2]) };
        s_set.socket[s] = true;
        return true;
    }
    if (def_is(k, "hit")) {
        // hit <часть> <от> <до> ax ay az bx by bz r — концы капсулы: суставы со сдвигом
        if (s_set.hit_new) b->hits = 0;
        s_set.hit_new = false;
        u8 p, from, to;
        f32 v[7];
        if (!want(l, 10) || !word_at(l, 1, g_bpart_word, PART_COUNT, &p) || !word_at(l, 2, g_role_word, BONE_COUNT, &from) ||
            !word_at(l, 3, g_role_word, BONE_COUNT, &to) || !nums_at(l, 4, 7, v))
            return false;
        if (b->hits == BODY_MAX_HIT_CAPSULES) {
            def_error(l, "капсул больше %u", BODY_MAX_HIT_CAPSULES);
            return false;
        }
        b->hit[b->hits++] = (HitDef){ p, from, from, to, v3(v[0], v[1], v[2]), v3(v[3], v[4], v[5]), v[6] };
        return true;
    }
    if (def_is(k, "ragdoll")) {
        // ragdoll <от> <до> r масса — корень; дальше <родитель> ball конус кручение_от кручение_до | <родитель> hinge от до
        if (s_set.rag_new) b->rags = 0;
        s_set.rag_new = false;
        if (l->n != 5 && l->n != 9 && l->n != 10) {
            def_error(l, "ragdoll: от до r масса [родитель ball конус от до | родитель hinge от до]");
            return false;
        }
        if (b->rags == BODY_RAG_MAX) {
            def_error(l, "частей ragdoll больше %u", BODY_RAG_MAX);
            return false;
        }
        RagPart rp = { .joint = RAG_ROOT };
        f32 v[3] = { 0 };
        if (!word_at(l, 1, g_role_word, BONE_COUNT, &rp.bone) || !word_at(l, 2, g_role_word, BONE_COUNT, &rp.to) ||
            !nums_at(l, 3, 1, &rp.r) || !nums_at(l, 4, 1, &rp.mass))
            return false;
        if (l->n > 5) {
            u8 pb;
            if (!word_at(l, 5, g_role_word, BONE_COUNT, &pb) || !word_at(l, 6, g_joint_word, 3, &rp.joint)) return false;
            u32 need = rp.joint == RAG_BALL ? 10u : rp.joint == RAG_HINGE ? 9u : 0u;
            if (l->n != need) {
                def_error(l, "ragdoll: у ball — конус и кручение от и до, у hinge — сгиб от и до");
                return false;
            }
            if (!nums_at(l, 7, l->n - 7, v)) return false;
            // Родитель — часть выше по списку, у которой кость pb
            u32 i = 0;
            while (i < b->rags && b->rag[i].bone != pb) i++;
            if (i == b->rags) {
                def_error(l, "у части ragdoll нет родителя %s выше по списку", g_role_word[pb]);
                return false;
            }
            rp.parent = (u8)i;
            if (rp.joint == RAG_BALL) rp.cone = v[0] * DEG_TO_RAD, rp.lo = v[1] * DEG_TO_RAD, rp.hi = v[2] * DEG_TO_RAD;
            else rp.lo = v[0] * DEG_TO_RAD, rp.hi = v[1] * DEG_TO_RAD;
            if (rp.lo > rp.hi) {
                def_error(l, "предел сустава: «от» больше «до»");
                return false;
            }
        }
        b->rag[b->rags++] = rp;
        return true;
    }
    def_error(l, "у тела нет поля %.*s", (int)k->n, k->s);
    return false;
}

bool bodydef_check(void)
{
    const BodyDef *b = &g_body;
    bool ok = true;
    if (!s_set.height || b->height <= 0.0f || !b->skeleton || !b->hits || !b->rags) {
        log_error("ОПИСАНИЯ: телу human не хватает height, skeleton, hit или ragdoll");
        ok = false;
    }
    for (u32 i = 0; i < BONE_COUNT; i++)
        if (!s_set.bone[i]) {
            log_error("ОПИСАНИЯ: у тела human нет кости для роли %s", g_role_word[i]);
            ok = false;
        }
    for (u32 i = 0; i < SOCKET_COUNT; i++)
        if (!s_set.socket[i]) {
            log_error("ОПИСАНИЯ: у тела human нет крепления %s", g_socket_word[i]);
            ok = false;
        }
    for (u32 i = 0; i < PART_COUNT; i++)
        if (!s_set.part[i] || g_part_hp[i] <= 0.0f) {
            log_error("ОПИСАНИЯ: у тела human нет части %s или её HP", g_bpart_word[i]);
            ok = false;
        }
    // Ragdoll: корень один и первый, доли массы в сумме — всё тело
    f32 mass = 0.0f;
    for (u32 i = 0; i < b->rags; i++) {
        mass += b->rag[i].mass;
        if ((b->rag[i].joint == RAG_ROOT) != (i == 0)) {
            log_error("ОПИСАНИЯ: у ragdoll тела human корень — первая часть и только она");
            ok = false;
            break;
        }
    }
    if (b->rags && fabsf(mass - 1.0f) > 0.005f) {
        log_error("ОПИСАНИЯ: доли массы частей ragdoll тела human в сумме %.3f, нужно 1", (f64)mass);
        ok = false;
    }
    return ok;
}
