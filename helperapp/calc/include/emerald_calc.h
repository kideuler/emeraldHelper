#ifndef EMERALD_CALC_H
#define EMERALD_CALC_H

/*
 * CODE_PLAN.md Phase 5: move outcome calculation, ported from the decomp
 * rather than reimplemented from a formula writeup -- Gen 3 truncates at
 * intermediate steps, and routes a lot of moves through special-case
 * script commands, in ways a hand-rolled calculator gets subtly wrong.
 *
 * Input is a struct BattleState (battle_state.h): the game's own battle
 * globals, mirrored one for one. EmeraldCalc_SimulateMove() then runs the
 * move's battle script from data/battle_scripts_1.s -- the same sequence
 * of script commands the game would, in the same order -- up to the point
 * damage is dealt:
 *
 *   effect-specific setup    e.g. weightdamagecalculation (Low Kick),
 *                            remaininghptopower (Flail), hiddenpowercalc,
 *                            setweatherballtype, Facade/Stomp/Surf/Gust/
 *                            Earthquake's `setbyte sDMG_MULTIPLIER, 2` ...
 *   attackcanceler           Soundproof, Protect
 *   accuracycheck            stat stages, Compound Eyes/Sand Veil/Hustle,
 *                            BrightPowder, Thunder in rain/sun, Fly/Dig/
 *                            Dive, Lock-On; then Volt/Water Absorb, Flash
 *                            Fire
 *   critcalc                 Focus Energy, high-crit moves, Scope Lens,
 *                            Stick, Lucky Punch, Battle/Shell Armor
 *   damagecalc               CalculateBaseDamage() + crit/dmgMultiplier,
 *                            Charge, Helping Hand
 *   typecalc                 STAB, type chart, Foresight, Levitate,
 *                            Wonder Guard
 *   adjustnormaldamage       the 85-100% random roll; Focus Band, Endure,
 *                            False Swipe
 *
 * Every Random() the script would call (damage roll, crit, accuracy,
 * multi-hit count, Magnitude, Present, Psywave, Focus Band) is enumerated
 * rather than rolled: the result is the full set of outcomes, and
 * EmeraldCalc_KOChance() turns that into an exact KO probability.
 *
 * Numeric ids (TYPE_*, ABILITY_*, HOLD_EFFECT_*, MOVE_*, EFFECT_*, ...) all
 * come straight from the project's include/constants headers, via
 * battle_state.h.
 */

#include "battle_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* include/battle.h defines these too, but that header cascades into the
 * GBA hardware/engine headers -- copied verbatim from include/battle.h
 * instead (see battle_state.h's comment on constants). */
#define MOVE_TARGET_SELECTED         0
#define MOVE_TARGET_DEPENDS          (1 << 0)
#define MOVE_TARGET_USER_OR_SELECTED (1 << 1)
#define MOVE_TARGET_RANDOM           (1 << 2)
#define MOVE_TARGET_BOTH             (1 << 3)
#define MOVE_TARGET_USER             (1 << 4)
#define MOVE_TARGET_FOES_AND_ALLY    (1 << 5)
#define MOVE_TARGET_OPPONENTS_FIELD  (1 << 6)

/* --- Game data, compiled in from the decomp's own tables ----------------- */

/* include/pokemon.h struct BattleMove, field for field, so that
 * src/data/battle_moves.h -- the real, unmodified move table -- can be
 * #included as-is against it (calc/src/game_data.c). */
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

/* include/pokemon.h struct SpeciesInfo, likewise for
 * src/data/pokemon/species_info.h (Beat Up reads base Attack/Defense). */
struct SpeciesInfo {
    u8 baseHP;
    u8 baseAttack;
    u8 baseDefense;
    u8 baseSpeed;
    u8 baseSpAttack;
    u8 baseSpDefense;
    u8 types[2];
    u8 catchRate;
    u8 expYield;
    u16 evYield_HP:2;
    u16 evYield_Attack:2;
    u16 evYield_Defense:2;
    u16 evYield_Speed:2;
    u16 evYield_SpAttack:2;
    u16 evYield_SpDefense:2;
    u16 itemCommon;
    u16 itemRare;
    u8 genderRatio;
    u8 eggCycles;
    u8 friendship;
    u8 growthRate;
    u8 eggGroups[2];
    u8 abilities[2];
    u8 safariZoneFleeRate;
    u8 bodyColor : 7;
    u8 noFlip : 1;
};

extern const struct SpeciesInfo gSpeciesInfo[];

/* Ported from src/pokemon.c / src/pokedex.c; backed by the real
 * sSpeciesToNationalPokedexNum and src/data/pokemon/pokedex_entries.h
 * (Low Kick reads the target's weight through these). */
u16 SpeciesToNationalPokedexNum(u16 species);
u16 GetPokedexHeightWeight(u16 dexNum, u8 data); /* data: 0 = height (dm), 1 = weight (hg) */

/* Sets the item -> hold effect / hold effect param lookup used by
 * GetItemHoldEffect()/GetItemHoldEffectParam(). Populate from
 * helperapp/config/item_hold_effects_us_rev0.json (tools/
 * gen_item_hold_effects.py) -- gItems[] itself carries function pointers
 * and can't be compiled in the way the tables above are. `count` is the
 * length of both arrays, indexed by item id. Not calling this leaves every
 * item's hold effect as HOLD_EFFECT_NONE. */
void EmeraldCalc_SetItemHoldEffects(const u8 *holdEffect, const u8 *holdEffectParam, u16 count);

/* Ported from src/item.c; read the table set above. */
u8 GetItemHoldEffect(u16 itemId);
u8 GetItemHoldEffectParam(u16 itemId);

/* --- Results ------------------------------------------------------------- */

#define EMERALD_CALC_NUM_ROLLS 16
#define EMERALD_CALC_MAX_BRANCHES 11 /* Psywave: 11 equally likely multipliers */
#define EMERALD_CALC_MAX_HITS PARTY_SIZE /* Beat Up: one hit per party member */

/* The 16 values ApplyRandomDmgMultiplier() can produce: index 0 = 100%
 * roll ... index 15 = 85% roll, matching its 100 - (Random() % 16). Moves
 * that go through adjustsetdamage instead (fixed damage) have all 16
 * equal. */
typedef struct EmeraldCalcRollSet {
    s32 rolls[EMERALD_CALC_NUM_ROLLS];
    s32 min;
    s32 max;
} EmeraldCalcRollSet;

/* One hit's damage. */
typedef struct EmeraldCalcHit {
    u16 power;                   /* base power CalculateBaseDamage() ran with (gDynamicBasePower when set); 0 when damage isn't power-based */
    EmeraldCalcRollSet normal;   /* gCritMultiplier == 1 */
    EmeraldCalcRollSet critical; /* gCritMultiplier == 2 (same as normal when the move can't crit) */
} EmeraldCalcHit;

/* One outcome of a random choice made before damage is calculated:
 * Magnitude's magnitude, Present's power, Psywave's multiplier. Every other
 * move has exactly one branch, with probability 1. */
typedef struct EmeraldCalcBranch {
    double probability;
    bool8 healsTarget; /* Present's heal branch: hit.normal holds HP restored, not damage */
    EmeraldCalcHit hit;
} EmeraldCalcBranch;

enum EmeraldCalcOutcome {
    EMERALD_CALC_DEALS_DAMAGE,   /* branches/hits below are the real damage */
    EMERALD_CALC_STATUS_MOVE,    /* no damage-dealing script (Growl, Swords Dance, Metronome, ...) */
    EMERALD_CALC_NO_EFFECT,      /* would run, but can't affect this target -- see `reason` */
    EMERALD_CALC_FAILS,          /* the move's own script fails in this state -- see `reason` */
    EMERALD_CALC_DAMAGE_UNKNOWN, /* damage comes from something that hasn't happened yet -- see `reason` */
};

enum EmeraldCalcReason {
    EMERALD_REASON_NONE,
    EMERALD_REASON_TYPE_IMMUNITY,            /* type chart 0x (Normal/Fighting -> Ghost unless Foresight) */
    EMERALD_REASON_LEVITATE,
    EMERALD_REASON_WONDER_GUARD,
    EMERALD_REASON_VOLT_ABSORB,
    EMERALD_REASON_WATER_ABSORB,
    EMERALD_REASON_FLASH_FIRE,
    EMERALD_REASON_SOUNDPROOF,
    EMERALD_REASON_DAMP,                     /* Explosion / Self-Destruct */
    EMERALD_REASON_STURDY,                   /* OHKO moves */
    EMERALD_REASON_TARGET_LEVEL_HIGHER,      /* OHKO moves */
    EMERALD_REASON_NO_STOCKPILE,             /* Spit Up */
    EMERALD_REASON_TARGET_HP_NOT_HIGHER,     /* Endeavor */
    EMERALD_REASON_USER_NOT_ASLEEP,          /* Snore */
    EMERALD_REASON_TARGET_NOT_ASLEEP,        /* Dream Eater */
    EMERALD_REASON_TARGET_BEHIND_SUBSTITUTE, /* Dream Eater */
    EMERALD_REASON_NOT_FIRST_TURN,           /* Fake Out */
    EMERALD_REASON_LOST_FOCUS,               /* Focus Punch after being hit this turn */
    EMERALD_REASON_NOT_HIT_THIS_TURN,        /* Counter / Mirror Coat before taking a qualifying hit */
    EMERALD_REASON_BIDE,                     /* Bide: 2x the damage taken while it charges */
    EMERALD_REASON_NO_PARTY_MEMBER_CAN_ATTACK, /* Beat Up */
    /* Reasons `accuracy` is 0 (outcome stays DEALS_DAMAGE -- the damage is
     * what it would be if the target could be hit): */
    EMERALD_REASON_TARGET_PROTECTED,
    EMERALD_REASON_TARGET_SEMI_INVULNERABLE, /* Fly/Bounce, Dig, Dive vs a move that can't reach it */
};

typedef struct EmeraldCalcResult {
    u8 outcome; /* enum EmeraldCalcOutcome */
    u8 reason;  /* enum EmeraldCalcReason */

    u16 move;           /* the move actually executed: differs from the one asked about for Nature Power */
    u8 moveType;        /* after Hidden Power / Weather Ball's dynamic type */
    u8 moveResultFlags; /* typecalc's MOVE_RESULT_* (SUPER_EFFECTIVE, NOT_VERY_EFFECTIVE, ...) */
    bool8 landsLater;   /* not dealt this turn: a two-turn move's charging turn (Solar Beam, Fly, ...) or Future Sight/Doom Desire */

    double accuracy;       /* P(accuracycheck passes) -- for every hit when accuracyPerHit, else once per use. 1 when the script doesn't check */
    bool8 accuracyPerHit;  /* Triple Kick: each hit re-checks and the move stops at the first miss */
    u8 missReason;         /* why accuracy is 0, when it is (TARGET_PROTECTED / TARGET_SEMI_INVULNERABLE) */
    double critChance;     /* per hit; 0 when the move can't crit (fixed damage, Spit Up, Future Sight, ...) */

    /* Alternatives (probabilities sum to 1). Every hit rolls its branch's
     * `hit` independently -- crit and random roll re-rolled per hit. */
    u8 numBranches;
    EmeraldCalcBranch branches[EMERALD_CALC_MAX_BRANCHES];

    /* How many times branches' hit repeats: 1 for most moves, 2 for
     * Double Kick/Twineedle/Bonemerang, 2-5 (3/8, 3/8, 1/8, 1/8) for the
     * multi-hit moves. hitCountChance[n] = P(exactly n hits). */
    u8 minHits;
    u8 maxHits;
    double hitCountChance[EMERALD_CALC_MAX_HITS + 1];

    /* Triple Kick (10/20/30 power) and Beat Up (one party member per hit)
     * hit with a different damage each time instead: hit i is
     * sequentialHits[i], and branches/minHits/maxHits are unused. */
    u8 numSequentialHits;
    EmeraldCalcHit sequentialHits[EMERALD_CALC_MAX_HITS];

    /* adjustnormaldamage / adjustsetdamage / tryKO on the target: */
    bool8 cannotKo;         /* False Swipe, or the target Endured: a KOing hit leaves it at 1 HP */
    u8 focusBandChance;     /* percent chance per KOing hit that the target's Focus Band leaves it at 1 HP */
    bool8 isOhko;           /* Fissure/Horn Drill/...: damage == the target's current HP */
    bool8 targetHasSubstitute; /* damage goes to the Substitute, not the target's HP */
} EmeraldCalcResult;

/* Runs `move`'s battle script with gBattlerAttacker = battlerAtk and
 * gBattlerTarget = battlerDef against `state`, enumerating every random
 * outcome. The move doesn't have to be one battlerAtk knows. */
EmeraldCalcResult EmeraldCalc_SimulateMove(const struct BattleState *state, u8 battlerAtk, u8 battlerDef, u16 move);

/* Total damage over one use assuming every accuracy check passes and no
 * crits: fewest hits at the lowest roll through most hits at the highest
 * roll, lowest-power branch through highest (Present's heal branch
 * excluded). What the UI shows as "min-max". */
void EmeraldCalc_DamageRange(const EmeraldCalcResult *result, s32 *minDamage, s32 *maxDamage);

typedef struct EmeraldCalcKO {
    double chance;        /* P(one use of the move faints a target at `targetHp`): accuracy, crits, every random roll, hit counts, Focus Band, Endure all folded in */
    double chanceIfHits;  /* the same, given the (first) accuracy check passes */
    s32 guaranteedHitsToKo; /* smallest n with n * (min no-crit total) >= targetHp, or -1 */
    s32 possibleHitsToKo;   /* smallest n with n * (max no-crit total) >= targetHp, or -1 */
} EmeraldCalcKO;

/* KO odds for `result` against a target with `targetHp` HP left. Exact:
 * walks the full outcome distribution rather than sampling it. Ignores
 * targetHasSubstitute (the odds are for the hits reaching the target). */
EmeraldCalcKO EmeraldCalc_KOChance(const EmeraldCalcResult *result, u16 targetHp);

#ifdef __cplusplus
}
#endif

#endif /* EMERALD_CALC_H */
