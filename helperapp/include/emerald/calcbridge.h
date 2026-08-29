#pragma once

#include "emerald/battlesnapshot.h"

extern "C" {
#include "emerald_calc.h"
}

// Bridges the Phase 2 decoder's BattleMon (helperapp/include/emerald/
// battlesnapshot.h) into the Phase 5 calc library's EmeraldCalcMon
// (helperapp/calc/include/emerald_calc.h). Field-by-field assignment, not
// a reinterpret -- see emerald_calc.h's header comment on why
// EmeraldCalcMon doesn't mirror BattlePokemon's real memory layout.
namespace emerald {

// hasFocusEnergy defaults false: gBattleMons' status2 field (which holds
// STATUS2_FOCUS_ENERGY) isn't decoded by decodeBattleMon() today, so a
// mon with Focus Energy active will under-report its crit chance here
// until that's added to BattleMon/decoder.cpp.
EmeraldCalcMon ToCalcMon(const BattleMon &mon);

} // namespace emerald
