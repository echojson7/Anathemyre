// Намерения: игрок и ИИ выдают только их, исполняет шаг мира (docs/invariants.md, шов 3).
// Намерения игрока пишутся в запись повтора по шагам
#pragma once
#include "../core/wpos.h"

enum IntentKind : u32 {
    INTENT_NONE = 0,
    INTENT_ATTRACT = 1,  // точки тянутся к target 3 секунды
    INTENT_RELEASE = 2,  // отпустить
    INTENT_BURST = 3,    // часть точек гибнет и рождается у target
};

struct Intent {
    u64 step;  // шаг, в начале которого намерение исполняется
    u32 kind;
    WPos target;
};
