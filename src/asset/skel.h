// Формат .skel (docs/mesh.md, «Скелет»): кости человека — имя, родитель, длина, бинд-поза в осях сетки и обратная
// бинд-матрица. Пишет tools/humanconv, игра только читает. Кости по порядку: родитель раньше детей
#pragma once
#include "../core/mem.h"
#include "../core/text.h"

constexpr u32 SKEL_MAX_BONES = 255;  // номер кости в вершине — u8

struct SkelBone {
    Str name;
    u64 hash;          // str_hash(name): кость ищется по имени
    i32 parent;        // −1 — корень
    f32 length;        // от головы до хвоста, м
    f32 pos[3];        // бинд-поза: голова кости в осях сетки
    f32 rot[4];        // бинд-поза: поворот (x, y, z, w); ось Y кости — к хвосту
    f32 inv[12];       // обратная бинд-матрица 3×4 по строкам: точка сетки → оси кости
};

struct Skel {
    Span<SkelBone> bones;
};

// Разобрать файл; строки и массивы — в арене. false — ошибка в *err
bool skel_parse(const u8 *data, usize size, Arena &arena, Skel &out, const char **err);
// Номер кости по хешу имени; −1 — нет
i32 skel_find(const Skel &s, u64 name_hash);
