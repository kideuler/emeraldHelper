#include "emerald/calcbridge.h"

#include <cstring>

namespace emerald {

EmeraldCalcMon ToCalcMon(const BattleMon &mon)
{
    EmeraldCalcMon out;
    std::memset(&out, 0, sizeof(out));

    out.species = mon.species;
    out.attack = mon.attack;
    out.defense = mon.defense;
    out.spAttack = mon.spAttack;
    out.spDefense = mon.spDefense;
    out.hp = mon.hp;
    out.maxHp = mon.maxHp;
    out.ability = mon.ability;
    out.types[0] = mon.type1;
    out.types[1] = mon.type2;
    out.level = mon.level;
    out.item = mon.item;
    out.status1 = mon.status1;
    out.hasFocusEnergy = FALSE;

    for (int i = 0; i < kNumBattleStats && i < NUM_BATTLE_STATS; ++i)
        out.statStages[i] = mon.statStages[i];

    return out;
}

} // namespace emerald
