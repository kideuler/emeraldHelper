#ifndef EMERALD_BATTLE_STATE_H
#define EMERALD_BATTLE_STATE_H

/*
 * struct BattleState: a one-for-one mirror of the global battle state the
 * game's own damage code reads (src/battle_main.c's EWRAM_DATA globals,
 * plus the save-block/heap data they point at).
 *
 * Every member is named after the global it mirrors (gBattleMons ->
 * battleMons, gSideStatuses -> sideStatuses, ...), and every struct below
 * is field-for-field the decomp's own definition of that type, same names,
 * same order. That's deliberate: the ported battle code in calc/src/ reads
 * `s->battleMons[battlerAtk].status2` exactly where src/ reads
 * `gBattleMons[gBattlerAttacker].status2`, so a port can be checked against
 * the original line by line.
 *
 * Two deliberate differences from the in-memory layout:
 *   - bitfields are widened to whole integers. These structs are filled
 *     field-by-field by the decoder (helperapp/src/decoder.cpp) using
 *     offsets from config/battle_pokemon_layout_us_rev0.json (tools/
 *     gen_struct_layout.py, checked against agbcc by tools/
 *     verify_struct_layout.py), never by memcpy'ing ROM bytes over them, so
 *     host bitfield packing never matters.
 *   - party Pokemon (struct PartyPokemon) are stored decrypted -- what
 *     GetMonData() returns -- rather than as the encrypted struct Pokemon.
 *
 * Header-only data, no functions: see emerald_calc.h for the calculator.
 */

#include <stdint.h>

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

/* The headers under include/constants are pure #define lists with no GBA
 * dependency, so they're used as-is rather than re-transcribed. They're
 * reached through a build-tree directory whose only entry is a
 * `constants` symlink to include/constants (see helperapp/CMakeLists.txt)
 * -- so "constants/foo.h" resolves exactly as it does in the decomp,
 * nested includes like flags.h's "constants/opponents.h" included, without
 * putting include/ itself on the search path (pret's include/strings.h
 * shadows libc's <strings.h>). */
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/battle_move_effects.h"
#include "constants/flags.h"
#include "constants/global.h"
#include "constants/hold_effects.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/pokedex.h"
#include "constants/pokemon.h"
#include "constants/species.h"
#include "constants/trainers.h"

/* From include/global.berry.h and include/global.h (not include-safe:
 * both are part of the include/global.h cascade into the GBA hardware
 * headers). NUM_FLAG_BYTES is include/global.h's
 * ROUND_BITS_TO_BYTES(FLAGS_COUNT), spelled out. */
#define BERRY_NAME_LENGTH 6
#define BERRY_ITEM_EFFECT_COUNT 18
#define NUM_FLAG_BYTES ((FLAGS_COUNT + 7) / 8)
#define RESOURCE_FLAG_FLASH_FIRE 1 /* include/battle.h: bit in gBattleResources->flags->flags[battler] */

/* include/pokemon.h struct BattlePokemon (gBattleMons). */
struct BattlePokemon {
    u16 species;
    u16 attack;
    u16 defense;
    u16 speed;
    u16 spAttack;
    u16 spDefense;
    u16 moves[MAX_MON_MOVES];
    u32 hpIV;        /* :5 */
    u32 attackIV;    /* :5 */
    u32 defenseIV;   /* :5 */
    u32 speedIV;     /* :5 */
    u32 spAttackIV;  /* :5 */
    u32 spDefenseIV; /* :5 */
    u32 isEgg;       /* :1 */
    u32 abilityNum;  /* :1 */
    s8 statStages[NUM_BATTLE_STATS];
    u8 ability;
    u8 types[2];
    u8 unknown;
    u8 pp[MAX_MON_MOVES];
    u16 hp;
    u8 level;
    u8 friendship;
    u16 maxHP;
    u16 item;
    u8 nickname[POKEMON_NAME_LENGTH + 1];
    u8 ppBonuses;
    u8 otName[PLAYER_NAME_LENGTH + 1];
    u32 experience;
    u32 personality;
    u32 status1;
    u32 status2;
    u32 otId;
};

/* include/battle.h struct DisableStruct (gDisableStructs). */
struct DisableStruct {
    u32 transformedMonPersonality;
    u16 disabledMove;
    u16 encoredMove;
    u8 protectUses;
    u8 stockpileCounter;
    u8 substituteHP;
    u8 disableTimer;              /* :4 */
    u8 disableTimerStartValue;    /* :4 */
    u8 encoredMovePos;
    u8 filler_D;
    u8 encoreTimer;               /* :4 */
    u8 encoreTimerStartValue;     /* :4 */
    u8 perishSongTimer;           /* :4 */
    u8 perishSongTimerStartValue; /* :4 */
    u8 furyCutterCounter;
    u8 rolloutTimer;              /* :4 */
    u8 rolloutTimerStartValue;    /* :4 */
    u8 chargeTimer;               /* :4 */
    u8 chargeTimerStartValue;     /* :4 */
    u8 tauntTimer;                /* :4 */
    u8 tauntTimer2;               /* :4 */
    u8 battlerPreventingEscape;
    u8 battlerWithSureHit;
    u8 isFirstTurn;
    u8 filler_17;
    u8 truantCounter;             /* :1 */
    u8 truantSwitchInHack;        /* :1 */
    u8 filler_18_2;               /* :2 */
    u8 mimickedMoves;             /* :4 */
    u8 rechargeTimer;
};

/* include/battle.h struct ProtectStruct (gProtectStructs). All the flags
 * are u32 :1 (fleeType :2) in the original. */
struct ProtectStruct {
    u32 protected_; /* `protected` in the original -- a C++ keyword, and this header is shared with the C++ decoder */
    u32 endured;
    u32 noValidMoves;
    u32 helpingHand;
    u32 bounceMove;
    u32 stealMove;
    u32 flag0Unknown;
    u32 prlzImmobility;
    u32 confusionSelfDmg;
    u32 targetNotAffected;
    u32 chargingTurn;
    u32 fleeType;
    u32 usedImprisonedMove;
    u32 loveImmobility;
    u32 usedDisabledMove;
    u32 usedTauntedMove;
    u32 flag2Unknown;
    u32 flinchImmobility;
    u32 notFirstStrike;
    u32 palaceUnableToUseMove;
    u32 physicalDmg;
    u32 specialDmg;
    u8 physicalBattlerId;
    u8 specialBattlerId;
};

/* include/battle.h struct SideTimer (gSideTimers). */
struct SideTimer {
    u8 reflectTimer;
    u8 reflectBattlerId;
    u8 lightscreenTimer;
    u8 lightscreenBattlerId;
    u8 mistTimer;
    u8 mistBattlerId;
    u8 safeguardTimer;
    u8 safeguardBattlerId;
    u8 followmeTimer;
    u8 followmeTarget;
    u8 spikesAmount;
};

/* include/global.berry.h struct BattleEnigmaBerry (gEnigmaBerries). */
struct BattleEnigmaBerry {
    u8 name[BERRY_NAME_LENGTH + 1];
    u8 holdEffect;
    u8 itemEffect[BERRY_ITEM_EFFECT_COUNT];
    u8 holdEffectParam;
};

/* One gPlayerParty/gEnemyParty entry as GetMonData() sees it: struct
 * Pokemon's encrypted BoxPokemon substructs already decrypted and
 * unshuffled (helperapp/src/partydecoder.cpp). Field names follow the
 * substruct/box/party fields they come from. */
struct PartyPokemon {
    /* BoxPokemon */
    u32 personality;
    u32 otId;
    u8 isBadEgg;
    u8 hasSpecies;
    /* PokemonSubstruct0..3 */
    u16 species;
    u16 heldItem;
    u32 experience;
    u8 ppBonuses;
    u8 friendship;
    u16 moves[MAX_MON_MOVES];
    u8 pp[MAX_MON_MOVES];
    u8 hpIV;
    u8 attackIV;
    u8 defenseIV;
    u8 speedIV;
    u8 spAttackIV;
    u8 spDefenseIV;
    u8 isEgg;
    u8 abilityNum;
    /* struct Pokemon (unencrypted tail) */
    u32 status;
    u8 level;
    u16 hp;
    u16 maxHP;
    u16 attack;
    u16 defense;
    u16 speed;
    u16 spAttack;
    u16 spDefense;
};

struct BattleState {
    u32 battleTypeFlags;                                     /* gBattleTypeFlags */
    u8 battleEnvironment;                                    /* gBattleEnvironment */
    u8 battlersCount;                                        /* gBattlersCount */
    u8 battlerPositions[MAX_BATTLERS_COUNT];                 /* gBattlerPositions */
    u16 battlerPartyIndexes[MAX_BATTLERS_COUNT];             /* gBattlerPartyIndexes */
    u8 absentBattlerFlags;                                   /* gAbsentBattlerFlags */
    u8 battlerAttacker;                                      /* gBattlerAttacker */
    u8 battlerTarget;                                        /* gBattlerTarget */
    struct BattlePokemon battleMons[MAX_BATTLERS_COUNT];     /* gBattleMons */
    u16 battleWeather;                                       /* gBattleWeather */
    u16 sideStatuses[NUM_BATTLE_SIDES];                      /* gSideStatuses */
    struct SideTimer sideTimers[NUM_BATTLE_SIDES];           /* gSideTimers */
    u32 statuses3[MAX_BATTLERS_COUNT];                       /* gStatuses3 */
    struct DisableStruct disableStructs[MAX_BATTLERS_COUNT]; /* gDisableStructs */
    struct ProtectStruct protectStructs[MAX_BATTLERS_COUNT]; /* gProtectStructs */
    struct BattleEnigmaBerry enigmaBerries[MAX_BATTLERS_COUNT]; /* gEnigmaBerries */
    u32 resourceFlags[MAX_BATTLERS_COUNT];                   /* gBattleResources->flags->flags */
    u16 trainerBattleOpponentA;                              /* gTrainerBattleOpponent_A */
    u8 saveFlags[NUM_FLAG_BYTES];                            /* gSaveBlock1Ptr->flags (FlagGet()) */
    struct PartyPokemon playerParty[PARTY_SIZE];             /* gPlayerParty */
    struct PartyPokemon enemyParty[PARTY_SIZE];              /* gEnemyParty */
};

/* A zeroed BattleState with every stat stage at DEFAULT_STAT_STAGE,
 * gBattlerPositions[i] = i and gBattlersCount = 2 -- i.e. what a freshly
 * started singles battle looks like before anything is decoded into it.
 * Handy for tests and for building "what if" states by hand. */
void BattleState_Init(struct BattleState *s);

#ifdef __cplusplus
}
#endif

#endif /* EMERALD_BATTLE_STATE_H */
