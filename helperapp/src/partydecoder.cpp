#include "emerald/partydecoder.h"

#include "emerald/byteio.h"

#include <cstring>

namespace emerald {

namespace {

// Ported from GetSubstruct()'s SUBSTRUCT_CASE table in src/pokemon.c.
// Row `personality % 24` gives, for each substruct type (Growth=0,
// Attacks=1, EVs=2, Misc=3), which of the four 12-byte slots in memory
// holds it: kSubstructOrder[n][type] = memory slot index.
constexpr int kSubstructOrder[24][4] = {
    {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 3, 1, 2},
    {0, 2, 3, 1}, {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2},
    {2, 0, 1, 3}, {3, 0, 1, 2}, {2, 0, 3, 1}, {3, 0, 2, 1},
    {1, 2, 0, 3}, {1, 3, 0, 2}, {2, 1, 0, 3}, {3, 1, 0, 2},
    {2, 3, 0, 1}, {3, 2, 0, 1}, {1, 2, 3, 0}, {1, 3, 2, 0},
    {2, 1, 3, 0}, {3, 1, 2, 0}, {2, 3, 1, 0}, {3, 2, 1, 0},
};

constexpr int kSubstructBytes = 12;

} // namespace

QByteArray decryptAndUnshuffle(const QByteArray &secureBlock48, quint32 personality, quint32 otId)
{
    Q_ASSERT(secureBlock48.size() == 48);

    // EncryptBoxMon/DecryptBoxMon XOR every 32-bit word of the secure block
    // with personality, then otId (order doesn't matter -- XOR is
    // commutative -- but this mirrors the source).
    const quint32 key = personality ^ otId;
    QByteArray decrypted(48, Qt::Uninitialized);
    for (int word = 0; word < 12; ++word) {
        const quint32 v = rd32(secureBlock48, word * 4) ^ key;
        decrypted[word * 4 + 0] = char(v & 0xFF);
        decrypted[word * 4 + 1] = char((v >> 8) & 0xFF);
        decrypted[word * 4 + 2] = char((v >> 16) & 0xFF);
        decrypted[word * 4 + 3] = char((v >> 24) & 0xFF);
    }

    // Un-shuffle: memory slot -> canonical (type0..type3) order.
    const int *order = kSubstructOrder[personality % 24];
    QByteArray canonical(48, Qt::Uninitialized);
    for (int type = 0; type < 4; ++type) {
        const int slot = order[type];
        std::memcpy(canonical.data() + type * kSubstructBytes,
                    decrypted.constData() + slot * kSubstructBytes, kSubstructBytes);
    }
    return canonical;
}

DecryptedSubstructs parseSubstructs(const QByteArray &decrypted48)
{
    Q_ASSERT(decrypted48.size() == 48);
    DecryptedSubstructs out;

    // substruct0 (Growth) @ 0
    out.species = rd16(decrypted48, 0);
    out.heldItem = rd16(decrypted48, 2);
    out.experience = rd32(decrypted48, 4);
    out.ppBonuses = rd8(decrypted48, 8);
    out.friendship = rd8(decrypted48, 9);

    // substruct1 (Attacks) @ 12
    for (int i = 0; i < 4; ++i)
        out.moves[i] = rd16(decrypted48, 12 + i * 2);
    for (int i = 0; i < 4; ++i)
        out.pp[i] = rd8(decrypted48, 20 + i);

    // substruct2 (EVs) @ 24
    out.hpEV = rd8(decrypted48, 24);
    out.attackEV = rd8(decrypted48, 25);
    out.defenseEV = rd8(decrypted48, 26);
    out.speedEV = rd8(decrypted48, 27);
    out.spAttackEV = rd8(decrypted48, 28);
    out.spDefenseEV = rd8(decrypted48, 29);

    // substruct3 (Misc) @ 36 -- IVs are a 30-bit run of 5-bit fields packed
    // into the first 4 bytes of this substruct. include/pokemon.h's own
    // /*0xNN*/ comments on PokemonSubstruct3's hpIV..spDefenseIV run are
    // internally inconsistent with BattlePokemon's *identical* field list
    // (same names, widths, order, both starting 4-byte-aligned) -- e.g.
    // spAttackIV is annotated 0x05 here but 0x16 there, which isn't
    // possible for the same field list under one ABI, so one comment is
    // stale. This follows BattlePokemon's layout (self-validated in
    // tools/gen_struct_layout.py against its own annotations) rather than
    // PokemonSubstruct3's. Not currently exposed on PartyMon/UI; verify
    // against a live memory dump (Testing strategy in CODE_PLAN.md) before
    // trusting these bits for anything user-facing.
    out.pokerus = rd8(decrypted48, 36);
    const quint32 ivWord = rd32(decrypted48, 40);
    out.hpIV = (ivWord >> 0) & 0x1F;
    out.attackIV = (ivWord >> 5) & 0x1F;
    out.defenseIV = (ivWord >> 10) & 0x1F;
    out.speedIV = (ivWord >> 15) & 0x1F;
    out.spAttackIV = (ivWord >> 20) & 0x1F;
    out.spDefenseIV = (ivWord >> 25) & 0x1F;
    out.isEgg = ((ivWord >> 30) & 0x1) != 0;
    out.abilityNum = (ivWord >> 31) & 0x1;

    return out;
}

quint16 calculateChecksum(const QByteArray &decrypted48)
{
    Q_ASSERT(decrypted48.size() == 48);
    quint16 checksum = 0;
    for (int i = 0; i < 24; ++i)
        checksum = quint16(checksum + rd16(decrypted48, i * 2));
    return checksum;
}

} // namespace emerald
