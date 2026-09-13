/* Ports of the small src/battle_util.c / src/pokemon.c / src/event_data.c
 * helpers the damage code leans on. Each reads struct BattleState where
 * the original reads the corresponding global. */
#include "battle_internal.h"

#include <string.h>

void MoveContext_Init(struct MoveContext *ctx, const struct BattleState *s, u8 battlerAtk, u8 battlerDef, u16 move)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->s = s;
    ctx->battlerAttacker = battlerAtk;
    ctx->battlerTarget = battlerDef;
    ctx->currentMove = move;
    ctx->sideStatuses[B_SIDE_PLAYER] = s->sideStatuses[B_SIDE_PLAYER];
    ctx->sideStatuses[B_SIDE_OPPONENT] = s->sideStatuses[B_SIDE_OPPONENT];
    ctx->critMultiplier = 1;
    ctx->dmgMultiplier = 1;
}

double RandomModBelow(u32 mod, u32 threshold)
{
    /* Of the 65536 values Random() can return, 65536 / mod full cycles
     * cover every residue equally; the 65536 % mod leftover values are
     * residues 0 .. (65536 % mod) - 1, one extra each. */
    const u32 values = 0x10000;
    u32 full = values / mod, extra = values % mod, count;

    if (threshold > mod)
        threshold = mod;
    count = full * threshold + (threshold < extra ? threshold : extra);
    return (double)count / values;
}

double RandomModEquals(u32 mod, u32 value)
{
    return RandomModBelow(mod, value + 1) - RandomModBelow(mod, value);
}

/* GetBattlerSide(): gBattlerPositions[battler] & BIT_SIDE. */
u8 GetBattlerSide(const struct BattleState *s, u8 battler)
{
    return s->battlerPositions[battler] & BIT_SIDE;
}

bool8 IsBattlerOfType(const struct BattleState *s, u8 battler, u8 type)
{
    return s->battleMons[battler].types[0] == type || s->battleMons[battler].types[1] == type;
}

/* AbilityBattleEffects(ABILITYEFFECT_CHECK_ON_FIELD, 0, ability, 0, 0). */
bool8 AbilityOnField(const struct BattleState *s, u8 ability)
{
    u8 i;
    for (i = 0; i < s->battlersCount; i++) {
        if (s->battleMons[i].ability == ability && s->battleMons[i].hp != 0)
            return TRUE;
    }
    return FALSE;
}

/* AbilityBattleEffects(ABILITYEFFECT_FIELD_SPORT, 0, ability, 0, 0) --
 * its `default:` case, which unlike ABILITYEFFECT_CHECK_ON_FIELD doesn't
 * check HP, so a fainted Cloud Nine/Air Lock/Plus/Minus still counts. */
bool8 AbilityOnField2(const struct BattleState *s, u8 ability)
{
    u8 i;
    for (i = 0; i < s->battlersCount; i++) {
        if (s->battleMons[i].ability == ability)
            return TRUE;
    }
    return FALSE;
}

/* AbilityBattleEffects(ABILITYEFFECT_FIELD_SPORT, 0, 0,
 * ABILITYEFFECT_MUD_SPORT / ABILITYEFFECT_WATER_SPORT, 0). */
bool8 FieldSportActive(const struct BattleState *s, u32 status3Sport)
{
    u8 i;
    for (i = 0; i < s->battlersCount; i++) {
        if (s->statuses3[i] & status3Sport)
            return TRUE;
    }
    return FALSE;
}

bool8 WeatherHasEffect(const struct BattleState *s)
{
    return !AbilityOnField(s, ABILITY_CLOUD_NINE) && !AbilityOnField(s, ABILITY_AIR_LOCK);
}

bool8 WeatherHasEffect2(const struct BattleState *s)
{
    return !AbilityOnField2(s, ABILITY_CLOUD_NINE) && !AbilityOnField2(s, ABILITY_AIR_LOCK);
}

/* CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE) (src/pokemon.c). Note it
 * loops MAX_BATTLERS_COUNT, not gBattlersCount, and "alive" means "not in
 * gAbsentBattlerFlags". */
u8 CountAliveMonsInBattleDefSide(const struct BattleState *s, u8 battlerTarget)
{
    u8 i, retVal = 0;
    for (i = 0; i < MAX_BATTLERS_COUNT; i++) {
        if (GetBattlerSide(s, i) == GetBattlerSide(s, battlerTarget) && !(s->absentBattlerFlags & (1u << i)))
            retVal++;
    }
    return retVal;
}

/* FlagGet() (src/event_data.c), for the save-block flag range only --
 * which is all ShouldGetStatBadgeBoost() asks about. */
bool8 FlagGet(const struct BattleState *s, u16 id)
{
    if (id == 0 || id / 8 >= NUM_FLAG_BYTES)
        return FALSE;
    return (s->saveFlags[id / 8] >> (id & 7)) & 1;
}

/* src/pokemon.c */
bool8 ShouldGetStatBadgeBoost(const struct BattleState *s, u16 badgeFlag, u8 battler)
{
    if (s->battleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_EREADER_TRAINER | BATTLE_TYPE_RECORDED_LINK | BATTLE_TYPE_FRONTIER))
        return FALSE;
    else if (GetBattlerSide(s, battler) != B_SIDE_PLAYER)
        return FALSE;
    else if (s->battleTypeFlags & BATTLE_TYPE_TRAINER && s->trainerBattleOpponentA == TRAINER_SECRET_BASE)
        return FALSE;
    else if (FlagGet(s, badgeFlag))
        return TRUE;
    else
        return FALSE;
}

void GetBattlerHoldEffect(const struct BattleState *s, u8 battler, u8 *holdEffect, u8 *holdEffectParam)
{
    u16 item = s->battleMons[battler].item;

    if (item == ITEM_ENIGMA_BERRY) {
        *holdEffect = s->enigmaBerries[battler].holdEffect;
        *holdEffectParam = s->enigmaBerries[battler].holdEffectParam;
    } else {
        *holdEffect = GetItemHoldEffect(item);
        *holdEffectParam = GetItemHoldEffectParam(item);
    }
}

/* src/battle_interface.c */
u8 GetScaledHPFraction(s16 hp, s16 maxhp, u8 scale)
{
    u8 result = hp * scale / maxhp;

    if (result == 0 && hp > 0)
        return 1;

    return result;
}

/* src/battle_util.c sSoundMovesTable, the list Soundproof blocks. */
static const u16 sSoundMovesTable[] =
{
    MOVE_GROWL, MOVE_ROAR, MOVE_SING, MOVE_SUPERSONIC, MOVE_SCREECH, MOVE_SNORE,
    MOVE_UPROAR, MOVE_METAL_SOUND, MOVE_GRASS_WHISTLE, MOVE_HYPER_VOICE, 0xFFFF /* SOUND_MOVES_END */
};

bool8 IsSoundMove(u16 move)
{
    int i;
    for (i = 0; sSoundMovesTable[i] != 0xFFFF; i++) {
        if (sSoundMovesTable[i] == move)
            return TRUE;
    }
    return FALSE;
}
