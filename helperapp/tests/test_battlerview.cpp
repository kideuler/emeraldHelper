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

BattleMon MakeMon(quint16 species, quint16 attack, quint16 defense, quint16 spAttack, quint16 spDefense,
                   quint16 hp, quint16 maxHp, quint8 type1, quint8 type2, quint8 level, quint16 move0 = 0)
{
    BattleMon mon;
    mon.species = species;
    mon.attack = attack;
    mon.defense = defense;
    mon.spAttack = spAttack;
    mon.spDefense = spDefense;
    mon.hp = hp;
    mon.maxHp = maxHp;
    mon.type1 = type1;
    mon.type2 = type2;
    mon.level = level;
    mon.moves[0] = move0;
    for (int i = 0; i < kNumBattleStats; ++i)
        mon.statStages[i] = DEFAULT_STAT_STAGE;
    return mon;
}
} // namespace

TEST(BattlerView, SinglesHasTwoColumnsInBattlerOrder)
{
    const NameTable names = loadNames();
    BattleSnapshot snap;
    snap.valid = true;
    snap.typeFlags = 0x2; // in battle, non-zero, BATTLE_TYPE_DOUBLE (bit 0) clear
    snap.mons[0] = MakeMon(SPECIES_SQUIRTLE, 60, 65, 50, 64, 44, 44, TYPE_WATER, TYPE_WATER, 20, MOVE_TACKLE);
    snap.mons[1] = MakeMon(SPECIES_CHARMANDER, 52, 43, 60, 50, 39, 39, TYPE_FIRE, TYPE_FIRE, 20);

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
    snap.typeFlags = 1 | BATTLE_TYPE_DOUBLE;
    for (int i = 0; i < kMaxBattlers; ++i)
        snap.mons[i] = MakeMon(SPECIES_RATTATA + i, 50, 50, 50, 50, 30, 30, TYPE_NORMAL, TYPE_NORMAL, 10);

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
    BattleSnapshot snap;
    snap.valid = true;
    snap.typeFlags = 0x2;
    snap.mons[0] = MakeMon(SPECIES_SQUIRTLE, 60, 65, 50, 64, 0, 44, TYPE_WATER, TYPE_WATER, 20);
    snap.mons[1] = MakeMon(SPECIES_CHARMANDER, 52, 43, 60, 50, 39, 39, TYPE_FIRE, TYPE_FIRE, 20);
    snap.absentBattlerFlags = 1u << 0; // battler 0 fainted/not sent out

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_FALSE(grid.columns[0].present);
    EXPECT_TRUE(grid.columns[1].present);
}

TEST(BattlerView, ComputesDamageRangeAndPercentForAKnownMove)
{
    const NameTable names = loadNames();
    BattleSnapshot snap;
    snap.valid = true;
    snap.typeFlags = 0x2;
    // Same matchup as helperapp/tests/test_damage.cpp's Tackle case:
    // 17 max / 14 min damage at level 50, attack 100 vs defense 100.
    snap.mons[0] = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50, MOVE_TACKLE);
    snap.mons[1] = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);

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
}

TEST(BattlerView, EmptyMoveSlotIsNotPresent)
{
    const NameTable names = loadNames();
    BattleSnapshot snap;
    snap.valid = true;
    snap.typeFlags = 0x2;
    snap.mons[0] = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 100, 100, TYPE_WATER, TYPE_WATER, 50); // move0 defaults MOVE_NONE
    snap.mons[1] = MakeMon(SPECIES_SQUIRTLE, 100, 100, 100, 100, 200, 200, TYPE_WATER, TYPE_WATER, 50);

    const BattlerGrid grid = BuildBattlerGrid(snap, names);

    EXPECT_FALSE(grid.columns[0].moves[0].present);
}
