#pragma once

#include "emerald/battlesnapshot.h"
#include "emerald/rawsnapshot.h"
#include "emerald/structlayout.h"
#include "emerald/symboltable.h"

// Phase 4.2 / 2: bytes -> typed BattleSnapshot. Pure functions, no sockets,
// no Qt widgets -- this is what the offline fixture tests hammer.
namespace emerald {

// Every struct layout decodeSnapshot() reads by field name, all from
// battle_pokemon_layout_us_rev0.json (tools/gen_struct_layout.py; checked
// against agbcc by tools/verify_struct_layout.py).
struct BattleLayouts {
    StructLayout battlePokemon;
    StructLayout disableStruct;
    StructLayout protectStruct;
    StructLayout sideTimer;
    StructLayout battleEnigmaBerry;
    StructLayout battleResources;
    StructLayout resourceFlags;
    StructLayout saveBlock1;

    // Fails (isValid() == false, `error` set) if any struct is missing --
    // i.e. the JSON predates the BattleState decoder and needs
    // regenerating.
    static BattleLayouts loadFromFile(const QString &path, QString *error = nullptr);
    bool isValid() const;
};

// Decodes a single gBattleMons entry field by field using `layout` (the
// "BattlePokemon" struct) for offsets instead of hardcoded magic numbers.
BattlePokemon decodeBattlePokemon(const QByteArray &raw, const StructLayout &layout, int base = 0);

// Decodes one raw 100-byte struct Pokemon (party slot) into what
// GetMonData() would read from it. Runs the Phase 2.2 decrypt/unshuffle/
// checksum pipeline internally; on a checksum mismatch the result is marked
// a bad egg, exactly as GetBoxMonData() marks it, and *checksumOk (if
// non-null) is set false.
PartyPokemon decodePartyPokemon(const QByteArray &raw100, bool *checksumOk = nullptr);

// Combines every region in `raw` into a BattleSnapshot. Each region is
// identified by resolving its gbaAddr through `symbols` (reverse lookup by
// address) rather than by position, so this tolerates regions arriving in
// any order or a future bridge version adding new ones. Regions whose
// address doesn't resolve to a known symbol are ignored. The result's
// `valid` is true iff the gBattleMons region was present and decodable;
// every other region missing is listed in `missingRegions`.
BattleSnapshot decodeSnapshot(const RawSnapshot &raw, const SymbolTable &symbols, const BattleLayouts &layouts);

} // namespace emerald
