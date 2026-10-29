// Тело человека из гладких форм (поле расстояний) -> сетка (surface nets).
// Мокап детализации: тело, лицо, кисти, волосы, бельё; поза привязки класса H
#ifndef ANA_HUMAN_H
#define ANA_HUMAN_H

#include "../core/arena.h"
#include "../render/mesh.h"

typedef struct HumanShape {
    f32 female;   // 0 мужчина .. 1 женщина
    f32 muscle;   // 0 сухой .. 1 мускулистый
    f32 fat;      // 0 худой .. 1 полный
    V3  skin, hair, under;
    u8  hair_style;  // 0 коротко, 1 бритый, 2 длинные собранные
    bool beard;
} HumanShape;

// Сетка тела; scratch — временная память под сетку поля
void human_build(MeshBuilder *mb, Arena *scratch, const HumanShape *s);

#endif
