#pragma once

#include <QMetaType>
#include <QtGlobal>

// Phase 4.2 decode target. Value types only -- no sockets, no globals, no
// Qt widgets -- so decode() (decoder.h) is a pure function these tests can
// hammer offline, per Phase 2's testability requirement.
namespace emerald {

constexpr int kMaxBattlers = 4;
constexpr int kMaxMonMoves = 4;
constexpr int kNumBattleStats = 8;
constexpr int kPartySize = 6;

struct BattleMon {
    quint16 species = 0;
    quint16 attack = 0;
    quint16 defense = 0;
    quint16 speed = 0;
    quint16 spAttack = 0;
    quint16 spDefense = 0;
    quint16 hp = 0;
    quint16 maxHp = 0;
    quint16 moves[kMaxMonMoves] = {};
    quint8 pp[kMaxMonMoves] = {};
    qint8 statStages[kNumBattleStats] = {};
    quint8 type1 = 0;
    quint8 type2 = 0;
    quint8 ability = 0;
    quint8 level = 0;
    quint32 status1 = 0;
    quint16 item = 0;
};

// A benched party member. Phase 2.2: party data is encrypted, so `valid`
// reflects whether the substruct checksum matched -- decode it before
// trusting anything else in here (a torn read mid-write in the emulator,
// or simply an empty slot, both fail the checksum).
struct PartyMon {
    bool valid = false;
    bool isEgg = false;
    quint16 species = 0;
    quint8 level = 0;
    quint16 hp = 0;
    quint16 maxHp = 0;
    quint16 attack = 0;
    quint16 defense = 0;
    quint16 speed = 0;
    quint16 spAttack = 0;
    quint16 spDefense = 0;
    quint16 moves[kMaxMonMoves] = {};
    quint8 pp[kMaxMonMoves] = {};
    quint32 status = 0;
};

struct BattleSnapshot {
    // False until a first fully-decoded frame has been applied. Phase 4's
    // "stale UI" gotcha: the UI should treat this the same as a dropped
    // bridge connection, not render zeroed placeholder data.
    bool valid = false;

    quint32 typeFlags = 0;
    quint8 battlerAttacker = 0;
    quint8 battlerTarget = 0;
    quint8 absentBattlerFlags = 0;
    quint16 battlerPartyIndexes[kMaxBattlers] = {};

    BattleMon mons[kMaxBattlers];
    PartyMon playerParty[kPartySize];
    PartyMon enemyParty[kPartySize];

    bool inBattle() const { return valid && typeFlags != 0; }
};

} // namespace emerald

Q_DECLARE_METATYPE(emerald::BattleSnapshot)
