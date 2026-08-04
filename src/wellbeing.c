/* wellbeing.c -- the six-factor projection. See wellbeing.h. */

#include "wellbeing.h"
#include "population.h"
#include "resident.h"
#include "building.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const WellbeingFactorDef WELLBEING_FACTORS[WB_FACTOR_COUNT] = {
    [WB_SOCIAL]     = { "Social support",  2.7f, 1 },
    [WB_INCOME]     = { "Income",          1.7f, 1 },
    [WB_FREEDOM]    = { "Freedom",         1.5f, 1 },
    [WB_HEALTH]     = { "Health",          1.2f, 1 },
    /* No corruption and no charitable giving exist in the sim. */
    [WB_CORRUPTION] = { "Freedom from corruption", 0.7f, 0 },
    [WB_GENEROSITY] = { "Generosity",      0.2f, 0 }
};

const char *wellbeing_factor_name(int factor)
{
    if (factor < 0 || factor >= WB_FACTOR_COUNT) return "?";
    return WELLBEING_FACTORS[factor].name;
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* An inverted U peaking at WB_DENSITY_PEAK: isolation scores badly and
 * so does overcrowding, which is what pulls against filling every
 * house to the brim. */
static float density_curve(float n)
{
    float d = (n - WB_DENSITY_PEAK) / WB_DENSITY_PEAK;
    return clamp01(1.0f - d * d);
}

/* Weighted sum over the modelled factors only, scaled to the ladder
 * with a floor under it. */
static float combine(const float f[WB_FACTOR_COUNT])
{
    float sum = 0.0f, weight = 0.0f;
    int   i;

    for (i = 0; i < WB_FACTOR_COUNT; i++) {
        if (!WELLBEING_FACTORS[i].modelled) continue;
        sum    += f[i] * WELLBEING_FACTORS[i].weight;
        weight += WELLBEING_FACTORS[i].weight;
    }
    if (weight <= 0.0f) return WB_BASELINE;

    return WB_BASELINE
         + (sum / weight) * (WB_SCALE_MAX - WB_BASELINE);
}

/* Distinct kinds of workplace on the island, which is what "somewhere
 * else to work" means when nobody may be ordered to a job. */
static int job_variety(const UiIsland *isl)
{
    int seen[BUILDING_TYPE_COUNT];
    int i, n = 0;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (!b->active || b->type < 0 || b->type >= BUILDING_TYPE_COUNT)
            continue;
        if (BUILDING_DEFS[b->type].tick_seconds <= 0.0f) continue;
        if (seen[b->type]) continue;
        seen[b->type] = 1; n++;
    }
    return n;
}

/* Freedom and health terms that belong to the island rather than to any
 * one person, so the island score and a resident's share them. */
static float freedom_of_place(const UiIsland *isl)
{
    float variety = clamp01((float)job_variety(isl) / 6.0f);
    float slack   = clamp01((float)isl->homes_empty / 3.0f);
    return clamp01(variety * 0.7f + slack * 0.3f);
}

void wellbeing_island(Wellbeing *out, const UiSnapshot *snap, int island)
{
    const UiIsland *isl;
    float           happy = 0.0f, density = 0.0f;
    int             i, houses = 0, people = 0;

    memset(out, 0, sizeof(*out));
    if (!snap || island < 0 || island >= MAX_ISLANDS) return;
    isl = &snap->islands[island];
    if (!isl->settled) return;

    for (i = 0; i < isl->building_count; i++) {
        const UiBuilding *b = &isl->buildings[i];
        if (!b->active || b->residents == 0) continue;
        houses++;
        people  += b->residents;
        happy   += (float)b->happiness / (float)HAPPINESS_MAX;
        density += density_curve((float)b->residents);
    }

    /* An island nobody lives on has no wellbeing to score. Without this
     * the income term — which is a constant — carries the whole weighted
     * sum on its own, and empty ground reports a number. */
    if (people == 0 && isl->reserve == 0) return;

    if (houses > 0) {
        happy   /= (float)houses;
        density /= (float)houses;
    }

    /* Social support: how full the houses are, against how many people
     * the island is failing to roof at all. */
    {
        float roofless = (people + isl->reserve) > 0
                       ? (float)isl->reserve / (float)(people + isl->reserve)
                       : 0.0f;
        out->factor[WB_SOCIAL] = clamp01(density * (1.0f - roofless));
    }

    /* Income: the wage a worker earns, logarithmically, so doubling a
     * poor island's pay moves the needle and doubling a rich one's
     * barely registers. */
    {
        float wage = (float)WAGE_PER_WORKER * (float)PRODUCTIVITY_BASE / 100.0f;
        out->factor[WB_INCOME] =
            clamp01(logf(1.0f + wage) / logf(1.0f + WB_WAGE_MAX));
    }

    out->factor[WB_FREEDOM] = freedom_of_place(isl);
    out->factor[WB_HEALTH]  = clamp01(happy);
    out->score = combine(out->factor);
}

void wellbeing_resident(Wellbeing *out, const UiSnapshot *snap, int island,
                        const UiResident *r)
{
    const UiIsland *isl;

    memset(out, 0, sizeof(*out));
    if (!snap || !r || island < 0 || island >= MAX_ISLANDS) return;
    isl = &snap->islands[island];

    /* Social support: married, and under a roof of a sane size. */
    {
        float d = r->home_idx >= 0 && r->home_idx < isl->building_count
                ? density_curve((float)isl->buildings[r->home_idx].residents)
                : 0.0f;
        float tenure = clamp01((float)r->tenure_months / WB_TENURE_FULL);
        out->factor[WB_SOCIAL] =
            clamp01(d * 0.5f + (r->married ? 0.3f : 0.0f) + tenure * 0.2f);
    }

    /* Income: their own wage, which varies only through productivity. */
    {
        float wage = (float)WAGE_PER_WORKER * (float)r->productivity / 100.0f;
        if (r->work_idx < 0) wage = 0.0f;
        out->factor[WB_INCOME] =
            clamp01(logf(1.0f + wage) / logf(1.0f + WB_WAGE_MAX));
    }

    /* Freedom: the island's, halved for somebody with no work to go to. */
    out->factor[WB_FREEDOM] = freedom_of_place(isl)
                            * (r->work_idx >= 0 ? 1.0f : 0.5f);

    /* Health: whether they ate, and how much life is left. */
    {
        float fed = r->happiness >= 0
                  ? (float)r->happiness / (float)HAPPINESS_MAX
                  : 0.0f;
        float years = clamp01(1.0f - (float)r->age_years
                                   / (float)LIFE_GUARANTEED_YEARS);
        out->factor[WB_HEALTH] = clamp01(fed * 0.7f + years * 0.3f);
    }

    out->score = combine(out->factor);
}

void wellbeing_describe(const UiResident *r, char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (!r) { out[0] = '\0'; return; }

    if (r->home_idx < 0) {
        snprintf(out, out_len, "%s, %d, has nowhere to live",
                 r->name, (int)r->age_years);
    } else if (r->work_idx >= 0 && r->tenure_months >= 12) {
        snprintf(out, out_len, "%s, %d, has worked the %s for %d years%s",
                 r->name, (int)r->age_years, r->workplace,
                 (int)(r->tenure_months / 12),
                 r->happiness < HAPPINESS_NEUTRAL ? " and is hungry" : "");
    } else if (r->work_idx >= 0) {
        snprintf(out, out_len, "%s, %d, works the %s%s",
                 r->name, (int)r->age_years, r->workplace,
                 r->happiness < HAPPINESS_NEUTRAL ? " and is hungry" : "");
    } else {
        snprintf(out, out_len, "%s, %d, %s%s",
                 r->name, (int)r->age_years,
                 life_stage_name((int)r->stage),
                 r->happiness < HAPPINESS_NEUTRAL ? ", hungry" : "");
    }
}
