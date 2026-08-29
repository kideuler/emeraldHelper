#include "emerald/partydecoder.h"

#include "emerald/byteio.h"
#include "emerald/decoder.h"

#include <array>
#include <cstring>
#include <gtest/gtest.h>

using namespace emerald;

namespace {

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

// Independently re-derived (not shared with src/) from the same
// SUBSTRUCT_CASE table in src/pokemon.c that partydecoder.cpp's
// kSubstructOrder ports, so this test doesn't just check the port against
// itself. Row personality%24 maps canonical type -> memory slot.
constexpr int kOrder[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2},
    {0, 2, 3, 1}, {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2},
    {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2},
    {2, 3, 0, 1}, {3, 2, 0, 1}, {1, 2, 3, 0}, {1, 3, 2, 0},
    {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

// Builds a canonical (unshuffled) 48-byte substruct block for a mon with
// the given species/moves, everything else zeroed.
QByteArray canonicalBlock(quint16 species, const std::array<quint16, 4> &moves)
{
    QByteArray b(48, '\0');
    wr16(b, 0, species); // substruct0.species
    for (int i = 0; i < 4; ++i)
        wr16(b, 12 + i * 2, moves[i]); // substruct1.moves
    return b;
}

// Shuffles a canonical block into on-wire memory order and XOR-encrypts it
// with personality^otId -- the inverse of decryptAndUnshuffle, built here
// purely to construct test fixtures (production code never needs to
// encrypt).
QByteArray shuffleAndEncrypt(const QByteArray &canonical48, quint32 personality, quint32 otId)
{
    const int *order = kOrder[personality % 24];
    QByteArray memBlock(48, '\0');
    for (int type = 0; type < 4; ++type) {
        const int slot = order[type];
        std::memcpy(memBlock.data() + slot * 12, canonical48.constData() + type * 12, 12);
    }
    const quint32 key = personality ^ otId;
    QByteArray encrypted(48, '\0');
    for (int w = 0; w < 12; ++w)
        wr32(encrypted, w * 4, rd32(memBlock, w * 4) ^ key);
    return encrypted;
}

// Builds a full raw 100-byte party slot around an encrypted secure block.
QByteArray buildRawPokemon(quint32 personality, quint32 otId, const QByteArray &encrypted48,
                            quint16 checksum, quint16 level, quint16 hp, quint16 maxHp)
{
    QByteArray raw(100, '\0');
    wr32(raw, 0, personality);
    wr32(raw, 4, otId);
    wr16(raw, 28, checksum);
    for (int i = 0; i < 48; ++i)
        raw[32 + i] = encrypted48.at(i);
    raw[84] = char(level);
    wr16(raw, 86, hp);
    wr16(raw, 88, maxHp);
    return raw;
}

} // namespace

TEST(PartyDecoder, RoundTripsThroughIdentityPermutation)
{
    // personality % 24 == 0 -> identity permutation, exercises the XOR
    // decrypt path without also depending on the shuffle being correct.
    const quint32 personality = 0;
    const quint32 otId = 0x12345678;

    const QByteArray canonical = canonicalBlock(413, {33, 75, 0, 0});
    const quint16 checksum = calculateChecksum(canonical);
    const QByteArray encrypted = shuffleAndEncrypt(canonical, personality, otId);

    const QByteArray decoded = decryptAndUnshuffle(encrypted, personality, otId);
    EXPECT_EQ(decoded, canonical);
    EXPECT_EQ(calculateChecksum(decoded), checksum);

    const DecryptedSubstructs subs = parseSubstructs(decoded);
    EXPECT_EQ(subs.species, 413);
    EXPECT_EQ(subs.moves[0], 33);
    EXPECT_EQ(subs.moves[1], 75);
}

TEST(PartyDecoder, RoundTripsThroughANonTrivialPermutation)
{
    // personality % 24 == 9 -> SUBSTRUCT_CASE(9,3,0,1,2): a genuinely
    // shuffled substruct order, not the identity case above.
    const quint32 personality = 9;
    const quint32 otId = 0xCAFEF00D;

    const QByteArray canonical = canonicalBlock(258, {55, 0, 0, 0});
    const quint16 checksum = calculateChecksum(canonical);
    const QByteArray encrypted = shuffleAndEncrypt(canonical, personality, otId);

    const QByteArray decoded = decryptAndUnshuffle(encrypted, personality, otId);
    EXPECT_EQ(decoded, canonical);

    const DecryptedSubstructs subs = parseSubstructs(decoded);
    EXPECT_EQ(subs.species, 258);
    EXPECT_EQ(calculateChecksum(decoded), checksum);
}

TEST(PartyDecoder, DecodePartyMonReportsInvalidOnChecksumMismatch)
{
    const quint32 personality = 0;
    const quint32 otId = 1;
    const QByteArray canonical = canonicalBlock(1, {1, 0, 0, 0});
    const QByteArray encrypted = shuffleAndEncrypt(canonical, personality, otId);

    // Deliberately store the wrong checksum.
    const QByteArray raw = buildRawPokemon(personality, otId, encrypted, /*checksum=*/0xFFFF,
                                            /*level=*/50, /*hp=*/1, /*maxHp=*/1);

    const PartyMon mon = decodePartyMon(raw);
    EXPECT_FALSE(mon.valid);
}

TEST(PartyDecoder, DecodePartyMonProducesAValidMonForACorrectChecksum)
{
    const quint32 personality = 0;
    const quint32 otId = 7;
    const QByteArray canonical = canonicalBlock(6, {80, 82, 0, 0}); // e.g. Charizard w/ moves
    const quint16 checksum = calculateChecksum(canonical);
    const QByteArray encrypted = shuffleAndEncrypt(canonical, personality, otId);

    const QByteArray raw =
        buildRawPokemon(personality, otId, encrypted, checksum, /*level=*/50, /*hp=*/120, /*maxHp=*/140);

    const PartyMon mon = decodePartyMon(raw);
    EXPECT_TRUE(mon.valid);
    EXPECT_EQ(mon.species, 6);
    EXPECT_EQ(mon.level, 50);
    EXPECT_EQ(mon.hp, 120);
    EXPECT_EQ(mon.maxHp, 140);
    EXPECT_EQ(mon.moves[0], 80);
    EXPECT_EQ(mon.moves[1], 82);
}
