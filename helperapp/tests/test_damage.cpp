extern "C" {
#include "emerald_calc.h"
}

#include <cstring>
#include <gtest/gtest.h>

// CODE_PLAN.md Phase 5.5 calls for validating computed rolls against ~25
// in-game executions from a savestate. That's not available in this
// environment, so per the chosen validation approach these cases are
// instead hand-computed against the real Gen 3 damage formula (using the
// project's own real move data -- src/data/battle_moves.h -- rather than
// invented power/type values) and checked for an exact match, truncation
// order included. Treat these as "port didn't drift from the documented
// formula," not as "matches a real cartridge" -- that still needs a live
// mGBA cross-check before this is trusted for anything higher-stakes.

namespace {

EmeraldCalcMon MakeMon(u16 species, u16 attack, u16 defense, u16 spAttack, u16 spDefense, u16 hp, u16 maxHp,
                       u8 type1, u8 type2, u8 level)
{
    EmeraldCalcMon mon;
    std::memset(&mon, 0, sizeof(mon));
    mon.species = species;
    mon.attack = attack;
    mon.defense = defense;
    mon.spAttack = spAttack;
    mon.spDefense = spDefense;
    mon.hp = hp;
    mon.maxHp = maxHp;
    mon.types[0] = type1;
    mon.types[1] = type2;
    mon.level = level;
    for (int i = 0; i < NUM_BATTLE_STATS; ++i)
        mon.statStages[i] = DEFAULT_STAT_STAGE;
    return mon;
}

} // namespace

TEST(Damage, TackleNeutralNoStabAtDefaultStages)
{
    EmeraldCalcMon attacker = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50);
    EmeraldCalcMon defender = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_TACKLE, &field);

    // Hand-computed: attack=100, power=35, level=50 -> ((100*35*(2*50/5+2))/100)/50 + 2 = 17
    EXPECT_EQ(result.normal.max, 17);
    EXPECT_EQ(result.normal.min, 14); // 17 * 85 / 100 = 14.45 -> 14
    EXPECT_FALSE(result.isImmune);
    EXPECT_FALSE(result.movePowerIsZero);
}

TEST(Damage, CriticalHitIgnoresLoweredAttackStage)
{
    EmeraldCalcMon attacker = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50);
    attacker.statStages[STAT_ATK] = DEFAULT_STAT_STAGE - 2; // -2 Attack
    EmeraldCalcMon defender = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_TACKLE, &field);

    // Non-crit uses the lowered stage (ratio 10/20 = 0.5x attack -> 9 max).
    EXPECT_EQ(result.normal.max, 9);
    // Crit ignores the stat drop entirely, falling back to raw attack --
    // identical to the neutral-stage case above (17 max, 14 min).
    EXPECT_EQ(result.critical.max, 17);
    EXPECT_EQ(result.critical.min, 14);
}

TEST(Damage, StabAndSuperEffectiveStack)
{
    EmeraldCalcMon attacker = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50);
    EmeraldCalcMon defender = MakeMon(SPECIES_VULPIX, 100, 100, 100, 100, 200, 200, TYPE_FIRE, TYPE_FIRE, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_SURF, &field);

    // Hand-computed: spAttack=100, power=95 -> base 41, +2 = 43;
    // STAB 43*1.5 = 64 (truncated); super effective (Water->Fire) 64*2 = 128.
    EXPECT_EQ(result.normal.max, 128);
    EXPECT_EQ(result.normal.min, 108); // 128 * 85 / 100 = 108.8 -> 108
    EXPECT_TRUE(result.typeEffectivenessFlags & EMERALD_MOVE_RESULT_SUPER_EFFECTIVE);
    EXPECT_FALSE(result.isImmune);
}

TEST(Damage, NoEffectTypeMatchupIsImmune)
{
    EmeraldCalcMon attacker = MakeMon(SPECIES_PIKACHU, 100, 100, 100, 100, 100, 100, TYPE_ELECTRIC, TYPE_ELECTRIC, 50);
    EmeraldCalcMon defender = MakeMon(SPECIES_SANDSHREW, 100, 100, 100, 100, 200, 200, TYPE_GROUND, TYPE_GROUND, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_THUNDERBOLT, &field);

    EXPECT_TRUE(result.isImmune);
    EXPECT_TRUE(result.typeEffectivenessFlags & EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE);
    for (int i = 0; i < EMERALD_CALC_NUM_ROLLS; ++i)
        EXPECT_EQ(result.normal.rolls[i], 0);
}

TEST(Damage, ZeroPowerStatusMoveIsFlaggedNotZeroDamage)
{
    EmeraldCalcMon attacker = MakeMon(SPECIES_PIKACHU, 100, 100, 100, 100, 100, 100, TYPE_ELECTRIC, TYPE_ELECTRIC, 50);
    EmeraldCalcMon defender = MakeMon(SPECIES_SANDSHREW, 100, 100, 100, 100, 200, 200, TYPE_GROUND, TYPE_GROUND, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_GROWL, &field);

    EXPECT_TRUE(result.movePowerIsZero);
}

TEST(Damage, ChoiceBandBoostsPhysicalAttack)
{
    u8 holdEffect[2] = {HOLD_EFFECT_NONE, HOLD_EFFECT_CHOICE_BAND};
    u8 holdEffectParam[2] = {0, 0};
    EmeraldCalc_SetItemHoldEffects(holdEffect, holdEffectParam, 2);

    EmeraldCalcMon attacker = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50);
    attacker.item = 1; // Choice Band, per the fake table above
    EmeraldCalcMon defender = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();

    EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&attacker, &defender, MOVE_TACKLE, &field);

    // attack 100 -> 150 (Choice Band +50%) -> ((150*35*22)/100)/50 + 2 = 25
    EXPECT_EQ(result.normal.max, 25);

    EmeraldCalc_SetItemHoldEffects(nullptr, nullptr, 0); // don't leak into other tests
}

TEST(KOChances, ComputesGuaranteedAndPossibleTurnsAndAccuracy)
{
    EmeraldCalcRollSet rolls;
    for (int i = 0; i < EMERALD_CALC_NUM_ROLLS; ++i)
        rolls.rolls[i] = 20 - i; // 20 (100%) down to 5 (85%)
    rolls.max = rolls.rolls[0];
    rolls.min = rolls.rolls[EMERALD_CALC_NUM_ROLLS - 1];

    EmeraldCalcKOChances ko = EmeraldCalc_KOChances(&rolls, /*targetHp=*/18, /*moveAccuracy=*/80);

    // Rolls >= 18: 20, 19, 18 -> 3 of 16.
    EXPECT_DOUBLE_EQ(ko.ohkoChance, 3.0 / 16.0);
    EXPECT_DOUBLE_EQ(ko.accuracyAdjustedOhkoChance, (3.0 / 16.0) * 0.8);
    EXPECT_EQ(ko.guaranteedKoTurns, 4);  // ceil(18/5)
    EXPECT_EQ(ko.possibleKoTurns, 1);    // ceil(18/20)
}
