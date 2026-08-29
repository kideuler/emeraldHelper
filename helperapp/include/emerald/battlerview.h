#pragma once

#include "emerald/battlesnapshot.h"
#include "emerald/nametable.h"

#include <QString>
#include <QVector>

// Builds the data for the requested GUI layout: 2 columns (4 in a double
// battle), one per battler, with name/HP/move-damage-ranges/stats rows.
// Pure function of a BattleSnapshot -- no QWidget here -- so column
// ordering, the ally/foe pairing used for move damage, and formatting are
// all testable without a GUI event loop (Phase 2's testing philosophy
// applied to the view layer too).
namespace emerald {

struct MoveDisplay {
    bool present = false; // false: this move slot is empty (MOVE_NONE)
    QString name;
    bool hasDamageRange = false; // false: status move (0 power) or no valid target
    bool isImmune = false;
    int minDamage = 0;
    int maxDamage = 0;
    double minPercent = 0.0; // % of the target's max HP
    double maxPercent = 0.0;
};

struct BattlerColumn {
    quint8 battlerId = 0;
    bool present = false; // false: absent/fainted/never sent out -- render as empty
    QString name;
    quint16 hp = 0;
    quint16 maxHp = 0;
    MoveDisplay moves[kMaxMonMoves];
    quint8 level = 0;
    QString typeText; // "FIRE/FLYING" or just "FIRE" for mono-type
    QString abilityText;
    QString statusText; // "OK", "BRN", "PSN", ...
    quint16 attack = 0;
    quint16 defense = 0;
    quint16 spAttack = 0;
    quint16 spDefense = 0;
    quint16 speed = 0;
};

struct BattlerGrid {
    bool isDoubleBattle = false;
    // 2 entries (singles) or 4 (doubles), in display order: ally-left,
    // ally-right, foe-left, foe-right (right-side entries only present in
    // doubles) -- see BuildBattlerGrid()'s comment for why.
    QVector<BattlerColumn> columns;
};

// Column battlerId ^ BIT_SIDE (see constants/battle.h) is used as that
// column's move-damage opponent -- i.e. battler 0 vs 1, battler 2 vs 3,
// the "directly across the field" pairing. Assumes battler id == battle
// position, true for standard (non-link) battles -- the same assumption
// CalculateBaseDamage()'s own double-battle handling makes elsewhere in
// this project (see emerald_calc.h).
BattlerGrid BuildBattlerGrid(const BattleSnapshot &snap, const NameTable &names);

} // namespace emerald
