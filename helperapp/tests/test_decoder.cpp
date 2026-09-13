#include "emerald/decoder.h"

#include <gtest/gtest.h>

using namespace emerald;

namespace {
QString configPath(const char *filename)
{
    return QStringLiteral(EMERALD_CONFIG_DIR "/") + filename;
}

BattleLayouts loadLayouts()
{
    QString error;
    BattleLayouts layouts = BattleLayouts::loadFromFile(configPath("battle_pokemon_layout_us_rev0.json"), &error);
    EXPECT_TRUE(layouts.isValid()) << error.toStdString();
    return layouts;
}

SymbolTable loadSymbols()
{
    QString error;
    SymbolTable symbols = SymbolTable::loadFromFile(configPath("symbols_us_rev0.json"), &error);
    EXPECT_TRUE(symbols.isValid()) << error.toStdString();
    return symbols;
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

// Sets bitfield `field` of the struct at `base` in `b` via the layout, the
// way the game's compiler would have packed it.
void wrBits(QByteArray &b, int base, const StructLayout &l, const QString &field, quint32 value)
{
    const int unit = base + l.offset(field) - l.bitOffset(field) / 8;
    quint32 word = 0;
    for (int i = 0; i < l.elemSize(field); ++i)
        word |= quint32(quint8(b[unit + i])) << (8 * i);
    const quint32 mask = ((1u << l.bits(field)) - 1) << l.bitOffset(field);
    word = (word & ~mask) | ((value << l.bitOffset(field)) & mask);
    for (int i = 0; i < l.elemSize(field); ++i)
        b[unit + i] = char((word >> (8 * i)) & 0xFF);
}

QByteArray oneMonRegion(const StructLayout &layout)
{
    QByteArray oneMon(layout.totalSize(), '\0');
    wr16(oneMon, layout.offset("species"), 1);
    wr16(oneMon, layout.offset("hp"), 100);
    wr16(oneMon, layout.offset("maxHP"), 100);
    QByteArray allMons;
    for (int i = 0; i < kMaxBattlers; ++i)
        allMons += oneMon;
    return allMons;
}
} // namespace

TEST(Decoder, DecodesBattlePokemonUsingLayoutOffsetsNotMagicNumbers)
{
    const StructLayout layout = loadLayouts().battlePokemon;
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
    wr8(raw, layout.offset("friendship"), 220);
    wr16(raw, layout.offset("item"), 0x00B3);
    wr32(raw, layout.offset("status1"), 0x00000040); // e.g. a sleep counter bit
    wr32(raw, layout.offset("status2"), STATUS2_FOCUS_ENERGY | STATUS2_FORESIGHT);
    wr32(raw, layout.offset("personality"), 0xDEADBEEF);

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

    const BattlePokemon mon = decodeBattlePokemon(raw, layout);

    EXPECT_EQ(mon.species, 253);
    EXPECT_EQ(mon.attack, 65);
    EXPECT_EQ(mon.defense, 45);
    EXPECT_EQ(mon.speed, 95);
    EXPECT_EQ(mon.spAttack, 85);
    EXPECT_EQ(mon.spDefense, 65);
    EXPECT_EQ(mon.hp, 70);
    EXPECT_EQ(mon.maxHP, 78);
    EXPECT_EQ(mon.ability, 65);
    EXPECT_EQ(mon.level, 36);
    EXPECT_EQ(mon.friendship, 220);
    EXPECT_EQ(mon.item, 0x00B3);
    EXPECT_EQ(mon.status1, 0x40u);
    EXPECT_EQ(mon.status2, quint32(STATUS2_FOCUS_ENERGY | STATUS2_FORESIGHT));
    EXPECT_EQ(mon.personality, 0xDEADBEEFu);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(mon.moves[i], moves[i]);
        EXPECT_EQ(mon.pp[i], pp[i]);
    }
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(mon.statStages[i], 6 + i);
    EXPECT_EQ(mon.types[0], 12);
    EXPECT_EQ(mon.types[1], 12);
}

TEST(Decoder, DecodesBattlePokemonIvBitfields)
{
    // Hidden Power reads these; they share one u32 with isEgg/abilityNum.
    const StructLayout layout = loadLayouts().battlePokemon;
    QByteArray raw(layout.totalSize(), '\0');
    wrBits(raw, 0, layout, "hpIV", 31);
    wrBits(raw, 0, layout, "attackIV", 1);
    wrBits(raw, 0, layout, "defenseIV", 30);
    wrBits(raw, 0, layout, "speedIV", 17);
    wrBits(raw, 0, layout, "spAttackIV", 2);
    wrBits(raw, 0, layout, "spDefenseIV", 29);
    wrBits(raw, 0, layout, "abilityNum", 1);

    const BattlePokemon mon = decodeBattlePokemon(raw, layout);

    EXPECT_EQ(mon.hpIV, 31u);
    EXPECT_EQ(mon.attackIV, 1u);
    EXPECT_EQ(mon.defenseIV, 30u);
    EXPECT_EQ(mon.speedIV, 17u);
    EXPECT_EQ(mon.spAttackIV, 2u);
    EXPECT_EQ(mon.spDefenseIV, 29u);
    EXPECT_EQ(mon.isEgg, 0u);
    EXPECT_EQ(mon.abilityNum, 1u);
    // And the packed word is what the ARM compiler would have produced:
    // hpIV in bits 0-4, ..., abilityNum in bit 31 (checked against agbcc by
    // tools/verify_struct_layout.py).
    const quint32 word = quint32(quint8(raw[20])) | quint32(quint8(raw[21])) << 8
                       | quint32(quint8(raw[22])) << 16 | quint32(quint8(raw[23])) << 24;
    EXPECT_EQ(word, 31u | 1u << 5 | 30u << 10 | 17u << 15 | 2u << 20 | 29u << 25 | 1u << 31);
}

TEST(Decoder, DecodeSnapshotResolvesRegionsByAddressNotPosition)
{
    const SymbolTable symbols = loadSymbols();
    const BattleLayouts layouts = loadLayouts();

    QByteArray typeFlags(4, '\0');
    wr32(typeFlags, 0, 0x00000001);

    // Regions deliberately fed out of "natural" order to prove address-based
    // resolution, not positional assumptions, drives decoding.
    RawSnapshot raw;
    raw.version = 1;
    raw.regions.append(RawRegion{symbols.address("gBattleTypeFlags"), typeFlags});
    raw.regions.append(RawRegion{symbols.address("gBattleMons"), oneMonRegion(layouts.battlePokemon)});

    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layouts);

    ASSERT_TRUE(snap.valid);
    EXPECT_EQ(snap.state.battleTypeFlags, 0x1u);
    EXPECT_TRUE(snap.inBattle());
    for (int i = 0; i < kMaxBattlers; ++i) {
        EXPECT_EQ(snap.state.battleMons[i].species, 1);
        EXPECT_EQ(snap.state.battleMons[i].hp, 100);
    }
    // gBattlersCount wasn't sent: falls back to what the game would have
    // set for a doubles battle, and says it's missing.
    EXPECT_EQ(snap.state.battlersCount, 4);
    EXPECT_TRUE(snap.missingRegions.contains(QStringLiteral("gBattlersCount")));
    EXPECT_TRUE(snap.missingRegions.contains(QStringLiteral("gBattleWeather")));
}

TEST(Decoder, DecodeSnapshotIsInvalidWithoutABattleMonsRegion)
{
    RawSnapshot raw; // no regions at all
    const BattleSnapshot snap = decodeSnapshot(raw, loadSymbols(), loadLayouts());

    EXPECT_FALSE(snap.valid);
}

TEST(Decoder, DecodesFieldAndPerBattlerState)
{
    const SymbolTable symbols = loadSymbols();
    const BattleLayouts layouts = loadLayouts();

    QByteArray weather(2, '\0');
    wr16(weather, 0, B_WEATHER_RAIN_TEMPORARY);

    QByteArray sideStatuses(4, '\0');
    wr16(sideStatuses, 2, SIDE_STATUS_REFLECT | SIDE_STATUS_LIGHTSCREEN); // opponent side

    const StructLayout &st = layouts.sideTimer;
    QByteArray sideTimers(st.totalSize() * NUM_BATTLE_SIDES, '\0');
    wr8(sideTimers, st.totalSize() + st.offset("reflectTimer"), 4);
    wr8(sideTimers, st.totalSize() + st.offset("spikesAmount"), 2);

    QByteArray statuses3(16, '\0');
    wr32(statuses3, 1 * 4, STATUS3_UNDERGROUND | STATUS3_CHARGED_UP);

    const StructLayout &ds = layouts.disableStruct;
    QByteArray disable(ds.totalSize() * kMaxBattlers, '\0');
    wr8(disable, ds.offset("stockpileCounter"), 3);
    wrBits(disable, 0, ds, "rolloutTimer", 2);
    wrBits(disable, 0, ds, "rolloutTimerStartValue", 5);
    wr8(disable, ds.offset("furyCutterCounter"), 4);

    const StructLayout &ps = layouts.protectStruct;
    QByteArray protect(ps.totalSize() * kMaxBattlers, '\0');
    wrBits(protect, 0, ps, "helpingHand", 1);
    wrBits(protect, ps.totalSize(), ps, "endured", 1);
    wr32(protect, ps.offset("physicalDmg"), 77);
    wr8(protect, ps.offset("physicalBattlerId"), 1);

    const StructLayout &eb = layouts.battleEnigmaBerry;
    QByteArray enigma(eb.totalSize() * kMaxBattlers, '\0');
    wr8(enigma, eb.totalSize() + eb.offset("holdEffect"), HOLD_EFFECT_FOCUS_BAND);
    wr8(enigma, eb.totalSize() + eb.offset("holdEffectParam"), 25);

    QByteArray positions(4, '\0');
    for (int i = 0; i < 4; ++i)
        wr8(positions, i, quint8(3 - i)); // unusual on purpose (link battles remap these)

    RawSnapshot raw;
    raw.regions = {
        {symbols.address("gBattleMons"), oneMonRegion(layouts.battlePokemon)},
        {symbols.address("gBattleWeather"), weather},
        {symbols.address("gSideStatuses"), sideStatuses},
        {symbols.address("gSideTimers"), sideTimers},
        {symbols.address("gStatuses3"), statuses3},
        {symbols.address("gDisableStructs"), disable},
        {symbols.address("gProtectStructs"), protect},
        {symbols.address("gEnigmaBerries"), enigma},
        {symbols.address("gBattlerPositions"), positions},
        {symbols.address("gBattlersCount"), QByteArray(1, char(2))},
    };

    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layouts);
    ASSERT_TRUE(snap.valid);
    const BattleState &s = snap.state;

    EXPECT_EQ(s.battleWeather, B_WEATHER_RAIN_TEMPORARY);
    EXPECT_EQ(s.sideStatuses[B_SIDE_PLAYER], 0);
    EXPECT_EQ(s.sideStatuses[B_SIDE_OPPONENT], SIDE_STATUS_REFLECT | SIDE_STATUS_LIGHTSCREEN);
    EXPECT_EQ(s.sideTimers[B_SIDE_OPPONENT].reflectTimer, 4);
    EXPECT_EQ(s.sideTimers[B_SIDE_OPPONENT].spikesAmount, 2);
    EXPECT_EQ(s.statuses3[1], quint32(STATUS3_UNDERGROUND | STATUS3_CHARGED_UP));
    EXPECT_EQ(s.disableStructs[0].stockpileCounter, 3);
    EXPECT_EQ(s.disableStructs[0].rolloutTimer, 2);
    EXPECT_EQ(s.disableStructs[0].rolloutTimerStartValue, 5);
    EXPECT_EQ(s.disableStructs[0].furyCutterCounter, 4);
    EXPECT_EQ(s.protectStructs[0].helpingHand, 1u);
    EXPECT_EQ(s.protectStructs[0].endured, 0u);
    EXPECT_EQ(s.protectStructs[1].endured, 1u);
    EXPECT_EQ(s.protectStructs[0].physicalDmg, 77u);
    EXPECT_EQ(s.protectStructs[0].physicalBattlerId, 1);
    EXPECT_EQ(s.enigmaBerries[1].holdEffect, HOLD_EFFECT_FOCUS_BAND);
    EXPECT_EQ(s.enigmaBerries[1].holdEffectParam, 25);
    EXPECT_EQ(s.battlerPositions[0], 3);
    EXPECT_EQ(s.battlerPositions[3], 0);
    EXPECT_EQ(s.battlersCount, 2);
    EXPECT_FALSE(snap.missingRegions.contains(QStringLiteral("gBattleWeather")));
}

TEST(Decoder, FollowsBattleResourcesAndSaveBlockPointers)
{
    const SymbolTable symbols = loadSymbols();
    const BattleLayouts layouts = loadLayouts();

    // gBattleResources -> BattleResources @ 0x02030000 -> ->flags @ 0x02031000
    const quint32 resourcesAddr = 0x02030000, flagsAddr = 0x02031000, saveBlockAddr = 0x02025A00;
    QByteArray resourcesPtr(4, '\0');
    wr32(resourcesPtr, 0, resourcesAddr);
    QByteArray resources(layouts.battleResources.totalSize(), '\0');
    wr32(resources, layouts.battleResources.offset("flags"), flagsAddr);
    QByteArray resourceFlags(layouts.resourceFlags.totalSize(), '\0');
    wr32(resourceFlags, 1 * 4, RESOURCE_FLAG_FLASH_FIRE); // battler 1's Flash Fire is lit

    // gSaveBlock1Ptr -> SaveBlock1; flags[] at +offsetof(flags), badges 1 and 7 set
    QByteArray savePtr(4, '\0');
    wr32(savePtr, 0, saveBlockAddr);
    QByteArray flags(NUM_FLAG_BYTES, '\0');
    flags[FLAG_BADGE01_GET / 8] = char(flags[FLAG_BADGE01_GET / 8] | (1 << (FLAG_BADGE01_GET % 8)));
    flags[FLAG_BADGE07_GET / 8] = char(flags[FLAG_BADGE07_GET / 8] | (1 << (FLAG_BADGE07_GET % 8)));

    RawSnapshot raw;
    raw.regions = {
        {symbols.address("gBattleMons"), oneMonRegion(layouts.battlePokemon)},
        {symbols.address("gBattleResources"), resourcesPtr},
        {resourcesAddr, resources},
        {flagsAddr, resourceFlags},
        {symbols.address("gSaveBlock1Ptr"), savePtr},
        {saveBlockAddr + quint32(layouts.saveBlock1.offset("flags")), flags},
    };

    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layouts);
    ASSERT_TRUE(snap.valid);

    EXPECT_EQ(snap.state.resourceFlags[0], 0u);
    EXPECT_EQ(snap.state.resourceFlags[1], quint32(RESOURCE_FLAG_FLASH_FIRE));
    EXPECT_FALSE(snap.missingRegions.contains(QStringLiteral("gBattleResources->flags")));
    EXPECT_FALSE(snap.missingRegions.contains(QStringLiteral("gSaveBlock1Ptr->flags")));
    auto flagSet = [&](int id) { return (snap.state.saveFlags[id / 8] >> (id % 8)) & 1; };
    EXPECT_TRUE(flagSet(FLAG_BADGE01_GET));
    EXPECT_FALSE(flagSet(FLAG_BADGE05_GET));
    EXPECT_TRUE(flagSet(FLAG_BADGE07_GET));
}

TEST(Decoder, ReportsUnfollowablePointersAsMissing)
{
    const SymbolTable symbols = loadSymbols();
    const BattleLayouts layouts = loadLayouts();

    QByteArray danglingPtr(4, '\0');
    wr32(danglingPtr, 0, 0x02030000); // points at a region the bridge didn't send

    RawSnapshot raw;
    raw.regions = {
        {symbols.address("gBattleMons"), oneMonRegion(layouts.battlePokemon)},
        {symbols.address("gBattleResources"), danglingPtr},
    };

    const BattleSnapshot snap = decodeSnapshot(raw, symbols, layouts);
    ASSERT_TRUE(snap.valid);
    EXPECT_TRUE(snap.missingRegions.contains(QStringLiteral("gBattleResources->flags")));
    EXPECT_TRUE(snap.missingRegions.contains(QStringLiteral("gSaveBlock1Ptr->flags")));
}
