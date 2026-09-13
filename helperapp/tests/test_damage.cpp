extern "C" {
#include "emerald_calc.h"
}

#include <cstring>
#include <gtest/gtest.h>
#include <initializer_list>
#include <tuple>
#include <vector>

// CODE_PLAN.md Phase 5.5 calls for validating computed rolls against ~25
// in-game executions from a savestate. That's not available in this
// environment, so these cases are instead hand-computed from the decomp's
// own code (CalculateBaseDamage(), the battle script commands, the battle
// scripts), using the project's own real move/species data, and checked
// for an exact match -- truncation order included. Every expected number
// below has its derivation in a comment. Treat these as "the port didn't
// drift from src/", not as "matches a real cartridge" -- that still needs
// a live mGBA cross-check.
//
// Probabilities: Random() returns a u16, so "Random() % 100 < n" is
// (655 * n + min(n, 36)) / 65536, not n / 100 -- the tests use the exact
// counts.

namespace {

constexpr double kU16 = 65536.0;

// A singles trainer battle: battler 0 (player side) attacks battler 1.
// Both start as level 50 Water-type Squirtle with 100 in every stat and
// 200 HP; tests override what they need.
struct Battle {
    BattleState s;

    Battle()
    {
        BattleState_Init(&s);
        s.battleTypeFlags = BATTLE_TYPE_TRAINER;
        Set(0, SPECIES_SQUIRTLE, TYPE_WATER, TYPE_WATER);
        Set(1, SPECIES_SQUIRTLE, TYPE_WATER, TYPE_WATER);
    }

    BattlePokemon &Set(int battler, u16 species, u8 type1, u8 type2)
    {
        BattlePokemon &mon = s.battleMons[battler];
        mon.species = species;
        mon.types[0] = type1;
        mon.types[1] = type2;
        mon.attack = mon.defense = mon.speed = mon.spAttack = mon.spDefense = 100;
        mon.level = 50;
        mon.hp = mon.maxHP = 200;
        return mon;
    }

    BattlePokemon &Atk() { return s.battleMons[0]; }
    BattlePokemon &Def() { return s.battleMons[1]; }
    EmeraldCalcResult Use(u16 move, u8 atk = 0, u8 def = 1) { return EmeraldCalc_SimulateMove(&s, atk, def, move); }

    void SetFlag(u16 id) { s.saveFlags[id / 8] |= u8(1 << (id % 8)); }

    // Doubles: battlers 0/2 vs 1/3, all present.
    void MakeDouble()
    {
        s.battleTypeFlags |= BATTLE_TYPE_DOUBLE;
        s.battlersCount = 4;
        Set(2, SPECIES_SQUIRTLE, TYPE_WATER, TYPE_WATER);
        Set(3, SPECIES_SQUIRTLE, TYPE_WATER, TYPE_WATER);
    }
};

s32 Max(const EmeraldCalcResult &r) { return r.branches[0].hit.normal.max; }
s32 Min(const EmeraldCalcResult &r) { return r.branches[0].hit.normal.min; }
u16 Power(const EmeraldCalcResult &r) { return r.branches[0].hit.power; }

// Installs a hold-effect table (real item ids, their real items.h hold
// effects/params) for one test, and clears it afterwards.
class HeldItems
{
public:
    HeldItems(std::initializer_list<std::tuple<u16, u8, u8>> items)
        : m_holdEffect(ITEMS_COUNT), m_param(ITEMS_COUNT)
    {
        for (const auto &[item, holdEffect, param] : items) {
            m_holdEffect[item] = holdEffect;
            m_param[item] = param;
        }
        EmeraldCalc_SetItemHoldEffects(m_holdEffect.data(), m_param.data(), ITEMS_COUNT);
    }
    ~HeldItems() { EmeraldCalc_SetItemHoldEffects(nullptr, nullptr, 0); }

private:
    std::vector<u8> m_holdEffect, m_param;
};

EmeraldCalcRollSet Fixed(s32 damage)
{
    EmeraldCalcRollSet set;
    for (s32 &roll : set.rolls)
        roll = damage;
    set.min = set.max = damage;
    return set;
}

// A hand-built one-branch, one-hit result, for testing the KO math alone.
EmeraldCalcResult FixedHitResult(s32 damage)
{
    EmeraldCalcResult r;
    std::memset(&r, 0, sizeof(r));
    r.outcome = EMERALD_CALC_DEALS_DAMAGE;
    r.accuracy = 1.0;
    r.numBranches = 1;
    r.branches[0].probability = 1.0;
    r.branches[0].hit.normal = r.branches[0].hit.critical = Fixed(damage);
    r.minHits = r.maxHits = 1;
    r.hitCountChance[1] = 1.0;
    return r;
}

} // namespace

// --- CalculateBaseDamage() + typecalc ----------------------------------------

TEST(Damage, TackleNeutralNoStabAtDefaultStages)
{
    Battle b;
    const EmeraldCalcResult r = b.Use(MOVE_TACKLE);

    // attack 100, power 35, level 50: 100*35*(2*50/5+2) / 100 / 50 + 2 = 17
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 17);
    EXPECT_EQ(Min(r), 14); // 17 * 85 / 100 = 14.45 -> 14
    EXPECT_EQ(Power(r), 35);
}

TEST(Damage, CriticalHitDoublesAndIgnoresLoweredAttack)
{
    Battle b;
    b.Atk().statStages[STAT_ATK] = DEFAULT_STAT_STAGE - 2;
    const EmeraldCalcResult r = b.Use(MOVE_TACKLE);

    // -2 Attack: 100 * 10/20 = 50 -> 50*35*22/100/50 + 2 = 9.
    EXPECT_EQ(Max(r), 9);
    // A crit ignores the drop (17), then Cmd_damagecalc doubles it.
    EXPECT_EQ(r.branches[0].hit.critical.max, 34);
    EXPECT_EQ(r.branches[0].hit.critical.min, 28); // 34 * 85 / 100 = 28.9
}

TEST(Damage, CriticalHitIgnoresRaisedDefenseAndReflect)
{
    Battle b;
    b.Def().statStages[STAT_DEF] = DEFAULT_STAT_STAGE + 2;
    b.s.sideStatuses[B_SIDE_OPPONENT] = SIDE_STATUS_REFLECT;
    const EmeraldCalcResult r = b.Use(MOVE_TACKLE);

    // +2 Defense (200): 77000 / 200 / 50 = 7, Reflect halves -> 3, +2 = 5.
    EXPECT_EQ(Max(r), 5);
    // Crit: defense 100, no Reflect -> 17, x2 = 34.
    EXPECT_EQ(r.branches[0].hit.critical.max, 34);
}

TEST(Damage, StabAndSuperEffectiveStack)
{
    Battle b;
    b.Set(1, SPECIES_VULPIX, TYPE_FIRE, TYPE_FIRE);
    const EmeraldCalcResult r = b.Use(MOVE_SURF);

    // spAttack 100, power 95: 100*95*22/100/50 = 41, +2 = 43; STAB 64; x2 = 128.
    EXPECT_EQ(Max(r), 128);
    EXPECT_EQ(Min(r), 108); // 128 * 85 / 100 = 108.8
    EXPECT_TRUE(r.moveResultFlags & MOVE_RESULT_SUPER_EFFECTIVE);
}

TEST(Damage, OpposingMultipliersOnDualTypeCancelOut)
{
    Battle b;
    b.Set(1, SPECIES_MARSHTOMP, TYPE_WATER, TYPE_GROUND);
    const EmeraldCalcResult r = b.Use(MOVE_SURF);

    // STAB 64; Water->Water x0.5 = 32; Water->Ground x2 = 64. Flags cancel.
    EXPECT_EQ(Max(r), 64);
    EXPECT_FALSE(r.moveResultFlags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE));
}

TEST(Damage, TypeImmunityIsNoEffect)
{
    Battle b;
    b.Set(0, SPECIES_PIKACHU, TYPE_ELECTRIC, TYPE_ELECTRIC);
    b.Set(1, SPECIES_SANDSHREW, TYPE_GROUND, TYPE_GROUND);
    const EmeraldCalcResult r = b.Use(MOVE_THUNDERBOLT);

    EXPECT_EQ(r.outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(r.reason, EMERALD_REASON_TYPE_IMMUNITY);
    EXPECT_TRUE(r.moveResultFlags & MOVE_RESULT_DOESNT_AFFECT_FOE);
}

TEST(Damage, ForesightLetsNormalMovesHitGhosts)
{
    Battle b;
    b.Set(1, SPECIES_GENGAR, TYPE_GHOST, TYPE_POISON);
    EXPECT_EQ(b.Use(MOVE_TACKLE).outcome, EMERALD_CALC_NO_EFFECT);

    b.Def().status2 |= STATUS2_FORESIGHT;
    const EmeraldCalcResult r = b.Use(MOVE_TACKLE);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 17); // Normal vs Poison is neutral
}

TEST(Damage, StatusMoveHasNoDamage)
{
    Battle b;
    EXPECT_EQ(b.Use(MOVE_GROWL).outcome, EMERALD_CALC_STATUS_MOVE);
}

TEST(Damage, ChoiceBandBoostsPhysicalAttack)
{
    HeldItems items({{ITEM_CHOICE_BAND, HOLD_EFFECT_CHOICE_BAND, 0}});
    Battle b;
    b.Atk().item = ITEM_CHOICE_BAND;

    // attack 100 -> 150: 150*35*22/100/50 + 2 = 25
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 25);
}

TEST(Damage, BurnHalvesPhysicalDamageUnlessGuts)
{
    Battle b;
    b.Atk().status1 = STATUS1_BURN;
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 9); // 15 / 2 = 7, +2

    b.Atk().ability = ABILITY_GUTS; // attack x1.5, no burn halving: 150*35*22/100/50 + 2 = 25
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 25);
}

// --- Weather --------------------------------------------------------------------

TEST(Damage, RainAndSunModifySpecialFireAndWater)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    b.s.battleWeather = B_WEATHER_RAIN_PERMANENT | B_WEATHER_RAIN_TEMPORARY; // what Drizzle sets
    EXPECT_EQ(Max(b.Use(MOVE_SURF)), 94);  // 41 * 1.5 = 61, +2 = 63, STAB 94

    b.s.battleWeather = B_WEATHER_SUN_TEMPORARY;
    EXPECT_EQ(Max(b.Use(MOVE_SURF)), 33);  // 41 / 2 = 20, +2 = 22, STAB 33
    EXPECT_EQ(Max(b.Use(MOVE_EMBER)), 27); // 17 * 1.5 = 25, +2 (no STAB)
}

TEST(Damage, SolarBeamIsHalvedInNonSunWeatherAndChargesOutsideSun)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    b.s.battleWeather = B_WEATHER_RAIN_TEMPORARY;
    EmeraldCalcResult r = b.Use(MOVE_SOLAR_BEAM);
    EXPECT_EQ(Max(r), 28); // 100*120*22/100/50 = 52, halved 26, +2
    EXPECT_TRUE(r.landsLater);

    b.s.battleWeather = B_WEATHER_SUN_TEMPORARY;
    r = b.Use(MOVE_SOLAR_BEAM);
    EXPECT_EQ(Max(r), 54);
    EXPECT_FALSE(r.landsLater); // fires the same turn in sun
}

TEST(Damage, AirLockNegatesWeather)
{
    Battle b;
    b.Set(1, SPECIES_RAYQUAZA, TYPE_DRAGON, TYPE_FLYING).ability = ABILITY_AIR_LOCK;
    b.s.battleWeather = B_WEATHER_RAIN_TEMPORARY;

    // No rain boost: 43, STAB 64; Water vs Dragon x0.5 = 32.
    EXPECT_EQ(Max(b.Use(MOVE_SURF)), 32);
}

TEST(Damage, WeatherBallTypeAndPowerFollowTheWeather)
{
    Battle b;
    b.Set(0, SPECIES_CASTFORM, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    // Clear: dynamic type Normal, physical: 100*50*22/100/50 + 2 = 24, STAB 36.
    EmeraldCalcResult r = b.Use(MOVE_WEATHER_BALL);
    EXPECT_EQ(Max(r), 36);
    EXPECT_EQ(r.moveType, TYPE_NORMAL);

    // Rain: Water (special), rain x1.5 on 22 -> 33, +2 = 35, dmgMultiplier x2 = 70.
    b.s.battleWeather = B_WEATHER_RAIN_TEMPORARY;
    r = b.Use(MOVE_WEATHER_BALL);
    EXPECT_EQ(Max(r), 70);
    EXPECT_EQ(r.moveType, TYPE_WATER);

    // Sandstorm: Rock (physical): 24 x2 = 48.
    b.s.battleWeather = B_WEATHER_SANDSTORM_TEMPORARY;
    r = b.Use(MOVE_WEATHER_BALL);
    EXPECT_EQ(Max(r), 48);
    EXPECT_EQ(r.moveType, TYPE_ROCK);

    // Cloud Nine out: no type change, no doubling, back to 36.
    b.s.battleWeather = B_WEATHER_RAIN_TEMPORARY;
    b.Def().ability = ABILITY_CLOUD_NINE;
    EXPECT_EQ(Max(b.Use(MOVE_WEATHER_BALL)), 36);
}

// --- Abilities and field effects ------------------------------------------------

TEST(Damage, ThickFatHalvesAttackersFireAndIceSpAttack)
{
    Battle b;
    b.Set(1, SPECIES_SNORLAX, TYPE_NORMAL, TYPE_NORMAL).ability = ABILITY_THICK_FAT;
    EXPECT_EQ(Max(b.Use(MOVE_EMBER)), 10); // spAttack 50: 50*40*22/100/50 = 8, +2
}

TEST(Damage, PlusBoostsWhenMinusIsOnTheField)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.Atk().ability = ABILITY_PLUS;
    EXPECT_EQ(Max(b.Use(MOVE_SURF)), 64); // no Minus: 43, STAB 64

    // ABILITY_ON_FIELD2 checks every battler -- a foe's Minus counts too.
    b.Def().ability = ABILITY_MINUS;
    EXPECT_EQ(Max(b.Use(MOVE_SURF)), 96); // spAttack 150: 150*95*22/100/50 = 62, +2 = 64, STAB 96
}

TEST(Damage, MudSportHalvesElectricPower)
{
    Battle b;
    b.Set(0, SPECIES_PIKACHU, TYPE_ELECTRIC, TYPE_ELECTRIC);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    EXPECT_EQ(Max(b.Use(MOVE_THUNDERBOLT)), 64); // 43, STAB 64

    b.s.statuses3[1] |= STATUS3_MUDSPORT; // anyone's Mud Sport counts
    EXPECT_EQ(Max(b.Use(MOVE_THUNDERBOLT)), 33); // power 47: 100*47*22/100/50 = 20, +2 = 22, STAB 33
}

TEST(Damage, ChargeAndHelpingHand)
{
    Battle b;
    b.Set(0, SPECIES_PIKACHU, TYPE_ELECTRIC, TYPE_ELECTRIC);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    b.s.statuses3[0] = STATUS3_CHARGED_UP;
    EXPECT_EQ(Max(b.Use(MOVE_THUNDERBOLT)), 129); // 43 x2 = 86, STAB 129
    b.s.statuses3[0] = 0;

    b.s.protectStructs[0].helpingHand = 1;
    EXPECT_EQ(Max(b.Use(MOVE_THUNDERBOLT)), 96); // 43 * 15 / 10 = 64, STAB 96
}

TEST(Damage, FlashFireBoostAndAbsorb)
{
    Battle b;
    b.Set(0, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    b.s.resourceFlags[0] = RESOURCE_FLAG_FLASH_FIRE; // attacker's Flash Fire is lit
    EXPECT_EQ(Max(b.Use(MOVE_EMBER)), 27);            // 17 * 1.5 = 25, +2
    b.s.resourceFlags[0] = 0;

    b.Def().ability = ABILITY_FLASH_FIRE;
    EmeraldCalcResult r = b.Use(MOVE_EMBER);
    EXPECT_EQ(r.outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(r.reason, EMERALD_REASON_FLASH_FIRE);

    b.Def().status1 = STATUS1_FREEZE; // a frozen Flash Fire doesn't absorb
    r = b.Use(MOVE_EMBER);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 19);
}

TEST(Damage, AbilityImmunities)
{
    Battle b;
    b.Def().ability = ABILITY_LEVITATE;
    EmeraldCalcResult r = b.Use(MOVE_EARTHQUAKE);
    EXPECT_EQ(r.outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(r.reason, EMERALD_REASON_LEVITATE);

    b.Def().ability = ABILITY_WATER_ABSORB;
    r = b.Use(MOVE_SURF);
    EXPECT_EQ(r.reason, EMERALD_REASON_WATER_ABSORB);

    b.Def().ability = ABILITY_SOUNDPROOF;
    r = b.Use(MOVE_HYPER_VOICE);
    EXPECT_EQ(r.outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(r.reason, EMERALD_REASON_SOUNDPROOF);
}

TEST(Damage, WonderGuardOnlyLetsSuperEffectiveHitsThrough)
{
    Battle b;
    b.Set(1, SPECIES_SHEDINJA, TYPE_BUG, TYPE_GHOST).ability = ABILITY_WONDER_GUARD;

    EmeraldCalcResult r = b.Use(MOVE_SURF);
    EXPECT_EQ(r.outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(r.reason, EMERALD_REASON_WONDER_GUARD);

    r = b.Use(MOVE_EMBER);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 38); // 17 + 2 = 19, Fire vs Bug x2
}

TEST(Damage, BadgeBoostsApplyOnlyToThePlayerSide)
{
    Battle b;
    b.SetFlag(FLAG_BADGE01_GET); // Attack
    b.SetFlag(FLAG_BADGE05_GET); // Defense

    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 18);       // player attacking: attack 110 -> 16, +2
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE, 1, 0)), 16); // foe attacking the player: defense 110 -> 14, +2

    b.s.battleTypeFlags |= BATTLE_TYPE_LINK; // never in link battles
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 17);
}

// --- Screens and doubles ----------------------------------------------------------

TEST(Damage, ReflectIsTwoThirdsInDoublesWithBothFoesUp)
{
    Battle b;
    b.s.sideStatuses[B_SIDE_OPPONENT] = SIDE_STATUS_REFLECT;
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 9); // 15 / 2 = 7, +2

    b.MakeDouble();
    EXPECT_EQ(Max(b.Use(MOVE_TACKLE)), 12); // 2 * (15 / 3) = 10, +2
}

TEST(Damage, SpreadMovesAreHalvedInDoublesButFoesAndAllyMovesAreNot)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.MakeDouble();
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    EXPECT_EQ(Max(b.Use(MOVE_ROCK_SLIDE)), 18); // MOVE_TARGET_BOTH: 33 / 2 = 16, +2
    EXPECT_EQ(Max(b.Use(MOVE_EARTHQUAKE)), 46); // MOVE_TARGET_FOES_AND_ALLY: 44 + 2

    b.s.absentBattlerFlags = 1u << 3; // the other foe is gone
    EXPECT_EQ(Max(b.Use(MOVE_ROCK_SLIDE)), 35);
}

TEST(Damage, BrickBreakShattersScreensBeforeDamage)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.s.sideStatuses[B_SIDE_OPPONENT] = SIDE_STATUS_REFLECT;
    b.s.sideTimers[B_SIDE_OPPONENT].reflectTimer = 3;

    EXPECT_EQ(Max(b.Use(MOVE_REVENGE)), 30);     // 26 / 2 = 13, +2 = 15, Fighting vs Normal x2
    EXPECT_EQ(Max(b.Use(MOVE_BRICK_BREAK)), 70); // Reflect removed first: 33 + 2 = 35, x2
}

// --- Move scripts: power / multiplier setup ----------------------------------------

TEST(MoveScripts, LowKickPowerFollowsTheTargetsPokedexWeight)
{
    Battle b;
    b.Set(0, SPECIES_MACHOP, TYPE_FIGHTING, TYPE_FIGHTING);

    // sWeightToDamageTable: first entry whose weight is *greater* than the
    // target's (in hectograms) gives the power; 10.0 kg exactly is 40.
    const struct { u16 species; u16 power; } cases[] = {
        {SPECIES_PIKACHU, 20},   //   6.0 kg
        {SPECIES_VULPIX, 20},    //   9.9 kg
        {SPECIES_MAGIKARP, 40},  //  10.0 kg
        {SPECIES_MACHOP, 40},    //  19.5 kg
        {SPECIES_FLAREON, 60},   //  25.0 kg
        {SPECIES_VENUSAUR, 100}, // 100.0 kg
        {SPECIES_SNORLAX, 120},  // 460.0 kg
    };
    for (const auto &c : cases) {
        b.Def().species = c.species;
        EXPECT_EQ(Power(b.Use(MOVE_LOW_KICK)), c.power) << "species " << c.species;
    }

    // Snorlax, Normal type: 100*120*22/100/50 = 52, +2 = 54, STAB 81, x2 = 162.
    b.Set(1, SPECIES_SNORLAX, TYPE_NORMAL, TYPE_NORMAL);
    const EmeraldCalcResult r = b.Use(MOVE_LOW_KICK);
    EXPECT_EQ(Max(r), 162);
    EXPECT_EQ(Min(r), 137); // 162 * 85 / 100 = 137.7
}

TEST(MoveScripts, FlailAndReversalScaleWithRemainingHp)
{
    Battle b;
    b.Atk().maxHP = 100;
    // GetScaledHPFraction(hp, 100, 48) against sFlailHpScaleToPowerTable
    const struct { u16 hp; u16 power; } cases[] = {
        {1, 200}, {8, 150}, {20, 100}, {30, 80}, {50, 40}, {100, 20},
    };
    for (const auto &c : cases) {
        b.Atk().hp = c.hp;
        EXPECT_EQ(Power(b.Use(MOVE_FLAIL)), c.power) << "hp " << c.hp;
        EXPECT_EQ(Power(b.Use(MOVE_REVERSAL)), c.power) << "hp " << c.hp;
    }
}

TEST(MoveScripts, EruptionAndWaterSpoutScaleWithRemainingHp)
{
    Battle b;
    b.Atk().maxHP = 300;
    b.Atk().hp = 300;
    EXPECT_EQ(Power(b.Use(MOVE_ERUPTION)), 150);
    b.Atk().hp = 150;
    EXPECT_EQ(Power(b.Use(MOVE_WATER_SPOUT)), 75);
    b.Atk().hp = 1;
    EXPECT_EQ(Power(b.Use(MOVE_ERUPTION)), 1); // 150 / 300 = 0 -> 1
}

TEST(MoveScripts, ReturnAndFrustrationFollowFriendship)
{
    Battle b;
    b.Atk().friendship = 255;
    EXPECT_EQ(Power(b.Use(MOVE_RETURN)), 102); // 10 * 255 / 25
    // 10 * 0 / 25 = 0 -> no gDynamicBasePower, so CalculateBaseDamage()
    // falls back to the move table's placeholder power of 1.
    EXPECT_EQ(Power(b.Use(MOVE_FRUSTRATION)), 1);

    b.Atk().friendship = 70;
    EXPECT_EQ(Power(b.Use(MOVE_RETURN)), 28);      // 700 / 25
    EXPECT_EQ(Power(b.Use(MOVE_FRUSTRATION)), 74); // 1850 / 25
}

TEST(MoveScripts, HiddenPowerTypeAndPowerComeFromIvs)
{
    Battle b;
    b.Set(0, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    BattlePokemon &a = b.Atk();

    // All 31: power bits 63 -> 70; type bits 63 -> 15*63/63 + 1 = 16, past
    // TYPE_MYSTERY -> 17 = Dark. Dark is special: 100*70*22/100/50 = 30, +2.
    a.hpIV = a.attackIV = a.defenseIV = a.speedIV = a.spAttackIV = a.spDefenseIV = 31;
    EmeraldCalcResult r = b.Use(MOVE_HIDDEN_POWER);
    EXPECT_EQ(r.moveType, TYPE_DARK);
    EXPECT_EQ(Power(r), 70);
    EXPECT_EQ(Max(r), 32);

    // All 30: power bits 63 -> 70; type bits 0 -> Fighting, which is
    // physical: attack 100 -> 32, x2 vs Normal = 64 (spAttack ignored).
    a.hpIV = a.attackIV = a.defenseIV = a.speedIV = a.spAttackIV = a.spDefenseIV = 30;
    a.spAttack = 1;
    r = b.Use(MOVE_HIDDEN_POWER);
    EXPECT_EQ(r.moveType, TYPE_FIGHTING);
    EXPECT_EQ(Max(r), 64);
}

TEST(MoveScripts, FacadeDoublesWhenStatusedButBurnStillHalves)
{
    Battle b;
    b.Set(0, SPECIES_ZIGZAGOON, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    EXPECT_EQ(Max(b.Use(MOVE_FACADE)), 48); // 30 + 2 = 32, STAB 48
    b.Atk().status1 = STATUS1_PARALYSIS;
    EXPECT_EQ(Max(b.Use(MOVE_FACADE)), 96); // 32 x2 = 64, STAB 96
    b.Atk().status1 = STATUS1_BURN;
    EXPECT_EQ(Max(b.Use(MOVE_FACADE)), 51); // 30 / 2 = 15, +2 = 17, x2 = 34, STAB 51
    b.Atk().ability = ABILITY_GUTS;
    EXPECT_EQ(Max(b.Use(MOVE_FACADE)), 144); // attack 150: 46 + 2 = 48, x2 = 96, STAB 144
}

TEST(MoveScripts, SemiInvulnerableTargetsOnlyReachableByTheRightMoves)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    b.s.statuses3[1] = STATUS3_ON_AIR;
    EmeraldCalcResult r = b.Use(MOVE_TACKLE);
    EXPECT_EQ(r.accuracy, 0.0);
    EXPECT_EQ(r.missReason, EMERALD_REASON_TARGET_SEMI_INVULNERABLE);
    r = b.Use(MOVE_GUST); // hits, doubled: 17 + 2 = 19, x2
    EXPECT_EQ(r.accuracy, 1.0);
    EXPECT_EQ(Max(r), 38);

    b.s.statuses3[1] = STATUS3_UNDERGROUND;
    r = b.Use(MOVE_EARTHQUAKE); // 44 + 2 = 46, x2
    EXPECT_EQ(r.accuracy, 1.0);
    EXPECT_EQ(Max(r), 92);

    b.s.statuses3[1] = STATUS3_UNDERWATER;
    r = b.Use(MOVE_SURF); // 43 x2 = 86, STAB 129
    EXPECT_EQ(r.accuracy, 1.0);
    EXPECT_EQ(Max(r), 129);
}

TEST(MoveScripts, StompDoublesAgainstMinimize)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    EXPECT_EQ(Max(b.Use(MOVE_STOMP)), 30); // 28 + 2
    b.s.statuses3[1] = STATUS3_MINIMIZED;
    EXPECT_EQ(Max(b.Use(MOVE_STOMP)), 60);
}

TEST(MoveScripts, RevengeDoublesOnlyIfHitByThatTarget)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    EXPECT_EQ(Max(b.Use(MOVE_REVENGE)), 56); // 26 + 2 = 28, x2 SE

    b.s.protectStructs[0].physicalDmg = 10;
    b.s.protectStructs[0].physicalBattlerId = 1;
    EXPECT_EQ(Max(b.Use(MOVE_REVENGE)), 112);
}

TEST(MoveScripts, RolloutAndFuryCutterEscalate)
{
    Battle b;
    DisableStruct &d = b.s.disableStructs[0];

    EXPECT_EQ(Power(b.Use(MOVE_ROLLOUT)), 30); // first hit
    b.Atk().status2 = STATUS2_MULTIPLETURNS;
    d.rolloutTimer = 4;
    EXPECT_EQ(Power(b.Use(MOVE_ROLLOUT)), 60); // second hit
    d.rolloutTimer = 1;
    EXPECT_EQ(Power(b.Use(MOVE_ROLLOUT)), 480); // fifth hit: 30 * 2^4
    b.Atk().status2 = STATUS2_DEFENSE_CURL;
    EXPECT_EQ(Power(b.Use(MOVE_ICE_BALL)), 60); // first hit after Defense Curl

    EXPECT_EQ(Power(b.Use(MOVE_FURY_CUTTER)), 10);
    d.furyCutterCounter = 2;
    EXPECT_EQ(Power(b.Use(MOVE_FURY_CUTTER)), 40);
    d.furyCutterCounter = 5; // capped at 5
    EXPECT_EQ(Power(b.Use(MOVE_FURY_CUTTER)), 160);
}

// --- Random power, multi-hit, fixed damage ---------------------------------------

TEST(MoveScripts, MultiHitCountsAndRange)
{
    Battle b;
    EmeraldCalcResult r = b.Use(MOVE_FURY_ATTACK);
    EXPECT_EQ(r.minHits, 2);
    EXPECT_EQ(r.maxHits, 5);
    EXPECT_DOUBLE_EQ(r.hitCountChance[2], 3.0 / 8);
    EXPECT_DOUBLE_EQ(r.hitCountChance[3], 3.0 / 8);
    EXPECT_DOUBLE_EQ(r.hitCountChance[4], 1.0 / 8);
    EXPECT_DOUBLE_EQ(r.hitCountChance[5], 1.0 / 8);
    // 15 power: 100*15*22/100/50 = 6, +2 = 8 per hit (min 6).
    s32 lo, hi;
    EmeraldCalc_DamageRange(&r, &lo, &hi);
    EXPECT_EQ(lo, 12);
    EXPECT_EQ(hi, 40);

    r = b.Use(MOVE_DOUBLE_KICK);
    EXPECT_EQ(r.minHits, 2);
    EXPECT_EQ(r.maxHits, 2);
}

TEST(MoveScripts, TripleKickHitsEscalateAndEachChecksAccuracy)
{
    Battle b;
    const EmeraldCalcResult r = b.Use(MOVE_TRIPLE_KICK);
    ASSERT_EQ(r.numSequentialHits, 3);
    EXPECT_EQ(r.sequentialHits[0].power, 10);
    EXPECT_EQ(r.sequentialHits[1].power, 20);
    EXPECT_EQ(r.sequentialHits[2].power, 30);
    EXPECT_TRUE(r.accuracyPerHit);
    EXPECT_DOUBLE_EQ(r.accuracy, 58986 / kU16); // 90%: 655*90 + 36
}

TEST(MoveScripts, MagnitudeBranchesMatchTheRng)
{
    Battle b;
    const EmeraldCalcResult r = b.Use(MOVE_MAGNITUDE);
    // Random() % 100 thresholds 5/15/35/65/85/95, counted over 65536 values.
    const struct { u16 power; int count; } expected[] = {
        {10, 3280}, {30, 6560}, {50, 13120}, {70, 19651}, {90, 13100}, {110, 6550}, {150, 3275},
    };
    ASSERT_EQ(r.numBranches, 7);
    double total = 0;
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(r.branches[i].hit.power, expected[i].power);
        EXPECT_DOUBLE_EQ(r.branches[i].probability, expected[i].count / kU16);
        total += r.branches[i].probability;
    }
    EXPECT_DOUBLE_EQ(total, 1.0);
}

TEST(MoveScripts, PresentBranches)
{
    Battle b;
    const EmeraldCalcResult r = b.Use(MOVE_PRESENT);
    ASSERT_EQ(r.numBranches, 4);
    EXPECT_EQ(r.branches[0].hit.power, 40);
    EXPECT_DOUBLE_EQ(r.branches[0].probability, 102 / 256.0);
    EXPECT_EQ(r.branches[1].hit.power, 80);
    EXPECT_DOUBLE_EQ(r.branches[1].probability, 76 / 256.0);
    EXPECT_EQ(r.branches[2].hit.power, 120);
    EXPECT_DOUBLE_EQ(r.branches[2].probability, 26 / 256.0);
    EXPECT_TRUE(r.branches[3].healsTarget);
    EXPECT_EQ(r.branches[3].hit.normal.max, 50); // 200 max HP / 4
    EXPECT_DOUBLE_EQ(r.branches[3].probability, 52 / 256.0);
}

TEST(MoveScripts, PsywaveIsElevenEquallyLikelyFixedDamages)
{
    Battle b;
    const EmeraldCalcResult r = b.Use(MOVE_PSYWAVE);
    ASSERT_EQ(r.numBranches, 11);
    for (int i = 0; i <= 10; ++i) {
        EXPECT_EQ(r.branches[i].hit.normal.min, 50 * (i * 10 + 50) / 100); // 25, 30, ..., 75
        EXPECT_EQ(r.branches[i].hit.normal.max, r.branches[i].hit.normal.min);
        EXPECT_DOUBLE_EQ(r.branches[i].probability, 1.0 / 11);
    }
}

TEST(MoveScripts, FixedDamageMovesIgnoreStabAndEffectivenessButNotImmunity)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    EmeraldCalcResult r = b.Use(MOVE_SEISMIC_TOSS);
    EXPECT_EQ(Max(r), 50);
    EXPECT_EQ(Min(r), 50);
    EXPECT_FALSE(r.moveResultFlags & MOVE_RESULT_SUPER_EFFECTIVE); // Fighting vs Normal, cleared
    EXPECT_EQ(r.critChance, 0.0);
    EXPECT_EQ(b.Use(MOVE_NIGHT_SHADE).outcome, EMERALD_CALC_NO_EFFECT); // Ghost vs Normal
    EXPECT_EQ(Max(b.Use(MOVE_DRAGON_RAGE)), 40);
    EXPECT_EQ(Max(b.Use(MOVE_SONIC_BOOM)), 20);

    b.Def().hp = 199;
    EXPECT_EQ(Max(b.Use(MOVE_SUPER_FANG)), 99);
    b.Def().hp = 1;
    EXPECT_EQ(Max(b.Use(MOVE_SUPER_FANG)), 1);

    b.Set(1, SPECIES_GENGAR, TYPE_GHOST, TYPE_POISON);
    EXPECT_EQ(b.Use(MOVE_SEISMIC_TOSS).outcome, EMERALD_CALC_NO_EFFECT);
    EXPECT_EQ(b.Use(MOVE_SONIC_BOOM).outcome, EMERALD_CALC_NO_EFFECT);
}

TEST(MoveScripts, EndeavorNeedsTheTargetAhead)
{
    Battle b;
    b.Atk().hp = 20;
    b.Def().hp = 150;
    EXPECT_EQ(Max(b.Use(MOVE_ENDEAVOR)), 130);

    b.Atk().hp = 150;
    const EmeraldCalcResult r = b.Use(MOVE_ENDEAVOR);
    EXPECT_EQ(r.outcome, EMERALD_CALC_FAILS);
    EXPECT_EQ(r.reason, EMERALD_REASON_TARGET_HP_NOT_HIGHER);
}

TEST(MoveScripts, OhkoMovesAccuracyLevelsAndSturdy)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    // Same level: Random() % 100 + 1 < 30 -> residues 0..28.
    EmeraldCalcResult r = b.Use(MOVE_HORN_DRILL);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_TRUE(r.isOhko);
    EXPECT_EQ(Max(r), 200);
    EXPECT_DOUBLE_EQ(r.accuracy, 19024 / kU16); // 655*29 + 29

    b.Atk().level = 60; // chance 30 + 10 = 40 -> residues 0..38
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HORN_DRILL).accuracy, 25581 / kU16); // 655*39 + 36

    b.s.statuses3[1] = STATUS3_ALWAYS_HITS; // Lock-On from battler 0
    b.s.disableStructs[1].battlerWithSureHit = 0;
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HORN_DRILL).accuracy, 1.0);

    b.Atk().level = 40;
    r = b.Use(MOVE_HORN_DRILL);
    EXPECT_EQ(r.outcome, EMERALD_CALC_FAILS);
    EXPECT_EQ(r.reason, EMERALD_REASON_TARGET_LEVEL_HIGHER);

    b.Atk().level = 50;
    b.Def().ability = ABILITY_STURDY;
    EXPECT_EQ(b.Use(MOVE_HORN_DRILL).reason, EMERALD_REASON_STURDY);

    b.Def().ability = ABILITY_LEVITATE;
    EXPECT_EQ(b.Use(MOVE_FISSURE).reason, EMERALD_REASON_LEVITATE);
}

TEST(MoveScripts, SpitUpMultipliesByStockpileWithoutARandomRoll)
{
    Battle b;
    b.Set(0, SPECIES_SWALOT, TYPE_POISON, TYPE_POISON);
    EmeraldCalcResult r = b.Use(MOVE_SPIT_UP);
    EXPECT_EQ(r.outcome, EMERALD_CALC_FAILS);
    EXPECT_EQ(r.reason, EMERALD_REASON_NO_STOCKPILE);

    b.s.disableStructs[0].stockpileCounter = 2;
    r = b.Use(MOVE_SPIT_UP);
    // 100*100*22/100/50 = 44, +2 = 46, x2 = 92; no STAB (Poison user), every roll equal.
    EXPECT_EQ(Max(r), 92);
    EXPECT_EQ(Min(r), 92);
    EXPECT_EQ(r.critChance, 0.0);
}

TEST(MoveScripts, FutureSightIsTypelessAndDelayed)
{
    Battle b;
    b.Set(1, SPECIES_UMBREON, TYPE_DARK, TYPE_DARK); // would be immune to Psychic
    const EmeraldCalcResult r = b.Use(MOVE_FUTURE_SIGHT);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_TRUE(r.landsLater);
    EXPECT_EQ(Max(r), 37); // 100*80*22/100/50 = 35, +2 -- no STAB, no type chart
    EXPECT_EQ(Min(r), 31);
    EXPECT_DOUBLE_EQ(r.accuracy, (655 * 90 + 36) / kU16);
}

TEST(MoveScripts, BeatUpHitsOncePerHealthyPartyMember)
{
    Battle b;
    b.Set(0, SPECIES_MACHOP, TYPE_FIGHTING, TYPE_FIGHTING);
    b.Set(1, SPECIES_SNORLAX, TYPE_NORMAL, TYPE_NORMAL); // base Defense 65
    PartyPokemon *party = b.s.playerParty;
    party[0] = {};
    party[0].species = SPECIES_MACHOP; // base Attack 80
    party[0].level = 50;
    party[0].hp = 100;
    party[1] = party[0];
    party[1].species = SPECIES_SNORLAX; // base Attack 110
    party[1].level = 40;
    party[2] = party[0];
    party[2].hp = 0; // fainted
    party[3] = party[0];
    party[3].status = STATUS1_BURN; // statused
    party[4] = party[0];
    party[4].isEgg = 1; // egg

    const EmeraldCalcResult r = b.Use(MOVE_BEAT_UP);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    ASSERT_EQ(r.numSequentialHits, 2);
    // 80*10*(50*2/5+2) = 17600 / 65 = 270 / 50 = 5, +2 = 7 (crit doubles to 14)
    EXPECT_EQ(r.sequentialHits[0].normal.max, 7);
    EXPECT_EQ(r.sequentialHits[0].critical.max, 14);
    // 110*10*(40*2/5+2) = 19800 / 65 = 304 / 50 = 6, +2 = 8
    EXPECT_EQ(r.sequentialHits[1].normal.max, 8);
    EXPECT_EQ(r.sequentialHits[1].critical.max, 16);
}

// --- Conditions a move's own script checks ----------------------------------------

TEST(MoveScripts, ScriptSpecificFailures)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);

    EXPECT_EQ(b.Use(MOVE_FAKE_OUT).reason, EMERALD_REASON_NOT_FIRST_TURN);
    b.s.disableStructs[0].isFirstTurn = 2;
    EXPECT_EQ(b.Use(MOVE_FAKE_OUT).outcome, EMERALD_CALC_DEALS_DAMAGE);

    EXPECT_EQ(b.Use(MOVE_SNORE).reason, EMERALD_REASON_USER_NOT_ASLEEP);
    EXPECT_EQ(b.Use(MOVE_DREAM_EATER).reason, EMERALD_REASON_TARGET_NOT_ASLEEP);
    b.Def().status1 = 3; // asleep for 3 turns
    const EmeraldCalcResult r = b.Use(MOVE_DREAM_EATER);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 46); // 44 + 2

    b.Def().ability = ABILITY_DAMP;
    EXPECT_EQ(b.Use(MOVE_EXPLOSION).reason, EMERALD_REASON_DAMP);

    b.s.protectStructs[0].specialDmg = 30;
    EXPECT_EQ(b.Use(MOVE_FOCUS_PUNCH).reason, EMERALD_REASON_LOST_FOCUS);
}

TEST(MoveScripts, ExplosionHalvesDefense)
{
    Battle b;
    b.Set(0, SPECIES_SNORLAX, TYPE_NORMAL, TYPE_NORMAL); // Normal, for STAB
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    // defense 50: 100*250*22 / 50 / 50 = 220, +2 = 222, STAB 333
    EXPECT_EQ(Max(b.Use(MOVE_EXPLOSION)), 333);
}

TEST(MoveScripts, CounterDependsOnDamageTaken)
{
    Battle b;
    b.Set(1, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    EmeraldCalcResult r = b.Use(MOVE_COUNTER);
    EXPECT_EQ(r.outcome, EMERALD_CALC_DAMAGE_UNKNOWN);
    EXPECT_EQ(r.reason, EMERALD_REASON_NOT_HIT_THIS_TURN);

    b.s.protectStructs[0].physicalDmg = 40;
    b.s.protectStructs[0].physicalBattlerId = 1;
    r = b.Use(MOVE_COUNTER);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(Max(r), 80);
}

TEST(MoveScripts, StruggleIsTypeless)
{
    Battle b;
    b.Set(0, SPECIES_RATTATA, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_GENGAR, TYPE_GHOST, TYPE_POISON);
    const EmeraldCalcResult r = b.Use(MOVE_STRUGGLE);
    ASSERT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE); // typecalc is skipped outright
    EXPECT_EQ(Max(r), 24);                            // 22 + 2, no STAB either
}

TEST(MoveScripts, NaturePowerRunsTheEnvironmentsMove)
{
    Battle b;
    b.s.battleEnvironment = BATTLE_ENVIRONMENT_BUILDING;
    const EmeraldCalcResult r = b.Use(MOVE_NATURE_POWER);
    EXPECT_EQ(r.move, MOVE_SWIFT);
    EXPECT_EQ(r.outcome, EMERALD_CALC_DEALS_DAMAGE);
    EXPECT_EQ(r.accuracy, 1.0);
}

TEST(MoveScripts, ProtectMeansAGuaranteedMiss)
{
    Battle b;
    b.s.protectStructs[1].protected_ = 1;
    const EmeraldCalcResult r = b.Use(MOVE_TACKLE);
    EXPECT_EQ(r.accuracy, 0.0);
    EXPECT_EQ(r.missReason, EMERALD_REASON_TARGET_PROTECTED);
}

TEST(MoveScripts, EndureFocusBandAndSubstituteAreReported)
{
    HeldItems items({{ITEM_FOCUS_BAND, HOLD_EFFECT_FOCUS_BAND, 10}});
    Battle b;
    b.Def().item = ITEM_FOCUS_BAND;
    EmeraldCalcResult r = b.Use(MOVE_TACKLE);
    EXPECT_EQ(r.focusBandChance, 10);
    EXPECT_FALSE(r.cannotKo);

    b.s.protectStructs[1].endured = 1;
    EXPECT_TRUE(b.Use(MOVE_TACKLE).cannotKo);
    b.s.protectStructs[1].endured = 0;
    EXPECT_TRUE(b.Use(MOVE_FALSE_SWIPE).cannotKo);

    b.Def().status2 = STATUS2_SUBSTITUTE; // damage goes to the Substitute; no caps apply
    r = b.Use(MOVE_FALSE_SWIPE);
    EXPECT_TRUE(r.targetHasSubstitute);
    EXPECT_FALSE(r.cannotKo);
}

// --- accuracycheck / critcalc -----------------------------------------------------

TEST(Accuracy, StagesAbilitiesItemsAndWeather)
{
    HeldItems items({{ITEM_BRIGHT_POWDER, HOLD_EFFECT_EVASION_UP, 10}});
    Battle b;

    EXPECT_DOUBLE_EQ(b.Use(MOVE_SURF).accuracy, 1.0);
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 80 + 36) / kU16);

    b.Def().statStages[STAT_EVASION] = DEFAULT_STAT_STAGE + 1; // 80 * 75/100 = 60
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 60 + 36) / kU16);
    b.Atk().ability = ABILITY_COMPOUND_EYES; // 60 * 130/100 = 78
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 78 + 36) / kU16);
    b.Atk().ability = 0;
    b.Def().status2 = STATUS2_FORESIGHT; // evasion ignored -> 80
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 80 + 36) / kU16);
    b.Def().status2 = 0;
    b.Def().statStages[STAT_EVASION] = MAX_STAT_STAGE;
    EXPECT_DOUBLE_EQ(b.Use(MOVE_SWIFT).accuracy, 1.0); // EFFECT_ALWAYS_HIT
    b.Def().statStages[STAT_EVASION] = DEFAULT_STAT_STAGE;

    b.Def().item = ITEM_BRIGHT_POWDER; // 80 * 90/100 = 72
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 72 + 36) / kU16);
    b.Def().item = ITEM_NONE;

    b.Atk().ability = ABILITY_HUSTLE; // physical only: Tackle 95 * 80/100 = 76
    EXPECT_DOUBLE_EQ(b.Use(MOVE_TACKLE).accuracy, (655 * 76 + 36) / kU16);
    EXPECT_DOUBLE_EQ(b.Use(MOVE_HYDRO_PUMP).accuracy, (655 * 80 + 36) / kU16);
    b.Atk().ability = 0;

    b.s.battleWeather = B_WEATHER_RAIN_TEMPORARY;
    EXPECT_DOUBLE_EQ(b.Use(MOVE_THUNDER).accuracy, 1.0);
    b.s.battleWeather = B_WEATHER_SUN_TEMPORARY;
    EXPECT_DOUBLE_EQ(b.Use(MOVE_THUNDER).accuracy, (655 * 50 + 36) / kU16);
}

TEST(Crit, ChanceStagesAndBlockers)
{
    HeldItems items({{ITEM_SCOPE_LENS, HOLD_EFFECT_SCOPE_LENS, 0}});
    Battle b;

    EXPECT_DOUBLE_EQ(b.Use(MOVE_TACKLE).critChance, 4096 / kU16); // 1/16
    EXPECT_DOUBLE_EQ(b.Use(MOVE_SLASH).critChance, 8192 / kU16);  // high-crit move: 1/8
    b.Atk().status2 = STATUS2_FOCUS_ENERGY;                        // +2 -> stage 3: !(Random() % 3)
    EXPECT_DOUBLE_EQ(b.Use(MOVE_SLASH).critChance, 21846 / kU16);
    b.Atk().item = ITEM_SCOPE_LENS; // stage 4: 1/2
    EXPECT_DOUBLE_EQ(b.Use(MOVE_SLASH).critChance, 0.5);

    b.Def().ability = ABILITY_SHELL_ARMOR;
    EXPECT_DOUBLE_EQ(b.Use(MOVE_SLASH).critChance, 0.0);
}

// --- KO chance --------------------------------------------------------------------

TEST(KOChance, AccuracyAndCritsFoldIn)
{
    EmeraldCalcResult r = FixedHitResult(10);
    r.branches[0].hit.critical = Fixed(20);
    r.critChance = 0.25;
    r.accuracy = 0.8;
    const EmeraldCalcKO ko = EmeraldCalc_KOChance(&r, 15);
    EXPECT_DOUBLE_EQ(ko.chanceIfHits, 0.25); // only a crit KOs
    EXPECT_DOUBLE_EQ(ko.chance, 0.2);
}

TEST(KOChance, GuaranteedAndPossibleHitsToKo)
{
    Battle b;
    const EmeraldCalcResult tackle = b.Use(MOVE_TACKLE); // 14-17
    const EmeraldCalcKO ko = EmeraldCalc_KOChance(&tackle, 50);
    EXPECT_EQ(ko.guaranteedHitsToKo, 4); // ceil(50 / 14)
    EXPECT_EQ(ko.possibleHitsToKo, 3);   // ceil(50 / 17)
}

TEST(KOChance, FocusBandAndEndure)
{
    const double band = 6560 / kU16; // Random() % 100 < 10: 655*10 + 10

    EmeraldCalcResult r = FixedHitResult(100);
    r.focusBandChance = 10;
    EXPECT_DOUBLE_EQ(EmeraldCalc_KOChance(&r, 50).chance, 1 - band);

    // Two 12-damage hits into 20 HP: the first can't KO, but its Focus Band
    // roll still counts -- gSpecialStatuses[].focusBanded stays set for the
    // rest of the move -- so the second only KOs if neither roll procs.
    r = FixedHitResult(12);
    r.focusBandChance = 10;
    r.minHits = r.maxHits = 2;
    r.hitCountChance[1] = 0;
    r.hitCountChance[2] = 1;
    EXPECT_NEAR(EmeraldCalc_KOChance(&r, 20).chance, (1 - band) * (1 - band), 1e-15);

    r = FixedHitResult(100);
    r.cannotKo = TRUE; // Endure / False Swipe
    EXPECT_EQ(EmeraldCalc_KOChance(&r, 50).chance, 0.0);
}

TEST(KOChance, MultiHitCountsAndPerHitAccuracy)
{
    // 5 per hit into 12 HP needs 3+ hits: 3/8 + 1/8 + 1/8.
    EmeraldCalcResult r = FixedHitResult(5);
    r.minHits = 2;
    r.maxHits = 5;
    r.hitCountChance[1] = 0;
    r.hitCountChance[2] = r.hitCountChance[3] = 3.0 / 8;
    r.hitCountChance[4] = r.hitCountChance[5] = 1.0 / 8;
    EXPECT_DOUBLE_EQ(EmeraldCalc_KOChance(&r, 12).chance, 5.0 / 8);

    // Triple-Kick-style: three 10s into 25 HP, every hit re-rolls 90% accuracy.
    r = FixedHitResult(0);
    r.numSequentialHits = 3;
    for (int i = 0; i < 3; ++i)
        r.sequentialHits[i].normal = r.sequentialHits[i].critical = Fixed(10);
    r.accuracyPerHit = TRUE;
    r.accuracy = 0.9;
    const EmeraldCalcKO ko = EmeraldCalc_KOChance(&r, 25);
    EXPECT_NEAR(ko.chanceIfHits, 0.81, 1e-12);
    EXPECT_NEAR(ko.chance, 0.729, 1e-12);
}

// --- Every move -------------------------------------------------------------------

TEST(AllMoves, EveryMoveProducesAWellFormedResult)
{
    Battle b;
    b.Set(0, SPECIES_KECLEON, TYPE_NORMAL, TYPE_NORMAL);
    b.Set(1, SPECIES_MACHOP, TYPE_FIGHTING, TYPE_FIGHTING);
    b.Atk().friendship = 128;
    b.s.disableStructs[0].stockpileCounter = 1;
    b.s.disableStructs[0].isFirstTurn = 2;
    b.s.playerParty[0].species = SPECIES_KECLEON;
    b.s.playerParty[0].level = 50;
    b.s.playerParty[0].hp = 100;

    for (u16 move = 1; move < MOVES_COUNT; ++move) {
        const EmeraldCalcResult r = b.Use(move);
        SCOPED_TRACE(testing::Message() << "move " << move);
        ASSERT_LE(r.outcome, EMERALD_CALC_DAMAGE_UNKNOWN);
        if (gBattleMoves[move].power > 0 && move != MOVE_NATURE_POWER)
            EXPECT_NE(r.outcome, EMERALD_CALC_STATUS_MOVE);
        if (r.outcome != EMERALD_CALC_DEALS_DAMAGE)
            continue;

        EXPECT_GE(r.accuracy, 0.0);
        EXPECT_LE(r.accuracy, 1.0);
        EXPECT_GE(r.critChance, 0.0);
        EXPECT_LE(r.critChance, 0.5);
        if (r.numSequentialHits == 0) {
            double branches = 0, hits = 0;
            for (int i = 0; i < r.numBranches; ++i) {
                branches += r.branches[i].probability;
                EXPECT_LE(r.branches[i].hit.normal.min, r.branches[i].hit.normal.max);
                EXPECT_LE(r.branches[i].hit.normal.max, r.branches[i].hit.critical.max);
            }
            for (int n = r.minHits; n <= r.maxHits; ++n)
                hits += r.hitCountChance[n];
            EXPECT_NEAR(branches, 1.0, 1e-12);
            EXPECT_NEAR(hits, 1.0, 1e-12);
        }
        const EmeraldCalcKO ko = EmeraldCalc_KOChance(&r, 60);
        EXPECT_GE(ko.chance, 0.0);
        EXPECT_LE(ko.chance, 1.0 + 1e-12);
    }
}
