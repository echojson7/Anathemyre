// Имена жителей пустоши: старославянские, древнегреческие, старолатинские.
// Выдаются по порядку из своего списка для мужчин и женщин, без повторов, пока список не кончится
#ifndef ANA_NAMES_H
#define ANA_NAMES_H

#include "../core/ser.h"

const char *name_next(bool female);
// Сохранение: сколько имён уже выдано
void names_ser(Ser *s);

#endif
