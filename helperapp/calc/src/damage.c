/* Ports of the damage pipeline: CalculateBaseDamage() (src/pokemon.c) and
 * the critcalc / damagecalc / typecalc / typecalc2 / accuracycheck /
 * adjustnormaldamage script commands (src/battle_script_commands.c).
 *
 * These follow the originals statement for statement -- truncation order,
 * quirks and all -- with globals read from struct BattleState /
 * struct MoveContext instead (see battle_internal.h). Where a command
 * rolls Random(), the port returns the probability of each outcome
 * instead of picking one. */
#include "battle_internal.h"

/* --- Tables, verbatim ----------------------------------------------------- */

/* include/battle_main.h */
#define TYPE_MUL_NO_EFFECT       0
#define TYPE_MUL_NOT_EFFECTIVE   5
#define TYPE_MUL_NORMAL          10
#define TYPE_MUL_SUPER_EFFECTIVE 20
#define TYPE_FORESIGHT 0xFE
#define TYPE_ENDTABLE  0xFF

/* src/battle_main.c gTypeEffectiveness[336], walked the same way via the
 * TYPE_EFFECT_* macros (include/battle_main.h). */
static const u8 sTypeEffectiveness[336] = {
    TYPE_NORMAL, TYPE_ROCK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_NORMAL, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIRE, TYPE_FIRE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIRE, TYPE_WATER, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIRE, TYPE_GRASS, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIRE, TYPE_ICE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIRE, TYPE_BUG, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIRE, TYPE_ROCK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIRE, TYPE_DRAGON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIRE, TYPE_STEEL, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_WATER, TYPE_FIRE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_WATER, TYPE_WATER, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_WATER, TYPE_GRASS, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_WATER, TYPE_GROUND, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_WATER, TYPE_ROCK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_WATER, TYPE_DRAGON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ELECTRIC, TYPE_WATER, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ELECTRIC, TYPE_ELECTRIC, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ELECTRIC, TYPE_GRASS, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ELECTRIC, TYPE_GROUND, TYPE_MUL_NO_EFFECT,
    TYPE_ELECTRIC, TYPE_FLYING, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ELECTRIC, TYPE_DRAGON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_FIRE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_WATER, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GRASS, TYPE_GRASS, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_POISON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_GROUND, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GRASS, TYPE_FLYING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_BUG, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_ROCK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GRASS, TYPE_DRAGON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GRASS, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ICE, TYPE_WATER, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ICE, TYPE_GRASS, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ICE, TYPE_ICE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ICE, TYPE_GROUND, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ICE, TYPE_FLYING, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ICE, TYPE_DRAGON, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ICE, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ICE, TYPE_FIRE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIGHTING, TYPE_NORMAL, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIGHTING, TYPE_ICE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIGHTING, TYPE_POISON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIGHTING, TYPE_FLYING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIGHTING, TYPE_PSYCHIC, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIGHTING, TYPE_BUG, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FIGHTING, TYPE_ROCK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIGHTING, TYPE_DARK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FIGHTING, TYPE_STEEL, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_POISON, TYPE_GRASS, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_POISON, TYPE_POISON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_POISON, TYPE_GROUND, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_POISON, TYPE_ROCK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_POISON, TYPE_GHOST, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_POISON, TYPE_STEEL, TYPE_MUL_NO_EFFECT,
    TYPE_GROUND, TYPE_FIRE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GROUND, TYPE_ELECTRIC, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GROUND, TYPE_GRASS, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GROUND, TYPE_POISON, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GROUND, TYPE_FLYING, TYPE_MUL_NO_EFFECT,
    TYPE_GROUND, TYPE_BUG, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GROUND, TYPE_ROCK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GROUND, TYPE_STEEL, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FLYING, TYPE_ELECTRIC, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FLYING, TYPE_GRASS, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FLYING, TYPE_FIGHTING, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FLYING, TYPE_BUG, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_FLYING, TYPE_ROCK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FLYING, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_PSYCHIC, TYPE_FIGHTING, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_PSYCHIC, TYPE_POISON, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_PSYCHIC, TYPE_PSYCHIC, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_PSYCHIC, TYPE_DARK, TYPE_MUL_NO_EFFECT,
    TYPE_PSYCHIC, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_FIRE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_GRASS, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_BUG, TYPE_FIGHTING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_POISON, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_FLYING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_PSYCHIC, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_BUG, TYPE_GHOST, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_BUG, TYPE_DARK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_BUG, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ROCK, TYPE_FIRE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ROCK, TYPE_ICE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ROCK, TYPE_FIGHTING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ROCK, TYPE_GROUND, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_ROCK, TYPE_FLYING, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ROCK, TYPE_BUG, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_ROCK, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GHOST, TYPE_NORMAL, TYPE_MUL_NO_EFFECT,
    TYPE_GHOST, TYPE_PSYCHIC, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_GHOST, TYPE_DARK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GHOST, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_GHOST, TYPE_GHOST, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_DRAGON, TYPE_DRAGON, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_DRAGON, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_DARK, TYPE_FIGHTING, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_DARK, TYPE_PSYCHIC, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_DARK, TYPE_GHOST, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_DARK, TYPE_DARK, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_DARK, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_STEEL, TYPE_FIRE, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_STEEL, TYPE_WATER, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_STEEL, TYPE_ELECTRIC, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_STEEL, TYPE_ICE, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_STEEL, TYPE_ROCK, TYPE_MUL_SUPER_EFFECTIVE,
    TYPE_STEEL, TYPE_STEEL, TYPE_MUL_NOT_EFFECTIVE,
    TYPE_FORESIGHT, TYPE_FORESIGHT, TYPE_MUL_NO_EFFECT,
    TYPE_NORMAL, TYPE_GHOST, TYPE_MUL_NO_EFFECT,
    TYPE_FIGHTING, TYPE_GHOST, TYPE_MUL_NO_EFFECT,
    TYPE_ENDTABLE, TYPE_ENDTABLE, TYPE_MUL_NO_EFFECT,
};

#define TYPE_EFFECT_ATK_TYPE(i)   (sTypeEffectiveness[(i) + 0])
#define TYPE_EFFECT_DEF_TYPE(i)   (sTypeEffectiveness[(i) + 1])
#define TYPE_EFFECT_MULTIPLIER(i) (sTypeEffectiveness[(i) + 2])

/* src/battle_script_commands.c sCriticalHitChance -- the chance is 1/N. */
static const u16 sCriticalHitChance[] = {16, 8, 4, 3, 2};
#define CRIT_CHANCE_TABLE_SIZE (sizeof(sCriticalHitChance) / sizeof(sCriticalHitChance[0]))

/* src/battle_script_commands.c sAccuracyStageRatios. */
static const struct { u8 dividend; u8 divisor; } sAccuracyStageRatios[] = {
    { 33, 100}, // -6
    { 36, 100}, // -5
    { 43, 100}, // -4
    { 50, 100}, // -3
    { 60, 100}, // -2
    { 75, 100}, // -1
    {  1,   1}, //  0
    {133, 100}, // +1
    {166, 100}, // +2
    {  2,   1}, // +3
    {233, 100}, // +4
    {133,  50}, // +5
    {  3,   1}, // +6
};

/* src/pokemon.c gStatStageRatios. */
static const u8 gStatStageRatios[MAX_STAT_STAGE + 1][2] = {
    {10, 40}, {10, 35}, {10, 30}, {10, 25}, {10, 20}, {10, 15}, {10, 10},
    {15, 10}, {20, 10}, {25, 10}, {30, 10}, {35, 10}, {40, 10},
};

/* src/pokemon.c sHoldEffectToType. */
static const u8 sHoldEffectToType[][2] = {
    {HOLD_EFFECT_BUG_POWER, TYPE_BUG},
    {HOLD_EFFECT_STEEL_POWER, TYPE_STEEL},
    {HOLD_EFFECT_GROUND_POWER, TYPE_GROUND},
    {HOLD_EFFECT_ROCK_POWER, TYPE_ROCK},
    {HOLD_EFFECT_GRASS_POWER, TYPE_GRASS},
    {HOLD_EFFECT_DARK_POWER, TYPE_DARK},
    {HOLD_EFFECT_FIGHTING_POWER, TYPE_FIGHTING},
    {HOLD_EFFECT_ELECTRIC_POWER, TYPE_ELECTRIC},
    {HOLD_EFFECT_WATER_POWER, TYPE_WATER},
    {HOLD_EFFECT_FLYING_POWER, TYPE_FLYING},
    {HOLD_EFFECT_POISON_POWER, TYPE_POISON},
    {HOLD_EFFECT_ICE_POWER, TYPE_ICE},
    {HOLD_EFFECT_GHOST_POWER, TYPE_GHOST},
    {HOLD_EFFECT_PSYCHIC_POWER, TYPE_PSYCHIC},
    {HOLD_EFFECT_FIRE_POWER, TYPE_FIRE},
    {HOLD_EFFECT_DRAGON_POWER, TYPE_DRAGON},
    {HOLD_EFFECT_NORMAL_POWER, TYPE_NORMAL},
};
#define HOLD_EFFECT_TO_TYPE_COUNT (sizeof(sHoldEffectToType) / sizeof(sHoldEffectToType[0]))

/* include/battle.h */
#define DYNAMIC_TYPE_MASK ((1 << 6) - 1)
#define IS_TYPE_PHYSICAL(moveType) ((moveType) < TYPE_MYSTERY)
#define IS_TYPE_SPECIAL(moveType)  ((moveType) > TYPE_MYSTERY)

#define APPLY_STAT_MOD(var, mon, stat, statIndex)                                   \
{                                                                                   \
    (var) = (stat) * (gStatStageRatios)[(mon)->statStages[(statIndex)]][0];         \
    (var) /= (gStatStageRatios)[(mon)->statStages[(statIndex)]][1];                 \
}

u8 GetMoveType(const struct MoveContext *ctx, u16 move)
{
    if (ctx->dynamicMoveType)
        return ctx->dynamicMoveType & DYNAMIC_TYPE_MASK;
    else
        return gBattleMoves[move].type;
}

/* --- CalculateBaseDamage() (src/pokemon.c) ------------------------------ */

s32 CalculateBaseDamage(struct MoveContext *ctx, const struct BattlePokemon *attacker,
                        const struct BattlePokemon *defender, u32 move, u16 sideStatus,
                        u16 powerOverride, u8 typeOverride, u8 battlerIdAtk, u8 battlerIdDef)
{
    const struct BattleState *s = ctx->s;
    u32 i;
    s32 damage = 0;
    s32 damageHelper;
    u8 type;
    u16 attack, defense;
    u16 spAttack, spDefense;
    u8 defenderHoldEffect;
    u8 defenderHoldEffectParam;
    u8 attackerHoldEffect;
    u8 attackerHoldEffectParam;
    u16 gBattleMovePower;

    if (!powerOverride)
        gBattleMovePower = gBattleMoves[move].power;
    else
        gBattleMovePower = powerOverride;

    if (!typeOverride)
        type = gBattleMoves[move].type;
    else
        type = typeOverride & DYNAMIC_TYPE_MASK;

    attack = attacker->attack;
    defense = defender->defense;
    spAttack = attacker->spAttack;
    spDefense = defender->spDefense;

    // Get attacker hold item info
    if (attacker->item == ITEM_ENIGMA_BERRY)
    {
        attackerHoldEffect = s->enigmaBerries[battlerIdAtk].holdEffect;
        attackerHoldEffectParam = s->enigmaBerries[battlerIdAtk].holdEffectParam;
    }
    else
    {
        attackerHoldEffect = GetItemHoldEffect(attacker->item);
        attackerHoldEffectParam = GetItemHoldEffectParam(attacker->item);
    }

    // Get defender hold item info
    if (defender->item == ITEM_ENIGMA_BERRY)
    {
        defenderHoldEffect = s->enigmaBerries[battlerIdDef].holdEffect;
        defenderHoldEffectParam = s->enigmaBerries[battlerIdDef].holdEffectParam;
    }
    else
    {
        defenderHoldEffect = GetItemHoldEffect(defender->item);
        defenderHoldEffectParam = GetItemHoldEffectParam(defender->item);
    }
    (void)defenderHoldEffectParam; // read but unused in the original too

    if (attacker->ability == ABILITY_HUGE_POWER || attacker->ability == ABILITY_PURE_POWER)
        attack *= 2;

    if (ShouldGetStatBadgeBoost(s, FLAG_BADGE01_GET, battlerIdAtk))
        attack = (110 * attack) / 100;
    if (ShouldGetStatBadgeBoost(s, FLAG_BADGE05_GET, battlerIdDef))
        defense = (110 * defense) / 100;
    if (ShouldGetStatBadgeBoost(s, FLAG_BADGE07_GET, battlerIdAtk))
        spAttack = (110 * spAttack) / 100;
    if (ShouldGetStatBadgeBoost(s, FLAG_BADGE07_GET, battlerIdDef))
        spDefense = (110 * spDefense) / 100;

    // Apply type-bonus hold item
    for (i = 0; i < HOLD_EFFECT_TO_TYPE_COUNT; i++)
    {
        if (attackerHoldEffect == sHoldEffectToType[i][0]
            && type == sHoldEffectToType[i][1])
        {
            if (IS_TYPE_PHYSICAL(type))
                attack = (attack * (attackerHoldEffectParam + 100)) / 100;
            else
                spAttack = (spAttack * (attackerHoldEffectParam + 100)) / 100;
            break;
        }
    }

    // Apply boosts from hold items
    if (attackerHoldEffect == HOLD_EFFECT_CHOICE_BAND)
        attack = (150 * attack) / 100;
    if (attackerHoldEffect == HOLD_EFFECT_SOUL_DEW && !(s->battleTypeFlags & (BATTLE_TYPE_FRONTIER)) && (attacker->species == SPECIES_LATIAS || attacker->species == SPECIES_LATIOS))
        spAttack = (150 * spAttack) / 100;
    if (defenderHoldEffect == HOLD_EFFECT_SOUL_DEW && !(s->battleTypeFlags & (BATTLE_TYPE_FRONTIER)) && (defender->species == SPECIES_LATIAS || defender->species == SPECIES_LATIOS))
        spDefense = (150 * spDefense) / 100;
    if (attackerHoldEffect == HOLD_EFFECT_DEEP_SEA_TOOTH && attacker->species == SPECIES_CLAMPERL)
        spAttack *= 2;
    if (defenderHoldEffect == HOLD_EFFECT_DEEP_SEA_SCALE && defender->species == SPECIES_CLAMPERL)
        spDefense *= 2;
    if (attackerHoldEffect == HOLD_EFFECT_LIGHT_BALL && attacker->species == SPECIES_PIKACHU)
        spAttack *= 2;
    if (defenderHoldEffect == HOLD_EFFECT_METAL_POWDER && defender->species == SPECIES_DITTO)
        defense *= 2;
    if (attackerHoldEffect == HOLD_EFFECT_THICK_CLUB && (attacker->species == SPECIES_CUBONE || attacker->species == SPECIES_MAROWAK))
        attack *= 2;

    // Apply abilities / field sports
    if (defender->ability == ABILITY_THICK_FAT && (type == TYPE_FIRE || type == TYPE_ICE))
        spAttack /= 2;
    if (attacker->ability == ABILITY_HUSTLE)
        attack = (150 * attack) / 100;
    if (attacker->ability == ABILITY_PLUS && AbilityOnField2(s, ABILITY_MINUS))
        spAttack = (150 * spAttack) / 100;
    if (attacker->ability == ABILITY_MINUS && AbilityOnField2(s, ABILITY_PLUS))
        spAttack = (150 * spAttack) / 100;
    if (attacker->ability == ABILITY_GUTS && attacker->status1)
        attack = (150 * attack) / 100;
    if (defender->ability == ABILITY_MARVEL_SCALE && defender->status1)
        defense = (150 * defense) / 100;
    if (type == TYPE_ELECTRIC && FieldSportActive(s, STATUS3_MUDSPORT))
        gBattleMovePower /= 2;
    if (type == TYPE_FIRE && FieldSportActive(s, STATUS3_WATERSPORT))
        gBattleMovePower /= 2;
    if (type == TYPE_GRASS && attacker->ability == ABILITY_OVERGROW && attacker->hp <= (attacker->maxHP / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_FIRE && attacker->ability == ABILITY_BLAZE && attacker->hp <= (attacker->maxHP / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_WATER && attacker->ability == ABILITY_TORRENT && attacker->hp <= (attacker->maxHP / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_BUG && attacker->ability == ABILITY_SWARM && attacker->hp <= (attacker->maxHP / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;

    // Self-destruct / Explosion cut defense in half
    if (gBattleMoves[ctx->currentMove].effect == EFFECT_EXPLOSION)
        defense /= 2;

    if (IS_TYPE_PHYSICAL(type))
    {
        if (ctx->critMultiplier == 2)
        {
            // Critical hit, if attacker has lost attack stat stages then ignore stat drop
            if (attacker->statStages[STAT_ATK] > DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damage, attacker, attack, STAT_ATK)
            else
                damage = attack;
        }
        else
            APPLY_STAT_MOD(damage, attacker, attack, STAT_ATK)

        damage = damage * gBattleMovePower;
        damage *= (2 * attacker->level / 5 + 2);

        if (ctx->critMultiplier == 2)
        {
            // Critical hit, if defender has gained defense stat stages then ignore stat increase
            if (defender->statStages[STAT_DEF] < DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damageHelper, defender, defense, STAT_DEF)
            else
                damageHelper = defense;
        }
        else
            APPLY_STAT_MOD(damageHelper, defender, defense, STAT_DEF)

        damage = damage / damageHelper;
        damage /= 50;

        // Burn cuts attack in half
        if ((attacker->status1 & STATUS1_BURN) && attacker->ability != ABILITY_GUTS)
            damage /= 2;

        // Apply Reflect
        if ((sideStatus & SIDE_STATUS_REFLECT) && ctx->critMultiplier == 1)
        {
            if ((s->battleTypeFlags & BATTLE_TYPE_DOUBLE) && CountAliveMonsInBattleDefSide(s, ctx->battlerTarget) == 2)
                damage = 2 * (damage / 3);
            else
                damage /= 2;
        }

        // Moves hitting both targets do half damage in double battles
        if ((s->battleTypeFlags & BATTLE_TYPE_DOUBLE) && gBattleMoves[move].target == MOVE_TARGET_BOTH && CountAliveMonsInBattleDefSide(s, ctx->battlerTarget) == 2)
            damage /= 2;

        // Moves always do at least 1 damage.
        if (damage == 0)
            damage = 1;
    }

    if (type == TYPE_MYSTERY)
        damage = 0; // is ??? type. does 0 damage.

    if (IS_TYPE_SPECIAL(type))
    {
        if (ctx->critMultiplier == 2)
        {
            // Critical hit, if attacker has lost sp. attack stat stages then ignore stat drop
            if (attacker->statStages[STAT_SPATK] > DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damage, attacker, spAttack, STAT_SPATK)
            else
                damage = spAttack;
        }
        else
            APPLY_STAT_MOD(damage, attacker, spAttack, STAT_SPATK)

        damage = damage * gBattleMovePower;
        damage *= (2 * attacker->level / 5 + 2);

        if (ctx->critMultiplier == 2)
        {
            // Critical hit, if defender has gained sp. defense stat stages then ignore stat increase
            if (defender->statStages[STAT_SPDEF] < DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damageHelper, defender, spDefense, STAT_SPDEF)
            else
                damageHelper = spDefense;
        }
        else
            APPLY_STAT_MOD(damageHelper, defender, spDefense, STAT_SPDEF)

        damage = (damage / damageHelper);
        damage /= 50;

        // Apply Lightscreen
        if ((sideStatus & SIDE_STATUS_LIGHTSCREEN) && ctx->critMultiplier == 1)
        {
            if ((s->battleTypeFlags & BATTLE_TYPE_DOUBLE) && CountAliveMonsInBattleDefSide(s, ctx->battlerTarget) == 2)
                damage = 2 * (damage / 3);
            else
                damage /= 2;
        }

        // Moves hitting both targets do half damage in double battles
        if ((s->battleTypeFlags & BATTLE_TYPE_DOUBLE) && gBattleMoves[move].target == MOVE_TARGET_BOTH && CountAliveMonsInBattleDefSide(s, ctx->battlerTarget) == 2)
            damage /= 2;

        // Are effects of weather negated with cloud nine or air lock
        if (WeatherHasEffect2(s))
        {
            // Rain weakens Fire, boosts Water
            if (s->battleWeather & B_WEATHER_RAIN_TEMPORARY)
            {
                switch (type)
                {
                case TYPE_FIRE:
                    damage /= 2;
                    break;
                case TYPE_WATER:
                    damage = (15 * damage) / 10;
                    break;
                }
            }

            // Any weather except sun weakens solar beam
            if ((s->battleWeather & (B_WEATHER_RAIN | B_WEATHER_SANDSTORM | B_WEATHER_HAIL)) && ctx->currentMove == MOVE_SOLAR_BEAM)
                damage /= 2;

            // Sun boosts Fire, weakens Water
            if (s->battleWeather & B_WEATHER_SUN)
            {
                switch (type)
                {
                case TYPE_FIRE:
                    damage = (15 * damage) / 10;
                    break;
                case TYPE_WATER:
                    damage /= 2;
                    break;
                }
            }
        }

        // Flash fire triggered
        if ((s->resourceFlags[battlerIdAtk] & RESOURCE_FLAG_FLASH_FIRE) && type == TYPE_FIRE)
            damage = (15 * damage) / 10;
    }

    return damage + 2;
}

/* --- damagecalc / typecalc / typecalc2 ----------------------------------- */

void Cmd_damagecalc(struct MoveContext *ctx)
{
    const struct BattleState *s = ctx->s;
    u16 sideStatus = ctx->sideStatuses[GetBattlerSide(s, ctx->battlerTarget)];

    ctx->battleMoveDamage = CalculateBaseDamage(ctx, &s->battleMons[ctx->battlerAttacker], &s->battleMons[ctx->battlerTarget], ctx->currentMove,
                                                sideStatus, ctx->dynamicBasePower,
                                                ctx->dynamicMoveType, ctx->battlerAttacker, ctx->battlerTarget);
    ctx->battleMoveDamage = ctx->battleMoveDamage * ctx->critMultiplier * ctx->dmgMultiplier;

    if (s->statuses3[ctx->battlerAttacker] & STATUS3_CHARGED_UP && gBattleMoves[ctx->currentMove].type == TYPE_ELECTRIC)
        ctx->battleMoveDamage *= 2;
    if (s->protectStructs[ctx->battlerAttacker].helpingHand)
        ctx->battleMoveDamage = ctx->battleMoveDamage * 15 / 10;
}

static void ModulateDmgByType(struct MoveContext *ctx, u8 multiplier)
{
    ctx->battleMoveDamage = ctx->battleMoveDamage * multiplier / 10;
    if (ctx->battleMoveDamage == 0 && multiplier != 0)
        ctx->battleMoveDamage = 1;

    switch (multiplier)
    {
    case TYPE_MUL_NO_EFFECT:
        ctx->moveResultFlags |= MOVE_RESULT_DOESNT_AFFECT_FOE;
        ctx->moveResultFlags &= ~MOVE_RESULT_NOT_VERY_EFFECTIVE;
        ctx->moveResultFlags &= ~MOVE_RESULT_SUPER_EFFECTIVE;
        break;
    case TYPE_MUL_NOT_EFFECTIVE:
        if (gBattleMoves[ctx->currentMove].power && !(ctx->moveResultFlags & MOVE_RESULT_NO_EFFECT))
        {
            if (ctx->moveResultFlags & MOVE_RESULT_SUPER_EFFECTIVE)
                ctx->moveResultFlags &= ~MOVE_RESULT_SUPER_EFFECTIVE;
            else
                ctx->moveResultFlags |= MOVE_RESULT_NOT_VERY_EFFECTIVE;
        }
        break;
    case TYPE_MUL_SUPER_EFFECTIVE:
        if (gBattleMoves[ctx->currentMove].power && !(ctx->moveResultFlags & MOVE_RESULT_NO_EFFECT))
        {
            if (ctx->moveResultFlags & MOVE_RESULT_NOT_VERY_EFFECTIVE)
                ctx->moveResultFlags &= ~MOVE_RESULT_NOT_VERY_EFFECTIVE;
            else
                ctx->moveResultFlags |= MOVE_RESULT_SUPER_EFFECTIVE;
        }
        break;
    }
}

/* AttacksThisTurn(gBattlerAttacker, gCurrentMove) == 2 in the Wonder Guard
 * checks below: it's only 1 on a two-turn move's charging turn
 * (HITMARKER_CHARGING), and the simulation is always of the turn that
 * deals damage. */
void Cmd_typecalc(struct MoveContext *ctx)
{
    const struct BattleState *s = ctx->s;
    const struct BattlePokemon *target = &s->battleMons[ctx->battlerTarget];
    s32 i = 0;
    u8 moveType;

    if (ctx->currentMove == MOVE_STRUGGLE)
        return;

    moveType = GetMoveType(ctx, ctx->currentMove);

    // check stab
    if (IsBattlerOfType(s, ctx->battlerAttacker, moveType))
    {
        ctx->battleMoveDamage = ctx->battleMoveDamage * 15;
        ctx->battleMoveDamage = ctx->battleMoveDamage / 10;
    }

    if (target->ability == ABILITY_LEVITATE && moveType == TYPE_GROUND)
    {
        ctx->lastUsedAbility = target->ability;
        ctx->moveResultFlags |= (MOVE_RESULT_MISSED | MOVE_RESULT_DOESNT_AFFECT_FOE);
    }
    else
    {
        while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
        {
            if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
            {
                if (target->status2 & STATUS2_FORESIGHT)
                    break;
                i += 3;
                continue;
            }
            else if (TYPE_EFFECT_ATK_TYPE(i) == moveType)
            {
                // check type1
                if (TYPE_EFFECT_DEF_TYPE(i) == target->types[0])
                    ModulateDmgByType(ctx, TYPE_EFFECT_MULTIPLIER(i));
                // check type2
                if (TYPE_EFFECT_DEF_TYPE(i) == target->types[1] &&
                    target->types[0] != target->types[1])
                    ModulateDmgByType(ctx, TYPE_EFFECT_MULTIPLIER(i));
            }
            i += 3;
        }
    }

    if (target->ability == ABILITY_WONDER_GUARD /* && AttacksThisTurn(...) == 2 */
     && (!(ctx->moveResultFlags & MOVE_RESULT_SUPER_EFFECTIVE) || ((ctx->moveResultFlags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)) == (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)))
     && gBattleMoves[ctx->currentMove].power)
    {
        ctx->lastUsedAbility = ABILITY_WONDER_GUARD;
        ctx->moveResultFlags |= MOVE_RESULT_MISSED;
    }
}

/* Used by Counter/Mirror Coat/Rollout: checks immunity but never touches
 * gBattleMoveDamage, and reads the move's static type. */
void Cmd_typecalc2(struct MoveContext *ctx)
{
    const struct BattleState *s = ctx->s;
    const struct BattlePokemon *target = &s->battleMons[ctx->battlerTarget];
    u8 flags = 0;
    s32 i = 0;
    u8 moveType = gBattleMoves[ctx->currentMove].type;

    if (target->ability == ABILITY_LEVITATE && moveType == TYPE_GROUND)
    {
        ctx->lastUsedAbility = target->ability;
        ctx->moveResultFlags |= (MOVE_RESULT_MISSED | MOVE_RESULT_DOESNT_AFFECT_FOE);
    }
    else
    {
        while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE)
        {
            if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
            {
                if (target->status2 & STATUS2_FORESIGHT)
                {
                    break;
                }
                else
                {
                    i += 3;
                    continue;
                }
            }

            if (TYPE_EFFECT_ATK_TYPE(i) == moveType)
            {
                // check type1
                if (TYPE_EFFECT_DEF_TYPE(i) == target->types[0])
                {
                    if (TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_NO_EFFECT)
                    {
                        ctx->moveResultFlags |= MOVE_RESULT_DOESNT_AFFECT_FOE;
                        break;
                    }
                    if (TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_NOT_EFFECTIVE)
                    {
                        flags |= MOVE_RESULT_NOT_VERY_EFFECTIVE;
                    }
                    if (TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_SUPER_EFFECTIVE)
                    {
                        flags |= MOVE_RESULT_SUPER_EFFECTIVE;
                    }
                }
                // check type2
                if (TYPE_EFFECT_DEF_TYPE(i) == target->types[1])
                {
                    if (target->types[0] != target->types[1]
                        && TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_NO_EFFECT)
                    {
                        ctx->moveResultFlags |= MOVE_RESULT_DOESNT_AFFECT_FOE;
                        break;
                    }
                    if (TYPE_EFFECT_DEF_TYPE(i) == target->types[1]
                        && target->types[0] != target->types[1]
                        && TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_NOT_EFFECTIVE)
                    {
                        flags |= MOVE_RESULT_NOT_VERY_EFFECTIVE;
                    }
                    if (TYPE_EFFECT_DEF_TYPE(i) == target->types[1]
                        && target->types[0] != target->types[1]
                        && TYPE_EFFECT_MULTIPLIER(i) == TYPE_MUL_SUPER_EFFECTIVE)
                    {
                        flags |= MOVE_RESULT_SUPER_EFFECTIVE;
                    }
                }
            }
            i += 3;
        }
    }

    if (target->ability == ABILITY_WONDER_GUARD
        && !(flags & MOVE_RESULT_NO_EFFECT)
        /* && AttacksThisTurn(...) == 2 */
        && (!(flags & MOVE_RESULT_SUPER_EFFECTIVE) || ((flags & (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)) == (MOVE_RESULT_SUPER_EFFECTIVE | MOVE_RESULT_NOT_VERY_EFFECTIVE)))
        && gBattleMoves[ctx->currentMove].power)
    {
        ctx->lastUsedAbility = ABILITY_WONDER_GUARD;
        ctx->moveResultFlags |= MOVE_RESULT_MISSED;
    }
}

/* --- critcalc -------------------------------------------------------------- */

double CritChance(const struct MoveContext *ctx)
{
    const struct BattleState *s = ctx->s;
    const struct BattlePokemon *attacker = &s->battleMons[ctx->battlerAttacker];
    u8 holdEffect, holdEffectParam;
    u16 critChance;

    GetBattlerHoldEffect(s, ctx->battlerAttacker, &holdEffect, &holdEffectParam);

    critChance  = 2 * ((attacker->status2 & STATUS2_FOCUS_ENERGY) != 0)
                + (gBattleMoves[ctx->currentMove].effect == EFFECT_HIGH_CRITICAL)
                + (gBattleMoves[ctx->currentMove].effect == EFFECT_SKY_ATTACK)
                + (gBattleMoves[ctx->currentMove].effect == EFFECT_BLAZE_KICK)
                + (gBattleMoves[ctx->currentMove].effect == EFFECT_POISON_TAIL)
                + (holdEffect == HOLD_EFFECT_SCOPE_LENS)
                + 2 * (holdEffect == HOLD_EFFECT_LUCKY_PUNCH && attacker->species == SPECIES_CHANSEY)
                + 2 * (holdEffect == HOLD_EFFECT_STICK && attacker->species == SPECIES_FARFETCHD);

    if (critChance >= CRIT_CHANCE_TABLE_SIZE)
        critChance = CRIT_CHANCE_TABLE_SIZE - 1;

    if ((s->battleMons[ctx->battlerTarget].ability != ABILITY_BATTLE_ARMOR && s->battleMons[ctx->battlerTarget].ability != ABILITY_SHELL_ARMOR)
     && !(s->statuses3[ctx->battlerAttacker] & STATUS3_CANT_SCORE_A_CRIT)
     && !(s->battleTypeFlags & (BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_FIRST_BATTLE)))
        return RandomModEquals(sCriticalHitChance[critChance], 0); // !(Random() % sCriticalHitChance[critChance])
    else
        return 0.0;
}

/* --- accuracycheck --------------------------------------------------------- */

/* DEFENDER_IS_PROTECTED */
static bool8 DefenderIsProtected(const struct MoveContext *ctx)
{
    return ctx->s->protectStructs[ctx->battlerTarget].protected_
        && (gBattleMoves[ctx->currentMove].flags & FLAG_PROTECT_AFFECTED);
}

/* AccuracyCalcHelper(): returns TRUE when the outcome is decided without
 * rolling -- *hitChance then holds it. */
static bool8 AccuracyCalcHelper(const struct MoveContext *ctx, u16 move, double *hitChance, u8 *missReason)
{
    const struct BattleState *s = ctx->s;
    u8 target = ctx->battlerTarget;

    if (s->statuses3[target] & STATUS3_ALWAYS_HITS && s->disableStructs[target].battlerWithSureHit == ctx->battlerAttacker)
    {
        *hitChance = 1.0;
        return TRUE;
    }

    if (!(ctx->hitMarker & HITMARKER_IGNORE_ON_AIR) && s->statuses3[target] & STATUS3_ON_AIR)
    {
        *hitChance = 0.0;
        *missReason = EMERALD_REASON_TARGET_SEMI_INVULNERABLE;
        return TRUE;
    }

    if (!(ctx->hitMarker & HITMARKER_IGNORE_UNDERGROUND) && s->statuses3[target] & STATUS3_UNDERGROUND)
    {
        *hitChance = 0.0;
        *missReason = EMERALD_REASON_TARGET_SEMI_INVULNERABLE;
        return TRUE;
    }

    if (!(ctx->hitMarker & HITMARKER_IGNORE_UNDERWATER) && s->statuses3[target] & STATUS3_UNDERWATER)
    {
        *hitChance = 0.0;
        *missReason = EMERALD_REASON_TARGET_SEMI_INVULNERABLE;
        return TRUE;
    }

    if ((WeatherHasEffect(s) && (s->battleWeather & B_WEATHER_RAIN) && gBattleMoves[move].effect == EFFECT_THUNDER)
     || (gBattleMoves[move].effect == EFFECT_ALWAYS_HIT || gBattleMoves[move].effect == EFFECT_VITAL_THROW))
    {
        *hitChance = 1.0;
        return TRUE;
    }

    return FALSE;
}

double AccuracyCheck(const struct MoveContext *ctx, u16 move, u8 *missReason)
{
    const struct BattleState *s = ctx->s;
    const struct BattlePokemon *attacker = &s->battleMons[ctx->battlerAttacker];
    const struct BattlePokemon *target = &s->battleMons[ctx->battlerTarget];
    double hitChance;

    *missReason = EMERALD_REASON_NONE;

    if (move == NO_ACC_CALC || move == NO_ACC_CALC_CHECK_LOCK_ON)
    {
        if (s->statuses3[ctx->battlerTarget] & STATUS3_ALWAYS_HITS && move == NO_ACC_CALC_CHECK_LOCK_ON && s->disableStructs[ctx->battlerTarget].battlerWithSureHit == ctx->battlerAttacker)
            return 1.0;
        else if (s->statuses3[ctx->battlerTarget] & (STATUS3_ON_AIR | STATUS3_UNDERGROUND | STATUS3_UNDERWATER))
        {
            *missReason = EMERALD_REASON_TARGET_SEMI_INVULNERABLE;
            return 0.0;
        }
        else if (DefenderIsProtected(ctx)) // !JumpIfMoveAffectedByProtect(0)
        {
            *missReason = EMERALD_REASON_TARGET_PROTECTED;
            return 0.0;
        }
        return 1.0;
    }
    else
    {
        u8 type, moveAcc, holdEffect, param;
        s8 buff;
        u16 calc;

        if (move == ACC_CURR_MOVE)
            move = ctx->currentMove;

        type = GetMoveType(ctx, move);

        if (DefenderIsProtected(ctx)) // JumpIfMoveAffectedByProtect(move)
        {
            *missReason = EMERALD_REASON_TARGET_PROTECTED;
            return 0.0;
        }
        if (AccuracyCalcHelper(ctx, move, &hitChance, missReason))
            return hitChance;

        if (target->status2 & STATUS2_FORESIGHT)
        {
            u8 acc = attacker->statStages[STAT_ACC];
            buff = acc;
        }
        else
        {
            u8 acc = attacker->statStages[STAT_ACC];
            buff = acc + DEFAULT_STAT_STAGE - target->statStages[STAT_EVASION];
        }

        if (buff < MIN_STAT_STAGE)
            buff = MIN_STAT_STAGE;
        if (buff > MAX_STAT_STAGE)
            buff = MAX_STAT_STAGE;

        moveAcc = gBattleMoves[move].accuracy;
        // check Thunder on sunny weather
        if (WeatherHasEffect(s) && s->battleWeather & B_WEATHER_SUN && gBattleMoves[move].effect == EFFECT_THUNDER)
            moveAcc = 50;

        calc = sAccuracyStageRatios[buff].dividend * moveAcc;
        calc /= sAccuracyStageRatios[buff].divisor;

        if (attacker->ability == ABILITY_COMPOUND_EYES)
            calc = (calc * 130) / 100; // 1.3 compound eyes boost
        if (WeatherHasEffect(s) && target->ability == ABILITY_SAND_VEIL && s->battleWeather & B_WEATHER_SANDSTORM)
            calc = (calc * 80) / 100; // 1.2 sand veil loss
        if (attacker->ability == ABILITY_HUSTLE && IS_TYPE_PHYSICAL(type))
            calc = (calc * 80) / 100; // 1.2 hustle loss

        GetBattlerHoldEffect(s, ctx->battlerTarget, &holdEffect, &param);

        if (holdEffect == HOLD_EFFECT_EVASION_UP)
            calc = (calc * (100 - param)) / 100;

        // final calculation: misses when (Random() % 100 + 1) > calc
        return RandomModBelow(100, calc);
    }
}

/* --- adjustnormaldamage's random roll -------------------------------------- */

EmeraldCalcRollSet RandomDamageRolls(s32 damage)
{
    EmeraldCalcRollSet set;
    int i;

    /* ApplyRandomDmgMultiplier(): randPercent = 100 - (Random() % 16), so
     * each of 85..100 comes up for exactly one residue. */
    for (i = 0; i < EMERALD_CALC_NUM_ROLLS; i++) {
        s32 randPercent = 100 - i;
        s32 d = damage;
        if (d != 0) {
            d *= randPercent;
            d /= 100;
            if (d == 0)
                d = 1;
        }
        set.rolls[i] = d;
    }
    set.max = set.rolls[0];
    set.min = set.rolls[EMERALD_CALC_NUM_ROLLS - 1];
    return set;
}

EmeraldCalcRollSet FixedDamageRolls(s32 damage)
{
    EmeraldCalcRollSet set;
    int i;

    for (i = 0; i < EMERALD_CALC_NUM_ROLLS; i++)
        set.rolls[i] = damage;
    set.min = set.max = damage;
    return set;
}
