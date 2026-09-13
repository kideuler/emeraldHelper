#include "emerald/battlerview.h"

extern "C" {
#include "emerald_calc.h" // MOVE_*, BATTLE_TYPE_DOUBLE
}

#include <gtest/gtest.h>

using namespace emerald;

namespace {
QString configPath(const char *filename)
{
    return QStringLiteral(EMERALD_CONFIG_DIR "/") + filename;
}

NameTable loadNames()
{
    QString error;
    NameTable names = NameTable::loadFromFile(configPath("display_names_us_rev0.json"), &error);
    EXPECT_TRUE(names.isValid()) << error.toStdString();
    return names;
}

void SetMon(BattleSnapshot &snap, int battler, quint16 species, quint16 attack, quint16 defense, quint16 spAttack,
            quint16 spDefense, quint16 hp, quint16 maxHp, quint8 type1, quint8 type2, quint8 level,
            quint16 move0 = MOVE_NONE)
{
    BattlePokemon &mon = snap.state.battleMons[battler];
    mon.species = species;
    mon.attack = attack;
    mon.defense = defense;
    mon.spAttack = spAttack;
    mon.spDefense = spDefense;
    mon.hp = hp;
    mon.maxHP = maxHp;
    mon.types[0] = type1;
    mon.types[1] = type2;
    mon.level = level;
    mon.moves[0] = move0;
}

BattleSnapshot SinglesSnapshot()
{
    BattleSnapshot snap;
    snap.valid = true;
    snap.state.battleTypeFlags = BATTLE_TYPE_TRAINER; // in battle, BATTLE_TYPE_DOUBLE clear
    return snap;
}
} // namespace

TEST(BattlerView, SinglesHasTwoColumnsInBattlerOrder)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_SQUIRTLE, 60, 65, 50, 64, 44, 44, TYPE_WATER, TYPE_WATER, 20, MOVE_TACKLE);
    SetMon(snap, 1, SPECIES_CHARMANDER, 52, 43, 60, 50, 39, 39, TYPE_FIRE, TYPE_FIRE, 20);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_FALSE(grid.isDoubleBattle);
    ASSERT_EQ(grid.columns.size(), 2);
    EXPECT_EQ(grid.columns[0].battlerId, 0);
    EXPECT_EQ(grid.columns[1].battlerId, 1);
    EXPECT_EQ(grid.columns[0].name, QStringLiteral("SQUIRTLE"));
    EXPECT_EQ(grid.columns[1].name, QStringLiteral("CHARMANDER"));
    EXPECT_EQ(grid.columns[0].hp, 44);
    EXPECT_EQ(grid.columns[0].maxHp, 44);
}

TEST(BattlerView, DoublesHasFourColumnsPairedLeftRight)
{
    const NameTable names = loadNames();
    BattleSnapshot snap;
    snap.valid = true;
    snap.state.battleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE;
    snap.state.battlersCount = 4;
    for (int i = 0; i < kMaxBattlers; ++i)
        SetMon(snap, i, SPECIES_RATTATA + i, 50, 50, 50, 50, 30, 30, TYPE_NORMAL, TYPE_NORMAL, 10);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_TRUE(grid.isDoubleBattle);
    ASSERT_EQ(grid.columns.size(), 4);
    // Ally-left, ally-right, foe-left, foe-right.
    EXPECT_EQ(grid.columns[0].battlerId, 0);
    EXPECT_EQ(grid.columns[1].battlerId, 2);
    EXPECT_EQ(grid.columns[2].battlerId, 1);
    EXPECT_EQ(grid.columns[3].battlerId, 3);
}

TEST(BattlerView, AbsentBattlerColumnIsMarkedNotPresent)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_SQUIRTLE, 60, 65, 50, 64, 0, 44, TYPE_WATER, TYPE_WATER, 20);
    SetMon(snap, 1, SPECIES_CHARMANDER, 52, 43, 60, 50, 39, 39, TYPE_FIRE, TYPE_FIRE, 20);
    snap.state.absentBattlerFlags = 1u << 0; // battler 0 fainted/not sent out

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_FALSE(grid.columns[0].present);
    EXPECT_TRUE(grid.columns[1].present);
}

TEST(BattlerView, ComputesDamageRangeAndPercentForAKnownMove)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    // Same matchup as helperapp/tests/test_damage.cpp's Tackle case:
    // 17 max / 14 min damage at level 50, attack 100 vs defense 100.
    SetMon(snap, 0, SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50, MOVE_TACKLE);
    SetMon(snap, 1, SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);
    const MoveDisplay &move = grid.columns[0].moves[0];

    ASSERT_TRUE(move.present);
    EXPECT_EQ(move.name, QStringLiteral("TACKLE"));
    ASSERT_TRUE(move.hasDamageRange);
    EXPECT_FALSE(move.isImmune);
    EXPECT_EQ(move.minDamage, 14);
    EXPECT_EQ(move.maxDamage, 17);
    EXPECT_DOUBLE_EQ(move.minPercent, 100.0 * 14 / 200);
    EXPECT_DOUBLE_EQ(move.maxPercent, 100.0 * 17 / 200);
    EXPECT_TRUE(move.details.isEmpty()); // plain move: nothing to qualify
    EXPECT_EQ(move.koChance, 0.0);       // 17 max vs 200 HP
}

TEST(BattlerView, EmptyMoveSlotIsNotPresent)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50); // move0 defaults MOVE_NONE
    SetMon(snap, 1, SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_FALSE(grid.columns[0].moves[0].present);
}

TEST(BattlerView, LowKickShowsTheWeightBasedPower)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_MACHOP, 100, 100, 100, 100, 100, 100, TYPE_FIGHTING, TYPE_FIGHTING, 50, MOVE_LOW_KICK);
    SetMon(snap, 1, SPECIES_SNORLAX, 100, 100, 100, 100, 300, 300, TYPE_NORMAL, TYPE_NORMAL, 50); // 460 kg -> 120

    const BattlerGrid grid = BuildBattlerGrid(snap, names);
    const MoveDisplay &move = grid.columns[0].moves[0];

    ASSERT_TRUE(move.hasDamageRange);
    EXPECT_TRUE(move.details.contains(QStringLiteral("120 BP"))) << move.details.join(QStringLiteral("; ")).toStdString();
}

TEST(BattlerView, ImmunityIsExplained)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_PIKACHU, 100, 100, 100, 100, 100, 100, TYPE_ELECTRIC, TYPE_ELECTRIC, 50, MOVE_THUNDERBOLT);
    SetMon(snap, 1, SPECIES_LANTURN, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_ELECTRIC, 50);
    snap.state.battleMons[1].ability = ABILITY_VOLT_ABSORB;

    const BattlerGrid grid = BuildBattlerGrid(snap, names);
    const MoveDisplay &move = grid.columns[0].moves[0];

    EXPECT_FALSE(move.hasDamageRange);
    EXPECT_TRUE(move.isImmune);
    EXPECT_TRUE(move.note.contains(QStringLiteral("VOLT ABSORB"))) << move.note.toStdString();
}

TEST(BattlerView, KoChanceAgainstCurrentHp)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    SetMon(snap, 0, SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50, MOVE_TACKLE);
    // 15 HP left: Tackle's rolls are 17*(100..85)/100 =
    // 17,16,16,16,16,16,15,15,15,15,15,15,14,14,14,14 -> 12 of 16 KO without
    // a crit, and every crit (34 base) KOs.
    SetMon(snap, 1, SPECIES_SQUIRTLE, 100, 100, 100, 100, 15, 200, TYPE_WATER, TYPE_WATER, 50);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);
    const MoveDisplay &move = grid.columns[0].moves[0];

    const double crit = 4096.0 / 65536;      // !(Random() % 16)
    const double accuracy = 62261.0 / 65536; // Tackle is 95% accurate: Random() % 100 + 1 <= 95, modulo bias included
    EXPECT_NEAR(move.koChance, accuracy * ((1 - crit) * 12.0 / 16 + crit), 1e-12);
}

TEST(BattlerView, FieldTextListsWeatherAndScreens)
{
    const NameTable names = loadNames();
    BattleSnapshot snap = SinglesSnapshot();
    snap.state.battleWeather = B_WEATHER_SANDSTORM_PERMANENT;
    snap.state.sideStatuses[B_SIDE_OPPONENT] = SIDE_STATUS_REFLECT;

    EXPECT_EQ(BuildBattlerGrid(snap, names).fieldText, QStringLiteral("Sandstorm | Foe: Reflect"));
}
