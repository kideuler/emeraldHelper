#pragma once

#include <QMetaType>
#include <QStringList>
#include <QtGlobal>

extern "C" {
#include "battle_state.h"
}

// Phase 4.2 decode target. Value types only -- no sockets, no globals, no
// Qt widgets -- so decode() (decoder.h) is a pure function these tests can
// hammer offline, per Phase 2's testability requirement.
namespace emerald {

constexpr int kMaxBattlers = MAX_BATTLERS_COUNT;
constexpr int kMaxMonMoves = MAX_MON_MOVES;
constexpr int kNumBattleStats = NUM_BATTLE_STATS;
constexpr int kPartySize = PARTY_SIZE;

struct BattleSnapshot {
    BattleSnapshot() { BattleState_Init(&state); }

    // False until a first fully-decoded frame has been applied. Phase 4's
    // "stale UI" gotcha: the UI should treat this the same as a dropped
    // bridge connection, not render zeroed placeholder data.
    bool valid = false;

    // The game's battle globals, mirrored one for one (calc/include/
    // battle_state.h) -- exactly what the calculator runs against.
    BattleState state;

    // Regions the calc reads that this frame didn't carry -- an older
    // lua/emerald_bridge.lua, or a pointer (gBattleResources,
    // gSaveBlock1Ptr) that couldn't be followed. Their part of `state` is
    // left at its BattleState_Init() default, so anything they feed into
    // (weather, screens, badge boosts, ...) is missing from the numbers;
    // the UI surfaces this list rather than presenting those numbers as
    // exact.
    QStringList missingRegions;

    bool inBattle() const { return valid && state.battleTypeFlags != 0; }
};

} // namespace emerald

Q_DECLARE_METATYPE(emerald::BattleSnapshot)
