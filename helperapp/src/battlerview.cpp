#include "emerald/battlerview.h"

#include "emerald/calcbridge.h"
#include "emerald/decoder.h"

extern "C" {
#include "emerald_calc.h"
}

namespace emerald {

namespace {

QString FormatStatus(quint32 status1)
{
    if (status1 & STATUS1_SLEEP)
        return QStringLiteral("SLP");
    if (status1 & STATUS1_TOXIC_POISON)
        return QStringLiteral("TOX");
    if (status1 & STATUS1_POISON)
        return QStringLiteral("PSN");
    if (status1 & STATUS1_BURN)
        return QStringLiteral("BRN");
    if (status1 & STATUS1_FREEZE)
        return QStringLiteral("FRZ");
    if (status1 & STATUS1_PARALYSIS)
        return QStringLiteral("PAR");
    return QStringLiteral("OK");
}

QString FormatTypes(const NameTable &names, quint8 type1, quint8 type2)
{
    if (type1 == type2)
        return names.type(type1);
    return names.type(type1) + QStringLiteral("/") + names.type(type2);
}

bool IsBattlerAbsent(const BattleSnapshot &snap, int battlerId)
{
    return (snap.absentBattlerFlags & (1u << battlerId)) != 0;
}

// Fills the four move rows for `mon` (attacker) against `opponent`
// (defender, may be absent/not present -- moves still show their name,
// just no damage range).
void FillMoves(const BattleMon &mon, bool opponentPresent, const BattleMon &opponent, const NameTable &names,
                bool isDoubleBattle, MoveDisplay (&out)[kMaxMonMoves])
{
    const EmeraldCalcMon calcAttacker = ToCalcMon(mon);
    const EmeraldCalcMon calcDefender = ToCalcMon(opponent);
    EmeraldCalcFieldConditions field = EmeraldCalc_DefaultField();
    field.isDoubleBattle = isDoubleBattle ? TRUE : FALSE;

    for (int i = 0; i < kMaxMonMoves; ++i) {
        MoveDisplay &m = out[i];
        const quint16 moveId = mon.moves[i];
        if (moveId == MOVE_NONE) {
            m = MoveDisplay{};
            continue;
        }

        m.present = true;
        m.name = names.move(moveId);

        if (!opponentPresent)
            continue; // name only -- no legal target to roll damage against

        const EmeraldCalcResult result = EmeraldCalc_ComputeDamage(&calcAttacker, &calcDefender, moveId, &field);
        if (result.movePowerIsZero)
            continue; // status move: a range would be meaningless

        m.hasDamageRange = true;
        m.isImmune = result.isImmune;
        m.minDamage = result.normal.min;
        m.maxDamage = result.normal.max;
        if (opponent.maxHp > 0) {
            m.minPercent = 100.0 * result.normal.min / opponent.maxHp;
            m.maxPercent = 100.0 * result.normal.max / opponent.maxHp;
        }
    }
}

BattlerColumn BuildColumn(const BattleSnapshot &snap, int battlerId, const NameTable &names, bool isDoubleBattle)
{
    BattlerColumn col;
    col.battlerId = quint8(battlerId);

    const BattleMon &mon = snap.mons[battlerId];
    const bool absent = IsBattlerAbsent(snap, battlerId);
    col.present = !absent && mon.species != 0;
    if (!col.present)
        return col;

    col.name = names.species(mon.species);
    col.hp = mon.hp;
    col.maxHp = mon.maxHp;
    col.level = mon.level;
    col.typeText = FormatTypes(names, mon.type1, mon.type2);
    col.abilityText = names.ability(mon.ability);
    col.statusText = FormatStatus(mon.status1);
    col.attack = mon.attack;
    col.defense = mon.defense;
    col.spAttack = mon.spAttack;
    col.spDefense = mon.spDefense;
    col.speed = mon.speed;

    // BATTLE_OPPOSITE(battlerId) -- see battlerview.h's header comment.
    const int opponentId = battlerId ^ BIT_SIDE;
    const bool opponentPresent = !IsBattlerAbsent(snap, opponentId) && snap.mons[opponentId].species != 0;
    FillMoves(mon, opponentPresent, snap.mons[opponentId], names, isDoubleBattle, col.moves);

    return col;
}

} // namespace

BattlerGrid BuildBattlerGrid(const BattleSnapshot &snap, const NameTable &names)
{
    BattlerGrid grid;
    grid.isDoubleBattle = (snap.typeFlags & BATTLE_TYPE_DOUBLE) != 0;

    // Ally-left, ally-right, foe-left, foe-right; singles only shows the
    // left pair. Battler ids follow enum BattlerPosition (constants/
    // battle.h): 0/1 = left pair, 2/3 = right pair, even = player side.
    static const int kSinglesOrder[] = {0, 1};
    static const int kDoublesOrder[] = {0, 2, 1, 3};

    const int *order = grid.isDoubleBattle ? kDoublesOrder : kSinglesOrder;
    const int count = grid.isDoubleBattle ? 4 : 2;

    grid.columns.reserve(count);
    for (int i = 0; i < count; ++i)
        grid.columns.append(BuildColumn(snap, order[i], names, grid.isDoubleBattle));

    return grid;
}

} // namespace emerald
