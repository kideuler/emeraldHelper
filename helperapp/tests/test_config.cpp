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
