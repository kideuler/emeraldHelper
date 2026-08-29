#include "emerald_calc.h"

#include <string.h>

/* --- Type effectiveness table --------------------------------------------
 * Ported verbatim from src/battle_main.c's gTypeEffectiveness[336], walked
 * the same way TypeCalc()/AI_TypeCalc() do via the TYPE_EFFECT_ATK_TYPE/
 * DEF_TYPE/MULTIPLIER macros (include/battle_main.h) -- redeclared here,
 * not included, for the same "cascades into GBA headers" reason as
 * emerald_calc.h's other constants. */
#define TYPE_MUL_NO_EFFECT       0
#define TYPE_MUL_NOT_EFFECTIVE   5
#define TYPE_MUL_NORMAL          10
#define TYPE_MUL_SUPER_EFFECTIVE 20
#define TYPE_FORESIGHT 0xFE
#define TYPE_ENDTABLE  0xFF

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

/* Ported verbatim from src/battle_script_commands.c's sCriticalHitChance
 * (index = critChance from Cmd_critcalc). */
static const u16 sCriticalHitChance[] = {16, 8, 4, 3, 2};
#define CRIT_CHANCE_TABLE_SIZE (sizeof(sCriticalHitChance) / sizeof(sCriticalHitChance[0]))

/* Ported verbatim from src/pokemon.c's gStatStageRatios. */
static const u8 sStatStageRatios[MAX_STAT_STAGE + 1][2] = {
    {10, 40}, {10, 35}, {10, 30}, {10, 25}, {10, 20}, {10, 15}, {10, 10},
    {15, 10}, {20, 10}, {25, 10}, {30, 10}, {35, 10}, {40, 10},
};
#define APPLY_STAT_MOD(var, mon, stat, statIndex)                        \
    do {                                                                 \
        (var) = (s32)(stat) * sStatStageRatios[(mon)->statStages[(statIndex)]][0]; \
        (var) /= sStatStageRatios[(mon)->statStages[(statIndex)]][1];    \
    } while (0)

/* Ported verbatim from src/pokemon.c's sHoldEffectToType. */
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

#define IS_TYPE_PHYSICAL(moveType) ((moveType) < TYPE_MYSTERY)
#define IS_TYPE_SPECIAL(moveType)  ((moveType) > TYPE_MYSTERY)

/*
 * Ported from CalculateBaseDamage() (src/pokemon.c:3106-3372). Deviations
 * from the original, all driven by not having a live 4-battler turn in
 * progress (see emerald_calc.h's EmeraldCalcFieldConditions comment):
 *   - powerOverride/typeOverride dropped (no Hidden Power/Weather Ball
 *     dynamic typing; always gBattleMoves[move].{power,type})
 *   - Enigma Berry special-cased item lookup dropped (GetItemHoldEffect()
 *     is used unconditionally)
 *   - ShouldGetStatBadgeBoost() -> field->applyBadgeBoosts (see its doc
 *     comment for the fidelity tradeoff)
 *   - AbilityBattleEffects() mud/water sport field check dropped (no field
 *     state); always "no sport in effect"
 *   - BATTLE_TYPE_FRONTIER Soul Dew check dropped (assumes non-Frontier)
 *   - gCurrentMove (for the Explosion/Self-Destruct defense halving) ->
 *     the `move` parameter
 *   - CountAliveMonsInBattle(BATTLE_ALIVE_DEF_SIDE) == 2 ->
 *     field->targetSideHasTwoAliveMons
 *   - gBattleResources flash-fire flag dropped; always "not triggered"
 * Everything else -- stat stage math, item/ability boosts, weather,
 * screens, badge/burn/self-destruct handling, the final "+2" -- is the
 * original arithmetic, truncation order included.
 */
static s32 CalculateBaseDamage(const EmeraldCalcMon *attacker, const EmeraldCalcMon *defender, u16 move,
                                const EmeraldCalcFieldConditions *field, s32 critMultiplier)
{
    const struct BattleMove *moveData = &gBattleMoves[move];
    s32 damage = 0;
    s32 damageHelper;
    u8 type = moveData->type;
    u16 gBattleMovePower = moveData->power;
    u16 attack, defense;
    u16 spAttack, spDefense;
    u8 attackerHoldEffect = GetItemHoldEffect(attacker->item);
    u8 attackerHoldEffectParam = GetItemHoldEffectParam(attacker->item);
    u8 defenderHoldEffect = GetItemHoldEffect(defender->item);
    u8 defenderHoldEffectParam = GetItemHoldEffectParam(defender->item);
    u32 i;

    attack = attacker->attack;
    defense = defender->defense;
    spAttack = attacker->spAttack;
    spDefense = defender->spDefense;

    if (attacker->ability == ABILITY_HUGE_POWER || attacker->ability == ABILITY_PURE_POWER)
        attack *= 2;

    if (field->applyBadgeBoosts) {
        attack = (110 * attack) / 100;
        defense = (110 * defense) / 100;
        spAttack = (110 * spAttack) / 100;
        spDefense = (110 * spDefense) / 100;
    }

    for (i = 0; i < HOLD_EFFECT_TO_TYPE_COUNT; i++) {
        if (attackerHoldEffect == sHoldEffectToType[i][0] && type == sHoldEffectToType[i][1]) {
            if (IS_TYPE_PHYSICAL(type))
                attack = (attack * (attackerHoldEffectParam + 100)) / 100;
            else
                spAttack = (spAttack * (attackerHoldEffectParam + 100)) / 100;
            break;
        }
    }

    if (attackerHoldEffect == HOLD_EFFECT_CHOICE_BAND)
        attack = (150 * attack) / 100;
    if (attackerHoldEffect == HOLD_EFFECT_SOUL_DEW && (attacker->species == SPECIES_LATIAS || attacker->species == SPECIES_LATIOS))
        spAttack = (150 * spAttack) / 100;
    if (defenderHoldEffect == HOLD_EFFECT_SOUL_DEW && (defender->species == SPECIES_LATIAS || defender->species == SPECIES_LATIOS))
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

    if (defender->ability == ABILITY_THICK_FAT && (type == TYPE_FIRE || type == TYPE_ICE))
        spAttack /= 2;
    if (attacker->ability == ABILITY_HUSTLE)
        attack = (150 * attack) / 100;
    if (attacker->ability == ABILITY_GUTS && attacker->status1)
        attack = (150 * attack) / 100;
    if (defender->ability == ABILITY_MARVEL_SCALE && defender->status1)
        defense = (150 * defense) / 100;
    if (type == TYPE_GRASS && attacker->ability == ABILITY_OVERGROW && attacker->hp <= (attacker->maxHp / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_FIRE && attacker->ability == ABILITY_BLAZE && attacker->hp <= (attacker->maxHp / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_WATER && attacker->ability == ABILITY_TORRENT && attacker->hp <= (attacker->maxHp / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;
    if (type == TYPE_BUG && attacker->ability == ABILITY_SWARM && attacker->hp <= (attacker->maxHp / 3))
        gBattleMovePower = (150 * gBattleMovePower) / 100;

    if (moveData->effect == EFFECT_EXPLOSION)
        defense /= 2;

    if (IS_TYPE_PHYSICAL(type)) {
        if (critMultiplier == 2) {
            if (attacker->statStages[STAT_ATK] > DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damage, attacker, attack, STAT_ATK);
            else
                damage = attack;
        } else {
            APPLY_STAT_MOD(damage, attacker, attack, STAT_ATK);
        }

        damage = damage * gBattleMovePower;
        damage *= (2 * attacker->level / 5 + 2);

        if (critMultiplier == 2) {
            if (defender->statStages[STAT_DEF] < DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damageHelper, defender, defense, STAT_DEF);
            else
                damageHelper = defense;
        } else {
            APPLY_STAT_MOD(damageHelper, defender, defense, STAT_DEF);
        }

        damage = damage / damageHelper;
        damage /= 50;

        if ((attacker->status1 & STATUS1_BURN) && attacker->ability != ABILITY_GUTS)
            damage /= 2;

        if (field->targetHasReflect && critMultiplier == 1) {
            if (field->isDoubleBattle && field->targetSideHasTwoAliveMons)
                damage = 2 * (damage / 3);
            else
                damage /= 2;
        }

        if (field->isDoubleBattle && moveData->target == MOVE_TARGET_BOTH && field->targetSideHasTwoAliveMons)
            damage /= 2;

        if (damage == 0)
            damage = 1;
    }

    if (type == TYPE_MYSTERY)
        damage = 0;

    if (IS_TYPE_SPECIAL(type)) {
        if (critMultiplier == 2) {
            if (attacker->statStages[STAT_SPATK] > DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damage, attacker, spAttack, STAT_SPATK);
            else
                damage = spAttack;
        } else {
            APPLY_STAT_MOD(damage, attacker, spAttack, STAT_SPATK);
        }

        damage = damage * gBattleMovePower;
        damage *= (2 * attacker->level / 5 + 2);

        if (critMultiplier == 2) {
            if (defender->statStages[STAT_SPDEF] < DEFAULT_STAT_STAGE)
                APPLY_STAT_MOD(damageHelper, defender, spDefense, STAT_SPDEF);
            else
                damageHelper = spDefense;
        } else {
            APPLY_STAT_MOD(damageHelper, defender, spDefense, STAT_SPDEF);
        }

        damage = damage / damageHelper;
        damage /= 50;

        if (field->targetHasLightScreen && critMultiplier == 1) {
            if (field->isDoubleBattle && field->targetSideHasTwoAliveMons)
                damage = 2 * (damage / 3);
            else
                damage /= 2;
        }

        if (field->isDoubleBattle && moveData->target == MOVE_TARGET_BOTH && field->targetSideHasTwoAliveMons)
            damage /= 2;

        if (field->weather != 0) {
            if (field->weather & B_WEATHER_RAIN_TEMPORARY) {
                if (type == TYPE_FIRE)
                    damage /= 2;
                else if (type == TYPE_WATER)
                    damage = (15 * damage) / 10;
            }
            if ((field->weather & (B_WEATHER_RAIN | B_WEATHER_SANDSTORM | B_WEATHER_HAIL)) && move == MOVE_SOLAR_BEAM)
                damage /= 2;
            if (field->weather & B_WEATHER_SUN) {
                if (type == TYPE_FIRE)
                    damage = (15 * damage) / 10;
                else if (type == TYPE_WATER)
                    damage /= 2;
            }
        }
    }

    return damage + 2;
}

/*
 * Ported from TypeCalc() (src/battle_script_commands.c:1536-1591), adapted
 * to operate on damage the caller passes in/out rather than the global
 * gBattleMoveDamage, and with the AttacksThisTurn()-gated Wonder Guard
 * double-strike carve-out dropped (that's a specific-move-sequence check
 * we have no turn context for; Wonder Guard's real effect --
 * DOESNT_AFFECT_FOE unless the move is super effective -- still applies).
 */
static void ApplyTypeEffectiveness(u16 move, const EmeraldCalcMon *attacker, const EmeraldCalcMon *defender,
                                    s32 *damage, u8 *flags)
{
    const struct BattleMove *moveData = &gBattleMoves[move];
    u8 moveType = moveData->type;
    s32 i = 0;

    *flags = 0;

    if (attacker->types[0] == moveType || attacker->types[1] == moveType) {
        *damage = *damage * 15;
        *damage = *damage / 10;
    }

    if (defender->ability == ABILITY_LEVITATE && moveType == TYPE_GROUND) {
        *flags |= (EMERALD_MOVE_RESULT_MISSED | EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE);
        return;
    }

    while (TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE) {
        if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT) {
            i += 3;
            continue;
        }
        if (TYPE_EFFECT_ATK_TYPE(i) == moveType) {
            u8 multiplier;
            if (TYPE_EFFECT_DEF_TYPE(i) == defender->types[0]) {
                multiplier = TYPE_EFFECT_MULTIPLIER(i);
                *damage = *damage * multiplier / 10;
                if (*damage == 0 && multiplier != 0)
                    *damage = 1;
                if (multiplier == TYPE_MUL_NO_EFFECT) {
                    *flags |= EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE;
                    *flags &= ~(EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE | EMERALD_MOVE_RESULT_SUPER_EFFECTIVE);
                } else if (multiplier == TYPE_MUL_NOT_EFFECTIVE && moveData->power) {
                    if (*flags & EMERALD_MOVE_RESULT_SUPER_EFFECTIVE)
                        *flags &= ~EMERALD_MOVE_RESULT_SUPER_EFFECTIVE;
                    else
                        *flags |= EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE;
                } else if (multiplier == TYPE_MUL_SUPER_EFFECTIVE && moveData->power) {
                    if (*flags & EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE)
                        *flags &= ~EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE;
                    else
                        *flags |= EMERALD_MOVE_RESULT_SUPER_EFFECTIVE;
                }
            }
            if (TYPE_EFFECT_DEF_TYPE(i) == defender->types[1] && defender->types[0] != defender->types[1]) {
                multiplier = TYPE_EFFECT_MULTIPLIER(i);
                *damage = *damage * multiplier / 10;
                if (*damage == 0 && multiplier != 0)
                    *damage = 1;
                if (multiplier == TYPE_MUL_NO_EFFECT) {
                    *flags |= EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE;
                    *flags &= ~(EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE | EMERALD_MOVE_RESULT_SUPER_EFFECTIVE);
                } else if (multiplier == TYPE_MUL_NOT_EFFECTIVE && moveData->power) {
                    if (*flags & EMERALD_MOVE_RESULT_SUPER_EFFECTIVE)
                        *flags &= ~EMERALD_MOVE_RESULT_SUPER_EFFECTIVE;
                    else
                        *flags |= EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE;
                } else if (multiplier == TYPE_MUL_SUPER_EFFECTIVE && moveData->power) {
                    if (*flags & EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE)
                        *flags &= ~EMERALD_MOVE_RESULT_NOT_VERY_EFFECTIVE;
                    else
                        *flags |= EMERALD_MOVE_RESULT_SUPER_EFFECTIVE;
                }
            }
        }
        i += 3;
    }

    if (defender->ability == ABILITY_WONDER_GUARD
        && !(*flags & (EMERALD_MOVE_RESULT_SUPER_EFFECTIVE)) && moveData->power)
        *flags |= EMERALD_MOVE_RESULT_DOESNT_AFFECT_FOE;
}

/* Ported from Cmd_critcalc (src/battle_script_commands.c:1253-1287),
 * returning a probability instead of rolling Random() -- this tool
 * predicts, it doesn't play a turn. gStatuses3 CANT_SCORE_A_CRIT and the
 * tutorial/first-battle no-crit flags aren't tracked (no turn/overworld
 * context); both default to "not blocked". */
static double CritChance(const EmeraldCalcMon *attacker, const EmeraldCalcMon *defender, u16 move)
{
    const struct BattleMove *moveData = &gBattleMoves[move];
    u8 holdEffect = GetItemHoldEffect(attacker->item);
    u32 critChance = 2 * (attacker->hasFocusEnergy != 0)
                    + (moveData->effect == EFFECT_HIGH_CRITICAL)
                    + (moveData->effect == EFFECT_SKY_ATTACK)
                    + (moveData->effect == EFFECT_BLAZE_KICK)
                    + (moveData->effect == EFFECT_POISON_TAIL)
                    + (holdEffect == HOLD_EFFECT_SCOPE_LENS)
                    + 2 * (holdEffect == HOLD_EFFECT_LUCKY_PUNCH && attacker->species == SPECIES_CHANSEY)
                    + 2 * (holdEffect == HOLD_EFFECT_STICK && attacker->species == SPECIES_FARFETCHD);

    if (critChance >= CRIT_CHANCE_TABLE_SIZE)
        critChance = CRIT_CHANCE_TABLE_SIZE - 1;

    if (defender->ability == ABILITY_BATTLE_ARMOR || defender->ability == ABILITY_SHELL_ARMOR)
        return 0.0;

    return 1.0 / sCriticalHitChance[critChance];
}

static EmeraldCalcRollSet BuildRollSet(s32 baseDamage)
{
    EmeraldCalcRollSet set;
    int i;

    /* Ported from ApplyRandomDmgMultiplier() (src/battle_script_commands.c:
     * 1639-1651): randPercent = 100 - (rand % 16) for rand = Random(), i.e.
     * every value 85..100 inclusive occurs for exactly one rand%16 value.
     * Enumerated here instead of rolled. */
    for (i = 0; i < EMERALD_CALC_NUM_ROLLS; i++) {
        s32 randPercent = 100 - i;
        s32 d = baseDamage;
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

EmeraldCalcResult EmeraldCalc_ComputeDamage(const EmeraldCalcMon *attacker, const EmeraldCalcMon *defender,
                                             u16 move, const EmeraldCalcFieldConditions *field)
{
    EmeraldCalcResult result;
    const struct BattleMove *moveData = &gBattleMoves[move];
    s32 normalBase, critBase;

    memset(&result, 0, sizeof(result));

    if (moveData->power == 0) {
        result.movePowerIsZero = TRUE;
        return result;
    }

    normalBase = CalculateBaseDamage(attacker, defender, move, field, 1);
    critBase = CalculateBaseDamage(attacker, defender, move, field, 2);

    if (field->attackerIsCharged && moveData->type == TYPE_ELECTRIC) {
        normalBase *= 2;
        critBase *= 2;
    }
    if (field->attackerHasHelpingHand) {
        normalBase = normalBase * 15 / 10;
        critBase = critBase * 15 / 10;
    }

    ApplyTypeEffectiveness(move, attacker, defender, &normalBase, &result.typeEffectivenessFlags);
    {
        u8 unusedFlags;
        ApplyTypeEffectiveness(move, attacker, defender, &critBase, &unusedFlags);
    }

    result.isImmune = (result.typeEffectivenessFlags & EMERALD_MOVE_RESULT_NO_EFFECT) ? TRUE : FALSE;
    if (result.isImmune) {
        normalBase = 0;
        critBase = 0;
    }

    result.normal = BuildRollSet(normalBase);
    result.critical = BuildRollSet(critBase);
    result.critChance = CritChance(attacker, defender, move);

    return result;
}

EmeraldCalcKOChances EmeraldCalc_KOChances(const EmeraldCalcRollSet *rolls, u16 targetHp, u8 moveAccuracy)
{
    EmeraldCalcKOChances result;
    int i, koCount = 0;
    u8 acc = moveAccuracy == 0 ? 100 : moveAccuracy;

    for (i = 0; i < EMERALD_CALC_NUM_ROLLS; i++) {
        if (rolls->rolls[i] >= targetHp)
            koCount++;
    }

    result.ohkoChance = (double)koCount / EMERALD_CALC_NUM_ROLLS;
    result.accuracyAdjustedOhkoChance = result.ohkoChance * acc / 100.0;
    result.guaranteedKoTurns = rolls->min > 0 ? (s32)((targetHp + rolls->min - 1) / rolls->min) : -1;
    result.possibleKoTurns = rolls->max > 0 ? (s32)((targetHp + rolls->max - 1) / rolls->max) : -1;

    return result;
}
