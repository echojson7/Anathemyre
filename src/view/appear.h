// Внешность человека от зерна (docs/people.md, «Тело»): рост, сложение, пропорции, лицо, тон кожи. Одна функция
// от зерна человека — целый хеш и точная арифметика, поэтому один и тот же человек выглядит одинаково на любой
// машине и в каждом запуске. Знает о ней только вид: шаг мира тела не различает (капсулы, досягаемость и длина
// шага у всех одни). Когда рост или масса понадобятся бою, функция переедет в шаг мира столбцами.
// Пропорции двигают суставы: у каждого человека с телом свой скелет — бинд тела плюс сдвиги суставов морфов
// (кусок MRPJ сетки, docs/mesh.md)
#pragma once
#include "../asset/skel.h"
#include "../render/render.h"

struct ViewBody;

// Морфы тела по имени (tools/humanconv MORPHS): порядок здесь свой, место в сетке — AppearBind
enum AppearMorph : u32 {
    AM_FEMALE, AM_FULL, AM_THIN, AM_STRONG, AM_OLD, AM_BELLY, AM_FEMALE_FULL, AM_FEMALE_THIN, AM_FEMALE_STRONG,
    AM_SHOULDERS, AM_HIPS, AM_LEGS, AM_TORSO, AM_ARMS, AM_NECK, AM_HEAD, AM_KIN_A, AM_KIN_B,
    AM_NOSE, AM_NOSE_HUMP, AM_JAW, AM_CHIN, AM_LIPS, AM_EYES, AM_CHEEKS, AM_BROW, AM_EARS, AM_COUNT
};
extern const char *const APPEAR_MORPH_NAMES[AM_COUNT];
constexpr f32 APPEAR_MESH_HEIGHT = 1.80f;  // рост сетки тела (tools/humanconv HEIGHT)

struct Appear {
    bool female;
    f32 height;          // рост, м
    f32 skin;            // тон кожи: 0 — светлый, 1 — тёмный
    f32 morph[AM_COUNT]; // веса морфов
};
Appear appear_make(u32 seed);
// Зерно экземпляра тела для материала кожи: младший байт — тон (вид skin, docs/materials.md)
inline u32 appear_skin_seed(u32 seed, const Appear &a) { return (seed & ~0xFFu) | (u32)(a.skin * 255.0f + 0.5f); }

// Место морфов Appear в сетке тела (−1 — в сетке такого нет)
struct AppearBind {
    i16 at[AM_COUNT];
};
void appear_bind(AppearBind &b, const ViewBody &body);
// Веса морфов в порядке сетки: out — RENDER_MAX_MORPHS чисел
void appear_weights(const AppearBind &b, const Appear &a, f32 *out);

// Скелет одного человека: бинд тела со сдвигами суставов его морфов. scale — рост к росту сетки (масштаб
// экземпляра), pelvis — высота таза к высоте таза сетки (на неё множится высота таза из клипа)
constexpr u32 APPEAR_MAX_BONES = 64;
struct AppearFit {
    SkelBone bones[APPEAR_MAX_BONES];
    Skel skel;
    f32 scale, pelvis;
};
// joints — сдвиги суставов тела [морф сетки][кость][4] (ViewBody::joints), nullptr — скелет как у тела
void appear_fit(AppearFit &f, const Skel &base, const f32 *joints, u32 morph_count, const f32 *weights, f32 height,
                i32 pelvis_bone);
