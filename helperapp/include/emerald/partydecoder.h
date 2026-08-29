#pragma once

#include <QByteArray>
#include <QtGlobal>

// Phase 2.2: party Pokemon are encrypted. This ports the exact algorithm
// from src/pokemon.c (EncryptBoxMon/DecryptBoxMon/GetSubstruct/
// CalculateBoxMonChecksum) rather than reimplementing it from a wiki
// description, since a hand-rolled version is exactly the kind of thing
// that gets truncation/ordering subtly wrong.
namespace emerald {

// One decrypted, unshuffled Pokemon's substruct fields (the 48-byte
// "secure" block of a BoxPokemon, i.e. offset 32..79 of a raw 100-byte
// Pokemon -- see helperapp/config/battle_pokemon_layout_us_rev0.json's
// "Pokemon" and "PokemonSubstructN" entries).
struct DecryptedSubstructs {
    // substruct0 (Growth)
    quint16 species = 0;
    quint16 heldItem = 0;
    quint32 experience = 0;
    quint8 ppBonuses = 0;
    quint8 friendship = 0;

    // substruct1 (Attacks)
    quint16 moves[4] = {};
    quint8 pp[4] = {};

    // substruct2 (EVs & contest stats) -- only EVs are surfaced, contest
    // stats aren't needed by anything this app currently decodes.
    quint8 hpEV = 0, attackEV = 0, defenseEV = 0, speedEV = 0, spAttackEV = 0, spDefenseEV = 0;

    // substruct3 (Misc)
    quint8 pokerus = 0;
    quint8 hpIV = 0, attackIV = 0, defenseIV = 0, speedIV = 0, spAttackIV = 0, spDefenseIV = 0;
    bool isEgg = false;
    quint8 abilityNum = 0;
};

// XOR-decrypts a BoxPokemon's 48-byte secure block (key = personality ^
// otId, applied per 32-bit word) and un-shuffles its four 12-byte
// substructs back into canonical (Growth, Attacks, EVs, Misc) order using
// the personality % 24 permutation table ported from GetSubstruct().
// `secureBlock48` must be exactly 48 bytes.
QByteArray decryptAndUnshuffle(const QByteArray &secureBlock48, quint32 personality, quint32 otId);

// Parses a decrypted-and-unshuffled 48-byte block (as returned by
// decryptAndUnshuffle) into typed fields.
DecryptedSubstructs parseSubstructs(const QByteArray &decrypted48);

// Ported from CalculateBoxMonChecksum: sum of all 24 u16 words across the
// four substructs, in canonical (unshuffled) order. Compare against the
// BoxPokemon's stored `checksum` field (layout offset 28) to know whether
// decryption succeeded / the slot holds real data.
quint16 calculateChecksum(const QByteArray &decrypted48);

} // namespace emerald
