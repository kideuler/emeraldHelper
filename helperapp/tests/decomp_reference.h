#ifndef EMERALD_DECOMP_REFERENCE_H
#define EMERALD_DECOMP_REFERENCE_H

/* The decomp's *original* battle code, compiled for the host (see
 * decomp_reference.c), driven from a struct BattleState. The differential
 * test (test_decomp_diff.cpp) checks calc/src/'s port against it. */

#include "battle_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Copies `s` into the reference's gBattleMons/gSideStatuses/... globals and
 * sets gBattlerAttacker/gBattlerTarget/gCurrentMove, with every
 * move-execution global reset as at the start of a move. */
void Ref_Load(const struct BattleState *s, u8 attacker, u8 target, u16 move);

/* Set the move-execution globals a battle script would have set by now. */
void Ref_SetDynamic(u16 dynamicBasePower, u8 dynamicMoveType, u8 dmgMultiplier, u32 hitMarker);

/* Cmd_damagecalc then Cmd_typecalc with gCritMultiplier = critMultiplier. */
void Ref_DamageCalcTypeCalc(u8 critMultiplier, s32 *damage, u8 *moveResultFlags, u8 *lastUsedAbility);

/* Cmd_critcalc run once for every value Random() can return; the fraction
 * that set gCritMultiplier = 2. */
double Ref_CritChance(void);

/* Cmd_accuracycheck with argument `accMove`, run once for every value
 * Random() can return; the fraction that didn't jump to its fail label. */
double Ref_Accuracy(u16 accMove);

/* One of the power/type-setting script commands. Returns gDynamicBasePower
 * afterwards; *dynamicMoveType / *dmgMultiplier get
 * gBattleStruct->dynamicMoveType / gBattleScripting.dmgMultiplier. */
enum RefPowerCommand {
    REF_WEIGHT_DAMAGE_CALCULATION,
    REF_REMAINING_HP_TO_POWER,
    REF_SCALE_DAMAGE_BY_HEALTH_RATIO,
    REF_FRIENDSHIP_TO_DAMAGE_CALCULATION,
    REF_HIDDEN_POWER_CALC,
    REF_SET_WEATHER_BALL_TYPE,
};
u16 Ref_PowerCommand(int command, u8 *dynamicMoveType, u8 *dmgMultiplier);

#ifdef __cplusplus
}
#endif

#endif /* EMERALD_DECOMP_REFERENCE_H */
