// Клипы захвата движения (.anim, docs/anim.md) и метки клипа (.txt): выборка по времени и смешивание двух поз.
// Только вид: шаг мира клипов не читает, у него запечённые дорожки (track.h). Без IK (этап 9)
#pragma once
#include "../asset/skel.h"
#include "../core/math3.h"

constexpr u32 ANIM_KEYS_PER_S = 30;
constexpr u32 ANIM_LOOP = 1;  // флаг клипа: петля

struct AnimClip {
    u64 skel;            // хеш имени скелета
    u32 keys, bones, flags;
    u32 trans_bone;      // номер кости переноса (таз) в списке костей клипа
    u64 *bone_hash;      // имена костей клипа (хеш)
    i32 *root;           // на ключ: x мм, z мм, курс (доля оборота u32 как i32)
    i16 *trans;          // на ключ: голова кости переноса в осях корня, мм
    u8 *rots;            // на ключ на кость 6 байт: кватернион smallest-three
};

// Поза клипа в осях корня: повороты костей скелета после бинда, голова кости переноса; корень — сдвиг
// от начала клипа (м) и курс
struct AnimPose {
    Quat *rot;           // s.bones.count
    Vec3 trans;
    f32 root_x, root_z;
    Turn root_yaw;
};

// Разобрать файл; массивы — в арене. false — ошибка в *err
bool anim_parse(const u8 *data, usize size, Arena &arena, AnimClip &out, const char **err);
// Длина клипа, с: (ключей − 1) / 30
f32 anim_duration(const AnimClip &c);
// Привязка к скелету: на кость скелета номер кости клипа (−1 — нет в клипе, бинд); номер кости переноса в
// скелете — в *trans_bone. false — скелет не тот
bool anim_bind(const AnimClip &c, const Skel &s, i16 *map, i32 *trans_bone);
// Поза в момент t, с (петля — по кругу, иначе — зажато в [0, длина]); соседние ключи смешиваются
void anim_sample(const AnimClip &c, const i16 *map, u32 nbones, f32 t, AnimPose &out);
// a = a·(1 − w) + b·w (повороты — нормированная сумма по короткой дуге)
void anim_blend(AnimPose &a, const AnimPose &b, u32 nbones, f32 w);

// Метки клипа: касания стоп, окна удара и блока, отмена (ключи 30/с)
constexpr u32 ANIM_MAX_SPANS = 16;
struct AnimSpan {
    u16 a, b;            // ключи от и до, включительно
};
struct AnimLabels {
    AnimSpan foot[2][ANIM_MAX_SPANS];  // 0 — левая, 1 — правая
    u32 foot_count[2];
    AnimSpan hit[ANIM_MAX_SPANS];
    u32 hit_count;
    AnimSpan block[ANIM_MAX_SPANS];
    u32 block_count;
    i32 cancel;          // с какого ключа можно отменить, −1 — нет
    bool weapon;         // клип с мечом (weapon sword)
};
// Разобрать текст меток (docs/anim.md); незнакомые слова пропускаются. false — ошибка в err
bool anim_labels_parse(const u8 *data, usize size, AnimLabels &out, char *err, usize err_size);
// Попадает ли ключ k в один из отрезков
bool anim_span_has(const AnimSpan *s, u32 n, u32 k);
