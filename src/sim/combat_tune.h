// Числа боя из data/defs/combat.txt (блок tuning combat), что какое значит — там же и docs/combat.md.
// Читаются один раз при загрузке описаний; бой берёт их из g_tune, без поиска по имени
#ifndef ANA_COMBAT_TUNE_H
#define ANA_COMBAT_TUNE_H

#include "../core/types.h"

// Ключ файла = имя поля
#define COMBAT_TUNE(X)                                                                                                 \
    /* нагрузка */                                                                                                     \
    X(load_free_kg) X(load_free_str_div) X(encumbrance_per_kg) X(encumbrance_max)                                      \
    X(weapon_free_kg) X(weapon_free_str_div) X(attack_overweight_pen) X(windup_overweight)                             \
    /* шок, броня, окружение, руки и ноги */                                                                           \
    X(shock_penalty) X(shock_full) X(armor_dodge_pen_max) X(surround_r) X(surround_mul)                                \
    X(attack_arm_min) X(defense_arm_l_min) X(defense_arm_r_min) X(move_legs_min)                                       \
    /* реакция, блок, уворот, контратака */                                                                            \
    X(react_angle_deg) X(react_recover_frac) X(block_base) X(block_skill_div) X(block_swing_pen)                       \
    X(polearm_close_d2) X(polearm_close_pen) X(block_min) X(block_max) X(dodge_base) X(dodge_skill_div)                \
    X(dodge_swing_bonus) X(dodge_max) X(guard_break) X(guard_break_arm) X(counter_max) X(counter_windup)               \
    /* замах, удар, урон */                                                                                            \
    X(windup_base) X(windup_dex_div) X(windup_attack_div) X(strike_reach_add) X(cleave_falloff)                        \
    X(dmg_str_base) X(dmg_str_div) X(dmg_rand_min) X(dmg_rand) X(thrust_cover) X(dmg_tough_div)                        \
    /* прицел */                                                                                                       \
    X(aim_dex_div) X(aim_high_p) X(aim_high_y) X(aim_high_dy) X(aim_high_s) X(aim_high_ds)                             \
    X(aim_mid_p) X(aim_mid_y) X(aim_mid_dy) X(aim_mid_s) X(aim_mid_ds) X(aim_low_y) X(aim_low_dy) X(aim_low_s)         \
    X(aim_over_x) X(aim_over_dx) X(aim_over_y) X(aim_over_s) X(aim_over_ds)                                            \
    X(aim_thrust_x) X(aim_face_p) X(aim_face_y) X(aim_thrust_y) X(aim_thrust_dy)                                       \
    /* раны, кровь, шок */                                                                                             \
    X(bleed_cut) X(bleed_pierce) X(bleed_blunt) X(sever_bleed) X(bleed_gut_mul) X(bleed_stop)                          \
    X(shock_blunt) X(shock_recover) X(shock_recover_tough) X(fracture_at) X(blood_full) X(blood_mass)                  \
    X(blood_ko) X(ko_tough) X(ko_heal) X(ko_blood) X(wake_blood)                                                       \
    /* сбить с ног, пошатнуть, выбить оружие */                                                                        \
    X(knock_shock) X(stagger_blunt) X(knock_resist) X(knock_push_min) X(drop_base) X(drop_k) X(drop_max)               \
    X(drop_str_div)                                                                                                    \
    /* мораль */                                                                                                       \
    X(morale_per_dmg) X(morale_calm_s) X(morale_recover) X(morale_shaken) X(morale_flee) X(morale_rally)               \
    X(morale_surrender) X(outnumber_r) X(morale_outnumber)                                                             \
    /* пауза между ударами */                                                                                          \
    X(first_pause) X(pause_min) X(pause_rand) X(pause_attack_div) X(pause_shaken_mul)                                  \
    /* шаг и поворот */                                                                                                \
    X(speed_ready) X(speed_retreat) X(speed_block) X(speed_dodge) X(speed_crawl) X(athletics_div)                      \
    X(turn) X(turn_windup) X(turn_crawl) X(turn_retreat)                                                               \
    /* навыки */                                                                                                       \
    X(skill_max) X(gain_rate) X(gain_own_div) X(gain_opp_div) X(gain_mul_min) X(gain_mul_max)                          \
    X(gain_attack_try) X(gain_weapon_try) X(gain_strength) X(gain_strength_opp) X(gain_dex) X(dex_weapon_kg)           \
    X(gain_block) X(gain_block_weapon) X(gain_dodge_try) X(gain_dodge) X(gain_hit) X(gain_hit_defense)                 \
    X(gain_tough) X(gain_tough_ko_mul)                                                                                 \
    /* ИИ бойца */                                                                                                     \
    X(target_attacker) X(target_current) X(target_wounded) X(target_turn) X(pick_max_r) X(cornered_r)                 \
    X(reach_far) X(reach_close) X(approach_slow_d) X(approach_slow) X(back_off) X(circle) X(ally_r)                    \
    X(attack_angle_deg) X(thrust_edge)                                                                                 \
    /* сила бойца для боя формулой */                                                                                  \
    X(rating_attack) X(rating_defense) X(rating_dodge) X(rating_tough) X(rating_strength) X(rating_dmg)                \
    X(rating_dmg_base) X(rating_armor) X(rating_health_min) X(rating_health_k)

#define COMBAT_TUNE_FIELD(n) f32 n;
typedef struct CombatTune {
    COMBAT_TUNE(COMBAT_TUNE_FIELD)
} CombatTune;
#undef COMBAT_TUNE_FIELD

extern CombatTune g_tune;

#endif
