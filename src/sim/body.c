#include "body.h"

#define HX BODY_HIP_X

const BoneDef g_body_bones[BONE_COUNT] = {
    [BONE_ROOT]        = { -1, { 0.0f, 0.0f, 0.0f } },
    [BONE_PELVIS]      = { BONE_ROOT, { 0.0f, 1.00f, 0.0f } },
    [BONE_SPINE]       = { BONE_PELVIS, { 0.0f, 1.05f, 0.0f } },
    [BONE_CHEST]       = { BONE_SPINE, { 0.0f, 1.25f, 0.0f } },
    [BONE_NECK]        = { BONE_CHEST, { 0.0f, 1.45f, 0.0f } },
    [BONE_HEAD]        = { BONE_NECK, { 0.0f, 1.53f, 0.0f } },
    [BONE_CLAVICLE_L]  = { BONE_CHEST, { 0.05f, 1.42f, 0.0f } },
    [BONE_UPPER_ARM_L] = { BONE_CLAVICLE_L, { 0.21f, 1.44f, 0.0f } },
    [BONE_FOREARM_L]   = { BONE_UPPER_ARM_L, { 0.24f, 1.16f, 0.0f } },
    [BONE_HAND_L]      = { BONE_FOREARM_L, { 0.25f, 0.95f, 0.01f } },
    [BONE_CLAVICLE_R]  = { BONE_CHEST, { -0.05f, 1.42f, 0.0f } },
    [BONE_UPPER_ARM_R] = { BONE_CLAVICLE_R, { -0.21f, 1.44f, 0.0f } },
    [BONE_FOREARM_R]   = { BONE_UPPER_ARM_R, { -0.24f, 1.16f, 0.0f } },
    [BONE_HAND_R]      = { BONE_FOREARM_R, { -0.25f, 0.95f, 0.01f } },
    [BONE_THIGH_L]     = { BONE_PELVIS, { HX, BODY_HIP_Y, 0.0f } },
    [BONE_SHIN_L]      = { BONE_THIGH_L, { HX, BODY_KNEE_Y, 0.0f } },
    [BONE_FOOT_L]      = { BONE_SHIN_L, { HX, BODY_ANKLE_Y, 0.0f } },
    [BONE_THIGH_R]     = { BONE_PELVIS, { -HX, BODY_HIP_Y, 0.0f } },
    [BONE_SHIN_R]      = { BONE_THIGH_R, { -HX, BODY_KNEE_Y, 0.0f } },
    [BONE_FOOT_R]      = { BONE_SHIN_R, { -HX, BODY_ANKLE_Y, 0.0f } },
};

const BodyClass g_body_classes[BODY_CLASS_COUNT] = {
    [BODY_H] = { 1.68f, 1.92f, 55.0f, 95.0f, 1.0f, 0.30f, 1.15f, 1.0f },
};

const HitCapsule g_body_hit[BODY_MAX_HIT_CAPSULES] = {
    { PART_HEAD, BONE_HEAD, { 0.0f, 1.65f, 0.02f }, { 0.0f, 1.65f, 0.02f }, 0.11f },
    { PART_CHEST, BONE_CHEST, { 0.0f, 1.24f, 0.0f }, { 0.0f, 1.40f, 0.0f }, 0.17f },
    { PART_STOMACH, BONE_SPINE, { 0.0f, 0.98f, 0.0f }, { 0.0f, 1.12f, 0.0f }, 0.15f },
    { PART_ARM_L, BONE_UPPER_ARM_L, { 0.21f, 1.44f, 0.0f }, { 0.24f, 1.16f, 0.0f }, 0.06f },
    { PART_ARM_L, BONE_FOREARM_L, { 0.24f, 1.16f, 0.0f }, { 0.25f, 0.88f, 0.02f }, 0.05f },
    { PART_ARM_R, BONE_UPPER_ARM_R, { -0.21f, 1.44f, 0.0f }, { -0.24f, 1.16f, 0.0f }, 0.06f },
    { PART_ARM_R, BONE_FOREARM_R, { -0.24f, 1.16f, 0.0f }, { -0.25f, 0.88f, 0.02f }, 0.05f },
    { PART_LEG_L, BONE_THIGH_L, { HX, BODY_HIP_Y, 0.0f }, { HX, BODY_KNEE_Y, 0.0f }, 0.08f },
    { PART_LEG_L, BONE_SHIN_L, { HX, BODY_KNEE_Y, 0.0f }, { HX, BODY_ANKLE_Y, 0.03f }, 0.07f },
    { PART_LEG_R, BONE_THIGH_R, { -HX, BODY_HIP_Y, 0.0f }, { -HX, BODY_KNEE_Y, 0.0f }, 0.08f },
    { PART_LEG_R, BONE_SHIN_R, { -HX, BODY_KNEE_Y, 0.0f }, { -HX, BODY_ANKLE_Y, 0.03f }, 0.07f },
};
const u32 g_body_hit_count = 11;

const SocketDef g_body_sockets[SOCKET_COUNT] = {
    [SOCKET_HAND_R] = { BONE_HAND_R, { -0.25f, 0.85f, 0.05f } },
    [SOCKET_HAND_L] = { BONE_HAND_L, { 0.25f, 0.85f, 0.05f } },
    [SOCKET_BACK]   = { BONE_CHEST, { 0.0f, 1.30f, -0.20f } },
    [SOCKET_BELT]   = { BONE_PELVIS, { -0.17f, 0.98f, 0.0f } },
};

const f32 g_part_hp[PART_COUNT] = { 60, 100, 80, 60, 60, 70, 70 };
const f32 g_part_hit_share[PART_COUNT] = { 0.08f, 0.30f, 0.22f, 0.10f, 0.10f, 0.10f, 0.10f };
const char *g_part_name[PART_COUNT] = {
    "ГОЛОВА", "ГРУДЬ", "ЖИВОТ", "ЛЕВАЯ РУКА", "ПРАВАЯ РУКА", "ЛЕВАЯ НОГА", "ПРАВАЯ НОГА",
};
