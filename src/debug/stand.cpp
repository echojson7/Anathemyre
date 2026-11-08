// Стенд людей (docs/people.md, «Стенд»): человек MakeHuman стоит на земле у руины в бинд-позе; F3 или B гнёт
// одну кость (локоть или колено) — проверка весов; F3 выбирает скелет (game_engine или как у Mixamo) и морф.
// Только вид: в шаг мира человек не входит (таблица людей — этап 9), повтор и хеши его не видят
#include "debug.h"
#include "../anim/pose.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../render/render.h"
#include "../sim/ground.h"
#include "../view/view.h"

// Место: к юго-западу от руины (её середина — prefab ruin 11704 -2336 в data/scene.txt), на площадке, лицом на
// юго-запад — к камере снимков CI (--cam 315 14 34 11704 -2336)
constexpr f32 STAND_X = 11704.0f - 6.0f, STAND_Z = -2336.0f - 12.5f;
constexpr f32 STAND_YAW = 0.625f;  // оборота: лицо (+z сетки) — на юго-запад

static u32 g_stand_mesh[MESH_MAX_SKINS];  // копия человека на скелет (render_skinned_create), 0 — ещё нет
static bool g_stand_failed = false;

static const char *const BEND_BONES[2][2] = {
    {"lowerarm_l", "calf_l"},                           // game_engine
    {"mixamorig:LeftForeArm", "mixamorig:LeftLeg"},     // как у Mixamo
};
static const f32 BEND_DEG[2] = {100.0f, -80.0f};  // локоть — вперёд, колено — назад

static u32 pack_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

void debug_stand(const DebugState &d, const ViewAssets &assets, const Terrain &ground, Arena &frame, RenderFrame &rf) {
    if (!d.stand || !assets.body_count || g_stand_failed) return;
    const ViewBody &b = assets.bodies[0];
    u32 k = d.stand_skel < b.skin_count ? d.stand_skel : 0;
    if (!g_stand_mesh[k]) {
        g_stand_mesh[k] = render_skinned_create(b.skin[k]);
        if (!g_stand_mesh[k]) {
            log_error("стенд людей: нет копии сетки");
            g_stand_failed = true;
            return;
        }
    }
    const Skel &s = b.skel[k];
    // Поза: бинд-поза, на отладке — одна кость согнута. Ось сгиба — в осях сетки, поперёк кости и взгляда тела
    // (+z): сгиб одинаков у обоих скелетов, хотя оси их костей разные (крен у game_engine и Mixamo свой)
    Quat *rot = arena_push<Quat>(frame, s.bones.count);
    for (usize i = 0; i < s.bones.count; ++i) rot[i] = Quat{};
    if (d.stand_bend) {
        u32 bone = d.stand_bone < 2 ? d.stand_bone : 0;
        i32 bi = skel_find(s, hash_str(BEND_BONES[k < 2 ? k : 0][bone]));
        if (bi >= 0) {
            const SkelBone &sb = s.bones.data[bi];
            f32 x = sb.rot[0], y = sb.rot[1], z = sb.rot[2], w = sb.rot[3];
            // Строки поворота бинд-позы: ось Y кости в сетке — второй столбец
            Vec3 c0{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)};
            Vec3 c1{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)};
            Vec3 c2{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
            Vec3 axis = normalize(cross(c1, Vec3{0, 0, 1}));
            Vec3 local{dot(c0, axis), dot(c1, axis), dot(c2, axis)};  // ось в осях кости: Rᵀ · ось
            rot[bi] = quat_axis_angle(local, BEND_DEG[bone] * 0.0174532925f);
        }
    }
    static_assert(sizeof(PoseMatrix) == sizeof(RenderBone), "матрица позы — как кость рендера");
    usize nb0 = rf.bones.count;
    RenderBone *bones = arena_push<RenderBone>(frame, nb0 + s.bones.count);
    if (nb0) memcpy(bones, rf.bones.data, nb0 * sizeof(RenderBone));
    pose_palette(s, rot, (PoseMatrix *)(bones + nb0));
    rf.bones = {bones, nb0 + s.bones.count};

    // Экземпляр: стоит на земле (ground_at), лицом на юго-запад
    usize ni = rf.instances.count;
    RenderInstance *inst = arena_push<RenderInstance>(frame, ni + 1);
    if (ni) memcpy(inst, rf.instances.data, ni * sizeof(RenderInstance));
    WPos at;
    at.x = STAND_X;
    at.z = STAND_Z;
    wpos_rebase(at);
    RenderInstance &it = inst[ni];
    it = RenderInstance{};
    it.cx = at.cx;
    it.cz = at.cz;
    it.x = at.x;
    it.z = at.z;
    it.y = ground_at(ground, at).height;
    Quat q = quat_yaw((Turn)(STAND_YAW * 4294967296.0));
    it.rot_xy = pack_snorm2(q.x, q.y);
    it.rot_zw = pack_snorm2(q.z, q.w);
    it.scale = 1.0f;
    it.mesh = g_stand_mesh[k];
    it.material = RENDER_NO_MATERIAL;
    it.seed = 7;
    rf.instances = {inst, ni + 1};

    usize ns = rf.skinned.count;
    RenderSkinned *sk = arena_push<RenderSkinned>(frame, ns + 1);
    if (ns) memcpy(sk, rf.skinned.data, ns * sizeof(RenderSkinned));
    sk[ns] = RenderSkinned{};
    sk[ns].instance = (u32)ni;
    sk[ns].first_bone = (u32)nb0;
    if (d.stand_morph > 0 && d.stand_morph <= b.morph_count) sk[ns].morph[d.stand_morph - 1] = 1.0f;
    rf.skinned = {sk, ns + 1};
}

void debug_stand_args(DebugState &d, const char *spec) {
    for (const char *p = spec; p && *p;) {
        const char *e = p;
        while (*e && *e != ',') ++e;
        usize n = (usize)(e - p);
        auto is = [&](const char *w) { return n == strlen(w) && !memcmp(p, w, n); };
        if (is("off")) d.stand = false;
        else if (is("elbow")) { d.stand_bend = true; d.stand_bone = 0; }
        else if (is("knee")) { d.stand_bend = true; d.stand_bone = 1; }
        else if (is("mixamo")) d.stand_skel = 1;
        else if (n == 6 && !memcmp(p, "morph", 5) && p[5] >= '1' && p[5] <= '8') d.stand_morph = (u32)(p[5] - '0');
        else log_warn("--stand: непонятное %.*s", (int)n, p);
        p = *e ? e + 1 : e;
    }
}
