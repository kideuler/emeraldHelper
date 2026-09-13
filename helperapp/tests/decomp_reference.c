/* The reference side of the differential test (test_decomp_diff.cpp).
 *
 * decomp_reference_extract.inc is generated at build time by
 * extract_decomp.py: CalculateBaseDamage(), the damagecalc / typecalc /
 * critcalc / accuracycheck / power-setting script commands, their helpers
 * and tables, and the macros they use -- copied verbatim out of src/ and
 * include/, not transcribed. This file only supplies what that code expects
 * to find around it: the game's globals (typed with the same structs
 * BattleState uses -- same field names, so the original code compiles
 * unchanged against them) and stubs for the few engine hooks it calls that
 * have nothing to do with the arithmetic (RecordAbilityBattle(), ...).
 *
 * What's stubbed rather than extracted, and why that's safe:
 *   - AbilityBattleEffects(): 700 lines of switch; only the two cases
 *     reached from here (ABILITYEFFECT_FIELD_SPORT, _CHECK_ON_FIELD) are
 *     reproduced, from src/battle_util.c. ABILITYEFFECT_ABSORBING returns
 *     "not absorbed" -- absorption is tested separately (test_damage.cpp).
 *   - FlagGet(): reads the same save-flag bytes BattleState carries.
 *   - Random(): returns a caller-chosen value, so outcomes can be counted
 *     over all 65536 values instead of sampled.
 *   - SpeciesToNationalPokedexNum()/GetPokedexHeightWeight(): the calc's
 *     own two-line lookups over the real, generated-from-src tables. */
#include "decomp_reference.h"

#include "emerald_calc.h"

#include "constants/battle_script_commands.h"
#include "constants/battle_string_ids.h"

#include <stdlib.h>
#include <string.h>

/* include/global.h / include/battle.h bits the extracted code needs that
 * aren't worth extracting (plain helpers, or not include-safe). */
#define T1_READ_PTR(ptr) sJumpTarget
struct StatFractions { u8 dividend; u8 divisor; };
struct ResourceFlags { u32 flags[MAX_BATTLERS_COUNT]; };
struct BattleResources { void *secretBase; struct ResourceFlags *flags; };
struct BattleStruct { u8 dynamicMoveType; };
struct BattleScripting { u8 dmgMultiplier; };

/* ProtectStruct's `protected` is `protected_` on the host (battle_state.h). */
#define protected protected_

/* The extracted non-static functions would collide with the port's (same
 * names, different signatures) at link time. */
#define CalculateBaseDamage Ref_CalculateBaseDamageOriginal
#define CountAliveMonsInBattle Ref_CountAliveMonsInBattle
#define GetBattlerSide Ref_GetBattlerSide
#define GetBattlerPosition Ref_GetBattlerPosition
#define GetScaledHPFraction Ref_GetScaledHPFraction
#define FlagGet Ref_FlagGet

/* --- The game's globals ---------------------------------------------------- */
static struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
static u32 gBattleTypeFlags;
static u8 gBattlersCount;
static u8 gBattlerPositions[MAX_BATTLERS_COUNT];
static u8 gAbsentBattlerFlags;
static u8 gBattlerAttacker, gBattlerTarget, gActiveBattler;
static u16 gBattleWeather;
static u16 gSideStatuses[NUM_BATTLE_SIDES];
static struct SideTimer gSideTimers[NUM_BATTLE_SIDES];
static u32 gStatuses3[MAX_BATTLERS_COUNT];
static struct DisableStruct gDisableStructs[MAX_BATTLERS_COUNT];
static struct ProtectStruct gProtectStructs[MAX_BATTLERS_COUNT];
static struct BattleEnigmaBerry gEnigmaBerries[MAX_BATTLERS_COUNT];
static struct ResourceFlags sResourceFlags;
static struct BattleResources sBattleResources = {NULL, &sResourceFlags};
static struct BattleResources *gBattleResources = &sBattleResources;
static u16 gTrainerBattleOpponent_A;
static u8 sSaveFlags[NUM_FLAG_BYTES];

static u16 gCurrentMove;
static u16 gBattleMovePower;
static u16 gDynamicBasePower;
static struct BattleStruct sBattleStruct;
static struct BattleStruct *gBattleStruct = &sBattleStruct;
static struct BattleScripting gBattleScripting;
static u8 gCritMultiplier;
static s32 gBattleMoveDamage;
static u8 gMoveResultFlags;
static u32 gHitMarker;
static u8 gLastUsedAbility;
static u8 gPotentialItemEffectBattler;
static u16 gLastLandedMoves[MAX_BATTLERS_COUNT];
static u8 gLastHitByType[MAX_BATTLERS_COUNT];
static u8 gBattleCommunication[8];
static const u32 gBitTable[] = {1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7};

static u8 sScript[16];
static const u8 *gBattlescriptCurrInstr;
static const u8 *const sJumpTarget = sScript + 15; /* "jumped to the fail label" */
static u16 sNextRandom;

/* --- Stubs ------------------------------------------------------------------- */
static u16 Random(void) { return sNextRandom; }
static void RecordAbilityBattle(u8 battler, u8 abilityId) { (void)battler; (void)abilityId; }
static void RecordItemEffectBattle(u8 battler, u8 itemEffect) { (void)battler; (void)itemEffect; }
static void TrySetDestinyBondToHappen(void) {}
static bool8 FlagGet(u16 id) { return (sSaveFlags[id / 8] >> (id & 7)) & 1; }

/* Macros (GET_MOVE_TYPE, WEATHER_HAS_EFFECT2, APPLY_STAT_MOD, ...) and
 * tables (gTypeEffectiveness, gStatStageRatios, ...), extracted. */
#include "decomp_reference_macros.inc"

/* The ABILITYEFFECT_FIELD_SPORT / _CHECK_ON_FIELD cases of
 * src/battle_util.c's AbilityBattleEffects(), as written there. */
static u8 AbilityBattleEffects(u8 caseID, u8 battler, u8 ability, u8 special, u16 moveArg)
{
    u8 effect = 0;
    s32 i;
    (void)moveArg;

    if (special)
        gLastUsedAbility = special;
    else
        gLastUsedAbility = gBattleMons[battler].ability;

    switch (caseID)
    {
    case ABILITYEFFECT_FIELD_SPORT:
        switch (gLastUsedAbility)
        {
        case ABILITYEFFECT_MUD_SPORT:
            for (i = 0; i < gBattlersCount; i++)
            {
                if (gStatuses3[i] & STATUS3_MUDSPORT)
                    effect = i + 1;
            }
            break;
        case ABILITYEFFECT_WATER_SPORT:
            for (i = 0; i < gBattlersCount; i++)
            {
                if (gStatuses3[i] & STATUS3_WATERSPORT)
                    effect = i + 1;
            }
            break;
        default:
            for (i = 0; i < gBattlersCount; i++)
            {
                if (gBattleMons[i].ability == ability)
                {
                    gLastUsedAbility = ability;
                    effect = i + 1;
                }
            }
            break;
        }
        break;
    case ABILITYEFFECT_CHECK_ON_FIELD:
        for (i = 0; i < gBattlersCount; i++)
        {
            if (gBattleMons[i].ability == ability && gBattleMons[i].hp != 0)
            {
                gLastUsedAbility = ability;
                effect = i + 1;
            }
        }
        break;
    case ABILITYEFFECT_ABSORBING:
    case ABILITYEFFECT_MOVES_BLOCK:
        break; /* absorption/Soundproof aren't what this harness compares */
    default:
        abort();
    }
    return effect;
}

/* Prototypes the extracted code's own file provides further up. */
u8 GetBattlerSide(u8 battler);
u8 GetBattlerPosition(u8 battler);
u8 CountAliveMonsInBattle(u8 caseId);
u8 GetScaledHPFraction(s16 hp, s16 maxhp, u8 scale);
static bool8 ShouldGetStatBadgeBoost(u16 badgeFlag, u8 battler);
static u8 AttacksThisTurn(u8 battler, u16 move);
static void CheckWonderGuardAndLevitate(void);
static void JumpIfMoveFailed(u8 adder, u16 move);
static bool8 JumpIfMoveAffectedByProtect(u16 move);
static bool8 AccuracyCalcHelper(u16 move);

#include "decomp_reference_extract.inc"

/* --- Harness ------------------------------------------------------------------- */

static void ResetMoveGlobals(void)
{
    gCritMultiplier = 1;
    gBattleScripting.dmgMultiplier = 1;
    gMoveResultFlags = 0;
    gHitMarker = 0;
    gDynamicBasePower = 0;
    gBattleStruct->dynamicMoveType = 0;
    gBattleMoveDamage = 0;
    gLastUsedAbility = 0;
    memset(gBattleCommunication, 0, sizeof(gBattleCommunication));
}

void Ref_Load(const struct BattleState *s, u8 attacker, u8 target, u16 move)
{
    memcpy(gBattleMons, s->battleMons, sizeof(gBattleMons));
    gBattleTypeFlags = s->battleTypeFlags;
    gBattlersCount = s->battlersCount;
    memcpy(gBattlerPositions, s->battlerPositions, sizeof(gBattlerPositions));
    gAbsentBattlerFlags = s->absentBattlerFlags;
    gBattleWeather = s->battleWeather;
    memcpy(gSideStatuses, s->sideStatuses, sizeof(gSideStatuses));
    memcpy(gSideTimers, s->sideTimers, sizeof(gSideTimers));
    memcpy(gStatuses3, s->statuses3, sizeof(gStatuses3));
    memcpy(gDisableStructs, s->disableStructs, sizeof(gDisableStructs));
    memcpy(gProtectStructs, s->protectStructs, sizeof(gProtectStructs));
    memcpy(gEnigmaBerries, s->enigmaBerries, sizeof(gEnigmaBerries));
    memcpy(sResourceFlags.flags, s->resourceFlags, sizeof(sResourceFlags.flags));
    gTrainerBattleOpponent_A = s->trainerBattleOpponentA;
    memcpy(sSaveFlags, s->saveFlags, sizeof(sSaveFlags));

    gBattlerAttacker = attacker;
    gBattlerTarget = target;
    gActiveBattler = attacker;
    gCurrentMove = move;
    ResetMoveGlobals();
}

void Ref_SetDynamic(u16 dynamicBasePower, u8 dynamicMoveType, u8 dmgMultiplier, u32 hitMarker)
{
    gDynamicBasePower = dynamicBasePower;
    gBattleStruct->dynamicMoveType = dynamicMoveType;
    gBattleScripting.dmgMultiplier = dmgMultiplier;
    gHitMarker = hitMarker;
}

void Ref_DamageCalcTypeCalc(u8 critMultiplier, s32 *damage, u8 *moveResultFlags, u8 *lastUsedAbility)
{
    const u8 dmgMultiplier = gBattleScripting.dmgMultiplier;
    const u16 dynamicBasePower = gDynamicBasePower;

    gCritMultiplier = critMultiplier;
    gMoveResultFlags = 0;
    gLastUsedAbility = 0;
    gBattlescriptCurrInstr = sScript;
    Cmd_damagecalc();
    /* CalculateBaseDamage()'s ABILITY_ON_FIELD2() checks also write
     * gLastUsedAbility, as a side effect of AbilityBattleEffects(); the port
     * only tracks typecalc's own assignment (Levitate / Wonder Guard), which
     * is all it reads, so compare just that. */
    gLastUsedAbility = 0;
    Cmd_typecalc();
    *damage = gBattleMoveDamage;
    *moveResultFlags = gMoveResultFlags;
    *lastUsedAbility = gLastUsedAbility;

    gBattleScripting.dmgMultiplier = dmgMultiplier;
    gDynamicBasePower = dynamicBasePower;
}

double Ref_CritChance(void)
{
    u32 r, crits = 0;
    for (r = 0; r < 0x10000; r++)
    {
        sNextRandom = (u16)r;
        gBattlescriptCurrInstr = sScript;
        Cmd_critcalc();
        crits += (gCritMultiplier == 2);
    }
    gCritMultiplier = 1;
    return crits / 65536.0;
}

double Ref_Accuracy(u16 accMove)
{
    u32 r, hits = 0;
    const u8 moveResultFlags = gMoveResultFlags;
    const u32 hitMarker = gHitMarker;

    for (r = 0; r < 0x10000; r++)
    {
        sNextRandom = (u16)r;
        memset(sScript, 0, sizeof(sScript));
        sScript[5] = accMove & 0xFF;
        sScript[6] = accMove >> 8;
        gBattlescriptCurrInstr = sScript;
        gMoveResultFlags = moveResultFlags;
        gHitMarker = hitMarker;
        Cmd_accuracycheck();
        /* A miss is the script jumping to accuracycheck's fail label -- not
         * always with MOVE_RESULT_MISSED set (a NO_ACC_CALC* check against
         * a semi-invulnerable target just jumps). */
        hits += (gBattlescriptCurrInstr != sJumpTarget);
    }
    gMoveResultFlags = moveResultFlags;
    gHitMarker = hitMarker;
    return hits / 65536.0;
}

u16 Ref_PowerCommand(int command, u8 *dynamicMoveType, u8 *dmgMultiplier)
{
    gBattlescriptCurrInstr = sScript;
    switch (command)
    {
    case REF_WEIGHT_DAMAGE_CALCULATION: Cmd_weightdamagecalculation(); break;
    case REF_REMAINING_HP_TO_POWER: Cmd_remaininghptopower(); break;
    case REF_SCALE_DAMAGE_BY_HEALTH_RATIO: Cmd_scaledamagebyhealthratio(); break;
    case REF_FRIENDSHIP_TO_DAMAGE_CALCULATION: Cmd_friendshiptodamagecalculation(); break;
    case REF_HIDDEN_POWER_CALC: Cmd_hiddenpowercalc(); break;
    case REF_SET_WEATHER_BALL_TYPE: Cmd_setweatherballtype(); break;
    default: abort();
    }
    *dynamicMoveType = gBattleStruct->dynamicMoveType;
    *dmgMultiplier = gBattleScripting.dmgMultiplier;
    return gDynamicBasePower;
}
