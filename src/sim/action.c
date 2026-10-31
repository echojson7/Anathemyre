#include "action.h"

ActionDef g_actions[AS_COUNT][ACT_COUNT];

const char *const g_action_word[ACT_COUNT] = {
    "fall_back", "fall_front", "down", "getup_back", "getup_front", "pickup", "stagger", "strike", "recover",
    "block", "dodge", "kneel", "collapse", "die", "bleed", "sink", "turn", "stance",
};
