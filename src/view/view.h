// Вид: что видно в кадре. Собирает RenderFrame из двух последних выписок с интерполяцией.
// Только читает выписки и буфер отладочного рисования, мир не трогает. Камера — часть вида, не мира:
// на шаг мира и повтор она не влияет (docs/invariants.md)
#pragma once
#include "../asset/material.h"
#include "../asset/mesh.h"
#include "../anim/clip.h"
#include "../anim/track.h"
#include "../asset/skel.h"
#include "../core/dbgdraw.h"
#include "../platform/platform.h"
#include "../render/render.h"
#include "../sim/extract.h"
#include "camera.h"
#include "scatter.h"
#include "sky.h"
#include "weapon.h"

struct PeopleView;
struct MechView;

// Сетки и материалы, загруженные для кадра: имя (хеш) → номер в рендере
struct ViewMesh {
    u64 name;
    u32 id;
};
constexpr u32 VIEW_MAX_MESHES = 1024;
// Тело человека (docs/people.md): сетка с весами; на скелет — источник скиннинга и сам скелет
constexpr u32 VIEW_MAX_BODIES = 8;
struct ViewBody {
    u64 name;                      // хеш имени сетки
    u32 mesh;                      // сетка бинд-позы
    u32 skin_count;
    u32 skin[MESH_MAX_SKINS];      // источник скиннинга (render_skin_create)
    u64 skel_name[MESH_MAX_SKINS]; // хеш имени скелета (game_engine, mixamo)
    Skel skel[MESH_MAX_SKINS];
    u32 morph_count;
    u64 morph_name[MESH_MAX_MORPHS];
    // Сдвиги суставов от морфов (кусок MRPJ): [морф][кость][4] — голова и длина, м; nullptr — у сетки их нет
    const f32 *joints[MESH_MAX_SKINS];
    // Вещь одежды (docs/wear.md): сетка wear_* на скелетах тела людей — скрываемые области тела, толщина у мест
    // ношения оружия
    u32 hide;
    f32 carry_hip, carry_back;
};
constexpr u32 VIEW_MAX_WEAR = 32;
// Клип захвата движения одного скелета (docs/anim.md): клип, запечённые дорожки и метки
constexpr u32 VIEW_MAX_CLIPS = 64;
struct ViewClip {
    u64 name;                      // хеш имени клипа (файл до первой точки)
    char label[32];                // имя для отладки (стенд)
    AnimClip clip;
    Track track;                   // ticks = 0 — нет дорожек
    AnimLabels labels;
};
struct ViewAssets {
    ViewMesh meshes[VIEW_MAX_MESHES];
    u32 mesh_count = 0;
    const MaterialTable *materials = nullptr;
    u32 ground_layers[RENDER_GROUND_LAYERS] = {};  // материалы слоёв земли по GroundKind
    u32 rocks[SCATTER_MESHES] = {};                 // сетки камней рассыпки
    ViewBody bodies[VIEW_MAX_BODIES];               // тела с весами; [0] — тело людей (скелет game_engine), дальше машины
    u32 body_count = 0;
    ViewBody wear[VIEW_MAX_WEAR];                   // вещи одежды людей (сетки wear_*, скелеты тела людей)
    u32 wear_count = 0;
    ViewClip clips[VIEW_MAX_CLIPS];                 // клипы людей: имя × скелет
    u32 clip_count = 0;
    SkyDesc sky;                                    // небо и воздух (data/sky.txt)
    WeaponTable weapons;                            // оружие (data/weapons.txt), сетки — weapon_<имя>
};

struct ViewInput {
    const Extract *prev;
    const Extract *cur;
    f32 alpha;              // 0 — prev, 1 — cur
    const DebugDraw *dbg;   // может быть nullptr
    i32 width, height;      // пиксели кадра
    bool pixel;             // старое правило ячеек (P, --cells)
    bool test_lights;       // тестовые огни вокруг опоры камеры (F3): проверка списков froxels
    bool froxel_heat;       // тепловая карта числа огней по клеткам (F3)
    bool gcache_off;        // без кеша поверхности земли: земля на пиксель (F3, --nogcache)
    ScatterCache *scatter;  // рассыпка камней (кеш между кадрами), может быть nullptr
    PeopleView *people = nullptr;  // тела людей (состояние вида между кадрами, docs/people.md), может быть nullptr
    bool people_debug = false;     // след касаний стоп (отладка)
    bool people_show = false;      // показ тел: поза привязки, без одежды и оружия (docs/body.md, «Показ»)
    // Тела беты вторым рядом показа (docs/body.md, «Показ»): места (высота — земля), курс лицом к камере
    const WPos *show_old = nullptr;
    u32 show_old_n = 0;
    Turn show_old_yaw = 0;
    MechView *mech = nullptr;      // шагающие машины (состояние вида между кадрами, docs/vezha.md, docs/tat.md), может быть nullptr
    bool lamps_off = false;        // лампы карты не светят (F3 «лампы карты», --nolamps): сравнение было/стало
    // Свет (docs/light.md): секунды кадра и «привыкнуть сразу» для глаза, тонмаппинг (F3), качество (меню)
    f32 dt = 0.0f;
    f32 time = 0.0f;  // время от запуска, с (мерцание звёзд, docs/sky.md); в снимках — номер кадра / 30
    f64 cloud_time = 0.0;  // то же без круга: ветер и смена формы облаков (docs/clouds.md)
    bool eye_snap = true;
    u32 tonemap = RENDER_TONEMAP_AGX;
    bool local_off = false;  // без местной экспозиции (F3, --nolocal): сравнение было/стало
    bool scans = false;  // поверхности из сканов CC0 (docs/scans.md), F3 и --scans
    bool gclight_off = false;  // отражённый свет земли на пиксель, не из кеша (docs/gcache.md, F3, --nogclight)
    u32 light_quality = RENDER_LIGHT_HIGH;
    u32 aa = RENDER_AA_TAA;
    u32 shadow_pages = 0;  // F3 (docs/shadow.md): 1 — страницы теней поверх кадра, 2 — только тень
    u32 sdf_view = RENDER_SDF_FRAME;  // F3 (docs/sdf.md): поле расстояний мира — кадр, только небо, поле, выкл
    u32 gi_view = RENDER_GI_FRAME;    // F3 (docs/gi.md): непрямой свет — кадр, только он, без зондов
};

// Память под экземпляры, точки и отрезки — из арены кадра
void view_build(const Camera &cam, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderFrame &out);
