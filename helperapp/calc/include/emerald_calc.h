#ifndef EMERALD_CALC_H
#define EMERALD_CALC_H

#include <stdint.h>

/*
 * CODE_PLAN.md Phase 5: damage roll calculation, ported from the decomp's
 * own CalculateBaseDamage()/TypeCalc() (src/pokemon.c, src/battle_script_
 * commands.c) rather than reimplemented from a formula writeup -- Gen 3
 * truncates at intermediate steps in ways a hand-rolled calculator gets
 * subtly wrong.
 *
 * Design note on struct BattlePokemon: the real one (include/pokemon.h)
 * can't be used directly here without pulling in include/global.h's GBA
 * hardware headers (gba/gba.h), which don't compile for a host tool. So
 * EmeraldCalcMon below is a plain (non-bitfield) struct holding only the
 * fields CalculateBaseDamage()/TypeCalc() actually read. It's populated by
 * explicit field assignment from the already-decoded BattleMon (see
 * helperapp/src/calcbridge.cpp), never by reinterpreting raw bytes, so
 * there is no struct-layout/bitfield-packing risk here the way there would
 * be for the Phase 2 memory decoder.
 *
 * Every numeric id used below (TYPE_*, ABILITY_*, HOLD_EFFECT_*, MOVE_*,
 * STATUS*_*, ...) comes directly from the project's own headers under
 * include/constants -- those headers are pure #define lists with no GBA
 * dependency, so they're #included as-is rather than re-transcribed.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef u8 bool8;
typedef u32 bool32;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

/* NOTE: included by basename, not "constants/foo.h" -- the include path
 * added for these is include/constants itself (not include/), precisely
 * so it can't shadow unrelated system headers for any other translation
 * unit that ends up with this path on its search list (include/strings.h
 * -- pret's own game-text declarations -- collided with libc's
 * <strings.h> the first time this was tried with the wider path). */
#include "abilities.h"
#include "battle.h"
#include "battle_move_effects.h"
#include "hold_effects.h"
#include "items.h"
#include "moves.h"
#include "pokemon.h"
#include "species.h"

/* include/battle.h defines these too, but that header cascades into the
 * GBA hardware/engine headers -- copied verbatim from
 * include/battle.h:46-53 instead (see this file's header comment). */
#define MOVE_TARGET_SELECTED         0
#define MOVE_TARGET_DEPENDS          (1 << 0)
#define MOVE_TARGET_USER_OR_SELECTED (1 << 1)
#define MOVE_TARGET_RANDOM           (1 << 2)
#define MOVE_TARGET_BOTH             (1 << 3)
#define MOVE_TARGET_USER             (1 << 4)
#define MOVE_TARGET_FOES_AND_ALLY    (1 << 5)
#define MOVE_TARGET_OPPONENTS_FIELD  (1 << 6)

/* struct BattleMove: field-for-field identical to include/pokemon.h's
 * struct BattleMove (effect, power, type, accuracy, pp,
 * secondaryEffectChance, target, priority, flags) so that
 * src/data/battle_moves.h -- the real, unmodified move table for all
 * MOVES_COUNT moves -- can be #included as-is against it. */
struct BattleMove {
    u8 effect;
    u8 power;
    u8 type;
    u8 accuracy;
    u8 pp;
    u8 secondaryEffectChance;
    u8 target;
    s8 priority;
    u8 flags;
};

extern const struct BattleMove gBattleMoves[MOVES_COUNT];

typedef struct EmeraldCalcMon {
    u16 species;
    u16 attack;
    u16 defense;
    u16 spAttack;
    u16 spDefense;
    u16 hp;
    u16 maxHp;
    s8 statStages[NUM_BATTLE_STATS]; /* indexed by STAT_ATK/DEF/SPATK/SPDEF (1..5), same convention as gBattleMons */
    u8 ability;
    u8 types[2];
    u8 level;
    u16 item;
    u32 status1;
    bool8 hasFocusEnergy; /* STATUS2_FOCUS_ENERGY: doubles crit chance. Not part of BattleMon's decoded fields (status2), so it's surfaced here explicitly rather than pulled from a snapshot. */
} EmeraldCalcMon;

/* Battle-context inputs CalculateBaseDamage()/Cmd_damagecalc() would
 * normally read off live global battle state (gBattleTypeFlags,
 * gSideStatuses, gBattleWeather, gStatuses3, gProtectStructs, ...) that a
 * memory snapshot doesn't carry (and that a "what would this roll be if…"
 * calculator arguably wants to be user-settable anyway). All default to
 * "no effect" -- see EmeraldCalc_DefaultField(). */
typedef struct EmeraldCalcFieldConditions {
    bool8 isDoubleBattle;
    bool8 targetSideHasTwoAliveMons; /* for Reflect/Light Screen's 2/3 (not 1/2) reduction, and spread-move halving */
    bool8 targetHasReflect;
    bool8 targetHasLightScreen;
    u32 weather; /* B_WEATHER_* bitmask; 0 = clear */
    bool8 attackerIsCharged;    /* STATUS3_CHARGED_UP: doubles Electric damage */
    bool8 attackerHasHelpingHand; /* x1.5 */
    /* Simplification: real badge boosts are per-stat, per-badge, and only
     * apply to the player's own battler (ShouldGetStatBadgeBoost() reads
     * save-block flags we don't have in a snapshot). This applies all four
     * (+10% Attack/Defense/Sp.Atk/Sp.Def) to `attacker`/`defender`
     * uniformly when set -- a reasonable stand-in for "player has all
     * gym badges", not a faithful port of the per-badge gating. */
    bool8 applyBadgeBoosts;
} EmeraldCalcFieldConditions;

EmeraldCalcFieldConditions EmeraldCalc_DefaultField(void);

/* Sets the item -> hold effect / hold effect param lookup used by
 * GetItemHoldEffect()/GetItemHoldEffectParam() in the port. Populate from
 * helperapp/config/item_hold_effects_us_rev0.json (see
 * tools/gen_item_hold_effects.py); `count` must be ITEMS_COUNT-sized
 * arrays indexed by item id. Not calling this leaves every item's hold
 * effect as HOLD_EFFECT_NONE. */
void EmeraldCalc_SetItemHoldEffects(const u8 *holdEffect, const u8 *holdEffectParam, u16 count);

/* Ported from src/item.c; read the table set by EmeraldCalc_SetItemHoldEffects(). */
u8 GetItemHoldEffect(u16 itemId);
u8 GetItemHoldEffectParam(u16 itemId);

#define EMERALD_CALC_NUM_ROLLS 16

typedef struct EmeraldCalcRollSet {
    s32 rolls[EMERALD_CALC_NUM_ROLLS]; /* index 0 = 100% roll ... index 15 = 85% roll, matches ApplyRandomDmgMultiplier()'s 100 - (rand%16) */
    s32 min;
    s32 max;
} EmeraldCalcRollSet;

typedef struct EmeraldCalcResult {
    EmeraldCalcRollSet normal;
    EmeraldCalcRollSet critical;
    double critChance; /* 0..1, from the real sCriticalHitChance table */
    u8 typeEffectivenessFlags; /* MOVE_RESULT_* bits from TypeCalc()/ModulateDmgByType2() */
    bool8 isImmune;
    bool8 movePowerIsZero; /* status move: rolls are meaningless, all zero */
} EmeraldCalcResult;

/* MOVE_RESULT_* flags mirrored from include/battle.h (that header itself
 * isn't include-safe here -- see this file's header comment) so callers
 * can interpret typeEffectivenessFlags. */
#define EMERALD_MOVE_RESULT_MISSED              (1 << 0)
#define EMERALD_MOVE_RESULT_SUPER_EFFECTIVE     (1 << 1)
#define EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE  (1 << 2)
#define EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE   (1 << 3)
#define EMERALD_MOVE_RESULT_FOE_ENDURED         (1 << 4)
#define EMERALD_MOVE_RESULT_FAILED              (1 << 5)
#define EMERALD_MOVE_RESULT_FOE_HUNG_ON         (1 << 6)
#define EMERALD_MOVE_RESULT_NO_EFFECT           (EMERALD_MOVE_RESULT_MISSED | EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE | EMERALD_MOVE_RESULT_FAILED)

/* Computes the 16-value damage roll set for `move` used by `attacker`
 * against `defender`, both without (normal) and with (critical) a
 * critical hit -- Phase 5.3: "Compute crit and non-crit sets separately
 * and surface both." `move` must be a valid MOVE_* id (not MOVE_NONE); use
 * gBattleMoves[move] for its power/type/etc. */
EmeraldCalcResult EmeraldCalc_ComputeDamage(const EmeraldCalcMon *attacker, const EmeraldCalcMon *defender,
                                             u16 move, const EmeraldCalcFieldConditions *field);

typedef struct EmeraldCalcKOChances {
    double ohkoChance;               /* fraction of the 16 rolls >= targetHp */
    double accuracyAdjustedOhkoChance; /* ohkoChance * moveAccuracy/100 */
    s32 guaranteedKoTurns;           /* smallest n where n * min(rolls) >= targetHp, else -1 */
    s32 possibleKoTurns;             /* smallest n where n * max(rolls) >= targetHp, else -1 */
} EmeraldCalcKOChances;

/* Phase 5.4 derived output: OHKO chance, guaranteed-vs-possible turns to
 * KO, and move accuracy folded in ("a guaranteed OHKO at 70% accuracy is a
 * different decision"). `moveAccuracy` is gBattleMoves[move].accuracy (0
 * means never-miss moves like Swift -- treated as 100 here). */
EmeraldCalcKOChances EmeraldCalc_KOChances(const EmeraldCalcRollSet *rolls, u16 targetHp, u8 moveAccuracy);

#ifdef __cplusplus
}
#endif

#endif /* EMERALD_CALC_H */
