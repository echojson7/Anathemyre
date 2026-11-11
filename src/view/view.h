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
#include "scatter.h"
#include "sky.h"

// Настройки камеры, которые меняет игрок (меню HUD пишет их сюда раз в кадр)
struct CameraSettings {
    f32 mouse_sens = 1.0f;  // множитель к 0.005 рад на пиксель
    bool invert_y = false;  // false: мышь вверх — взгляд вверх
    f32 fov_deg = 60.0f;    // поле зрения по вертикали, градусы
};

struct PeopleView;
struct Colliders;

// Камера: точка опоры, направление взгляда и штанга от опоры до глаза (как у камеры за персонажем).
// Орбита: опора над землёй на высоте глаз человека, позже — голова персонажа. Если землю пересекает штанга,
// она укорачивается, а взгляд не трогается — так у земли можно смотреть в небо. Полёт (F): штанга нулевая,
// опора — сам глаз. yaw, pitch — направление от опоры к глазу, взгляд — обратный
struct Camera {
    bool free = false;
    WPos pivot;             // точка опоры
    f32 yaw = 0.55f;        // радианы, 0 — глаз на юге от опоры (−z), смотрит на север
    f32 pitch = 0.42f;      // радианы: > 0 — глаз выше опоры, смотрит вниз; < 0 — смотрит вверх
    f32 dist = 24.0f;       // желаемая длина штанги, метров (0 — вид от глаз)
    f32 arm = -1.0f;        // длина штанги в кадре, после земли (< 0 — ещё не считали)
    f32 pivot_above = -1.0f;  // опора орбиты над землёй, м (< 0 — на высоте глаз; --pivot для крупных планов)
    bool follow = false;      // опора — голова выбранного человека (view_camera_follow); WASD и сдвиг опоры снимают
    CameraSettings settings;
};

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
};
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
    ViewBody bodies[VIEW_MAX_BODIES];               // люди: тела с весами (стенд, потом таблица людей)
    u32 body_count = 0;
    ViewClip clips[VIEW_MAX_CLIPS];                 // клипы людей: имя × скелет
    u32 clip_count = 0;
    SkyDesc sky;                                    // небо и воздух (data/sky.txt)
};

struct ViewInput {
    const Extract *prev;
    const Extract *cur;
    f32 alpha;              // 0 — prev, 1 — cur
    const DebugDraw *dbg;   // может быть nullptr
    i32 width, height;      // пиксели кадра
    bool pixel;             // правило ячейки (P)
    bool test_lights;       // тестовые огни вокруг опоры камеры (F3): проверка списков froxels
    bool froxel_heat;       // тепловая карта числа огней по клеткам (F3)
    bool gcache_off;        // без кеша поверхности земли: земля на пиксель (F3, --nogcache)
    ScatterCache *scatter;  // рассыпка камней (кеш между кадрами), может быть nullptr
    PeopleView *people = nullptr;  // тела людей (состояние вида между кадрами, docs/people.md), может быть nullptr
    bool people_debug = false;     // след касаний стоп (отладка)
    // Свет (docs/light.md): секунды кадра и «привыкнуть сразу» для глаза, тонмаппинг (F3), качество (меню)
    f32 dt = 0.0f;
    bool eye_snap = true;
    u32 tonemap = RENDER_TONEMAP_AGX;
    u32 light_quality = RENDER_LIGHT_HIGH;
    u32 shadow_pages = 0;  // F3 (docs/shadow.md): 1 — страницы теней поверх кадра, 2 — только тень
    u32 sdf_view = RENDER_SDF_FRAME;  // F3 (docs/sdf.md): поле расстояний мира — кадр, только небо, поле, выкл
};

// Память под экземпляры, точки и отрезки — из арены кадра
void view_build(const Camera &cam, const ViewAssets &assets, const ViewInput &in, Arena &frame, RenderFrame &out);
// Мышь, колесо и клавиши → камера. dt — секунды кадра (камера движется по времени кадра, не мира).
// Опора орбиты — над землёй, глаз не уходит под землю
void view_camera_control(Camera &cam, const Input &in, f32 dt, const Terrain &ground);
// Опора орбиты над точкой земли p
WPos view_pivot_at(const Terrain &ground, WPos p);
// Глаз камеры (чанк + смещение)
WPos view_eye(const Camera &cam);
// Луч из пикселя окна до земли (по ground_at, шагами с уточнением). false — луч не попал в землю
bool view_screen_to_ground(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground, WPos &out);
// Луч из пикселя окна: глаз и направление
bool view_screen_ray(const Camera &cam, i32 width, i32 height, f32 px, f32 py, WPos &eye, Vec3 &dir);
// Луч до земли или коробки постройки (пол, плита, ступень): точка приказа «идти»
bool view_screen_to_support(const Camera &cam, i32 width, i32 height, f32 px, f32 py, const Terrain &ground,
                            const Colliders *coll, WPos &out);
// Камера за человеком: опора — его голова, штанга укорачивается землёй и постройками (глаз не входит в стены
// руины, когда человек внутри)
void view_camera_follow(Camera &cam, const WPos &head, f32 dt, const Terrain &ground, const Colliders *coll);
