// Общие генераторы предметов: одна функция на вид, любая локация берёт отсюда, а не пишет свой.
// Каждый предмет разный за счёт Rng (оттенок, размер, износ), а не за счёт новых функций (docs/props.md)
#ifndef ANA_PROPS_H
#define ANA_PROPS_H

#include "../core/palette.h"
#include "../core/rng.h"
#include "../render/mesh.h"

#define PAL_CLOTH_N 7
#define PAL_SHEET_N 5
extern const V3 g_pal_cloth[PAL_CLOTH_N];
extern const V3 g_pal_sheet[PAL_SHEET_N];

// Разброс яркости без сдвига оттенка: бетон, железо
static inline V3 prop_shade(Rng *r, V3 c, f32 k) { return v3_mul(c, 1.0f + rng_range(r, -k, k)); }

// Разброс цвета: у каждого предмета свой оттенок и износ
static inline V3 prop_tint(Rng *r, V3 c, f32 a)
{
    f32 k = 1.0f + rng_range(r, -a, a);
    return v3(c.x * k * (1.0f + rng_range(r, -a, a) * 0.4f), c.y * k, c.z * k * (1.0f + rng_range(r, -a, a) * 0.4f));
}

static inline V3 prop_pick(Rng *r, const V3 *pal, u32 n, f32 a) { return prop_tint(r, pal[rng_below(r, n)], a); }

static inline void prop_box(MeshBuilder *mb, V3 c, V3 s, f32 yaw) { mb_box(mb, m4_trs(c, s, 0, yaw, 0)); }

// Локальная система предмета: +z вперёд (к улице, от стены), +x вправо
static inline M4 prop_frame(V3 p, f32 yaw) { return m4_mul(m4_translate(p), m4_rot_y(yaw)); }

static inline void prop_lbox(MeshBuilder *mb, M4 L, V3 c, V3 s, f32 rx, f32 ry, f32 rz) { mb_box(mb, m4_mul(L, m4_trs(c, s, rx, ry, rz))); }

static inline void prop_lcyl(MeshBuilder *mb, M4 L, V3 a, V3 b, f32 r, u32 segs) { mb_cylinder(mb, m4_point(L, a), m4_point(L, b), r, segs); }

// Ящик из листа с рёбрами; возвращает высоту
f32 prop_crate(MeshBuilder *mb, Rng *r, V3 p, f32 yaw, f32 s);
// Несколько ящиков: внизу рядом, сверху со сдвигом и поворотом
void prop_crate_stack(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);
void prop_barrel(MeshBuilder *mb, Rng *r, V3 p, bool tipped);
void prop_sack(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);
void prop_sack_pile(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);
// Куча лома: листы, трубы, обломки, колёса; выше к центру
void prop_scrap_heap(MeshBuilder *mb, Rng *r, V3 c, f32 rad);
// Провод или кабель с провисом по параболе: segs кусков, sides граней. Цвет и материал — у вызывающего
void prop_wire(MeshBuilder *mb, V3 a, V3 b, f32 sag, f32 r, u32 segs, u32 sides);
// Провод с провисом; hang: 0 голый, 1 тряпки, 2 лампа
void prop_cable(MeshBuilder *mb, Rng *r, V3 a, V3 b, f32 sag, u32 hang);
void prop_lamp_post(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);
void prop_rug(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);
// Очаг: камни кругом, угли, тренога с котлом
void prop_fire_pit(MeshBuilder *mb, Rng *r, V3 p);
// Бак на ногах с лестницей
void prop_water_tower(MeshBuilder *mb, Rng *r, V3 p, f32 h);
// Забор из листов и блоков от a до b; дыры и разная высота
void prop_fence(MeshBuilder *mb, Rng *r, V3 a, V3 b);
// Перила из труб вдоль a→b; часть пролётов выломана
void prop_railing(MeshBuilder *mb, Rng *r, V3 a, V3 b);
void prop_ladder(MeshBuilder *mb, M4 L, V3 base, f32 h);
// Мелочь у стены дома или прилавка
void prop_clutter(MeshBuilder *mb, Rng *r, V3 p, f32 yaw);

// Труба по точкам вдоль стены: колена, фланцы через ~3 м, хомуты к стене (wall — наружу от стены).
// rust — цвет трубы, iron — хомутов
void prop_pipe_run(MeshBuilder *mb, const V3 *pts, u32 n, f32 r, V3 wall, V3 rust, V3 iron, Rng *rng);

// Мелочи на земле: бревно, пень, труба, лист, шестерня древних машин, обломок плиты. Строится из зерна, у (0, 0, 0)
typedef enum PropKind { PROP_LOG, PROP_STUMP, PROP_PIPE, PROP_PLATE, PROP_GEAR, PROP_SLAB, PROP_KINDS } PropKind;
void prop_ground(MeshBuilder *mb, PropKind kind, u32 seed);

#endif
