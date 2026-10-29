#include "body.h"

#define HX BODY_HIP_X

// Суставы мужского тела MakeHuman в позе привязки (tools/humanconv печатает их при сборке сеток)
const BoneDef g_body_bones[BONE_COUNT] = {
    [BONE_ROOT]        = { -1, { 0.0f, 0.0f, 0.0f } },
    [BONE_PELVIS]      = { BONE_ROOT, { 0.0f, 0.970f, 0.003f } },
    [BONE_SPINE]       = { BONE_PELVIS, { 0.0f, 1.061f, -0.030f } },
    [BONE_CHEST]       = { BONE_SPINE, { 0.0f, 1.197f, -0.030f } },
    [BONE_NECK]        = { BONE_CHEST, { 0.0f, 1.555f, 0.012f } },
    [BONE_HEAD]        = { BONE_NECK, { 0.0f, 1.661f, 0.053f } },
    [BONE_CLAVICLE_L]  = { BONE_CHEST, { 0.024f, 1.477f, 0.023f } },
    [BONE_UPPER_ARM_L] = { BONE_CLAVICLE_L, { 0.195f, 1.446f, 0.019f } },
    [BONE_FOREARM_L]   = { BONE_UPPER_ARM_L, { 0.227f, 1.140f, 0.019f } },
    [BONE_HAND_L]      = { BONE_FOREARM_L, { 0.240f, 0.880f, 0.031f } },
    [BONE_CLAVICLE_R]  = { BONE_CHEST, { -0.024f, 1.477f, 0.023f } },
    [BONE_UPPER_ARM_R] = { BONE_CLAVICLE_R, { -0.195f, 1.446f, 0.019f } },
    [BONE_FOREARM_R]   = { BONE_UPPER_ARM_R, { -0.227f, 1.140f, 0.019f } },
    [BONE_HAND_R]      = { BONE_FOREARM_R, { -0.240f, 0.880f, 0.031f } },
    [BONE_THIGH_L]     = { BONE_PELVIS, { HX, BODY_HIP_Y, -0.008f } },
    [BONE_SHIN_L]      = { BONE_THIGH_L, { 0.136f, BODY_KNEE_Y, 0.010f } },
    [BONE_FOOT_L]      = { BONE_SHIN_L, { 0.159f, BODY_ANKLE_Y, 0.029f } },
    [BONE_THIGH_R]     = { BONE_PELVIS, { -HX, BODY_HIP_Y, -0.008f } },
    [BONE_SHIN_R]      = { BONE_THIGH_R, { -0.136f, BODY_KNEE_Y, 0.010f } },
    [BONE_FOOT_R]      = { BONE_SHIN_R, { -0.159f, BODY_ANKLE_Y, 0.029f } },
};

const BodyClass g_body_classes[BODY_CLASS_COUNT] = {
    [BODY_H] = { 1.68f, 1.92f, 55.0f, 95.0f, 1.0f, 0.30f, 1.15f, 1.0f },
};

const HitCapsule g_body_hit[BODY_MAX_HIT_CAPSULES] = {
    { PART_HEAD, BONE_HEAD, { 0.0f, 1.69f, 0.03f }, { 0.0f, 1.69f, 0.03f }, 0.11f },
    { PART_CHEST, BONE_CHEST, { 0.0f, 1.25f, 0.0f }, { 0.0f, 1.40f, 0.0f }, 0.16f },
    { PART_STOMACH, BONE_SPINE, { 0.0f, 0.98f, 0.0f }, { 0.0f, 1.12f, 0.0f }, 0.15f },
    { PART_ARM_L, BONE_UPPER_ARM_L, { 0.195f, 1.446f, 0.019f }, { 0.227f, 1.140f, 0.019f }, 0.06f },
    { PART_ARM_L, BONE_FOREARM_L, { 0.227f, 1.140f, 0.019f }, { 0.243f, 0.80f, 0.04f }, 0.05f },
    { PART_ARM_R, BONE_UPPER_ARM_R, { -0.195f, 1.446f, 0.019f }, { -0.227f, 1.140f, 0.019f }, 0.06f },
    { PART_ARM_R, BONE_FOREARM_R, { -0.227f, 1.140f, 0.019f }, { -0.243f, 0.80f, 0.04f }, 0.05f },
    { PART_LEG_L, BONE_THIGH_L, { HX, BODY_HIP_Y, -0.008f }, { 0.136f, BODY_KNEE_Y, 0.010f }, 0.08f },
    { PART_LEG_L, BONE_SHIN_L, { 0.136f, BODY_KNEE_Y, 0.010f }, { 0.158f, BODY_ANKLE_Y, 0.04f }, 0.07f },
    { PART_LEG_R, BONE_THIGH_R, { -HX, BODY_HIP_Y, -0.008f }, { -0.136f, BODY_KNEE_Y, 0.010f }, 0.08f },
    { PART_LEG_R, BONE_SHIN_R, { -0.136f, BODY_KNEE_Y, 0.010f }, { -0.158f, BODY_ANKLE_Y, 0.04f }, 0.07f },
};
const u32 g_body_hit_count = 11;

// Те же капсулы в боевой стойке с одноручным (снято с позы character_pose, guard = 1):
// руки вперёд, левая нога впереди. По ним считается, куда пришёлся удар по стоящему
const HitCapsule g_body_hit_guard[BODY_MAX_HIT_CAPSULES] = {
    { PART_HEAD, BONE_HEAD, { 0.003f, 1.607f, 0.160f }, { 0.003f, 1.607f, 0.160f }, 0.11f },
    { PART_CHEST, BONE_CHEST, { 0.007f, 1.173f, 0.041f }, { 0.003f, 1.320f, 0.072f }, 0.16f },
    { PART_STOMACH, BONE_SPINE, { 0.008f, 0.909f, -0.003f }, { 0.004f, 1.047f, 0.018f }, 0.15f },
    { PART_ARM_L, BONE_UPPER_ARM_L, { 0.198f, 1.359f, 0.082f }, { 0.208f, 1.158f, 0.215f }, 0.06f },
    { PART_ARM_L, BONE_FOREARM_L, { 0.208f, 1.158f, 0.215f }, { 0.121f, 1.344f, 0.482f }, 0.05f },
    { PART_ARM_R, BONE_UPPER_ARM_R, { -0.184f, 1.339f, 0.135f }, { -0.161f, 1.118f, 0.227f }, 0.06f },
    { PART_ARM_R, BONE_FOREARM_R, { -0.161f, 1.118f, 0.227f }, { 0.006f, 1.160f, 0.516f }, 0.05f },
    { PART_LEG_L, BONE_THIGH_L, { 0.111f, 0.889f, 0.026f }, { 0.182f, 0.532f, 0.260f }, 0.08f },
    { PART_LEG_L, BONE_SHIN_L, { 0.182f, 0.532f, 0.260f }, { 0.236f, 0.082f, 0.244f }, 0.07f },
    { PART_LEG_R, BONE_THIGH_R, { -0.105f, 0.889f, -0.041f }, { -0.173f, 0.469f, 0.041f }, 0.08f },
    { PART_LEG_R, BONE_SHIN_R, { -0.173f, 0.469f, 0.041f }, { -0.216f, 0.063f, -0.156f }, 0.07f },
};

const SocketDef g_body_sockets[SOCKET_COUNT] = {
    [SOCKET_HAND_R] = { BONE_HAND_R, { -0.258f, 0.792f, 0.05f } },
    [SOCKET_HAND_L] = { BONE_HAND_L, { 0.258f, 0.792f, 0.05f } },
    [SOCKET_BACK]   = { BONE_CHEST, { 0.0f, 1.30f, -0.20f } },
    [SOCKET_BELT]   = { BONE_PELVIS, { -0.17f, 0.98f, 0.0f } },
};

const f32 g_part_hp[PART_COUNT] = { 60, 100, 80, 60, 60, 70, 70 };
const f32 g_part_hit_share[PART_COUNT] = { 0.08f, 0.30f, 0.22f, 0.10f, 0.10f, 0.10f, 0.10f };
const char *g_part_name[PART_COUNT] = {
    "ГОЛОВА", "ГРУДЬ", "ЖИВОТ", "ЛЕВАЯ РУКА", "ПРАВАЯ РУКА", "ЛЕВАЯ НОГА", "ПРАВАЯ НОГА",
};
