/* The battle scripts (data/battle_scripts_1.s), run up to the point damage
 * is dealt, plus ports of the effect-specific script commands they call
 * (src/battle_script_commands.c).
 *
 * Each Script_* function below is one script label and runs its commands
 * in the script's order; comments quote the label. Commands that only
 * print text, play animations, or apply secondary effects after damage
 * (setmoveeffect, attackstring, ppreduce, ...) don't affect the numbers
 * and are skipped. Commands that roll Random() are enumerated into
 * EmeraldCalcResult branches / hit counts instead. */
#include "battle_internal.h"

#include <string.h>

/* --- Tables, verbatim from src/battle_script_commands.c ------------------ */

static const u8 sFlailHpScaleToPowerTable[] =
{
    1, 200,
    4, 150,
    9, 100,
    16, 80,
    32, 40,
    48, 20
};

// format: min. weight (hectograms), base power
static const u16 sWeightToDamageTable[] =
{
    100, 20,
    250, 40,
    500, 60,
    1000, 80,
    2000, 100,
    0xFFFF, 0xFFFF
};

static const u16 sNaturePowerMoves[] =
{
    [BATTLE_ENVIRONMENT_GRASS]      = MOVE_STUN_SPORE,
    [BATTLE_ENVIRONMENT_LONG_GRASS] = MOVE_RAZOR_LEAF,
    [BATTLE_ENVIRONMENT_SAND]       = MOVE_EARTHQUAKE,
    [BATTLE_ENVIRONMENT_UNDERWATER] = MOVE_HYDRO_PUMP,
    [BATTLE_ENVIRONMENT_WATER]      = MOVE_SURF,
    [BATTLE_ENVIRONMENT_POND]       = MOVE_BUBBLE_BEAM,
    [BATTLE_ENVIRONMENT_MOUNTAIN]   = MOVE_ROCK_SLIDE,
    [BATTLE_ENVIRONMENT_CAVE]       = MOVE_SHADOW_BALL,
    [BATTLE_ENVIRONMENT_BUILDING]   = MOVE_SWIFT,
    [BATTLE_ENVIRONMENT_PLAIN]      = MOVE_SWIFT
};

/* include/battle.h */
#define F_DYNAMIC_TYPE_IGNORE_PHYSICALITY (1 << 6)
#define F_DYNAMIC_TYPE_SET                (1 << 7)

#define TARGET(ctx)   (&(ctx)->s->battleMons[(ctx)->battlerTarget])
#define ATTACKER(ctx) (&(ctx)->s->battleMons[(ctx)->battlerAttacker])

/* --- Effect-specific script commands ------------------------------------- */

void Cmd_weightdamagecalculation(struct MoveContext *ctx)
{
    s32 i;
    for (i = 0; sWeightToDamageTable[i] != 0xFFFF; i += 2)
    {
        if (sWeightToDamageTable[i] > GetPokedexHeightWeight(SpeciesToNationalPokedexNum(TARGET(ctx)->species), 1))
            break;
    }

    if (sWeightToDamageTable[i] != 0xFFFF)
        ctx->dynamicBasePower = sWeightToDamageTable[i + 1];
    else
        ctx->dynamicBasePower = 120;
}

void Cmd_remaininghptopower(struct MoveContext *ctx)
{
    s32 i;
    s32 hpFraction = GetScaledHPFraction(ATTACKER(ctx)->hp, ATTACKER(ctx)->maxHP, 48);

    for (i = 0; i < (s32) sizeof(sFlailHpScaleToPowerTable); i += 2)
    {
        if (hpFraction <= sFlailHpScaleToPowerTable[i])
            break;
    }

    ctx->dynamicBasePower = sFlailHpScaleToPowerTable[i + 1];
}

void Cmd_scaledamagebyhealthratio(struct MoveContext *ctx)
{
    if (ctx->dynamicBasePower == 0)
    {
        u8 power = gBattleMoves[ctx->currentMove].power;
        ctx->dynamicBasePower = ATTACKER(ctx)->hp * power / ATTACKER(ctx)->maxHP;
        if (ctx->dynamicBasePower == 0)
            ctx->dynamicBasePower = 1;
    }
}

void Cmd_friendshiptodamagecalculation(struct MoveContext *ctx)
{
    if (gBattleMoves[ctx->currentMove].effect == EFFECT_RETURN)
        ctx->dynamicBasePower = 10 * (ATTACKER(ctx)->friendship) / 25;
    else // EFFECT_FRUSTRATION
        ctx->dynamicBasePower = 10 * (MAX_FRIENDSHIP - ATTACKER(ctx)->friendship) / 25;
}

void Cmd_hiddenpowercalc(struct MoveContext *ctx)
{
    const struct BattlePokemon *mon = ATTACKER(ctx);
    u8 powerBits = ((mon->hpIV & 2) >> 1)
                 | ((mon->attackIV & 2) << 0)
                 | ((mon->defenseIV & 2) << 1)
                 | ((mon->speedIV & 2) << 2)
                 | ((mon->spAttackIV & 2) << 3)
                 | ((mon->spDefenseIV & 2) << 4);

    u8 typeBits  = ((mon->hpIV & 1) << 0)
                 | ((mon->attackIV & 1) << 1)
                 | ((mon->defenseIV & 1) << 2)
                 | ((mon->speedIV & 1) << 3)
                 | ((mon->spAttackIV & 1) << 4)
                 | ((mon->spDefenseIV & 1) << 5);

    ctx->dynamicBasePower = (40 * powerBits) / 63 + 30;

    // Subtract 3 instead of 1 below because 2 types are excluded (TYPE_NORMAL and TYPE_MYSTERY)
    // The final + 1 skips past Normal, and the following conditional skips TYPE_MYSTERY
    ctx->dynamicMoveType = ((NUMBER_OF_MON_TYPES - 3) * typeBits) / 63 + 1;
    if (ctx->dynamicMoveType >= TYPE_MYSTERY)
        ctx->dynamicMoveType++;
    ctx->dynamicMoveType |= F_DYNAMIC_TYPE_IGNORE_PHYSICALITY | F_DYNAMIC_TYPE_SET;
}

void Cmd_setweatherballtype(struct MoveContext *ctx)
{
    const struct BattleState *s = ctx->s;

    if (WeatherHasEffect(s))
    {
        if (s->battleWeather & B_WEATHER_ANY)
            ctx->dmgMultiplier = 2;
        if (s->battleWeather & B_WEATHER_RAIN)
            ctx->dynamicMoveType = TYPE_WATER | F_DYNAMIC_TYPE_SET;
        else if (s->battleWeather & B_WEATHER_SANDSTORM)
            ctx->dynamicMoveType = TYPE_ROCK | F_DYNAMIC_TYPE_SET;
        else if (s->battleWeather & B_WEATHER_SUN)
            ctx->dynamicMoveType = TYPE_FIRE | F_DYNAMIC_TYPE_SET;
        else if (s->battleWeather & B_WEATHER_HAIL)
            ctx->dynamicMoveType = TYPE_ICE | F_DYNAMIC_TYPE_SET;
        else
            ctx->dynamicMoveType = TYPE_NORMAL | F_DYNAMIC_TYPE_SET;
    }
}

/* The power half of Cmd_rolloutdamagecalculation. Its bookkeeping (first
 * hit arms rolloutTimer = 5 and STATUS2_MULTIPLETURNS, every hit counts it
 * down first) is done on a local copy of the timer, since BattleState is
 * the *current* state and the command runs as part of the next hit. */
static void Cmd_rolloutdamagecalculation(struct MoveContext *ctx)
{
    const struct BattlePokemon *attacker = ATTACKER(ctx);
    u8 rolloutTimer = ctx->s->disableStructs[ctx->battlerAttacker].rolloutTimer;
    s32 i;

    if (!(attacker->status2 & STATUS2_MULTIPLETURNS)) // first hit
        rolloutTimer = 5;
    --rolloutTimer;

    ctx->dynamicBasePower = gBattleMoves[ctx->currentMove].power;

    for (i = 1; i < (5 - rolloutTimer); i++)
        ctx->dynamicBasePower *= 2;

    if (attacker->status2 & STATUS2_DEFENSE_CURL)
        ctx->dynamicBasePower *= 2;
}

/* Likewise for Cmd_furycuttercalc's counter. */
static void Cmd_furycuttercalc(struct MoveContext *ctx)
{
    u8 furyCutterCounter = ctx->s->disableStructs[ctx->battlerAttacker].furyCutterCounter;
    s32 i;

    if (furyCutterCounter != 5)
        furyCutterCounter++;

    ctx->dynamicBasePower = gBattleMoves[ctx->currentMove].power;

    for (i = 1; i < furyCutterCounter; i++)
        ctx->dynamicBasePower *= 2;
}

static void Cmd_doubledamagedealtifdamaged(struct MoveContext *ctx)
{
    const struct ProtectStruct *p = &ctx->s->protectStructs[ctx->battlerAttacker];

    if ((p->physicalDmg != 0
         && p->physicalBattlerId == ctx->battlerTarget)
        || (p->specialDmg != 0
            && p->specialBattlerId == ctx->battlerTarget))
    {
        ctx->dmgMultiplier = 2;
    }
}

/* Brick Break. Only the gSideStatuses half matters for damage. */
static void Cmd_removelightscreenreflect(struct MoveContext *ctx)
{
    u8 opposingSide = GetBattlerSide(ctx->s, ctx->battlerAttacker) ^ BIT_SIDE; // BATTLE_OPPOSITE

    if (ctx->s->sideTimers[opposingSide].reflectTimer || ctx->s->sideTimers[opposingSide].lightscreenTimer)
    {
        ctx->sideStatuses[opposingSide] &= ~SIDE_STATUS_REFLECT;
        ctx->sideStatuses[opposingSide] &= ~SIDE_STATUS_LIGHTSCREEN;
    }
}

/* --- Result bookkeeping ----------------------------------------------------- */

static void InitResult(EmeraldCalcResult *r, u16 move)
{
    memset(r, 0, sizeof(*r));
    r->outcome = EMERALD_CALC_DEALS_DAMAGE;
    r->move = move;
    r->moveType = gBattleMoves[move].type;
    r->accuracy = 1.0;
    r->numBranches = 1;
    r->branches[0].probability = 1.0;
    r->minHits = r->maxHits = 1;
    r->hitCountChance[1] = 1.0;
}

static void SetOutcome(EmeraldCalcResult *r, u8 outcome, u8 reason)
{
    r->outcome = outcome;
    r->reason = reason;
}

/* After a typecalc: record its flags, and turn a MOVE_RESULT_NO_EFFECT
 * into the outcome (gLastUsedAbility says which ability, if any, did it). */
static void FinishTypecalc(const struct MoveContext *ctx, EmeraldCalcResult *r)
{
    r->moveResultFlags = ctx->moveResultFlags;
    r->moveType = GetMoveType(ctx, ctx->currentMove);
    if (ctx->moveResultFlags & MOVE_RESULT_NO_EFFECT)
    {
        if (ctx->lastUsedAbility == ABILITY_LEVITATE)
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_LEVITATE);
        else if (ctx->lastUsedAbility == ABILITY_WONDER_GUARD)
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_WONDER_GUARD);
        else
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_TYPE_IMMUNITY);
    }
}

/* The part of adjustnormaldamage / adjustnormaldamage2 / adjustsetdamage
 * after the random roll: a hit that would KO leaves the target at 1 HP on
 * Endure, False Swipe (not adjustnormaldamage2), or a Focus Band proc --
 * none of which apply through a Substitute. */
static void AdjustDamageCaps(const struct MoveContext *ctx, EmeraldCalcResult *r, bool8 checksFalseSwipe)
{
    const struct BattleState *s = ctx->s;
    u8 holdEffect, param;

    if (TARGET(ctx)->status2 & STATUS2_SUBSTITUTE)
    {
        r->targetHasSubstitute = TRUE;
        return;
    }

    GetBattlerHoldEffect(s, ctx->battlerTarget, &holdEffect, &param);
    if (holdEffect == HOLD_EFFECT_FOCUS_BAND)
        r->focusBandChance = param;
    if ((checksFalseSwipe && gBattleMoves[ctx->currentMove].effect == EFFECT_FALSE_SWIPE)
     || s->protectStructs[ctx->battlerTarget].endured)
        r->cannotKo = TRUE;
}

/* --- The shared script steps ------------------------------------------------ */

/* attackcanceler, the part that can stop a damaging move before the
 * accuracy check: Soundproof (ABILITYEFFECT_MOVES_BLOCK). Its Protect
 * check is the same DEFENDER_IS_PROTECTED accuracycheck repeats, so it's
 * reported there. Returns FALSE when the move is stopped. */
static bool8 Script_AttackCanceler(const struct MoveContext *ctx, EmeraldCalcResult *r)
{
    if (TARGET(ctx)->ability == ABILITY_SOUNDPROOF && IsSoundMove(ctx->currentMove))
    {
        SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_SOUNDPROOF);
        return FALSE;
    }
    return TRUE;
}

/* ABILITYEFFECT_ABSORBING, reached through JumpIfMoveFailed() once an
 * accuracycheck has passed. */
static bool8 AbsorbedByAbility(const struct MoveContext *ctx, EmeraldCalcResult *r)
{
    const struct BattlePokemon *target = TARGET(ctx);
    u16 move = ctx->currentMove;
    u8 moveType = GetMoveType(ctx, move);

    switch (target->ability)
    {
    case ABILITY_VOLT_ABSORB:
        if (moveType == TYPE_ELECTRIC && gBattleMoves[move].power != 0)
        {
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_VOLT_ABSORB);
            return TRUE;
        }
        break;
    case ABILITY_WATER_ABSORB:
        if (moveType == TYPE_WATER && gBattleMoves[move].power != 0)
        {
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_WATER_ABSORB);
            return TRUE;
        }
        break;
    case ABILITY_FLASH_FIRE:
        if (moveType == TYPE_FIRE && !(target->status1 & STATUS1_FREEZE))
        {
            SetOutcome(r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_FLASH_FIRE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* accuracycheck <fail>, accMove. Returns FALSE when an ability absorbed
 * the move; a certain miss (Protect, Fly/Dig/Dive) just leaves
 * r->accuracy at 0 and the script carries on, so the damage it *would*
 * do is still reported. */
static bool8 Script_AccuracyCheck(const struct MoveContext *ctx, EmeraldCalcResult *r, u16 accMove)
{
    r->accuracy = AccuracyCheck(ctx, accMove, &r->missReason);
    if (accMove != NO_ACC_CALC && accMove != NO_ACC_CALC_CHECK_LOCK_ON
     && r->accuracy > 0 && AbsorbedByAbility(ctx, r))
        return FALSE;
    return TRUE;
}

/* critcalc, damagecalc, typecalc, adjustnormaldamage -- once per
 * gCritMultiplier value. Leaves the typecalc flags in ctx. */
static void CritDamageTypeAdjust(struct MoveContext *ctx, EmeraldCalcResult *r, EmeraldCalcHit *hit)
{
    struct MoveContext pass;

    r->critChance = CritChance(ctx);

    pass = *ctx;
    pass.critMultiplier = 2;
    Cmd_damagecalc(&pass);
    Cmd_typecalc(&pass);
    hit->critical = RandomDamageRolls(pass.battleMoveDamage);

    pass = *ctx;
    pass.critMultiplier = 1;
    Cmd_damagecalc(&pass);
    Cmd_typecalc(&pass);
    hit->normal = RandomDamageRolls(pass.battleMoveDamage);

    hit->power = ctx->dynamicBasePower ? ctx->dynamicBasePower : gBattleMoves[ctx->currentMove].power;
    ctx->moveResultFlags = pass.moveResultFlags;
    ctx->lastUsedAbility = pass.lastUsedAbility;
    AdjustDamageCaps(ctx, r, TRUE);
}

/* BattleScript_HitFromCritCalc (== BattleScript_HitFromAtkString: the
 * attackstring/ppreduce in between don't matter). */
static void Script_HitFromCritCalc(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    CritDamageTypeAdjust(ctx, r, &r->branches[0].hit);
    FinishTypecalc(ctx, r);
}

/* BattleScript_HitFromAccCheck */
static void Script_HitFromAccCheck(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    if (!Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;
    Script_HitFromCritCalc(ctx, r);
}

/* BattleScript_HitFromAtkCanceler */
static void Script_HitFromAtkCanceler(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    if (!Script_AttackCanceler(ctx, r))
        return;
    Script_HitFromAccCheck(ctx, r);
}

/* BattleScript_EffectHit */
static void Script_EffectHit(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    // jumpifnotmove MOVE_SURF / jumpifnostatus3 BS_TARGET, STATUS3_UNDERWATER
    if (ctx->currentMove == MOVE_SURF && ctx->s->statuses3[ctx->battlerTarget] & STATUS3_UNDERWATER)
    {
        ctx->hitMarker |= HITMARKER_IGNORE_UNDERWATER;
        ctx->dmgMultiplier = 2;
    }
    Script_HitFromAtkCanceler(ctx, r);
}

/* The two-turn moves' first-turn check (BattleScript_EffectSkullBash etc.):
 * jumpifstatus2 BS_ATTACKER, STATUS2_MULTIPLETURNS -> second turn. The
 * simulation always reports the damaging turn; this only notes whether
 * that's this turn or next. */
static void NoteTwoTurnMove(const struct MoveContext *ctx, EmeraldCalcResult *r)
{
    if (!(ATTACKER(ctx)->status2 & STATUS2_MULTIPLETURNS))
        r->landsLater = TRUE;
}

/* BattleScript_HitsAllWithUndergroundBonusLoop (Earthquake, Magnitude)
 * for one target, from just after magnitudedamagecalculation. */
static void Script_HitsAllWithUndergroundBonus(struct MoveContext *ctx, EmeraldCalcResult *r, EmeraldCalcHit *hit)
{
    // movevaluescleanup
    ctx->moveResultFlags = 0;
    ctx->critMultiplier = 1;
    if (ctx->s->statuses3[ctx->battlerTarget] & STATUS3_UNDERGROUND)
    {
        ctx->hitMarker |= HITMARKER_IGNORE_UNDERGROUND;
        ctx->dmgMultiplier = 2;
    }
    else
    {
        ctx->hitMarker &= ~HITMARKER_IGNORE_UNDERGROUND;
        ctx->dmgMultiplier = 1;
    }
    if (!Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;
    CritDamageTypeAdjust(ctx, r, hit);
    FinishTypecalc(ctx, r);
}

/* A move whose damage isn't rolled: typecalc for immunity only (effect-
 * iveness flags then cleared), fixed damage, adjustsetdamage. */
static void FixedDamage(struct MoveContext *ctx, EmeraldCalcResult *r, EmeraldCalcHit *hit, s32 damage)
{
    hit->power = 0;
    hit->normal = hit->critical = FixedDamageRolls(damage);
    AdjustDamageCaps(ctx, r, TRUE);
}

/* typecalc; bicbyte gMoveResultFlags, MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE */
static void TypecalcForImmunityOnly(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    Cmd_typecalc(ctx);
    ctx->moveResultFlags &= ~(MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE);
    FinishTypecalc(ctx, r);
}

/* --- Scripts with their own damage flow ------------------------------------ */

/* BattleScript_EffectMultiHit / _EffectDoubleHit / _EffectTwineedle */
static void Script_MultiHit(struct MoveContext *ctx, EmeraldCalcResult *r, u8 fixedHits)
{
    if (!Script_AttackCanceler(ctx, r) || !Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;

    // setmultihitcounter
    memset(r->hitCountChance, 0, sizeof(r->hitCountChance));
    if (fixedHits)
    {
        r->minHits = r->maxHits = fixedHits;
        r->hitCountChance[fixedHits] = 1.0;
    }
    else
    {
        // gMultiHitCounter = Random() & 3; then 0/1 -> +2 (2 or 3 hits,
        // 1/4 each), 2/3 -> (Random() & 3) + 2 (2..5 hits, 1/8 each).
        r->minHits = 2;
        r->maxHits = 5;
        r->hitCountChance[2] = 1.0 / 4 + 1.0 / 8; // 3/8
        r->hitCountChance[3] = 1.0 / 4 + 1.0 / 8; // 3/8
        r->hitCountChance[4] = 1.0 / 8;
        r->hitCountChance[5] = 1.0 / 8;
    }

    // BattleScript_DoMultiHit: movevaluescleanup, critcalc, damagecalc, typecalc, adjustnormaldamage
    ctx->moveResultFlags = 0;
    ctx->dmgMultiplier = 1;
    ctx->critMultiplier = 1;
    Script_HitFromCritCalc(ctx, r);
}

/* BattleScript_EffectTripleKick */
static void Script_TripleKick(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    u16 tripleKickPower = 0; // sethword sTRIPLE_KICK_POWER, 0
    int i;

    if (!Script_AttackCanceler(ctx, r))
        return;

    r->accuracyPerHit = TRUE;
    r->numSequentialHits = 3; // setmultihit 3
    for (i = 0; i < 3; i++)
    {
        // BattleScript_DoTripleKickAttack: accuracycheck (every hit),
        // movevaluescleanup, addbyte sTRIPLE_KICK_POWER, 10,
        // copyhword gDynamicBasePower, sTRIPLE_KICK_POWER, critcalc, ...
        if (!Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
            return;
        ctx->moveResultFlags = 0;
        ctx->dmgMultiplier = 1;
        ctx->critMultiplier = 1;
        tripleKickPower += 10;
        ctx->dynamicBasePower = tripleKickPower;
        CritDamageTypeAdjust(ctx, r, &r->sequentialHits[i]);
        FinishTypecalc(ctx, r);
        if (r->outcome != EMERALD_CALC_DEALS_DAMAGE)
            return; // jumpifmovehadnoeffect
    }
}

/* BattleScript_EffectBeatUp: one hit per healthy, status-free party
 * member, each typeless (no typecalc) with that member's species' base
 * Attack vs the target species' base Defense (Cmd_trydobeatup). */
static void Script_BeatUp(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    const struct BattleState *s = ctx->s;
    const struct PartyPokemon *party;
    int i;

    if (!Script_AttackCanceler(ctx, r) || !Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;

    if (GetBattlerSide(s, ctx->battlerAttacker) == B_SIDE_PLAYER)
        party = s->playerParty;
    else
        party = s->enemyParty;

    if (TARGET(ctx)->hp == 0)
    {
        SetOutcome(r, EMERALD_CALC_FAILS, EMERALD_REASON_NO_PARTY_MEMBER_CAN_ATTACK);
        return;
    }

    for (i = 0; i < PARTY_SIZE; i++)
    {
        const struct PartyPokemon *mon = &party[i];
        u16 speciesOrEgg = (mon->species && (mon->isEgg || mon->isBadEgg)) ? SPECIES_EGG : mon->species; // MON_DATA_SPECIES_OR_EGG
        u16 species = mon->isBadEgg ? SPECIES_EGG : mon->species;                                        // MON_DATA_SPECIES
        EmeraldCalcHit *hit;
        s32 damage;

        if (!(mon->hp && speciesOrEgg != SPECIES_NONE && speciesOrEgg != SPECIES_EGG && !mon->status))
            continue;

        damage = gSpeciesInfo[species].baseAttack;
        damage *= gBattleMoves[ctx->currentMove].power;
        damage *= (mon->level * 2 / 5 + 2);
        damage /= gSpeciesInfo[TARGET(ctx)->species].baseDefense;
        damage = (damage / 50) + 2;
        if (s->protectStructs[ctx->battlerAttacker].helpingHand)
            damage = damage * 15 / 10;

        // movevaluescleanup, critcalc; jumpifbyte gCritMultiplier == 2 -> manipulatedamage DMG_DOUBLED; adjustnormaldamage
        hit = &r->sequentialHits[r->numSequentialHits++];
        hit->power = gBattleMoves[ctx->currentMove].power;
        hit->normal = RandomDamageRolls(damage);
        hit->critical = RandomDamageRolls(damage * 2);
    }

    if (r->numSequentialHits == 0)
    {
        SetOutcome(r, EMERALD_CALC_FAILS, EMERALD_REASON_NO_PARTY_MEMBER_CAN_ATTACK);
        return;
    }
    ctx->critMultiplier = 1;
    r->critChance = CritChance(ctx);
    r->moveType = TYPE_MYSTERY; // typeless: no STAB, no type chart
    AdjustDamageCaps(ctx, r, TRUE);
}

/* BattleScript_EffectPresent */
static void Script_Present(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    static const struct { u16 threshold; u16 power; } kPresent[] = {{102, 40}, {178, 80}, {204, 120}};
    struct MoveContext afterTypecalc;
    u16 prev = 0;
    int i;

    if (!Script_AttackCanceler(ctx, r) || !Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;

    // typecalc runs once on gBattleMoveDamage == 0 before the power roll;
    // only its flags survive into the damage branches' own typecalc.
    Cmd_typecalc(ctx);
    ctx->battleMoveDamage = 0;
    afterTypecalc = *ctx;

    // presentdamagecalculation: rand = Random() & 0xFF
    r->numBranches = 0;
    for (i = 0; i < 3; i++)
    {
        EmeraldCalcBranch *b = &r->branches[r->numBranches++];
        struct MoveContext branch = afterTypecalc;
        b->probability = (kPresent[i].threshold - prev) / 256.0;
        prev = kPresent[i].threshold;
        branch.dynamicBasePower = kPresent[i].power;
        CritDamageTypeAdjust(&branch, r, &b->hit); // -> BattleScript_HitFromCritCalc
        *ctx = branch;
    }
    {
        EmeraldCalcBranch *b = &r->branches[r->numBranches++];
        s32 heal = TARGET(ctx)->maxHP / 4;
        if (heal == 0)
            heal = 1;
        b->probability = (256 - prev) / 256.0;
        b->healsTarget = TRUE;
        b->hit.normal = b->hit.critical = FixedDamageRolls(heal);
    }
    FinishTypecalc(ctx, r);
}

/* BattleScript_EffectMagnitude -> BattleScript_HitsAllWithUndergroundBonusLoop */
static void Script_Magnitude(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    // Cmd_magnitudedamagecalculation: magnitude = Random() % 100
    static const struct { u8 below; u8 power; } kMagnitude[] = {
        {5, 10}, {15, 30}, {35, 50}, {65, 70}, {85, 90}, {95, 110}, {100, 150},
    };
    double prev = 0.0;
    int i;

    if (!Script_AttackCanceler(ctx, r))
        return;

    r->numBranches = 0;
    for (i = 0; i < (int)(sizeof(kMagnitude) / sizeof(kMagnitude[0])); i++)
    {
        EmeraldCalcBranch *b = &r->branches[r->numBranches++];
        struct MoveContext branch = *ctx;
        double upTo = RandomModBelow(100, kMagnitude[i].below);
        b->probability = upTo - prev;
        prev = upTo;
        branch.dynamicBasePower = kMagnitude[i].power;
        Script_HitsAllWithUndergroundBonus(&branch, r, &b->hit);
        if (r->outcome != EMERALD_CALC_DEALS_DAMAGE)
            return;
        *ctx = branch;
    }
}

/* BattleScript_EffectPsywave: 11 equally likely damages
 * (Cmd_psywavedamageeffect rejects rand % 16 > 10). */
static void Script_Psywave(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    int randDamage;

    if (!Script_AttackCanceler(ctx, r) || !Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;
    TypecalcForImmunityOnly(ctx, r);

    r->numBranches = 0;
    for (randDamage = 0; randDamage <= 10; randDamage++)
    {
        EmeraldCalcBranch *b = &r->branches[r->numBranches++];
        b->probability = 1.0 / 11;
        FixedDamage(ctx, r, &b->hit, ATTACKER(ctx)->level * (randDamage * 10 + 50) / 100);
    }
}

/* BattleScript_EffectCounter / _EffectMirrorCoat: 2x the physical/special
 * damage taken this turn from the other side, at whoever dealt it. */
static void Script_CounterOrMirrorCoat(struct MoveContext *ctx, EmeraldCalcResult *r, bool8 special)
{
    const struct BattleState *s = ctx->s;
    const struct ProtectStruct *p = &s->protectStructs[ctx->battlerAttacker];
    u32 dmg = special ? p->specialDmg : p->physicalDmg;
    u8 hitBy = special ? p->specialBattlerId : p->physicalBattlerId;
    u8 sideTarget = GetBattlerSide(s, hitBy);

    if (!Script_AttackCanceler(ctx, r))
        return;

    // counterdamagecalculator / mirrorcoatdamagecalculator
    if (dmg && GetBattlerSide(s, ctx->battlerAttacker) != sideTarget && s->battleMons[hitBy].hp)
    {
        if (s->sideTimers[sideTarget].followmeTimer && s->battleMons[s->sideTimers[sideTarget].followmeTarget].hp)
            ctx->battlerTarget = s->sideTimers[sideTarget].followmeTarget;
        else
            ctx->battlerTarget = hitBy;
    }
    else
    {
        SetOutcome(r, EMERALD_CALC_DAMAGE_UNKNOWN, EMERALD_REASON_NOT_HIT_THIS_TURN);
        return;
    }

    if (!Script_AccuracyCheck(ctx, r, ACC_CURR_MOVE))
        return;
    Cmd_typecalc2(ctx);
    FinishTypecalc(ctx, r);
    FixedDamage(ctx, r, &r->branches[0].hit, (s32)(dmg * 2));
}

/* BattleScript_EffectOHKO + Cmd_tryKO */
static void Script_OHKO(struct MoveContext *ctx, EmeraldCalcResult *r)
{
    const struct BattleState *s = ctx->s;
    const struct BattlePokemon *attacker = ATTACKER(ctx), *target = TARGET(ctx);
    u8 holdEffect, param;

    if (!Script_AttackCanceler(ctx, r) || !Script_AccuracyCheck(ctx, r, NO_ACC_CALC_CHECK_LOCK_ON))
        return;

    Cmd_typecalc(ctx);
    FinishTypecalc(ctx, r);
    if (r->outcome != EMERALD_CALC_DEALS_DAMAGE)
        return; // jumpifmovehadnoeffect

    r->isOhko = TRUE;
    if (target->ability == ABILITY_STURDY)
    {
        SetOutcome(r, EMERALD_CALC_FAILS, EMERALD_REASON_STURDY);
        return;
    }
    if (attacker->level < target->level)
    {
        SetOutcome(r, EMERALD_CALC_FAILS, EMERALD_REASON_TARGET_LEVEL_HIGHER);
        return;
    }

    if ((s->statuses3[ctx->battlerTarget] & STATUS3_ALWAYS_HITS)
     && s->disableStructs[ctx->battlerTarget].battlerWithSureHit == ctx->battlerAttacker)
    {
        // Lock-On / Mind Reader: no roll (the accuracycheck above already passed)
    }
    else
    {
        // Random() % 100 + 1 < chance
        u16 chance = gBattleMoves[ctx->currentMove].accuracy + (attacker->level - target->level);
        r->accuracy *= chance ? RandomModBelow(100, chance - 1) : 0.0;
    }

    r->branches[0].hit.normal = r->branches[0].hit.critical = FixedDamageRolls(target->hp);
    GetBattlerHoldEffect(s, ctx->battlerTarget, &holdEffect, &param);
    if (holdEffect == HOLD_EFFECT_FOCUS_BAND)
        r->focusBandChance = param;
    if (s->protectStructs[ctx->battlerTarget].endured)
        r->cannotKo = TRUE;
    r->targetHasSubstitute = (target->status2 & STATUS2_SUBSTITUTE) != 0;
}

/* --- Entry point ------------------------------------------------------------ */

EmeraldCalcResult EmeraldCalc_SimulateMove(const struct BattleState *s, u8 battlerAtk, u8 battlerDef, u16 move)
{
    EmeraldCalcResult r;
    struct MoveContext ctx;
    const struct BattlePokemon *attacker = &s->battleMons[battlerAtk];
    const struct BattlePokemon *target = &s->battleMons[battlerDef];

    // BattleScript_EffectNaturePower: callenvironmentattack swaps
    // gCurrentMove for the environment's move and runs *its* script.
    if (move == MOVE_NATURE_POWER && s->battleEnvironment < sizeof(sNaturePowerMoves) / sizeof(sNaturePowerMoves[0]))
        move = sNaturePowerMoves[s->battleEnvironment];

    InitResult(&r, move);
    MoveContext_Init(&ctx, s, battlerAtk, battlerDef, move);

    switch (gBattleMoves[move].effect)
    {
    // --- moves that set up power / type / dmgMultiplier, then EffectHit ---
    case EFFECT_FLAIL:
        Cmd_remaininghptopower(&ctx);
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_ERUPTION:
        Cmd_scaledamagebyhealthratio(&ctx);
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_HIDDEN_POWER:
        Cmd_hiddenpowercalc(&ctx);
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_WEATHER_BALL:
        Cmd_setweatherballtype(&ctx);
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_REVENGE:
        Cmd_doubledamagedealtifdamaged(&ctx);
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_FACADE:
        if (attacker->status1 & (STATUS1_POISON | STATUS1_BURN | STATUS1_PARALYSIS | STATUS1_TOXIC_POISON))
            ctx.dmgMultiplier = 2; // BattleScript_FacadeDoubleDmg
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_SMELLINGSALT:
        if (!(target->status2 & STATUS2_SUBSTITUTE) && (target->status1 & STATUS1_PARALYSIS))
            ctx.dmgMultiplier = 2; // BattleScript_SmellingsaltDoubleDmg
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_FLINCH_MINIMIZE_HIT: // BattleScript_EffectStomp
        if (s->statuses3[battlerDef] & STATUS3_MINIMIZED)
            ctx.dmgMultiplier = 2;
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_GUST:
    case EFFECT_TWISTER:
        if (s->statuses3[battlerDef] & STATUS3_ON_AIR)
        {
            ctx.hitMarker |= HITMARKER_IGNORE_ON_AIR;
            ctx.dmgMultiplier = 2;
        }
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_THUNDER:
    case EFFECT_SKY_UPPERCUT:
        ctx.hitMarker |= HITMARKER_IGNORE_ON_AIR;
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_TRAP: // BattleScript_EffectTrap: Whirlpool vs Dive
        if (move == MOVE_WHIRLPOOL && s->statuses3[battlerDef] & STATUS3_UNDERWATER)
        {
            ctx.hitMarker |= HITMARKER_IGNORE_UNDERWATER;
            ctx.dmgMultiplier = 2;
        }
        Script_EffectHit(&ctx, &r);
        break;

    // --- own flow, standard damage ---
    case EFFECT_LOW_KICK:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        Cmd_weightdamagecalculation(&ctx);
        Script_HitFromAccCheck(&ctx, &r);
        break;
    case EFFECT_RETURN:
    case EFFECT_FRUSTRATION:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        Cmd_friendshiptodamagecalculation(&ctx);
        Script_HitFromCritCalc(&ctx, &r);
        break;
    case EFFECT_ROLLOUT:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        Cmd_typecalc2(&ctx);
        if (ctx.moveResultFlags & MOVE_RESULT_NO_EFFECT)
        {
            // rolloutdamagecalculation -> BattleScript_MoveMissedPause
            FinishTypecalc(&ctx, &r);
            break;
        }
        Cmd_rolloutdamagecalculation(&ctx);
        Script_HitFromCritCalc(&ctx, &r);
        break;
    case EFFECT_FURY_CUTTER:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        Cmd_furycuttercalc(&ctx);
        Script_HitFromCritCalc(&ctx, &r);
        break;
    case EFFECT_BRICK_BREAK:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        Cmd_removelightscreenreflect(&ctx);
        Script_HitFromCritCalc(&ctx, &r);
        break;
    case EFFECT_EARTHQUAKE:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        Script_HitsAllWithUndergroundBonus(&ctx, &r, &r.branches[0].hit);
        break;
    case EFFECT_MAGNITUDE:
        Script_Magnitude(&ctx, &r);
        break;
    case EFFECT_EXPLOSION:
    {
        // Cmd_tryexplosion: fails if any battler (fainted or not) has Damp
        u8 i;
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        for (i = 0; i < s->battlersCount; i++)
        {
            if (s->battleMons[i].ability == ABILITY_DAMP)
                break;
        }
        if (i != s->battlersCount)
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_DAMP);
            break;
        }
        // BattleScript_ExplosionLoop: critcalc, damagecalc, typecalc, adjustnormaldamage, accuracycheck
        Script_HitFromCritCalc(&ctx, &r);
        if (r.outcome == EMERALD_CALC_DEALS_DAMAGE)
            Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE);
        break;
    }
    case EFFECT_FAKE_OUT:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        if (!s->disableStructs[battlerAtk].isFirstTurn) // jumpifnotfirstturn
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_NOT_FIRST_TURN);
            break;
        }
        Script_EffectHit(&ctx, &r);
        break;
    case EFFECT_FOCUS_PUNCH:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        // jumpifnodamage: loses focus if hit earlier this turn
        if (s->protectStructs[battlerAtk].physicalDmg || s->protectStructs[battlerAtk].specialDmg)
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_LOST_FOCUS);
            break;
        }
        Script_HitFromAccCheck(&ctx, &r);
        break;
    case EFFECT_SNORE:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        if (!(attacker->status1 & STATUS1_SLEEP))
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_USER_NOT_ASLEEP);
            break;
        }
        Script_HitFromAccCheck(&ctx, &r);
        break;
    case EFFECT_DREAM_EATER:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        if (target->status2 & STATUS2_SUBSTITUTE)
        {
            SetOutcome(&r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_TARGET_BEHIND_SUBSTITUTE);
            break;
        }
        if (!(target->status1 & STATUS1_SLEEP))
        {
            SetOutcome(&r, EMERALD_CALC_NO_EFFECT, EMERALD_REASON_TARGET_NOT_ASLEEP);
            break;
        }
        Script_HitFromAccCheck(&ctx, &r);
        break;

    // --- two-turn moves: damage is the attacking turn's ---
    case EFFECT_SOLAR_BEAM:
        // BattleScript_EffectSolarBeam: fires immediately in sun unless
        // Cloud Nine/Air Lock is out (jumpifabilitypresent)
        if (AbilityOnField(s, ABILITY_CLOUD_NINE) || AbilityOnField(s, ABILITY_AIR_LOCK) || !(s->battleWeather & B_WEATHER_SUN))
            NoteTwoTurnMove(&ctx, &r);
        Script_HitFromAtkCanceler(&ctx, &r);
        break;
    case EFFECT_RAZOR_WIND:
    case EFFECT_SKULL_BASH:
    case EFFECT_SKY_ATTACK:
    case EFFECT_SEMI_INVULNERABLE:
        NoteTwoTurnMove(&ctx, &r);
        Script_HitFromAtkCanceler(&ctx, &r); // BattleScript_TwoTurnMovesSecondTurn / _SecondTurnSemiInvulnerable
        break;

    // --- multi-hit ---
    case EFFECT_MULTI_HIT:
        Script_MultiHit(&ctx, &r, 0);
        break;
    case EFFECT_DOUBLE_HIT:
    case EFFECT_TWINEEDLE:
        Script_MultiHit(&ctx, &r, 2);
        break;
    case EFFECT_TRIPLE_KICK:
        Script_TripleKick(&ctx, &r);
        break;
    case EFFECT_BEAT_UP:
        Script_BeatUp(&ctx, &r);
        break;

    // --- random power ---
    case EFFECT_PRESENT:
        Script_Present(&ctx, &r);
        break;
    case EFFECT_PSYWAVE:
        Script_Psywave(&ctx, &r);
        break;

    // --- fixed damage ---
    case EFFECT_SUPER_FANG:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        TypecalcForImmunityOnly(&ctx, &r);
        {
            // damagetohalftargethp -> BattleScript_HitFromAtkAnimation (no adjustsetdamage)
            s32 damage = target->hp / 2;
            if (damage == 0)
                damage = 1;
            r.branches[0].hit.normal = r.branches[0].hit.critical = FixedDamageRolls(damage);
            r.targetHasSubstitute = (target->status2 & STATUS2_SUBSTITUTE) != 0;
        }
        break;
    case EFFECT_DRAGON_RAGE:
    case EFFECT_SONICBOOM:
    case EFFECT_LEVEL_DAMAGE:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        TypecalcForImmunityOnly(&ctx, &r);
        FixedDamage(&ctx, &r, &r.branches[0].hit,
                    gBattleMoves[move].effect == EFFECT_DRAGON_RAGE ? 40     // setword gBattleMoveDamage, 40
                    : gBattleMoves[move].effect == EFFECT_SONICBOOM ? 20     // setword gBattleMoveDamage, 20
                    : attacker->level);                                    // dmgtolevel
        break;
    case EFFECT_ENDEAVOR:
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        if (target->hp <= attacker->hp) // setdamagetohealthdifference
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_TARGET_HP_NOT_HIGHER);
            break;
        }
        if (!Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        TypecalcForImmunityOnly(&ctx, &r);
        FixedDamage(&ctx, &r, &r.branches[0].hit, target->hp - attacker->hp);
        break;
    case EFFECT_COUNTER:
        Script_CounterOrMirrorCoat(&ctx, &r, FALSE);
        break;
    case EFFECT_MIRROR_COAT:
        Script_CounterOrMirrorCoat(&ctx, &r, TRUE);
        break;
    case EFFECT_BIDE:
        SetOutcome(&r, EMERALD_CALC_DAMAGE_UNKNOWN, EMERALD_REASON_BIDE);
        break;
    case EFFECT_OHKO:
        Script_OHKO(&ctx, &r);
        break;

    // --- CalculateBaseDamage() without the usual pipeline ---
    case EFFECT_SPIT_UP:
        if (!Script_AttackCanceler(&ctx, &r) || !Script_AccuracyCheck(&ctx, &r, ACC_CURR_MOVE))
            break;
        if (s->disableStructs[battlerAtk].stockpileCounter == 0)
        {
            SetOutcome(&r, EMERALD_CALC_FAILS, EMERALD_REASON_NO_STOCKPILE);
            break;
        }
        // stockpiletobasedamage (no critcalc: gCritMultiplier is still 1), typecalc, adjustsetdamage
        ctx.battleMoveDamage = CalculateBaseDamage(&ctx, attacker, target, move,
                                                   ctx.sideStatuses[GetBattlerSide(s, battlerDef)], 0,
                                                   0, battlerAtk, battlerDef)
                               * s->disableStructs[battlerAtk].stockpileCounter;
        if (s->protectStructs[battlerAtk].helpingHand)
            ctx.battleMoveDamage = ctx.battleMoveDamage * 15 / 10;
        Cmd_typecalc(&ctx);
        FinishTypecalc(&ctx, &r);
        FixedDamage(&ctx, &r, &r.branches[0].hit, ctx.battleMoveDamage);
        break;
    case EFFECT_FUTURE_SIGHT:
        // trysetfutureattack stores CalculateBaseDamage() (+ Helping Hand)
        // now -- no crit, no STAB, no type chart; two turns later
        // BattleScript_MonTookFutureAttack rolls accuracy against it and
        // applies adjustnormaldamage2's random roll.
        r.landsLater = TRUE;
        if (!Script_AttackCanceler(&ctx, &r))
            break;
        ctx.battleMoveDamage = CalculateBaseDamage(&ctx, attacker, target, move,
                                                   ctx.sideStatuses[GetBattlerSide(s, battlerDef)], 0,
                                                   0, battlerAtk, battlerDef);
        if (s->protectStructs[battlerAtk].helpingHand)
            ctx.battleMoveDamage = ctx.battleMoveDamage * 15 / 10;
        r.accuracy = AccuracyCheck(&ctx, move, &r.missReason);
        r.moveType = TYPE_MYSTERY; // typeless
        r.branches[0].hit.power = gBattleMoves[move].power;
        r.branches[0].hit.normal = r.branches[0].hit.critical = RandomDamageRolls(ctx.battleMoveDamage);
        AdjustDamageCaps(&ctx, &r, FALSE);
        break;

    default:
        if (gBattleMoves[move].power == 0)
        {
            SetOutcome(&r, EMERALD_CALC_STATUS_MOVE, EMERALD_REASON_NONE);
            break;
        }
        // Everything else that deals damage -- the stat-change/status/
        // flinch/recoil/drain "_HIT" effects, High Critical, Always Hit,
        // Vital Throw, Rampage, Uproar, Recharge, Rage, Pursuit, False
        // Swipe, Knock Off, Thief, Secret Power, ... -- reaches damage
        // through BattleScript_EffectHit or an equivalent attackcanceler ->
        // accuracycheck -> critcalc -> damagecalc -> typecalc ->
        // adjustnormaldamage sequence; the effect only changes what happens
        // after the damage is dealt.
        Script_EffectHit(&ctx, &r);
        break;
    }

    return r;
}
