// Внешность человека от зерна (docs/people.md, «Тело»): рост, сложение, пропорции, лицо, тон кожи. Одна функция
// от зерна человека — целый хеш и точная арифметика, поэтому один и тот же человек выглядит одинаково на любой
// машине и в каждом запуске. Знает о ней только вид: шаг мира тела не различает (капсулы, досягаемость и длина
// шага у всех одни). Когда рост или масса понадобятся бою, функция переедет в шаг мира столбцами.
// Пропорции двигают суставы: у каждого человека с телом свой скелет — бинд тела плюс сдвиги суставов морфов
// (кусок MRPJ сетки, docs/mesh.md)
#pragma once
#include "../asset/skel.h"
#include "../core/hash.h"
#include "../render/render.h"

struct ViewBody;

// Байты зерна по порядку (hash_mix от счётчика): каждая черта берёт свои, новая черта дописывается в конец и
// старых не сдвигает. Внешность (appear_make) и одежда (wear_make) — у каждой своя соль зерна
struct Dice {
    u64 h;
    u32 left;
    u64 next;
    u32 byte() {
        if (!left) {
            h = hash_mix(next);
            next += 0x9E3779B97F4A7C15ull;
            left = 8;
        }
        u32 b = (u32)(h & 255);
        h >>= 8;
        left--;
        return b;
    }
    f32 uni() { return (f32)byte() / 255.0f; }                            // 0..1 ровно
    f32 bell() { return ((f32)byte() + (f32)byte() - 255.0f) / 255.0f; }  // −1..1, чаще у нуля
};

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
// Облик k ряда показа тел (docs/body.md, «Показ»): пол, рост, сложение и возраст — по таблице (крайние и средние
// тела рядом), лицо, род, пропорции и тон — от зерна. Номер больше таблицы идёт по кругу. *look_seed — зерно,
// от которого взят облик (на него же — волосы)
Appear appear_show(u32 k, u32 seed, u32 *look_seed);
// Зерно экземпляра тела для материала кожи (вид skin, docs/materials.md): младший байт — тон, бит 8 — женщина
// (грани лифа у неё — холст, у мужчины — кожа)
inline u32 appear_skin_seed(u32 seed, const Appear &a) {
    return (seed & ~0x1FFu) | (u32)(a.skin * 255.0f + 0.5f) | (a.female ? 0x100u : 0u);
}

// Место морфов Appear в сетке тела (−1 — в сетке такого нет)
struct AppearBind {
    i16 at[AM_COUNT];
};
void appear_bind(AppearBind &b, const ViewBody &body);
// Веса морфов в порядке сетки: out — RENDER_MAX_MORPHS чисел
void appear_weights(const AppearBind &b, const Appear &a, f32 *out);

// Насколько сложение отодвигает поверхность тела от кости у мест ношения оружия (src/view/arms.cpp, carry_place): у
// левого бедра наружу и за спиной назад, м в осях сетки. Полный или широкобёдрый носит меч дальше от кости, иначе
// клинок уходит в тело; одежда добавит сюда свою толщину
struct AppearCarry {
    f32 hip, back;
};
AppearCarry appear_carry(const Appear &a);

// Скелет одного человека: бинд тела со сдвигами суставов его морфов. scale — рост к росту сетки (масштаб
// экземпляра), pelvis — высота таза к высоте таза сетки (на неё множится высота таза из клипа)
constexpr u32 APPEAR_MAX_BONES = 64;
struct AppearFit {
    SkelBone bones[APPEAR_MAX_BONES];
    Skel skel;
    f32 scale, pelvis;
    AppearCarry carry;  // appear_carry его внешности
};
// joints — сдвиги суставов тела [морф сетки][кость][4] (ViewBody::joints), nullptr — скелет как у тела
void appear_fit(AppearFit &f, const Skel &base, const f32 *joints, u32 morph_count, const f32 *weights, f32 height,
                i32 pelvis_bone);
