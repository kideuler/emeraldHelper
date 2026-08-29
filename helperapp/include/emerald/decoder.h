#pragma once

#include "emerald/battlesnapshot.h"
#include "emerald/rawsnapshot.h"
#include "emerald/structlayout.h"
#include "emerald/symboltable.h"

// Phase 4.2 / 2: bytes -> typed BattleSnapshot. Pure functions, no sockets,
// no Qt widgets -- this is what the offline fixture tests hammer.
namespace emerald {

// Decodes a single 0x58-byte gBattleMons entry using `layout` (loaded from
// battle_pokemon_layout_us_rev0.json's "BattlePokemon" struct) for field
// offsets instead of hardcoded magic numbers -- `layout` is the Phase 1.4
// contract between the Lua script and this decoder.
BattleMon decodeBattleMon(const QByteArray &raw, const StructLayout &layout);

// Decodes one raw 100-byte Pokemon (party slot) into a PartyMon. Runs the
// Phase 2.2 decrypt/unshuffle/checksum pipeline internally; `valid` on the
// result reflects whether the checksum matched.
PartyMon decodePartyMon(const QByteArray &raw100);

// Combines every region in `raw` into a BattleSnapshot. Each region is
// identified by resolving its gbaAddr through `symbols` (reverse lookup by
// address) rather than by position, so this tolerates regions arriving in
// any order or a future bridge version adding new ones. Regions whose
// address doesn't resolve to a known symbol are ignored. The result's
// `valid` is true iff the gBattleMons region was present and decodable.
BattleSnapshot decodeSnapshot(const RawSnapshot &raw, const SymbolTable &symbols,
                               const StructLayout &monLayout);

} // namespace emerald
