// Differential test: the port in calc/src/ against the decomp's *original*
// code (decomp_reference.c -- CalculateBaseDamage(), the damagecalc /
// typecalc / critcalc / accuracycheck / power-setting script commands,
// extracted verbatim from src/ at build time), over many random battle
// states. Where test_damage.cpp checks hand-computed cases, this checks
// that the port and the original agree bit for bit on everything in
// between -- every stat stage, ability, item, weather, screen and status
// combination the generator below can reach.

extern "C" {
#include "battle_internal.h"
#include "decomp_reference.h"
}

#include <gtest/gtest.h>
#include <random>
#include <vector>

namespace {

// Abilities/hold effects the damage code branches on, plus noise.
const u8 kAbilities[] = {
    ABILITY_NONE, ABILITY_HUGE_POWER, ABILITY_PURE_POWER, ABILITY_THICK_FAT, ABILITY_HUSTLE, ABILITY_PLUS,
    ABILITY_MINUS, ABILITY_GUTS, ABILITY_MARVEL_SCALE, ABILITY_OVERGROW, ABILITY_BLAZE, ABILITY_TORRENT,
    ABILITY_SWARM, ABILITY_LEVITATE, ABILITY_WONDER_GUARD, ABILITY_CLOUD_NINE, ABILITY_AIR_LOCK,
    ABILITY_BATTLE_ARMOR, ABILITY_SHELL_ARMOR, ABILITY_COMPOUND_EYES, ABILITY_SAND_VEIL, ABILITY_STURDY,
    ABILITY_INTIMIDATE, ABILITY_SPEED_BOOST,
};
const u8 kHoldEffects[] = {
    HOLD_EFFECT_NONE, HOLD_EFFECT_CHOICE_BAND, HOLD_EFFECT_SOUL_DEW, HOLD_EFFECT_DEEP_SEA_TOOTH,
    HOLD_EFFECT_DEEP_SEA_SCALE, HOLD_EFFECT_LIGHT_BALL, HOLD_EFFECT_METAL_POWDER, HOLD_EFFECT_THICK_CLUB,
    HOLD_EFFECT_SCOPE_LENS, HOLD_EFFECT_LUCKY_PUNCH, HOLD_EFFECT_STICK, HOLD_EFFECT_EVASION_UP,
    HOLD_EFFECT_FIRE_POWER, HOLD_EFFECT_WATER_POWER, HOLD_EFFECT_NORMAL_POWER, HOLD_EFFECT_DARK_POWER,
    HOLD_EFFECT_FOCUS_BAND,
};
// Species the item/ability checks name, plus a spread of others.
const u16 kSpecies[] = {
    SPECIES_LATIAS, SPECIES_LATIOS, SPECIES_CLAMPERL, SPECIES_PIKACHU, SPECIES_DITTO, SPECIES_CUBONE,
    SPECIES_MAROWAK, SPECIES_CHANSEY, SPECIES_FARFETCHD, SPECIES_SNORLAX, SPECIES_MAGIKARP, SPECIES_GROUDON,
    SPECIES_SHEDINJA, SPECIES_BULBASAUR, SPECIES_SQUIRTLE, SPECIES_VULPIX, SPECIES_FLAREON,
};

// Item id i+1 carries kHoldEffects[i] (param 5..50); the same table serves
// both sides, since both call the calc's GetItemHoldEffect().
class ItemTable
{
public:
    ItemTable() : m_he(ITEMS_COUNT), m_param(ITEMS_COUNT)
    {
        for (size_t i = 0; i < sizeof(kHoldEffects); ++i) {
            m_he[i + 1] = kHoldEffects[i];
            m_param[i + 1] = u8(5 + 3 * i);
        }
        EmeraldCalc_SetItemHoldEffects(m_he.data(), m_param.data(), ITEMS_COUNT);
    }
    ~ItemTable() { EmeraldCalc_SetItemHoldEffects(nullptr, nullptr, 0); }

private:
    std::vector<u8> m_he, m_param;
};

struct Scenario {
    BattleState s;
    u8 atk, def;
    u16 move;
};

class Gen
{
public:
    explicit Gen(u32 seed) : m_rng(seed) {}

    int Int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(m_rng); }
    bool Chance(int percent) { return Int(0, 99) < percent; }
    template <typename T, size_t N> T Pick(const T (&arr)[N]) { return arr[Int(0, int(N) - 1)]; }

    Scenario Next()
    {
        Scenario sc;
        BattleState &s = sc.s;
        BattleState_Init(&s);
        s.battleTypeFlags = BATTLE_TYPE_TRAINER;
        if (Chance(30)) {
            s.battleTypeFlags |= BATTLE_TYPE_DOUBLE;
            s.battlersCount = 4;
        }
        if (Chance(5))
            s.battleTypeFlags |= Chance(50) ? BATTLE_TYPE_LINK : BATTLE_TYPE_FRONTIER;
        if (Chance(5))
            s.battleTypeFlags |= BATTLE_TYPE_FIRST_BATTLE;
        if (Chance(5))
            s.trainerBattleOpponentA = TRAINER_SECRET_BASE;

        for (int i = 0; i < MAX_BATTLERS_COUNT; ++i)
            RandomMon(s.battleMons[i], s.enigmaBerries[i]);

        const u16 weathers[] = {0, B_WEATHER_RAIN_TEMPORARY, B_WEATHER_RAIN_PERMANENT, B_WEATHER_RAIN_PERMANENT | B_WEATHER_RAIN_TEMPORARY,
                                B_WEATHER_SANDSTORM_TEMPORARY, B_WEATHER_SUN_TEMPORARY, B_WEATHER_SUN_PERMANENT, B_WEATHER_HAIL_TEMPORARY};
        s.battleWeather = Pick(weathers);
        for (int side = 0; side < NUM_BATTLE_SIDES; ++side) {
            s.sideStatuses[side] = (Chance(25) ? SIDE_STATUS_REFLECT : 0) | (Chance(25) ? SIDE_STATUS_LIGHTSCREEN : 0);
            s.sideTimers[side].reflectTimer = u8(Int(0, 5));
            s.sideTimers[side].lightscreenTimer = u8(Int(0, 5));
        }
        const u32 statuses3[] = {STATUS3_MUDSPORT, STATUS3_WATERSPORT, STATUS3_CHARGED_UP, STATUS3_ON_AIR,
                                 STATUS3_UNDERGROUND, STATUS3_UNDERWATER, STATUS3_MINIMIZED, STATUS3_ALWAYS_HITS,
                                 STATUS3_CANT_SCORE_A_CRIT};
        for (int i = 0; i < MAX_BATTLERS_COUNT; ++i) {
            for (u32 bit : statuses3)
                if (Chance(8))
                    s.statuses3[i] |= bit;
            s.protectStructs[i].helpingHand = Chance(15);
            s.protectStructs[i].protected_ = Chance(10);
            s.disableStructs[i].battlerWithSureHit = u8(Int(0, 3));
            s.resourceFlags[i] = Chance(20) ? RESOURCE_FLAG_FLASH_FIRE : 0;
            s.battlerPositions[i] = u8(i);
        }
        if (Chance(10)) // link battles can remap positions
            std::swap(s.battlerPositions[0], s.battlerPositions[2]);
        for (int badge = FLAG_BADGE01_GET; badge <= FLAG_BADGE08_GET; ++badge)
            if (Chance(50))
                s.saveFlags[badge / 8] |= u8(1 << (badge % 8));

        sc.atk = u8(Int(0, s.battlersCount - 1));
        do
            sc.def = u8(Int(0, s.battlersCount - 1));
        while (sc.def == sc.atk);
        for (int i = 0; i < MAX_BATTLERS_COUNT; ++i)
            if (i != sc.atk && i != sc.def && Chance(20))
                s.absentBattlerFlags |= u8(1 << i);
        sc.move = u16(Int(1, MOVES_COUNT - 1));
        return sc;
    }

private:
    void RandomMon(BattlePokemon &m, BattleEnigmaBerry &enigma)
    {
        m.species = Chance(60) ? Pick(kSpecies) : u16(Int(1, NUM_SPECIES - 1));
        // Stats >= 20 keep every divisor nonzero (a 1-Defense mon at -6
        // using Explosion divides by zero on the real hardware too).
        m.attack = u16(Int(20, 700));
        m.defense = u16(Int(20, 700));
        m.spAttack = u16(Int(20, 700));
        m.spDefense = u16(Int(20, 700));
        m.speed = u16(Int(20, 700));
        m.level = u8(Int(1, 100));
        m.maxHP = u16(Int(1, 714));
        m.hp = u16(Chance(10) ? 0 : Int(1, m.maxHP));
        m.types[0] = u8(Int(0, NUMBER_OF_MON_TYPES - 1));
        m.types[1] = Chance(40) ? m.types[0] : u8(Int(0, NUMBER_OF_MON_TYPES - 1));
        m.ability = Chance(80) ? Pick(kAbilities) : u8(Int(0, ABILITIES_COUNT - 1));
        m.item = Chance(70) ? u16(Int(0, int(sizeof(kHoldEffects)))) : ITEM_ENIGMA_BERRY;
        enigma.holdEffect = Pick(kHoldEffects);
        enigma.holdEffectParam = u8(Int(0, 60));
        for (s8 &stage : m.statStages)
            stage = s8(Chance(50) ? DEFAULT_STAT_STAGE : Int(MIN_STAT_STAGE, MAX_STAT_STAGE));
        const u32 status1[] = {0, 0, 0, STATUS1_BURN, STATUS1_POISON, STATUS1_TOXIC_POISON, STATUS1_PARALYSIS, STATUS1_FREEZE, 3};
        m.status1 = Pick(status1);
        const u32 status2[] = {STATUS2_FOCUS_ENERGY, STATUS2_FORESIGHT, STATUS2_SUBSTITUTE, STATUS2_MULTIPLETURNS, STATUS2_DEFENSE_CURL};
        for (u32 bit : status2)
            if (Chance(15))
                m.status2 |= bit;
        m.hpIV = u32(Int(0, 31));
        m.attackIV = u32(Int(0, 31));
        m.defenseIV = u32(Int(0, 31));
        m.speedIV = u32(Int(0, 31));
        m.spAttackIV = u32(Int(0, 31));
        m.spDefenseIV = u32(Int(0, 31));
        m.friendship = u8(Int(0, 255));
    }

    std::mt19937 m_rng;
};

std::string Describe(const Scenario &sc)
{
    const BattlePokemon &a = sc.s.battleMons[sc.atk], &d = sc.s.battleMons[sc.def];
    return testing::PrintToString(testing::Message()
                                  << "move " << sc.move << ", attacker " << int(sc.atk) << " (species " << a.species
                                  << ", ability " << int(a.ability) << ", item " << a.item << "), target "
                                  << int(sc.def) << " (species " << d.species << ", ability " << int(d.ability)
                                  << "), weather " << sc.s.battleWeather << ", flags " << sc.s.battleTypeFlags);
}

} // namespace

TEST(DecompDiff, DamageCalcAndTypeCalcMatchTheOriginal)
{
    ItemTable items;
    Gen gen(0xE3E7A1D);
    int compared = 0;

    for (int iter = 0; iter < 200000; ++iter) {
        const Scenario sc = gen.Next();
        const u16 dynamicPower = gen.Chance(30) ? u16(gen.Int(1, 255)) : 0;
        const u8 dynamicType = gen.Chance(20) ? u8(gen.Int(0, NUMBER_OF_MON_TYPES - 1) | (1 << 7)) : 0;
        const u8 dmgMultiplier = gen.Chance(20) ? 2 : 1;

        for (u8 crit = 1; crit <= 2; ++crit) {
            MoveContext ctx;
            MoveContext_Init(&ctx, &sc.s, sc.atk, sc.def, sc.move);
            ctx.dynamicBasePower = dynamicPower;
            ctx.dynamicMoveType = dynamicType;
            ctx.dmgMultiplier = dmgMultiplier;
            ctx.critMultiplier = crit;
            Cmd_damagecalc(&ctx);
            Cmd_typecalc(&ctx);

            s32 refDamage;
            u8 refFlags, refAbility;
            Ref_Load(&sc.s, sc.atk, sc.def, sc.move);
            Ref_SetDynamic(dynamicPower, dynamicType, dmgMultiplier, 0);
            Ref_DamageCalcTypeCalc(crit, &refDamage, &refFlags, &refAbility);

            ASSERT_EQ(ctx.battleMoveDamage, refDamage) << Describe(sc) << ", crit " << int(crit);
            ASSERT_EQ(ctx.moveResultFlags, refFlags) << Describe(sc);
            ASSERT_EQ(ctx.lastUsedAbility, refAbility) << Describe(sc);
            ++compared;
        }
    }
    EXPECT_EQ(compared, 400000);
}

TEST(DecompDiff, CritChanceMatchesTheOriginalOverEveryRandomValue)
{
    ItemTable items;
    Gen gen(0xC417);

    for (int iter = 0; iter < 1500; ++iter) {
        const Scenario sc = gen.Next();
        MoveContext ctx;
        MoveContext_Init(&ctx, &sc.s, sc.atk, sc.def, sc.move);
        Ref_Load(&sc.s, sc.atk, sc.def, sc.move);
        ASSERT_EQ(CritChance(&ctx), Ref_CritChance()) << Describe(sc);
    }
}

TEST(DecompDiff, AccuracyMatchesTheOriginalOverEveryRandomValue)
{
    ItemTable items;
    Gen gen(0xACC);
    const u32 hitMarkers[] = {0, HITMARKER_IGNORE_ON_AIR, HITMARKER_IGNORE_UNDERGROUND, HITMARKER_IGNORE_UNDERWATER};

    for (int iter = 0; iter < 1500; ++iter) {
        const Scenario sc = gen.Next();
        const u16 accMove = gen.Chance(15) ? u16(NO_ACC_CALC_CHECK_LOCK_ON) : u16(ACC_CURR_MOVE);
        const u8 dynamicType = gen.Chance(20) ? u8(gen.Int(0, NUMBER_OF_MON_TYPES - 1) | (1 << 7)) : 0;
        const u32 hitMarker = gen.Pick(hitMarkers);

        MoveContext ctx;
        MoveContext_Init(&ctx, &sc.s, sc.atk, sc.def, sc.move);
        ctx.dynamicMoveType = dynamicType;
        ctx.hitMarker = hitMarker;
        u8 missReason;
        const double port = AccuracyCheck(&ctx, accMove, &missReason);

        Ref_Load(&sc.s, sc.atk, sc.def, sc.move);
        Ref_SetDynamic(0, dynamicType, 1, hitMarker);
        ASSERT_EQ(port, Ref_Accuracy(accMove)) << Describe(sc) << ", accMove " << accMove << ", hitMarker " << hitMarker;
    }
}

TEST(DecompDiff, PowerAndTypeCommandsMatchTheOriginal)
{
    Gen gen(0x90E5);
    void (*const port[])(MoveContext *) = {
        Cmd_weightdamagecalculation, Cmd_remaininghptopower, Cmd_scaledamagebyhealthratio,
        Cmd_friendshiptodamagecalculation, Cmd_hiddenpowercalc, Cmd_setweatherballtype,
    };
    const u16 moves[] = {MOVE_LOW_KICK, MOVE_FLAIL, MOVE_ERUPTION, MOVE_RETURN, MOVE_HIDDEN_POWER, MOVE_WEATHER_BALL};
    const u16 friendshipMoves[] = {MOVE_RETURN, MOVE_FRUSTRATION};

    for (int iter = 0; iter < 20000; ++iter) {
        Scenario sc = gen.Next();
        for (int cmd = REF_WEIGHT_DAMAGE_CALCULATION; cmd <= REF_SET_WEATHER_BALL_TYPE; ++cmd) {
            sc.move = cmd == REF_FRIENDSHIP_TO_DAMAGE_CALCULATION ? gen.Pick(friendshipMoves) : moves[cmd];
            if (cmd == REF_REMAINING_HP_TO_POWER || cmd == REF_SCALE_DAMAGE_BY_HEALTH_RATIO)
                sc.s.battleMons[sc.atk].hp = u16(gen.Int(1, sc.s.battleMons[sc.atk].maxHP)); // the user is up

            MoveContext ctx;
            MoveContext_Init(&ctx, &sc.s, sc.atk, sc.def, sc.move);
            port[cmd](&ctx);

            u8 refType, refMultiplier;
            Ref_Load(&sc.s, sc.atk, sc.def, sc.move);
            const u16 refPower = Ref_PowerCommand(cmd, &refType, &refMultiplier);

            ASSERT_EQ(ctx.dynamicBasePower, refPower) << "command " << cmd << ": " << Describe(sc);
            ASSERT_EQ(ctx.dynamicMoveType, refType) << "command " << cmd << ": " << Describe(sc);
            ASSERT_EQ(ctx.dmgMultiplier, refMultiplier) << "command " << cmd << ": " << Describe(sc);
        }
    }
}
