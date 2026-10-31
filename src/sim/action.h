// Наборы действий (data/defs/actionsets.txt, вид actionset): действие = клип всем телом, длительность,
// скорость и метки. Основа human, наборы по семействам оружия берут её через parent и правят своё (как ActionSets
// у Bannerlord). Боец берёт набор по классу своего оружия. docs/combat.md, раздел «Наборы действий»
#ifndef ANA_ACTION_H
#define ANA_ACTION_H

#include "items.h"

// Действия, которые знает код: бой ставит по ним состояния, вид и звук читают клип и метки
typedef enum ActionId {
    ACT_FALL_BACK,    // сбит с ног ударом спереди или сбоку
    ACT_FALL_FRONT,   // сбит ударом в спину
    ACT_DOWN,         // лежит в сознании, прежде чем вставать
    ACT_GETUP_BACK,   // встаёт со спины
    ACT_GETUP_FRONT,  // встаёт ничком
    ACT_PICKUP,       // поднимает выроненное оружие
    ACT_STAGGER,      // пошатывается
    ACT_STRIKE,       // проводка удара после касания
    ACT_RECOVER,      // возврат оружия после удара
    ACT_BLOCK,        // держит блок после удара противника
    ACT_DODGE,        // держит уворот после удара противника
    ACT_KNEEL,        // сдался: на колени
    ACT_COLLAPSE,     // теряет сознание стоя
    ACT_DIE,          // убит ударом стоя
    ACT_BLEED,        // истёк кровью стоя
    ACT_SINK,         // ноги не держат: оседает
    ACT_TURN,         // со спины на живот, прежде чем ползти
    ACT_STANCE,       // стойка и блок с оружием: ключ клипа weapon_guard
    ACT_COUNT
} ActionId;

// Наборы: основа и по одному на класс оружия (номер — 1 + WeaponClass)
enum { AS_HUMAN, AS_COUNT = 1 + WC_COUNT };

// Длительность, с: time; + vary·бросок; по весу оружия kg — либо clamp(t + per_kg·(kg − kg_ref), time, max),
// либо t·(kg_add + kg/kg_div); затем ·(1 − стойкость/tough). Ноль в поле — части нет
typedef struct ActionDef {
    u8  clip;        // CharClip, CLIP_NONE — действие без клипа (только время)
    u8  key;         // номер ключа клипа (стойка оружия)
    f32 time, vary, per_kg, kg_ref, max, kg_add, kg_div, tough;
    f32 speed;       // темп клипа внутри действия, 1 — как записан
    f32 contact;     // метка: доля действия, когда тело касается земли (звук), 0 — нет
} ActionDef;

extern ActionDef g_actions[AS_COUNT][ACT_COUNT];
extern const char *const g_action_word[ACT_COUNT];

static inline const ActionDef *action_of(u8 weapon_class, ActionId a) { return &g_actions[1 + weapon_class][a]; }

static inline f32 action_time(const ActionDef *a, f32 roll, f32 kg, f32 tough)
{
    f32 t = a->time;
    if (a->vary != 0.0f) t = t + a->vary * roll;
    if (a->per_kg != 0.0f) t = clampf(t + a->per_kg * (kg - a->kg_ref), a->time, a->max);
    if (a->kg_div != 0.0f) t = t * (a->kg_add + kg / a->kg_div);
    if (a->tough != 0.0f) t = t * (1.0f - tough / a->tough);
    return t;
}

#endif
