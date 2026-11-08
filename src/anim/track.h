// Запечённые дорожки клипа (.trk, docs/anim.md): на тик 60 Гц корень, 11 капсул тела и отрезок оружия целыми
// мм в осях корня. Пишет tools/retarget/bake.py из .anim; шаг мира будет читать их без float-смешивания
// (этап 9–10), сейчас — стенд (капсулы поверх тела)
#pragma once
#include "../core/mem.h"

constexpr u32 TRACK_TICKS_PER_S = 60;
constexpr u32 TRACK_LOOP = 1, TRACK_WEAPON = 2;
constexpr u32 TRACK_MAX_PARTS = 16;

struct TrackPart {
    u64 name;            // хеш имени части (head, chest, …)
    u16 radius;          // мм
};
struct Track {
    u64 skel;            // хеш имени скелета
    u32 ticks, parts, flags;
    TrackPart part[TRACK_MAX_PARTS];
    i32 *root;           // на тик: x мм, z мм, курс (доля оборота u32 как i32)
    i16 *caps;           // на тик на часть: начало и конец (x, y, z), мм
    i16 *weapon;         // на тик: начало и конец клинка, мм; nullptr — без оружия
};

// Разобрать файл; массивы — в арене. false — ошибка в *err
bool track_parse(const u8 *data, usize size, Arena &arena, Track &out, const char **err);
// Тик дорожки по тику от начала клипа: петля — по кругу, иначе — последний
inline u32 track_tick(const Track &t, u64 tick) {
    if (t.flags & TRACK_LOOP) return (u32)(tick % (u64)(t.ticks - 1));
    return tick < t.ticks ? (u32)tick : t.ticks - 1;
}
