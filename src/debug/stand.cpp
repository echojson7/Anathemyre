// Стенд людей (docs/people.md, «Стенд»; docs/anim.md): человек MakeHuman у руины. Без клипа — бинд-поза, B гнёт
// одну кость (проверка весов). С клипом — проигрывание по кругу (скорость, пауза, смешивание с вторым клипом),
// поверх тела — 11 капсул и клинок из запечённых дорожек (.trk): видно расхождение запечённого и показанного.
// «Оба скелета» ставит рядом game_engine и Mixamo. Только вид: в шаг мира человек не входит, повтор его не видит
#include "debug.h"
#include "../anim/pose.h"
#include "../core/hash.h"
#include "../core/log.h"
#include "../core/text.h"
#include "../render/render.h"
#include "../sim/ground.h"
#include "../view/view.h"
#include <stdio.h>
#include <string.h>

// Место: к юго-западу от руины (её середина — prefab ruin 11704 -2336 в data/scene.txt), на площадке, лицом на
// юго-запад — к камере снимков CI (--cam 315 14 34 11704 -2336)
constexpr f32 STAND_X = 11704.0f - 6.0f, STAND_Z = -2336.0f - 12.5f;
constexpr f32 STAND_YAW = 0.625f;  // оборота: лицо (+z сетки) — на юго-запад
constexpr f32 STAND_GAP = 1.1f;    // «оба скелета»: второй — на столько метров правее первого

static u32 g_stand_mesh[2][MESH_MAX_SKINS];  // копия человека на место и скелет (render_skinned_create), 0 — ещё нет
static bool g_stand_failed = false;

static const char *const BEND_BONES[2][2] = {
    {"lowerarm_l", "calf_l"},                           // game_engine
    {"mixamorig:LeftForeArm", "mixamorig:LeftLeg"},     // как у Mixamo
};
static const f32 BEND_DEG[2] = {100.0f, -80.0f};  // локоть — вперёд, колено — назад

// Клипы по имени (без скелета), по алфавиту; 0 — «нет»
constexpr u32 STAND_MAX_NAMES = VIEW_MAX_CLIPS + 1;
static const char *g_names[STAND_MAX_NAMES];
static u64 g_name_hash[STAND_MAX_NAMES];
static u32 g_name_count = 0;
static char g_want[2][32];  // --stand clip=, blend=: имя до загрузки списка
static f64 g_time = 0;      // время клипа, с
// Очередь клипов (--stand seq=a+b+…, видео): петля играет два круга, остальные — раз и 0.4 с стоят в конце
constexpr u32 STAND_MAX_SEQ = 32;
static char g_seq_names[STAND_MAX_SEQ][32];
static u32 g_seq[STAND_MAX_SEQ];
static u32 g_seq_count = 0, g_seq_at = 0;
static bool g_seq_named = false;

// Поза места на кадр (stand_update → debug_stand)
struct StandPose {
    bool on;
    u32 skel;
    Quat rot[SKEL_MAX_BONES];
    i32 trans_bone;
    Vec3 trans;
    WPos at;      // корень тела в мире
    Turn yaw;
};
static StandPose g_pose[2];

const char *const *debug_stand_clip_names(u32 *count) {
    *count = g_name_count;
    return g_names;
}

static void build_names(const ViewAssets &assets) {
    if (g_name_count) return;
    g_names[0] = "нет";
    g_name_count = 1;
    for (u32 i = 0; i < assets.clip_count; ++i) {
        const ViewClip &c = assets.clips[i];
        bool seen = false;
        for (u32 k = 1; k < g_name_count; ++k) seen |= g_name_hash[k] == c.name;
        if (seen || g_name_count >= STAND_MAX_NAMES) continue;
        u32 k = g_name_count++;
        while (k > 1 && strcmp(g_names[k - 1], c.label) > 0) {  // вставка по алфавиту
            g_names[k] = g_names[k - 1];
            g_name_hash[k] = g_name_hash[k - 1];
            --k;
        }
        g_names[k] = c.label;
        g_name_hash[k] = c.name;
    }
}

static u32 find_name(const char *s) {
    for (u32 k = 1; k < g_name_count; ++k)
        if (!strcmp(g_names[k], s)) return k;
    if (*s) log_warn("стенд: нет клипа %s", s);
    return 0;
}

static const ViewClip *find_clip(const ViewAssets &assets, u32 name_index, u64 skel) {
    if (!name_index || name_index >= g_name_count) return nullptr;
    for (u32 i = 0; i < assets.clip_count; ++i)
        if (assets.clips[i].name == g_name_hash[name_index] && assets.clips[i].clip.skel == skel) return &assets.clips[i];
    return nullptr;
}

static u32 pack_snorm2(f32 a, f32 b) {
    auto q = [](f32 v) -> u32 {
        f32 x = f_clamp(v, -1.0f, 1.0f) * 32767.0f;
        i32 i = (i32)(x + (x >= 0 ? 0.5f : -0.5f));
        return (u32)(u16)(i16)i;
    };
    return q(a) | (q(b) << 16);
}

// Точка в осях тела (мм) → мир
static WPos body_point(const WPos &at, f32 s, f32 c, const i16 *mm) {
    f32 x = (f32)mm[0] * 0.001f, y = (f32)mm[1] * 0.001f, z = (f32)mm[2] * 0.001f;
    WPos p = at;
    p.x += c * x + s * z;
    p.z += -s * x + c * z;
    p.y += y;
    wpos_rebase(p);
    return p;
}

// Капсула отрезками: ось, кольца на концах поперёк оси, четыре образующих
static void capsule(DebugDraw &dbg, const WPos &a, const WPos &b, f32 r, u32 rgba) {
    Vec2 dxz = wpos_delta_xz(a, b);
    Vec3 d{dxz.x, b.y - a.y, dxz.y};
    f32 len = length(d);
    Vec3 ax = len > 1e-4f ? d * (1.0f / len) : Vec3{0, 1, 0};
    Vec3 u = cross(ax, f_abs(ax.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0});
    u = normalize(u);
    Vec3 v = cross(ax, u);
    dbg_line(dbg, a, b, rgba);
    constexpr u32 SEG = 8;
    WPos pa[SEG], pb[SEG];
    for (u32 i = 0; i < SEG; ++i) {
        f32 s, c;
        sincos_turn((Turn)(i * (0x100000000ull / SEG)), &s, &c);
        Vec3 o = (u * c + v * s) * r;
        pa[i] = a;
        pa[i].x += o.x; pa[i].y += o.y; pa[i].z += o.z;
        pb[i] = b;
        pb[i].x += o.x; pb[i].y += o.y; pb[i].z += o.z;
        wpos_rebase(pa[i]);
        wpos_rebase(pb[i]);
    }
    for (u32 i = 0; i < SEG; ++i) {
        dbg_line(dbg, pa[i], pa[(i + 1) % SEG], rgba);
        dbg_line(dbg, pb[i], pb[(i + 1) % SEG], rgba);
        if (i % 2 == 0) dbg_line(dbg, pa[i], pb[i], rgba);
    }
}

static void bend_pose(const DebugState &d, const Skel &s, u32 k, Quat *rot) {
    if (!d.stand_bend) return;
    u32 bone = d.stand_bone < 2 ? d.stand_bone : 0;
    i32 bi = skel_find(s, hash_str(BEND_BONES[k < 2 ? k : 0][bone]));
    if (bi < 0) return;
    // Ось сгиба — в осях сетки, поперёк кости и взгляда тела (+z): сгиб одинаков у обоих скелетов
    const SkelBone &sb = s.bones.data[bi];
    f32 x = sb.rot[0], y = sb.rot[1], z = sb.rot[2], w = sb.rot[3];
    Vec3 c0{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)};
    Vec3 c1{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)};
    Vec3 c2{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)};
    Vec3 axis = normalize(cross(c1, Vec3{0, 0, 1}));
    Vec3 local{dot(c0, axis), dot(c1, axis), dot(c2, axis)};  // ось в осях кости: Rᵀ · ось
    rot[bi] = quat_axis_angle(local, BEND_DEG[bone] * 0.0174532925f);
}

void debug_stand_update(DebugState &d, const ViewAssets &assets, const Terrain &ground, f64 dt, DebugDraw *dbg) {
    g_pose[0].on = g_pose[1].on = false;
    if (!d.stand || !assets.body_count) return;
    build_names(assets);
    for (u32 i = 0; i < 2; ++i)
        if (g_want[i][0]) {
            (i ? d.stand_clip_b : d.stand_clip) = find_name(g_want[i]);
            g_want[i][0] = 0;
        }
    if (!g_seq_named) {
        for (u32 i = 0; i < g_seq_count; ++i) g_seq[i] = find_name(g_seq_names[i]);
        g_seq_named = true;
        if (g_seq_count) d.stand_clip = g_seq[0];
    }
    if (!d.stand_pause) g_time += dt * STAND_SPEEDS[d.stand_speed < ANA_COUNTOF(STAND_SPEEDS) ? d.stand_speed : 0];
    if (d.stand_restart) g_time = 0, d.stand_restart = false;
    if (g_seq_count && d.stand_clip == g_seq[g_seq_at]) {
        const ViewClip *c = find_clip(assets, d.stand_clip, assets.bodies[0].skel_name[0]);
        if (c) {
            f32 dur = anim_duration(c->clip);
            f32 play = (c->clip.flags & ANIM_LOOP) ? 2.0f * dur : dur + 0.4f;
            if (g_time >= play) {
                g_time -= play;
                g_seq_at = (g_seq_at + 1) % g_seq_count;
                d.stand_clip = g_seq[g_seq_at];
                log_info("стенд: клип %s", g_names[d.stand_clip]);
            }
        }
    }

    const ViewBody &b = assets.bodies[0];
    WPos base;
    base.x = STAND_X;
    base.z = STAND_Z;
    wpos_rebase(base);
    Turn yaw0 = (Turn)(STAND_YAW * 4294967296.0);
    u32 places = d.stand_both && b.skin_count > 1 ? 2 : 1;
    for (u32 h = 0; h < places; ++h) {
        StandPose &P = g_pose[h];
        u32 k = places == 2 ? h : (d.stand_skel < b.skin_count ? d.stand_skel : 0);
        const Skel &s = b.skel[k];
        P.on = true;
        P.skel = k;
        P.trans_bone = -1;
        for (usize i = 0; i < s.bones.count; ++i) P.rot[i] = Quat{};
        // Место: «оба» — второй правее (вправо от лица — −x тела в осях игры)
        f32 sy, cy;
        sincos_turn(yaw0, &sy, &cy);
        P.at = base;
        if (h == 1) {
            P.at.x -= cy * STAND_GAP;
            P.at.z += sy * STAND_GAP;
            wpos_rebase(P.at);
        }
        P.at.y = ground_at(ground, P.at).height;
        P.yaw = yaw0;
        const WPos place = P.at;
        const ViewClip *ca = find_clip(assets, d.stand_clip, b.skel_name[k]);
        if (!ca) {
            bend_pose(d, s, k, P.rot);
            continue;
        }
        i16 map[SKEL_MAX_BONES];
        if (!anim_bind(ca->clip, s, map, &P.trans_bone)) continue;
        AnimPose pa{P.rot, {}, 0, 0, 0};
        f32 t = g_time > 0 ? (f32)g_time : 0.0f;  // t < 0 — выдержка перед клипом (видео)
        anim_sample(ca->clip, map, (u32)s.bones.count, t, pa);
        const ViewClip *cb = find_clip(assets, d.stand_clip_b, b.skel_name[k]);
        i16 mapb[SKEL_MAX_BONES];
        i32 tb = -1;
        if (cb && anim_bind(cb->clip, s, mapb, &tb) && tb == P.trans_bone) {
            Quat rb[SKEL_MAX_BONES];
            AnimPose pb{rb, {}, 0, 0, 0};
            anim_sample(cb->clip, mapb, (u32)s.bones.count, t, pb);
            anim_blend(pa, pb, (u32)s.bones.count, d.stand_blend);
        }
        P.trans = pa.trans;
        // Корень: на месте — только курс; иначе ещё и сдвиг клипа (петля начинается заново с места)
        if (!d.stand_inplace) {
            P.at.x += cy * pa.root_x + sy * pa.root_z;
            P.at.z += -sy * pa.root_x + cy * pa.root_z;
            wpos_rebase(P.at);
            P.at.y = ground_at(ground, P.at).height;
        }
        P.yaw = yaw0 + pa.root_yaw;

        // Капсулы и клинок из дорожек главного клипа: корень дорожки — тот же, что у показанного клипа
        if (!dbg || !d.stand_caps || !ca->track.ticks) continue;
        const Track &tr = ca->track;
        f32 dur = anim_duration(ca->clip);
        f32 tt = (ca->clip.flags & ANIM_LOOP) ? t - dur * (f32)f_floor_i(t / dur) : f_clamp(t, 0.0f, dur);
        u32 tick = track_tick(tr, (u64)f_floor_i(tt * (f32)TRACK_TICKS_PER_S + 0.5f));
        const i32 *rt = tr.root + tick * 3;
        WPos at = place;
        if (!d.stand_inplace) {  // место дорожки: место + её корень, без смешивания
            f32 rx = (f32)rt[0] * 0.001f, rz = (f32)rt[1] * 0.001f;
            at.x += cy * rx + sy * rz;
            at.z += -sy * rx + cy * rz;
            wpos_rebase(at);
            at.y = ground_at(ground, at).height;
        }
        f32 s2, c2;
        sincos_turn(yaw0 + (Turn)rt[2], &s2, &c2);
        u32 key = (u32)(tt * (f32)ANIM_KEYS_PER_S + 0.5f);
        for (u32 i = 0; i < tr.parts; ++i) {
            const i16 *cp = tr.caps + ((usize)tick * tr.parts + i) * 6;
            u32 col = rgba8(90, 230, 255);
            // Голень на касании стопы — жёлтая (метки foot)
            if ((i == 9 || i == 10) && anim_span_has(ca->labels.foot[i - 9], ca->labels.foot_count[i - 9], key))
                col = rgba8(255, 220, 40);
            capsule(*dbg, body_point(at, s2, c2, cp), body_point(at, s2, c2, cp + 3), (f32)tr.part[i].radius * 0.001f, col);
        }
        if (tr.weapon) {
            const i16 *wp = tr.weapon + tick * 6;
            u32 col = anim_span_has(ca->labels.hit, ca->labels.hit_count, key)       ? rgba8(255, 60, 40)
                      : anim_span_has(ca->labels.block, ca->labels.block_count, key) ? rgba8(60, 120, 255)
                                                                                      : rgba8(255, 255, 255);
            WPos wa = body_point(at, s2, c2, wp), wb = body_point(at, s2, c2, wp + 3);
            dbg_line(*dbg, wa, wb, col);
            capsule(*dbg, wa, wb, 0.012f, col);
        }
    }
}

void debug_stand(const DebugState &d, const ViewAssets &assets, Arena &frame, RenderFrame &rf) {
    if (!d.stand || !assets.body_count || g_stand_failed) return;
    const ViewBody &b = assets.bodies[0];
    for (u32 h = 0; h < 2; ++h) {
        const StandPose &P = g_pose[h];
        if (!P.on) continue;
        u32 k = P.skel;
        if (!g_stand_mesh[h][k]) {
            g_stand_mesh[h][k] = render_skinned_create(b.skin[k]);
            if (!g_stand_mesh[h][k]) {
                log_error("стенд людей: нет копии сетки");
                g_stand_failed = true;
                return;
            }
        }
        const Skel &s = b.skel[k];
        static_assert(sizeof(PoseMatrix) == sizeof(RenderBone), "матрица позы — как кость рендера");
        usize nb0 = rf.bones.count;
        RenderBone *bones = arena_push<RenderBone>(frame, nb0 + s.bones.count);
        if (nb0) memcpy(bones, rf.bones.data, nb0 * sizeof(RenderBone));
        pose_palette(s, P.rot, (PoseMatrix *)(bones + nb0), P.trans_bone, P.trans_bone >= 0 ? &P.trans : nullptr);
        rf.bones = {bones, nb0 + s.bones.count};

        usize ni = rf.instances.count;
        RenderInstance *inst = arena_push<RenderInstance>(frame, ni + 1);
        if (ni) memcpy(inst, rf.instances.data, ni * sizeof(RenderInstance));
        RenderInstance &it = inst[ni];
        it = RenderInstance{};
        it.cx = P.at.cx;
        it.cz = P.at.cz;
        it.x = P.at.x;
        it.z = P.at.z;
        it.y = P.at.y;
        Quat q = quat_yaw(P.yaw);
        it.rot_xy = pack_snorm2(q.x, q.y);
        it.rot_zw = pack_snorm2(q.z, q.w);
        it.scale = 1.0f;
        it.mesh = g_stand_mesh[h][k];
        it.material = RENDER_NO_MATERIAL;
        it.seed = 7 + h;
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
}

void debug_stand_args(DebugState &d, const char *spec) {
    for (const char *p = spec; p && *p;) {
        const char *e = p;
        while (*e && *e != ',') ++e;
        usize n = (usize)(e - p);
        auto is = [&](const char *w) { return n == strlen(w) && !memcmp(p, w, n); };
        auto val = [&](const char *key, char *out, usize cap) {
            usize kl = strlen(key);
            if (n <= kl || memcmp(p, key, kl)) return false;
            snprintf(out, cap, "%.*s", (int)(n - kl), p + kl);
            return true;
        };
        char num[16];
        auto num_u = [&]() {
            u32 v = 0;
            parse_u32(Str{num, (u32)strlen(num)}, v);
            return v;
        };
        if (is("off")) d.stand = false;
        else if (is("elbow")) { d.stand_bend = true; d.stand_bone = 0; }
        else if (is("knee")) { d.stand_bend = true; d.stand_bone = 1; }
        else if (is("mixamo")) d.stand_skel = 1;
        else if (is("both")) d.stand_both = true;
        else if (is("move")) d.stand_inplace = false;
        else if (is("nocaps")) d.stand_caps = false;
        else if (n == 6 && !memcmp(p, "morph", 5) && p[5] >= '1' && p[5] <= '8') d.stand_morph = (u32)(p[5] - '0');
        else if (val("clip=", g_want[0], sizeof g_want[0]) || val("blend=", g_want[1], sizeof g_want[1])) {}
        else if (n > 4 && !memcmp(p, "seq=", 4)) {  // seq=a+b+c
            g_seq_count = 0;
            for (const char *q = p + 4; q < e && g_seq_count < STAND_MAX_SEQ;) {
                const char *qe = q;
                while (qe < e && *qe != '+') ++qe;
                snprintf(g_seq_names[g_seq_count++], 32, "%.*s", (int)(qe - q), q);
                q = qe < e ? qe + 1 : qe;
            }
            g_seq_named = false;
            g_seq_at = 0;
        }
        else if (val("speed=", num, sizeof num)) {
            for (u32 i = 0; i < ANA_COUNTOF(STAND_SPEEDS); ++i)
                if ((u32)(STAND_SPEEDS[i] * 100.0f + 0.5f) == num_u()) d.stand_speed = i;
        } else if (val("w=", num, sizeof num)) d.stand_blend = (f32)num_u() * 0.01f;
        else if (val("t=", num, sizeof num)) {
            i32 v = 0;
            parse_i32(Str{num, (u32)strlen(num)}, v);
            g_time = (f64)v * 0.01;
        }
        else log_warn("--stand: непонятное %.*s", (int)n, p);
        p = *e ? e + 1 : e;
    }
}
