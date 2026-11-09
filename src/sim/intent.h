// Намерения: игрок и ИИ выдают только их, исполняет шаг мира (docs/invariants.md, шов 3).
// Намерения игрока пишутся в запись повтора по шагам
#pragma once
#include "../core/wpos.h"

enum IntentKind : u32 {
    INTENT_NONE = 0,
    INTENT_ATTRACT = 1,  // точки тянутся к target 3 секунды
    INTENT_RELEASE = 2,  // отпустить
    INTENT_BURST = 3,    // часть точек гибнет и рождается у target
    INTENT_CLOCK_SET = 4,   // час дня: arg — доля суток в оборотах (2^32 — сутки), день тот же (отладка)
    INTENT_CLOCK_RATE = 5,  // ход часов: arg — тиков за шаг (отладка)
    INTENT_PEOPLE_SPAWN = 6,  // родить людей у target: arg — число | радиус, м << 16 (отладка, бенч)
    INTENT_GO = 7,            // идти в target: arg — номер человека | аллюр << 32 (0 шаг, 1 трусца, 2 бег)
};

struct Intent {
    u64 step;  // шаг, в начале которого намерение исполняется
    u32 kind;
    WPos target;
    u64 arg;   // число намерения (часы, люди); у остальных 0
};
