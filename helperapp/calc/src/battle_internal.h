#ifndef EMERALD_BATTLE_INTERNAL_H
#define EMERALD_BATTLE_INTERNAL_H

/* Shared by the ported battle code in calc/src/. Not part of the public
 * API (emerald_calc.h). */

#include "emerald_calc.h"

#include "constants/battle_script_commands.h" /* ACC_CURR_MOVE, NO_ACC_CALC_CHECK_LOCK_ON */

/* The move-execution globals battle script commands hand state through,
 * one field per global, named after it. The game keeps these as globals
 * (reset by MoveValuesCleanUp()/HandleAction_UseMove()); here each
 * simulated hit gets its own copy, so a port reads
 * `ctx->battleMoveDamage` where the original reads `gBattleMoveDamage`. */
struct MoveContext {
    const struct BattleState *s;
    u8 battlerAttacker;                 /* gBattlerAttacker */
    u8 battlerTarget;                   /* gBattlerTarget */
    u16 currentMove;                    /* gCurrentMove */
    u16 sideStatuses[NUM_BATTLE_SIDES]; /* gSideStatuses -- copied, since Brick Break's removelightscreenreflect clears it mid-script */
    u16 dynamicBasePower;               /* gDynamicBasePower */
    u8 dynamicMoveType;                 /* gBattleStruct->dynamicMoveType */
    u8 dmgMultiplier;                   /* gBattleScripting.dmgMultiplier */
    u8 critMultiplier;                  /* gCritMultiplier */
    u32 hitMarker;                      /* gHitMarker -- the HITMARKER_IGNORE_* bits scripts set before accuracycheck */
    u8 moveResultFlags;                 /* gMoveResultFlags */
    u8 lastUsedAbility;                 /* gLastUsedAbility -- which ability typecalc blamed for NO_EFFECT */
    s32 battleMoveDamage;               /* gBattleMoveDamage */
};

/* HandleAction_UseMove()'s resets (src/battle_util.c) plus the turn-start
 * zeroing of gDynamicBasePower/dynamicMoveType (src/battle_main.c). */
void MoveContext_Init(struct MoveContext *ctx, const struct BattleState *s, u8 battlerAtk, u8 battlerDef, u16 move);

/* --- RNG ------------------------------------------------------------------
 * Random() (src/random.c) returns the top 16 bits of the LCG state, which
 * over the generator's period takes every u16 value equally often. These
 * give the exact probability of the `Random() % mod` checks the scripts
 * make, modulo bias included (65536 isn't a multiple of 100 or 3). */
double RandomModBelow(u32 mod, u32 threshold); /* P(Random() % mod < threshold) */
double RandomModEquals(u32 mod, u32 value);    /* P(Random() % mod == value) */

/* --- src/battle_util.c / src/pokemon.c helpers (battle_util.c) --------- */

u8 GetBattlerSide(const struct BattleState *s, u8 battler);
bool8 IsBattlerOfType(const struct BattleState *s, u8 battler, u8 type); /* IS_BATTLER_OF_TYPE */
bool8 AbilityOnField(const struct BattleState *s, u8 ability);  /* ABILITY_ON_FIELD: living battlers only */
bool8 AbilityOnField2(const struct BattleState *s, u8 ability); /* ABILITY_ON_FIELD2: fainted battlers count too */
bool8 FieldSportActive(const struct BattleState *s, u32 status3Sport); /* ABILITYEFFECT_FIELD_SPORT for STATUS3_MUDSPORT/WATERSPORT */
bool8 WeatherHasEffect(const struct BattleState *s);  /* WEATHER_HAS_EFFECT */
bool8 WeatherHasEffect2(const struct BattleState *s); /* WEATHER_HAS_EFFECT2 */
u8 CountAliveMonsInBattleDefSide(const struct BattleState *s, u8 battlerTarget); /* CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE) */
bool8 FlagGet(const struct BattleState *s, u16 id);
bool8 ShouldGetStatBadgeBoost(const struct BattleState *s, u16 badgeFlag, u8 battler);
/* The `item == ITEM_ENIGMA_BERRY ? gEnigmaBerries[battler] : GetItemHoldEffect(item)`
 * pattern every hold-effect read in the battle code uses. */
void GetBattlerHoldEffect(const struct BattleState *s, u8 battler, u8 *holdEffect, u8 *holdEffectParam);
u8 GetScaledHPFraction(s16 hp, s16 maxhp, u8 scale); /* src/battle_interface.c */
bool8 IsSoundMove(u16 move); /* sSoundMovesTable (src/battle_util.c) */

/* --- src/pokemon.c / src/battle_script_commands.c damage code (damage.c) - */

s32 CalculateBaseDamage(struct MoveContext *ctx, const struct BattlePokemon *attacker,
                        const struct BattlePokemon *defender, u32 move, u16 sideStatus,
                        u16 powerOverride, u8 typeOverride, u8 battlerIdAtk, u8 battlerIdDef);
void Cmd_damagecalc(struct MoveContext *ctx);
void Cmd_typecalc(struct MoveContext *ctx);
void Cmd_typecalc2(struct MoveContext *ctx);
u8 GetMoveType(const struct MoveContext *ctx, u16 move); /* GET_MOVE_TYPE */

/* Cmd_critcalc: probability it sets gCritMultiplier = 2. */
double CritChance(const struct MoveContext *ctx);

/* Cmd_accuracycheck (src/battle_script_commands.c): probability it passes.
 * `accMove` is the command's argument: ACC_CURR_MOVE, NO_ACC_CALC_CHECK_LOCK_ON,
 * or a move id. On a guaranteed miss sets *missReason
 * (EMERALD_REASON_TARGET_PROTECTED / _SEMI_INVULNERABLE). */
double AccuracyCheck(const struct MoveContext *ctx, u16 accMove, u8 *missReason);

/* --- Power / type-setting script commands (move_scripts.c) -----------------
 * The ones that only read BattleState and write the context; exposed so the
 * differential test can hold them against the originals. */
void Cmd_weightdamagecalculation(struct MoveContext *ctx);
void Cmd_remaininghptopower(struct MoveContext *ctx);
void Cmd_scaledamagebyhealthratio(struct MoveContext *ctx);
void Cmd_friendshiptodamagecalculation(struct MoveContext *ctx);
void Cmd_hiddenpowercalc(struct MoveContext *ctx);
void Cmd_setweatherballtype(struct MoveContext *ctx);

/* ApplyRandomDmgMultiplier(), enumerated over all 16 rand % 16 values. */
EmeraldCalcRollSet RandomDamageRolls(s32 damage);
/* adjustsetdamage's no-random-roll damage, as a roll set of 16 equal values. */
EmeraldCalcRollSet FixedDamageRolls(s32 damage);

#endif /* EMERALD_BATTLE_INTERNAL_H */
