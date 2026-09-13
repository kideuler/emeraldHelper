#include "emerald/battlerview.h"

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

QString FormatStages(const BattlePokemon &mon)
{
    static const char *const kStatNames[NUM_BATTLE_STATS] = {"HP", "Atk", "Def", "Spe", "SpA", "SpD", "Acc", "Eva"};
    QStringList parts;
    for (int i = STAT_ATK; i < NUM_BATTLE_STATS; ++i) {
        const int stage = mon.statStages[i] - DEFAULT_STAT_STAGE;
        if (stage != 0)
            parts << QStringLiteral("%1 %2%3").arg(QLatin1String(kStatNames[i])).arg(stage > 0 ? "+" : "").arg(stage);
    }
    return parts.join(QStringLiteral(", "));
}

QString FormatField(const BattleState &s)
{
    QStringList parts;
    if (s.battleWeather & B_WEATHER_RAIN)
        parts << QStringLiteral("Rain");
    else if (s.battleWeather & B_WEATHER_SANDSTORM)
        parts << QStringLiteral("Sandstorm");
    else if (s.battleWeather & B_WEATHER_SUN)
        parts << QStringLiteral("Sun");
    else if (s.battleWeather & B_WEATHER_HAIL)
        parts << QStringLiteral("Hail");

    static const char *const kSideNames[NUM_BATTLE_SIDES] = {"Ally", "Foe"};
    for (int side = 0; side < NUM_BATTLE_SIDES; ++side) {
        QStringList effects;
        const quint16 st = s.sideStatuses[side];
        if (st & SIDE_STATUS_REFLECT)
            effects << QStringLiteral("Reflect");
        if (st & SIDE_STATUS_LIGHTSCREEN)
            effects << QStringLiteral("Light Screen");
        if (st & SIDE_STATUS_SAFEGUARD)
            effects << QStringLiteral("Safeguard");
        if (st & SIDE_STATUS_MIST)
            effects << QStringLiteral("Mist");
        if (st & SIDE_STATUS_SPIKES)
            effects << QStringLiteral("Spikes x%1").arg(s.sideTimers[side].spikesAmount);
        if (!effects.isEmpty())
            parts << QStringLiteral("%1: %2").arg(QLatin1String(kSideNames[side]), effects.join(QStringLiteral(", ")));
    }
    for (int i = 0; i < s.battlersCount && i < MAX_BATTLERS_COUNT; ++i) {
        if (s.statuses3[i] & STATUS3_MUDSPORT) {
            parts << QStringLiteral("Mud Sport");
            break;
        }
    }
    for (int i = 0; i < s.battlersCount && i < MAX_BATTLERS_COUNT; ++i) {
        if (s.statuses3[i] & STATUS3_WATERSPORT) {
            parts << QStringLiteral("Water Sport");
            break;
        }
    }
    return parts.join(QStringLiteral(" | "));
}

QString ReasonText(quint8 reason, const NameTable &names)
{
    switch (reason) {
    case EMERALD_REASON_TYPE_IMMUNITY: return QStringLiteral("type immunity");
    case EMERALD_REASON_LEVITATE: return names.ability(ABILITY_LEVITATE);
    case EMERALD_REASON_WONDER_GUARD: return names.ability(ABILITY_WONDER_GUARD);
    case EMERALD_REASON_VOLT_ABSORB: return names.ability(ABILITY_VOLT_ABSORB);
    case EMERALD_REASON_WATER_ABSORB: return names.ability(ABILITY_WATER_ABSORB);
    case EMERALD_REASON_FLASH_FIRE: return names.ability(ABILITY_FLASH_FIRE);
    case EMERALD_REASON_SOUNDPROOF: return names.ability(ABILITY_SOUNDPROOF);
    case EMERALD_REASON_DAMP: return names.ability(ABILITY_DAMP);
    case EMERALD_REASON_STURDY: return names.ability(ABILITY_STURDY);
    case EMERALD_REASON_TARGET_LEVEL_HIGHER: return QStringLiteral("target's level is higher");
    case EMERALD_REASON_NO_STOCKPILE: return QStringLiteral("nothing Stockpiled");
    case EMERALD_REASON_TARGET_HP_NOT_HIGHER: return QStringLiteral("target's HP isn't higher");
    case EMERALD_REASON_USER_NOT_ASLEEP: return QStringLiteral("user isn't asleep");
    case EMERALD_REASON_TARGET_NOT_ASLEEP: return QStringLiteral("target isn't asleep");
    case EMERALD_REASON_TARGET_BEHIND_SUBSTITUTE: return QStringLiteral("target is behind a Substitute");
    case EMERALD_REASON_NOT_FIRST_TURN: return QStringLiteral("only works on the first turn out");
    case EMERALD_REASON_LOST_FOCUS: return QStringLiteral("user was hit this turn");
    case EMERALD_REASON_NOT_HIT_THIS_TURN: return QStringLiteral("2x the damage taken this turn");
    case EMERALD_REASON_BIDE: return QStringLiteral("2x the damage taken while biding");
    case EMERALD_REASON_NO_PARTY_MEMBER_CAN_ATTACK: return QStringLiteral("no party member can attack");
    case EMERALD_REASON_TARGET_PROTECTED: return QStringLiteral("target is protected");
    case EMERALD_REASON_TARGET_SEMI_INVULNERABLE: return QStringLiteral("target is out of reach");
    default: return QString();
    }
}

bool IsBattlerAbsent(const BattleState &s, int battlerId)
{
    return (s.absentBattlerFlags & (1u << battlerId)) != 0;
}

// Fills the four move rows for battler `atk` against battler `def` (which
// may be absent -- moves still show their name, just no damage range).
void FillMoves(const BattleState &s, int atk, bool opponentPresent, int def, const NameTable &names,
               MoveDisplay (&out)[kMaxMonMoves])
{
    const BattlePokemon &mon = s.battleMons[atk];
    const BattlePokemon &opponent = s.battleMons[def];

    for (int i = 0; i < kMaxMonMoves; ++i) {
        MoveDisplay &m = out[i];
        m = MoveDisplay{};
        const quint16 moveId = mon.moves[i];
        if (moveId == MOVE_NONE)
            continue;

        m.present = true;
        m.name = names.move(moveId);

        if (!opponentPresent)
            continue; // name only -- no legal target to roll damage against

        const EmeraldCalcResult r = EmeraldCalc_SimulateMove(&s, quint8(atk), quint8(def), moveId);
        if (r.move != moveId)
            m.details << names.move(r.move); // Nature Power

        switch (r.outcome) {
        case EMERALD_CALC_STATUS_MOVE:
            continue;
        case EMERALD_CALC_NO_EFFECT:
            m.isImmune = true;
            m.note = QStringLiteral("no effect: %1").arg(ReasonText(r.reason, names));
            continue;
        case EMERALD_CALC_FAILS:
            m.note = QStringLiteral("fails: %1").arg(ReasonText(r.reason, names));
            continue;
        case EMERALD_CALC_DAMAGE_UNKNOWN:
            m.note = ReasonText(r.reason, names);
            continue;
        default:
            break;
        }

        s32 minDamage, maxDamage;
        EmeraldCalc_DamageRange(&r, &minDamage, &maxDamage);
        m.hasDamageRange = true;
        m.minDamage = minDamage;
        m.maxDamage = maxDamage;
        if (opponent.maxHP > 0) {
            m.minPercent = 100.0 * minDamage / opponent.maxHP;
            m.maxPercent = 100.0 * maxDamage / opponent.maxHP;
        }
        m.koChance = EmeraldCalc_KOChance(&r, opponent.hp).chance;

        // Power a script computed rather than read from gBattleMoves.
        const quint8 basePower = gBattleMoves[r.move].power;
        if (r.numBranches == 1 && !r.numSequentialHits && r.branches[0].hit.power != 0
            && r.branches[0].hit.power != basePower)
            m.details << QStringLiteral("%1 BP").arg(r.branches[0].hit.power);
        if (r.moveType != gBattleMoves[r.move].type && r.moveType != TYPE_MYSTERY)
            m.details << names.type(r.moveType);
        if (r.numSequentialHits)
            m.details << QStringLiteral("x%1").arg(r.numSequentialHits);
        else if (r.maxHits > 1)
            m.details << (r.minHits == r.maxHits ? QStringLiteral("x%1").arg(r.maxHits)
                                                 : QStringLiteral("x%1-%2").arg(r.minHits).arg(r.maxHits));
        if (r.isOhko)
            m.details << QStringLiteral("OHKO");
        if (r.landsLater)
            m.details << QStringLiteral("lands later");
        if (r.targetHasSubstitute)
            m.details << QStringLiteral("hits Substitute");
        if (r.accuracy == 0.0)
            m.note = ReasonText(r.missReason, names);
    }
}

BattlerColumn BuildColumn(const BattleSnapshot &snap, int battlerId, const NameTable &names)
{
    const BattleState &s = snap.state;
    BattlerColumn col;
    col.battlerId = quint8(battlerId);

    const BattlePokemon &mon = s.battleMons[battlerId];
    col.present = !IsBattlerAbsent(s, battlerId) && mon.species != 0;
    if (!col.present)
        return col;

    col.name = names.species(mon.species);
    col.hp = mon.hp;
    col.maxHp = mon.maxHP;
    col.level = mon.level;
    col.typeText = FormatTypes(names, mon.types[0], mon.types[1]);
    col.abilityText = names.ability(mon.ability);
    col.statusText = FormatStatus(mon.status1);
    col.stagesText = FormatStages(mon);
    col.attack = mon.attack;
    col.defense = mon.defense;
    col.spAttack = mon.spAttack;
    col.spDefense = mon.spDefense;
    col.speed = mon.speed;

    // BATTLE_OPPOSITE(battlerId) -- see battlerview.h's header comment.
    const int opponentId = battlerId ^ BIT_SIDE;
    const bool opponentPresent = !IsBattlerAbsent(s, opponentId) && s.battleMons[opponentId].species != 0;
    FillMoves(s, battlerId, opponentPresent, opponentId, names, col.moves);

    return col;
}

} // namespace

BattlerGrid BuildBattlerGrid(const BattleSnapshot &snap, const NameTable &names)
{
    BattlerGrid grid;
    grid.isDoubleBattle = (snap.state.battleTypeFlags & BATTLE_TYPE_DOUBLE) != 0;
    grid.fieldText = FormatField(snap.state);

    // Ally-left, ally-right, foe-left, foe-right; singles only shows the
    // left pair. Battler ids follow enum BattlerPosition (constants/
    // battle.h): 0/1 = left pair, 2/3 = right pair, even = player side.
    static const int kSinglesOrder[] = {0, 1};
    static const int kDoublesOrder[] = {0, 2, 1, 3};

    const int *order = grid.isDoubleBattle ? kDoublesOrder : kSinglesOrder;
    const int count = grid.isDoubleBattle ? 4 : 2;

    grid.columns.reserve(count);
    for (int i = 0; i < count; ++i)
        grid.columns.append(BuildColumn(snap, order[i], names));

    return grid;
}

} // namespace emerald
