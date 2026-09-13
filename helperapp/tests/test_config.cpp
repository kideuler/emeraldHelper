#include "emerald/decoder.h"
#include "emerald/structlayout.h"
#include "emerald/symboltable.h"

#include <gtest/gtest.h>

// Sanity-checks that the Phase 1.3/1.4 generated configs actually load and
// contain what the decoder needs -- catches a regenerated JSON silently
// dropping a required symbol/field before it becomes a runtime crash.
namespace {
QString configPath(const char *filename)
{
    return QStringLiteral(EMERALD_CONFIG_DIR "/") + filename;
}
} // namespace

TEST(SymbolTable, LoadsGeneratedSymbolsAndHasEverythingTheDecoderNeeds)
{
    QString error;
    const emerald::SymbolTable table =
        emerald::SymbolTable::loadFromFile(configPath("symbols_us_rev0.json"), &error);
    ASSERT_TRUE(table.isValid()) << error.toStdString();

    const QStringList required = {
        "gBattleMons", "gBattleTypeFlags", "gBattlerAttacker", "gBattlerTarget",
        "gAbsentBattlerFlags", "gBattlerPartyIndexes", "gPlayerParty", "gEnemyParty",
    };
    EXPECT_TRUE(table.missing(required).isEmpty())
        << table.missing(required).join(", ").toStdString();

    // Reverse lookup round-trips.
    const quint32 addr = table.address("gBattleMons");
    ASSERT_NE(addr, 0u);
    EXPECT_EQ(table.nameForAddress(addr), QStringLiteral("gBattleMons"));
}

TEST(StructLayout, LoadsBattlePokemonWithExpectedFields)
{
    QString error;
    const emerald::StructLayout layout = emerald::StructLayout::loadFromFile(
        configPath("battle_pokemon_layout_us_rev0.json"), "BattlePokemon", &error);
    ASSERT_TRUE(layout.isValid()) << error.toStdString();

    EXPECT_EQ(layout.totalSize(), 0x58);
    EXPECT_EQ(layout.offset("species"), 0x00);
    EXPECT_EQ(layout.offset("moves"), 0x0C);
    EXPECT_EQ(layout.count("moves"), 4);
    EXPECT_EQ(layout.offset("statStages"), 0x18);
    EXPECT_EQ(layout.count("statStages"), 8);
    EXPECT_EQ(layout.offset("hp"), 0x28);
    EXPECT_EQ(layout.offset("otId"), 0x54);
}

TEST(SymbolTable, HasEveryGlobalBattleStateMirrors)
{
    QString error;
    const emerald::SymbolTable table =
        emerald::SymbolTable::loadFromFile(configPath("symbols_us_rev0.json"), &error);
    ASSERT_TRUE(table.isValid()) << error.toStdString();

    const QStringList battleState = {
        "gBattlersCount", "gBattlerPositions", "gBattleWeather", "gSideStatuses", "gSideTimers",
        "gStatuses3", "gDisableStructs", "gProtectStructs", "gEnigmaBerries", "gBattleEnvironment",
        "gBattleResources", "gSaveBlock1Ptr", "gTrainerBattleOpponent_A",
    };
    EXPECT_TRUE(table.missing(battleState).isEmpty()) << table.missing(battleState).join(", ").toStdString();
}

TEST(StructLayout, LoadsEveryStructTheDecoderReads)
{
    QString error;
    const emerald::BattleLayouts layouts =
        emerald::BattleLayouts::loadFromFile(configPath("battle_pokemon_layout_us_rev0.json"), &error);
    ASSERT_TRUE(layouts.isValid()) << error.toStdString();

    // Sizes agbcc gives these structs (tools/verify_struct_layout.py): the
    // decoder strides gDisableStructs[] etc. by them.
    EXPECT_EQ(layouts.disableStruct.totalSize(), 28);
    EXPECT_EQ(layouts.protectStruct.totalSize(), 16);
    EXPECT_EQ(layouts.sideTimer.totalSize(), 12);
    EXPECT_EQ(layouts.battleEnigmaBerry.totalSize(), 28);
    EXPECT_EQ(layouts.battleResources.offset("flags"), 4);
    EXPECT_EQ(layouts.saveBlock1.offset("flags"), 0x1270);
    // Bitfields carry their position, so e.g. Helping Hand can be read by name.
    EXPECT_EQ(layouts.protectStruct.bits("helpingHand"), 1);
    EXPECT_EQ(layouts.protectStruct.bitOffset("helpingHand"), 3);
    EXPECT_EQ(layouts.battlePokemon.bitOffset("spDefenseIV"), 25);
}
