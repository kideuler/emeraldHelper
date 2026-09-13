#include "emerald/decoder.h"

#include "emerald/byteio.h"
#include "emerald/partydecoder.h"

namespace emerald {

BattleLayouts BattleLayouts::loadFromFile(const QString &path, QString *error)
{
    BattleLayouts l;
    const struct {
        StructLayout *out;
        const char *name;
    } structs[] = {
        {&l.battlePokemon, "BattlePokemon"},
        {&l.disableStruct, "DisableStruct"},
        {&l.protectStruct, "ProtectStruct"},
        {&l.sideTimer, "SideTimer"},
        {&l.battleEnigmaBerry, "BattleEnigmaBerry"},
        {&l.battleResources, "BattleResources"},
        {&l.resourceFlags, "ResourceFlags"},
        {&l.saveBlock1, "SaveBlock1"},
    };
    for (const auto &s : structs) {
        *s.out = StructLayout::loadFromFile(path, QLatin1String(s.name), error);
        if (!s.out->isValid())
            return BattleLayouts();
    }
    return l;
}

bool BattleLayouts::isValid() const
{
    return battlePokemon.isValid() && disableStruct.isValid() && protectStruct.isValid() && sideTimer.isValid()
        && battleEnigmaBerry.isValid() && battleResources.isValid() && resourceFlags.isValid() && saveBlock1.isValid();
}

// Field-by-field reads into the C mirror structs, by the name the field
// has in both the decomp and battle_state.h.
#define READ(out, layout, raw, base, field) \
    (out).field = decltype((out).field)((layout).read((raw), (base), QStringLiteral(#field)))
#define READ_ARRAY(out, layout, raw, base, field)                                           \
    for (int i_ = 0; i_ < int(sizeof((out).field) / sizeof((out).field[0])); ++i_)          \
        (out).field[i_] = decltype(+(out).field[0])((layout).read((raw), (base), QStringLiteral(#field), i_))

BattlePokemon decodeBattlePokemon(const QByteArray &raw, const StructLayout &l, int base)
{
    BattlePokemon mon = {};
    READ(mon, l, raw, base, species);
    READ(mon, l, raw, base, attack);
    READ(mon, l, raw, base, defense);
    READ(mon, l, raw, base, speed);
    READ(mon, l, raw, base, spAttack);
    READ(mon, l, raw, base, spDefense);
    READ_ARRAY(mon, l, raw, base, moves);
    READ(mon, l, raw, base, hpIV);
    READ(mon, l, raw, base, attackIV);
    READ(mon, l, raw, base, defenseIV);
    READ(mon, l, raw, base, speedIV);
    READ(mon, l, raw, base, spAttackIV);
    READ(mon, l, raw, base, spDefenseIV);
    READ(mon, l, raw, base, isEgg);
    READ(mon, l, raw, base, abilityNum);
    READ_ARRAY(mon, l, raw, base, statStages);
    READ(mon, l, raw, base, ability);
    READ_ARRAY(mon, l, raw, base, types);
    READ(mon, l, raw, base, unknown);
    READ_ARRAY(mon, l, raw, base, pp);
    READ(mon, l, raw, base, hp);
    READ(mon, l, raw, base, level);
    READ(mon, l, raw, base, friendship);
    mon.maxHP = quint16(l.read(raw, base, QStringLiteral("maxHP")));
    READ(mon, l, raw, base, item);
    READ_ARRAY(mon, l, raw, base, nickname);
    READ(mon, l, raw, base, ppBonuses);
    READ_ARRAY(mon, l, raw, base, otName);
    READ(mon, l, raw, base, experience);
    READ(mon, l, raw, base, personality);
    READ(mon, l, raw, base, status1);
    READ(mon, l, raw, base, status2);
    READ(mon, l, raw, base, otId);
    return mon;
}

PartyPokemon decodePartyPokemon(const QByteArray &raw100, bool *checksumOk)
{
    PartyPokemon mon = {};
    if (checksumOk)
        *checksumOk = false;
    if (raw100.size() != 100)
        return mon;

    mon.personality = rd32(raw100, 0);
    mon.otId = rd32(raw100, 4);
    const quint8 boxFlags = rd8(raw100, 19);
    mon.isBadEgg = boxFlags & 0x1;
    mon.hasSpecies = (boxFlags >> 1) & 0x1;
    const quint16 storedChecksum = rd16(raw100, 28);

    const QByteArray decrypted = decryptAndUnshuffle(raw100.mid(32, 48), mon.personality, mon.otId);
    const DecryptedSubstructs subs = parseSubstructs(decrypted);
    const bool ok = calculateChecksum(decrypted) == storedChecksum;
    if (checksumOk)
        *checksumOk = ok;

    mon.species = subs.species;
    mon.heldItem = subs.heldItem;
    mon.experience = subs.experience;
    mon.ppBonuses = subs.ppBonuses;
    mon.friendship = subs.friendship;
    for (int i = 0; i < kMaxMonMoves; ++i) {
        mon.moves[i] = subs.moves[i];
        mon.pp[i] = subs.pp[i];
    }
    mon.hpIV = subs.hpIV;
    mon.attackIV = subs.attackIV;
    mon.defenseIV = subs.defenseIV;
    mon.speedIV = subs.speedIV;
    mon.spAttackIV = subs.spAttackIV;
    mon.spDefenseIV = subs.spDefenseIV;
    mon.isEgg = subs.isEgg;
    mon.abilityNum = subs.abilityNum;

    // GetBoxMonData(): a checksum mismatch turns the mon into a Bad Egg.
    if (!ok) {
        mon.isBadEgg = 1;
        mon.isEgg = 1;
    }

    mon.status = rd32(raw100, 80);
    mon.level = rd8(raw100, 84);
    mon.hp = rd16(raw100, 86);
    mon.maxHP = rd16(raw100, 88);
    mon.attack = rd16(raw100, 90);
    mon.defense = rd16(raw100, 92);
    mon.speed = rd16(raw100, 94);
    mon.spAttack = rd16(raw100, 96);
    mon.spDefense = rd16(raw100, 98);
    return mon;
}

namespace {

const RawRegion *findRegionAt(const RawSnapshot &raw, quint32 addr, int minSize)
{
    for (const RawRegion &r : raw.regions) {
        if (r.addr == addr && r.bytes.size() >= minSize)
            return &r;
    }
    return nullptr;
}

// Regions arrive addressed, not positioned, so this must resolve each one
// through the symbol table rather than assuming a fixed slot -- see
// decoder.h's contract note.
const RawRegion *findRegion(const RawSnapshot &raw, const SymbolTable &symbols, const char *name, int minSize)
{
    if (!symbols.contains(QLatin1String(name)))
        return nullptr;
    return findRegionAt(raw, symbols.address(QLatin1String(name)), minSize);
}

void decodeDisableStruct(const QByteArray &raw, int base, const StructLayout &l, DisableStruct &d)
{
    READ(d, l, raw, base, transformedMonPersonality);
    READ(d, l, raw, base, disabledMove);
    READ(d, l, raw, base, encoredMove);
    READ(d, l, raw, base, protectUses);
    READ(d, l, raw, base, stockpileCounter);
    READ(d, l, raw, base, substituteHP);
    READ(d, l, raw, base, disableTimer);
    READ(d, l, raw, base, disableTimerStartValue);
    READ(d, l, raw, base, encoredMovePos);
    READ(d, l, raw, base, filler_D);
    READ(d, l, raw, base, encoreTimer);
    READ(d, l, raw, base, encoreTimerStartValue);
    READ(d, l, raw, base, perishSongTimer);
    READ(d, l, raw, base, perishSongTimerStartValue);
    READ(d, l, raw, base, furyCutterCounter);
    READ(d, l, raw, base, rolloutTimer);
    READ(d, l, raw, base, rolloutTimerStartValue);
    READ(d, l, raw, base, chargeTimer);
    READ(d, l, raw, base, chargeTimerStartValue);
    READ(d, l, raw, base, tauntTimer);
    READ(d, l, raw, base, tauntTimer2);
    READ(d, l, raw, base, battlerPreventingEscape);
    READ(d, l, raw, base, battlerWithSureHit);
    READ(d, l, raw, base, isFirstTurn);
    READ(d, l, raw, base, filler_17);
    READ(d, l, raw, base, truantCounter);
    READ(d, l, raw, base, truantSwitchInHack);
    READ(d, l, raw, base, filler_18_2);
    READ(d, l, raw, base, mimickedMoves);
    READ(d, l, raw, base, rechargeTimer);
}

void decodeProtectStruct(const QByteArray &raw, int base, const StructLayout &l, ProtectStruct &p)
{
    p.protected_ = l.read(raw, base, QStringLiteral("protected"));
    READ(p, l, raw, base, endured);
    READ(p, l, raw, base, noValidMoves);
    READ(p, l, raw, base, helpingHand);
    READ(p, l, raw, base, bounceMove);
    READ(p, l, raw, base, stealMove);
    READ(p, l, raw, base, flag0Unknown);
    READ(p, l, raw, base, prlzImmobility);
    READ(p, l, raw, base, confusionSelfDmg);
    READ(p, l, raw, base, targetNotAffected);
    READ(p, l, raw, base, chargingTurn);
    READ(p, l, raw, base, fleeType);
    READ(p, l, raw, base, usedImprisonedMove);
    READ(p, l, raw, base, loveImmobility);
    READ(p, l, raw, base, usedDisabledMove);
    READ(p, l, raw, base, usedTauntedMove);
    READ(p, l, raw, base, flag2Unknown);
    READ(p, l, raw, base, flinchImmobility);
    READ(p, l, raw, base, notFirstStrike);
    READ(p, l, raw, base, palaceUnableToUseMove);
    READ(p, l, raw, base, physicalDmg);
    READ(p, l, raw, base, specialDmg);
    READ(p, l, raw, base, physicalBattlerId);
    READ(p, l, raw, base, specialBattlerId);
}

void decodeSideTimer(const QByteArray &raw, int base, const StructLayout &l, SideTimer &t)
{
    READ(t, l, raw, base, reflectTimer);
    READ(t, l, raw, base, reflectBattlerId);
    READ(t, l, raw, base, lightscreenTimer);
    READ(t, l, raw, base, lightscreenBattlerId);
    READ(t, l, raw, base, mistTimer);
    READ(t, l, raw, base, mistBattlerId);
    READ(t, l, raw, base, safeguardTimer);
    READ(t, l, raw, base, safeguardBattlerId);
    READ(t, l, raw, base, followmeTimer);
    READ(t, l, raw, base, followmeTarget);
    READ(t, l, raw, base, spikesAmount);
}

void decodeEnigmaBerry(const QByteArray &raw, int base, const StructLayout &l, BattleEnigmaBerry &e)
{
    READ_ARRAY(e, l, raw, base, name);
    READ(e, l, raw, base, holdEffect);
    READ_ARRAY(e, l, raw, base, itemEffect);
    READ(e, l, raw, base, holdEffectParam);
}

// An array global of `count` structs of `layout`'s size, decoded with
// `decodeOne`; records `name` as missing if the region isn't there.
template <typename T, int N, typename Fn>
void decodeStructArray(const RawSnapshot &raw, const SymbolTable &symbols, const char *name,
                       const StructLayout &layout, T (&out)[N], Fn decodeOne, QStringList &missing)
{
    const RawRegion *r = findRegion(raw, symbols, name, layout.totalSize() * N);
    if (!r) {
        missing << QLatin1String(name);
        return;
    }
    for (int i = 0; i < N; ++i)
        decodeOne(r->bytes, i * layout.totalSize(), layout, out[i]);
}

} // namespace

BattleSnapshot decodeSnapshot(const RawSnapshot &raw, const SymbolTable &symbols, const BattleLayouts &layouts)
{
    BattleSnapshot snap;
    BattleState &s = snap.state;
    QStringList &missing = snap.missingRegions;

    const StructLayout &monLayout = layouts.battlePokemon;
    const RawRegion *battleMons = findRegion(raw, symbols, "gBattleMons", monLayout.totalSize() * kMaxBattlers);
    if (!battleMons)
        return snap; // leave valid=false: no usable gBattleMons region

    for (int i = 0; i < kMaxBattlers; ++i)
        s.battleMons[i] = decodeBattlePokemon(battleMons->bytes, monLayout, i * monLayout.totalSize());

    // Scalars and plain arrays.
    if (const RawRegion *r = findRegion(raw, symbols, "gBattleTypeFlags", 4))
        s.battleTypeFlags = rd32(r->bytes, 0);
    else
        missing << QStringLiteral("gBattleTypeFlags");
    if (const RawRegion *r = findRegion(raw, symbols, "gBattleEnvironment", 1))
        s.battleEnvironment = rd8(r->bytes, 0);
    else
        missing << QStringLiteral("gBattleEnvironment");
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlersCount", 1)) {
        s.battlersCount = rd8(r->bytes, 0);
    } else {
        missing << QStringLiteral("gBattlersCount");
        s.battlersCount = (s.battleTypeFlags & BATTLE_TYPE_DOUBLE) ? 4 : 2; // what the game sets it to
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerPositions", kMaxBattlers)) {
        for (int i = 0; i < kMaxBattlers; ++i)
            s.battlerPositions[i] = rd8(r->bytes, i);
    } else {
        missing << QStringLiteral("gBattlerPositions"); // BattleState_Init()'s identity mapping stands
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerPartyIndexes", 2 * kMaxBattlers)) {
        for (int i = 0; i < kMaxBattlers; ++i)
            s.battlerPartyIndexes[i] = rd16(r->bytes, i * 2);
    } else {
        missing << QStringLiteral("gBattlerPartyIndexes");
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gAbsentBattlerFlags", 1))
        s.absentBattlerFlags = rd8(r->bytes, 0);
    else
        missing << QStringLiteral("gAbsentBattlerFlags");
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerAttacker", 1))
        s.battlerAttacker = rd8(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerTarget", 1))
        s.battlerTarget = rd8(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gBattleWeather", 2))
        s.battleWeather = rd16(r->bytes, 0);
    else
        missing << QStringLiteral("gBattleWeather");
    if (const RawRegion *r = findRegion(raw, symbols, "gSideStatuses", 2 * NUM_BATTLE_SIDES)) {
        for (int i = 0; i < NUM_BATTLE_SIDES; ++i)
            s.sideStatuses[i] = rd16(r->bytes, i * 2);
    } else {
        missing << QStringLiteral("gSideStatuses");
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gStatuses3", 4 * kMaxBattlers)) {
        for (int i = 0; i < kMaxBattlers; ++i)
            s.statuses3[i] = rd32(r->bytes, i * 4);
    } else {
        missing << QStringLiteral("gStatuses3");
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gTrainerBattleOpponent_A", 2))
        s.trainerBattleOpponentA = rd16(r->bytes, 0);
    else
        missing << QStringLiteral("gTrainerBattleOpponent_A");

    // Struct arrays.
    decodeStructArray(raw, symbols, "gSideTimers", layouts.sideTimer, s.sideTimers, decodeSideTimer, missing);
    decodeStructArray(raw, symbols, "gDisableStructs", layouts.disableStruct, s.disableStructs, decodeDisableStruct, missing);
    decodeStructArray(raw, symbols, "gProtectStructs", layouts.protectStruct, s.protectStructs, decodeProtectStruct, missing);
    decodeStructArray(raw, symbols, "gEnigmaBerries", layouts.battleEnigmaBerry, s.enigmaBerries, decodeEnigmaBerry, missing);

    // gBattleResources->flags->flags[]: the bridge sends the pointer
    // variable, the BattleResources it points at, and the ResourceFlags
    // *that* points at, each addressed by where it lives in RAM.
    bool haveResourceFlags = false;
    if (const RawRegion *ptr = findRegion(raw, symbols, "gBattleResources", 4)) {
        const StructLayout &res = layouts.battleResources;
        if (const RawRegion *resources = findRegionAt(raw, rd32(ptr->bytes, 0), res.offset(QStringLiteral("flags")) + 4)) {
            const quint32 flagsAddr = res.read(resources->bytes, 0, QStringLiteral("flags"));
            if (const RawRegion *flags = findRegionAt(raw, flagsAddr, layouts.resourceFlags.totalSize())) {
                for (int i = 0; i < kMaxBattlers; ++i)
                    s.resourceFlags[i] = layouts.resourceFlags.read(flags->bytes, 0, QStringLiteral("flags"), i);
                haveResourceFlags = true;
            }
        }
    }
    if (!haveResourceFlags)
        missing << QStringLiteral("gBattleResources->flags");

    // gSaveBlock1Ptr->flags[] (badges): pointer variable, then the flag
    // array at pointer + offsetof(struct SaveBlock1, flags).
    bool haveSaveFlags = false;
    if (const RawRegion *ptr = findRegion(raw, symbols, "gSaveBlock1Ptr", 4)) {
        const StructLayout &sb1 = layouts.saveBlock1;
        const quint32 flagsAddr = rd32(ptr->bytes, 0) + quint32(sb1.offset(QStringLiteral("flags")));
        if (const RawRegion *flags = findRegionAt(raw, flagsAddr, NUM_FLAG_BYTES)) {
            for (int i = 0; i < NUM_FLAG_BYTES; ++i)
                s.saveFlags[i] = rd8(flags->bytes, i);
            haveSaveFlags = true;
        }
    }
    if (!haveSaveFlags)
        missing << QStringLiteral("gSaveBlock1Ptr->flags");

    // Parties.
    if (const RawRegion *r = findRegion(raw, symbols, "gPlayerParty", 100 * kPartySize)) {
        for (int i = 0; i < kPartySize; ++i)
            s.playerParty[i] = decodePartyPokemon(r->bytes.mid(i * 100, 100));
    } else {
        missing << QStringLiteral("gPlayerParty");
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gEnemyParty", 100 * kPartySize)) {
        for (int i = 0; i < kPartySize; ++i)
            s.enemyParty[i] = decodePartyPokemon(r->bytes.mid(i * 100, 100));
    } else {
        missing << QStringLiteral("gEnemyParty");
    }

    snap.valid = true;
    return snap;
}

} // namespace emerald
