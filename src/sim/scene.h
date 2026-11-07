// Описание сцены (data/scene.txt, docs/text.md): что стоит в мире при создании. Текст читает app,
// разбирает и ставит шаг мира; хеш текста идёт в запись повтора (хеш описаний)
#pragma once
#include "world.h"

#include "../core/text.h"

// Текст заготовки по имени (data/prefabs/<имя>.txt): читает app, он же кладёт её хеш в хеш описаний.
// false — заготовки нет
typedef bool (*ScenePrefabFn)(void *ctx, Str name, const u8 **text, usize *size);

// Разобрать и поставить объекты и заготовки. false — ошибка, текст в err (с номером строки)
bool scene_load(World &w, const u8 *text, usize size, ScenePrefabFn prefab, void *ctx, char *err, usize err_cap);
