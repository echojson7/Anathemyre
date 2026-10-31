#include "body.h"

#include <stdio.h>
#include <string.h>

#include "../core/log.h"

// Стандартный скелет гуманоида: родители костей. Суставы читаются при старте из g_body.skeleton (body_load)
BoneDef g_body_bones[BONE_COUNT] = {
    [BONE_ROOT] = { -1, { 0 } },
    [BONE_PELVIS] = { BONE_ROOT, { 0 } },
    [BONE_SPINE] = { BONE_PELVIS, { 0 } },
    [BONE_CHEST] = { BONE_SPINE, { 0 } },
    [BONE_NECK] = { BONE_CHEST, { 0 } },
    [BONE_HEAD] = { BONE_NECK, { 0 } },
    [BONE_CLAVICLE_L] = { BONE_CHEST, { 0 } },
    [BONE_UPPER_ARM_L] = { BONE_CLAVICLE_L, { 0 } },
    [BONE_FOREARM_L] = { BONE_UPPER_ARM_L, { 0 } },
    [BONE_HAND_L] = { BONE_FOREARM_L, { 0 } },
    [BONE_CLAVICLE_R] = { BONE_CHEST, { 0 } },
    [BONE_UPPER_ARM_R] = { BONE_CLAVICLE_R, { 0 } },
    [BONE_FOREARM_R] = { BONE_UPPER_ARM_R, { 0 } },
    [BONE_HAND_R] = { BONE_FOREARM_R, { 0 } },
    [BONE_THIGH_L] = { BONE_PELVIS, { 0 } },
    [BONE_SHIN_L] = { BONE_THIGH_L, { 0 } },
    [BONE_FOOT_L] = { BONE_SHIN_L, { 0 } },
    [BONE_THIGH_R] = { BONE_PELVIS, { 0 } },
    [BONE_SHIN_R] = { BONE_THIGH_R, { 0 } },
    [BONE_FOOT_R] = { BONE_SHIN_R, { 0 } },
};

BodyDef g_body;
f32 g_part_hp[PART_COUNT];
const char *g_part_name[PART_COUNT];

#define SKEL_MAX_BONES 64
_Static_assert(BONE_COUNT < 32, "маска ролей в body_skeleton_load — u32");

bool body_skeleton_load(const char *path, V3 *joints)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_error("СКЕЛЕТ: нет файла %s", path);
        return false;
    }
    char magic[4];
    u32 head[2];
    bool ok = fread(magic, 4, 1, f) == 1 && fread(head, sizeof head, 1, f) == 1 && !memcmp(magic, "ASKL", 4) &&
              head[0] == 1 && head[1] <= SKEL_MAX_BONES;
    char name[SKEL_MAX_BONES][16];
    i32 parent[SKEL_MAX_BONES];
    f32 pos[SKEL_MAX_BONES][3];
    for (u32 i = 0; ok && i < head[1]; i++)
        ok = fread(name[i], 16, 1, f) == 1 && fread(&parent[i], 4, 1, f) == 1 && fread(pos[i], 4, 3, f) == 3;
    fclose(f);
    if (!ok) {
        log_error("СКЕЛЕТ: файл %s повреждён", path);
        return false;
    }
    // Кость файла -> роль. Роль без кости или кость, чей родитель не тот, что у роли, — ошибка
    i32 role_of[SKEL_MAX_BONES];
    u32 found = 0; // маска ролей: повтор имени не закрывает недостающую кость
    for (u32 i = 0; i < head[1]; i++) {
        role_of[i] = -1;
        for (u32 b = 0; b < BONE_COUNT; b++)
            if (!strncmp(name[i], g_body.bone_name[b], 16)) role_of[i] = (i32)b;
    }
    for (u32 i = 0; i < head[1]; i++) {
        i32 b = role_of[i];
        if (b < 0) continue;
        i32 want = g_body_bones[b].parent, got = parent[i] >= 0 && parent[i] < (i32)head[1] ? role_of[parent[i]] : -1;
        if (want != got) {
            log_error("СКЕЛЕТ: %s: у кости %.16s родитель не по стандарту", path, g_body.bone_name[b]);
            return false;
        }
        joints[b] = v3(pos[i][0], pos[i][1], pos[i][2]);
        found |= 1u << b;
    }
    if (found != (1u << BONE_COUNT) - 1) {
        log_error("СКЕЛЕТ: %s: не хватает костей стандарта", path);
        return false;
    }
    return true;
}

HitCapsule g_body_hit[BODY_MAX_HIT_CAPSULES];
HitCapsule g_body_hit_guard[BODY_MAX_HIT_CAPSULES];

bool body_load(const char *data_dir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/meshes/%s.skel", data_dir, g_body.skeleton);
    V3 j[BONE_COUNT];
    if (!body_skeleton_load(path, j)) return false;
    for (u32 i = 0; i < BONE_COUNT; i++) g_body_bones[i].joint = j[i];
    for (u32 c = 0; c < g_body.hits; c++) {
        const HitDef *d = &g_body.hit[c];
        g_body_hit[c] = (HitCapsule){ d->part, d->bone, v3_add(g_body_bones[d->from].joint, d->da),
                                      v3_add(g_body_bones[d->to].joint, d->db), d->r };
    }
    return true;
}
