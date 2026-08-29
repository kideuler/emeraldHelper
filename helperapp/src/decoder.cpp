#include "emerald/decoder.h"

#include "emerald/byteio.h"
#include "emerald/partydecoder.h"

namespace emerald {

namespace {

template <typename T, int N>
void readArray(const QByteArray &raw, const StructLayout &layout, const QString &field, T (&out)[N])
{
    const int off = layout.offset(field);
    const int elem = layout.elemSize(field);
    for (int i = 0; i < N && i < layout.count(field); ++i) {
        if (elem == 1)
            out[i] = T(rd8(raw, off + i * elem));
        else if (elem == 2)
            out[i] = T(rd16(raw, off + i * elem));
        else
            out[i] = T(rd32(raw, off + i * elem));
    }
}

} // namespace

BattleMon decodeBattleMon(const QByteArray &raw, const StructLayout &layout)
{
    BattleMon mon;
    mon.species = rd16(raw, layout.offset("species"));
    mon.attack = rd16(raw, layout.offset("attack"));
    mon.defense = rd16(raw, layout.offset("defense"));
    mon.speed = rd16(raw, layout.offset("speed"));
    mon.spAttack = rd16(raw, layout.offset("spAttack"));
    mon.spDefense = rd16(raw, layout.offset("spDefense"));
    mon.hp = rd16(raw, layout.offset("hp"));
    mon.maxHp = rd16(raw, layout.offset("maxHP"));
    mon.ability = rd8(raw, layout.offset("ability"));
    mon.level = rd8(raw, layout.offset("level"));
    mon.status1 = rd32(raw, layout.offset("status1"));

    readArray(raw, layout, "moves", mon.moves);
    readArray(raw, layout, "pp", mon.pp);
    readArray(raw, layout, "statStages", mon.statStages);

    const int typesOff = layout.offset("types");
    mon.type1 = rd8(raw, typesOff);
    mon.type2 = rd8(raw, typesOff + 1);

    return mon;
}

PartyMon decodePartyMon(const QByteArray &raw100)
{
    PartyMon mon;
    if (raw100.size() != 100)
        return mon;

    const quint32 personality = rd32(raw100, 0);
    const quint32 otId = rd32(raw100, 4);
    const quint8 boxFlags = rd8(raw100, 19);
    const bool boxIsEgg = ((boxFlags >> 2) & 0x1) != 0;
    const quint16 storedChecksum = rd16(raw100, 28);
    const QByteArray secure = raw100.mid(32, 48);

    const QByteArray decrypted = decryptAndUnshuffle(secure, personality, otId);
    const quint16 checksum = calculateChecksum(decrypted);
    const DecryptedSubstructs subs = parseSubstructs(decrypted);

    mon.valid = (checksum == storedChecksum) && subs.species != 0;
    mon.isEgg = boxIsEgg;
    mon.species = subs.species;
    for (int i = 0; i < 4; ++i) {
        mon.moves[i] = subs.moves[i];
        mon.pp[i] = subs.pp[i];
    }

    mon.status = rd32(raw100, 80);
    mon.level = rd8(raw100, 84);
    mon.hp = rd16(raw100, 86);
    mon.maxHp = rd16(raw100, 88);
    mon.attack = rd16(raw100, 90);
    mon.defense = rd16(raw100, 92);
    mon.speed = rd16(raw100, 94);
    mon.spAttack = rd16(raw100, 96);
    mon.spDefense = rd16(raw100, 98);

    return mon;
}

namespace {

// Regions arrive addressed, not positioned, so this must resolve each one
// through the symbol table rather than assuming a fixed slot -- see
// decoder.h's contract note.
const RawRegion *findRegion(const RawSnapshot &raw, const SymbolTable &symbols, const char *name)
{
    if (!symbols.contains(QLatin1String(name)))
        return nullptr;
    const quint32 addr = symbols.address(QLatin1String(name));
    for (const RawRegion &r : raw.regions) {
        if (r.addr == addr)
            return &r;
    }
    return nullptr;
}

} // namespace

BattleSnapshot decodeSnapshot(const RawSnapshot &raw, const SymbolTable &symbols,
                               const StructLayout &monLayout)
{
    BattleSnapshot snap;

    const RawRegion *battleMons = findRegion(raw, symbols, "gBattleMons");
    if (!battleMons || battleMons->bytes.size() < monLayout.totalSize() * kMaxBattlers)
        return snap; // leave valid=false: no usable gBattleMons region

    for (int i = 0; i < kMaxBattlers; ++i) {
        const QByteArray one = battleMons->bytes.mid(i * monLayout.totalSize(), monLayout.totalSize());
        snap.mons[i] = decodeBattleMon(one, monLayout);
    }

    if (const RawRegion *r = findRegion(raw, symbols, "gBattleTypeFlags"); r && r->bytes.size() >= 4)
        snap.typeFlags = rd32(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerAttacker"); r && r->bytes.size() >= 1)
        snap.battlerAttacker = rd8(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerTarget"); r && r->bytes.size() >= 1)
        snap.battlerTarget = rd8(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gAbsentBattlerFlags"); r && r->bytes.size() >= 1)
        snap.absentBattlerFlags = rd8(r->bytes, 0);
    if (const RawRegion *r = findRegion(raw, symbols, "gBattlerPartyIndexes"); r && r->bytes.size() >= 8) {
        for (int i = 0; i < kMaxBattlers; ++i)
            snap.battlerPartyIndexes[i] = rd16(r->bytes, i * 2);
    }

    if (const RawRegion *r = findRegion(raw, symbols, "gPlayerParty"); r) {
        for (int i = 0; i < kPartySize && (i + 1) * 100 <= r->bytes.size(); ++i)
            snap.playerParty[i] = decodePartyMon(r->bytes.mid(i * 100, 100));
    }
    if (const RawRegion *r = findRegion(raw, symbols, "gEnemyParty"); r) {
        for (int i = 0; i < kPartySize && (i + 1) * 100 <= r->bytes.size(); ++i)
            snap.enemyParty[i] = decodePartyMon(r->bytes.mid(i * 100, 100));
    }

    snap.valid = true;
    return snap;
}

} // namespace emerald
