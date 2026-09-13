/* Folds an EmeraldCalcResult's enumerated outcomes (damage rolls, crits,
 * branches, hit counts, per-hit accuracy, Focus Band / Endure) into an
 * exact KO probability. */
#include "battle_internal.h"

#include <stdlib.h>
#include <string.h>

void EmeraldCalc_DamageRange(const EmeraldCalcResult *r, s32 *minDamage, s32 *maxDamage)
{
    int i;
    s32 lo = 0, hi = 0;
    bool8 any = FALSE;

    if (r->outcome == EMERALD_CALC_DEALS_DAMAGE) {
        if (r->numSequentialHits) {
            for (i = 0; i < r->numSequentialHits; i++) {
                lo += r->sequentialHits[i].normal.min;
                hi += r->sequentialHits[i].normal.max;
            }
        } else {
            for (i = 0; i < r->numBranches; i++) {
                const EmeraldCalcBranch *b = &r->branches[i];
                if (b->healsTarget)
                    continue;
                if (!any || b->hit.normal.min * r->minHits < lo)
                    lo = b->hit.normal.min * r->minHits;
                if (!any || b->hit.normal.max * r->maxHits > hi)
                    hi = b->hit.normal.max * r->maxHits;
                any = TRUE;
            }
        }
    }
    *minDamage = lo;
    *maxDamage = hi;
}

/* P(the target has taken x damage and is still standing), x in [0, hp),
 * split by whether a Focus Band has already proc'd this move: once
 * gSpecialStatuses[target].focusBanded is set it stays set until the
 * action ends (SpecialStatusesClear() runs between actions), so every
 * later KOing hit of the same move is survived too. */
struct HpDistribution {
    u16 hp;
    double *alive[2]; /* [0] = no band proc yet, [1] = banded */
    double ko;
};

static bool8 Dist_Init(struct HpDistribution *d, u16 hp)
{
    d->hp = hp;
    d->ko = 0.0;
    d->alive[0] = calloc(hp, sizeof(double));
    d->alive[1] = calloc(hp, sizeof(double));
    if (!d->alive[0] || !d->alive[1])
        return FALSE;
    d->alive[0][0] = 1.0;
    return TRUE;
}

static void Dist_Free(struct HpDistribution *d)
{
    free(d->alive[0]);
    free(d->alive[1]);
}

static void Dist_Scale(struct HpDistribution *d, double factor)
{
    int band, x;
    for (band = 0; band < 2; band++)
        for (x = 0; x < d->hp; x++)
            d->alive[band][x] *= factor;
}

/* One hit landing: each of the 16 rolls, crit or not, then
 * adjustnormaldamage's Focus Band roll and KO cap. */
static void Dist_ApplyHit(struct HpDistribution *d, const EmeraldCalcHit *hit, bool8 heals, double critChance,
                          double bandChance, bool8 cannotKo)
{
    const int hp = d->hp;
    double *next[2];
    int band, x, j, crit;

    next[0] = calloc(hp, sizeof(double));
    next[1] = calloc(hp, sizeof(double));
    if (!next[0] || !next[1]) {
        free(next[0]);
        free(next[1]);
        return;
    }

    for (band = 0; band < 2; band++) {
        for (x = 0; x < hp; x++) {
            const double m = d->alive[band][x];
            if (m == 0.0)
                continue;
            for (crit = 0; crit < 2; crit++) {
                const EmeraldCalcRollSet *rolls = crit ? &hit->critical : &hit->normal;
                const double pCrit = crit ? critChance : 1.0 - critChance;
                if (pCrit == 0.0)
                    continue;
                for (j = 0; j < EMERALD_CALC_NUM_ROLLS; j++) {
                    const double q = m * pCrit / EMERALD_CALC_NUM_ROLLS;
                    const s32 dmg = heals ? 0 : rolls->rolls[j];
                    const s32 nx = x + (dmg > 0 ? dmg : 0);
                    /* This hit's own band roll happens before its cap check. */
                    const double banded = band ? q : q * bandChance;
                    const double unbanded = q - banded;
                    if (nx >= hp) {
                        if (cannotKo) {
                            next[band][hp - 1] += unbanded;
                            next[1][hp - 1] += banded;
                        } else {
                            next[1][hp - 1] += banded;
                            d->ko += unbanded;
                        }
                    } else {
                        next[0][nx] += unbanded;
                        next[1][nx] += banded;
                    }
                }
            }
        }
    }

    free(d->alive[0]);
    free(d->alive[1]);
    d->alive[0] = next[0];
    d->alive[1] = next[1];
}

static s32 HitsToKo(s32 perUse, u16 hp)
{
    return perUse > 0 ? (s32)((hp + perUse - 1) / perUse) : -1;
}

EmeraldCalcKO EmeraldCalc_KOChance(const EmeraldCalcResult *r, u16 targetHp)
{
    EmeraldCalcKO ko;
    s32 minDamage, maxDamage;
    double bandChance;
    int i, n;

    memset(&ko, 0, sizeof(ko));
    EmeraldCalc_DamageRange(r, &minDamage, &maxDamage);
    ko.guaranteedHitsToKo = HitsToKo(minDamage, targetHp);
    ko.possibleHitsToKo = HitsToKo(maxDamage, targetHp);

    if (r->outcome != EMERALD_CALC_DEALS_DAMAGE || targetHp == 0)
        return ko;

    // (Random() % 100) < holdEffectParam
    bandChance = r->focusBandChance ? RandomModBelow(100, r->focusBandChance) : 0.0;

    if (r->numSequentialHits) {
        struct HpDistribution d;
        if (!Dist_Init(&d, targetHp))
            return ko;
        for (i = 0; i < r->numSequentialHits; i++) {
            if (i > 0 && r->accuracyPerHit)
                Dist_Scale(&d, r->accuracy); // a later hit only lands if its own check passes
            Dist_ApplyHit(&d, &r->sequentialHits[i], FALSE, r->critChance, bandChance, r->cannotKo);
        }
        ko.chanceIfHits = d.ko;
        Dist_Free(&d);
    } else {
        for (i = 0; i < r->numBranches; i++) {
            const EmeraldCalcBranch *b = &r->branches[i];
            struct HpDistribution d;
            if (b->probability == 0.0)
                continue;
            if (!Dist_Init(&d, targetHp))
                return ko;
            for (n = 1; n <= r->maxHits; n++) {
                Dist_ApplyHit(&d, &b->hit, b->healsTarget, r->critChance, bandChance, r->cannotKo);
                ko.chanceIfHits += b->probability * r->hitCountChance[n] * d.ko;
            }
            Dist_Free(&d);
        }
    }

    ko.chance = r->accuracy * ko.chanceIfHits;
    return ko;
}
