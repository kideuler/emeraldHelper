#include "emerald/decoder.h"

#include <gtest/gtest.h>

using namespace emerald;

namespace {
QString configPath(const char *filename)
{
    return QStringLiteral(EMERALD_CONFIG_DIR "/") + filename;
}

StructLayout loadMonLayout()
{
    QString error;
    StructLayout layout = StructLayout::loadFromFile(
        configPath("battle_pokemon_layout_us_rev0.json"), "BattlePokemon", &error);
    EXPECT_TRUE(layout.isValid()) << error.toStdString();
    return layout;
}

void wr8(QByteArray &b, int off, quint8 v) { b[off] = char(v); }
void wr16(QByteArray &b, int off, quint16 v)
{
    b[off] = char(v & 0xFF);
    b[off + 1] = char((v >> 8) & 0xFF);
}
void wr32(QByteArray &b, int off, quint32 v)
{
    wr16(b, off, quint16(v & 0xFFFF));
    wr16(b, off + 2, quint16((v >> 16) & 0xFFFF));
}
} // namespace

TEST(Decoder, DecodesBattleMonUsingLayoutOffsetsNotMagicNumbers)
{
    const StructLayout layout = loadMonLayout();
    QByteArray raw(layout.totalSize(), '\0');

    wr16(raw, layout.offset("species"), 253);   // Grovyle-ish placeholder id
    wr16(raw, layout.offset("attack"), 65);
    wr16(raw, layout.offset("defense"), 45);
    wr16(raw, layout.offset("speed"), 95);
    wr16(raw, layout.offset("spAttack"), 85);
    wr16(raw, layout.offset("spDefense"), 65);
    wr16(raw, layout.offset("hp"), 70);
    wr16(raw, layout.offset("maxHP"), 78);
    wr8(raw, layout.offset("ability"), 65);
    wr8(raw, layout.offset("level"), 36);
    wr32(raw, layout.offset("status1"), 0x00000040); // e.g. a sleep counter bit

    const int movesOff = layout.offset("moves");
    const quint16 moves[4] = {33, 75, 226, 0};
    for (int i = 0; i < 4; ++i)
        wr16(raw, movesOff + i * 2, moves[i]);

    const int ppOff = layout.offset("pp");
    const quint8 pp[4] = {35, 24, 10, 0};
    for (int i = 0; i < 4; ++i)
        wr8(raw, ppOff + i, pp[i]);

    const int statOff = layout.offset("statStages");
    for (int i = 0; i < 8; ++i)
        wr8(raw, statOff + i, quint8(6 + i)); // 6 == no stage change

    const int typesOff = layout.offset("types");
    wr8(raw, typesOff, 12);     // e.g. Grass
    wr8(raw, typesOff + 1, 12); // mono-type: same twice

    const BattleMon mon = decodeBattleMon(raw, layout);

    EXPECT_EQ(mon.species, 253);
    EXPECT_EQ(mon.attack, 65);
    EXPECT_EQ(mon.defense, 45);
    EXPECT_EQ(mon.speed, 95);
    EXPECT_EQ(mon.spAttack, 85);
    EXPECT_EQ(mon.spDefense, 65);
    EXPECT_EQ(mon.hp, 70);
    EXPECT_EQ(mon.maxHp, 78);
    EXPECT_EQ(mon.ability, 65);
    EXPECT_EQ(mon.level, 36);
    EXPECT_EQ(mon.status1, 0x40u);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(mon.moves[i], moves[i]);
        EXPECT_EQ(mon.pp[i], pp[i]);
    }
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(mon.statStages[i], 6 + i);
    EXPECT_EQ(mon.type1, 12);
    EXPECT_EQ(mon.type2, 12);
}

TEST(Decoder, DecodeSnapshotResolvesRegionsByAddressNotPosition)
{
    QString error;
    const SymbolTable symbols =
        SymbolTable::loadFromFile(configPath("symbols_us_rev0.json"), &error);
    ASSERT_TRUE(symbols.isValid()) << error.toStdString();
    const StructLayout layout = loadMonLayout();

    QByteArray oneMon(layout.totalSize(), '\0');
    wr16(oneMon, layout.offset("species"), 1);
    wr16(oneMon, layout.offset("hp"), 100);
    wr16(oneMon, layout.offset("maxHP"), 100);

    QByteArray allMons;
    for (int i = 0; i < kMaxBattlers; ++i)
        allMons += oneMon;

    QByteArray typeFlags(4, '\0');
    wr32(typeFlags, 0, 0x00000001);

    // Regions deliberately fed out of "natural" order to prove address-based
    // resolution, not positional assumptions, drives decoding.
    RawSnapshot raw;
    raw.version = 1;
    raw.regions.append(RawRegion{symbols.address("gBattleTypeFlags"), typeFlags});
    raw.regions.append(RawRegion{symbols.address("gBattleMons"), allMons});

    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layout);

    ASSERT_TRUE(snap.valid);
    EXPECT_EQ(snap.typeFlags, 0x1u);
    EXPECT_TRUE(snap.inBattle());
    for (int i = 0; i < kMaxBattlers; ++i) {
        EXPECT_EQ(snap.mons[i].species, 1);
        EXPECT_EQ(snap.mons[i].hp, 100);
    }
}

TEST(Decoder, DecodeSnapshotIsInvalidWithoutABattleMonsRegion)
{
    QString error;
    const SymbolTable symbols =
        SymbolTable::loadFromFile(configPath("symbols_us_rev0.json"), &error);
    ASSERT_TRUE(symbols.isValid()) << error.toStdString();
    const StructLayout layout = loadMonLayout();

    RawSnapshot raw; // no regions at all
    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layout);

    EXPECT_FALSE(snap.valid);
}
