/* test_closure.c  --  can an island staff its own supply?
 * (NEEDS_PLAN Phase 5) */

#include "building.h"
#include "population.h"
#include "resource.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- the two limits --------------------------------------- */
#define SURVIVAL_MAX   0.80
#define THE_WALL       1.00

/* ---- the chain walk --------------------------------------- */
#define UNPRODUCIBLE  (-1.0)
#define CYCLIC        (-2.0)

static int visiting[RES_COUNT];

/* The single building that makes `g`, or -1. */
static int producer_of(ResourceType g)
{
    int b;
    for (b = 0; b < BUILDING_TYPE_COUNT; b++) {
        const BuildingDef *d = &BUILDING_DEFS[b];
        if (d->produces == g && d->tick_seconds > 0.0f && d->produce_amt > 0)
            return b;
    }
    return -1;
}

static int producer_count(ResourceType g)
{
    int b, n = 0;
    for (b = 0; b < BUILDING_TYPE_COUNT; b++) {
        const BuildingDef *d = &BUILDING_DEFS[b];
        if (d->produces == g && d->tick_seconds > 0.0f && d->produce_amt > 0)
            n++;
    }
    return n;
}

/* Workers needed to sustain `rate` units/sec of `g`, inputs included. */
static double chain_workers(ResourceType g, double rate)
{
    const BuildingDef *d;
    double workers, total;
    int    b, i;

    if (visiting[g]) return CYCLIC;       /* a good in its own chain */
    b = producer_of(g);
    if (b < 0) return UNPRODUCIBLE;

    visiting[g] = 1;
    d = &BUILDING_DEFS[b];

    /* Units per second one worker sustains, times the full-crew bonus: */
    {
        int    cap  = building_worker_cap(d);
        double per  = (double)building_work_advance(d, cap) / (double)cap;
        workers = rate * (double)d->tick_seconds / (double)d->produce_amt / per;
    }
    total = workers;

    for (i = 0; i < MAX_BUILDING_INPUTS; i++) {
        double sub;
        if (d->consumes[i] == RES_COUNT) continue;
        /* Making `rate` of the output needs rate*consume/produce of each */
        sub = chain_workers(d->consumes[i],
                            rate * (double)d->consume_amt[i]
                                / (double)d->produce_amt);
        if (sub < 0.0) { visiting[g] = 0; return sub; }
        total += sub;
    }
    visiting[g] = 0;
    return total;
}

/* Workers per resident for one good at a full house's demand. */
static double good_ratio(ResourceType g)
{
    double per_tick = (double)tier_good_amount(g, HOUSE_CAPACITY);
    double w;

    memset(visiting, 0, sizeof(visiting));
    w = chain_workers(g, per_tick / (double)NEEDS_INTERVAL);
    return w < 0.0 ? w : w / (double)HOUSE_CAPACITY;
}

/* ---- one tier's bill --------------------------------------- */
typedef struct {
    const char  *label;
    BuildingType house;
    double       basics;      /* workers/resident, survival goods only  */
    double       total;       /* including every luxury                 */
    ResourceType worst;       /* the single most expensive good         */
    double       worst_cost;
    /* Tracked apart from `worst` because the two assertions are about
     * different lists, and a diagnostic that names a luxury while
     * failing a survival limit sends the reader to the wrong table. */
    ResourceType worst_basic;
    double       worst_basic_cost;
    /* Split by whether the tier's demand scales with the number of
     * MOUTHS or with the number of HOUSES — tier_good_amount's rule. */
    double       raw, refined;
    int          unpriceable; /* a need nothing produces, or a cycle    */
} TierBill;

static TierBill tier_bill(BuildingType house, const char *label)
{
    const TierDef *t = tier_def_for(house);
    ResourceType   basic[MAX_TIER_GOODS];
    TierBill       bill;
    int            n, i;

    memset(&bill, 0, sizeof(bill));
    bill.label = label;
    bill.house = house;
    bill.worst       = RES_COUNT;
    bill.worst_basic = RES_COUNT;
    if (!t) { bill.unpriceable = 1; return bill; }

    /* BUILDING_NONE origin: for every tier but Scholars this is just */
    n = tier_basic_needs(t, BUILDING_NONE, basic);

    for (i = 0; i < n; i++) {
        double r = good_ratio(basic[i]);
        if (r < 0.0) { bill.unpriceable = 1; continue; }
        bill.basics += r;
        if (RESOURCE_CATEGORIES[basic[i]] == RCAT_RAW) bill.raw += r;
        else                                           bill.refined += r;
        if (r > bill.worst_cost) { bill.worst_cost = r; bill.worst = basic[i]; }
        if (r > bill.worst_basic_cost) {
            bill.worst_basic_cost = r;
            bill.worst_basic      = basic[i];
        }
    }
    bill.total = bill.basics;

    for (i = 0; i < MAX_TIER_GOODS; i++) {
        double r;
        if (t->luxury[i] == RES_COUNT) continue;
        r = good_ratio(t->luxury[i]);
        if (r < 0.0) { bill.unpriceable = 1; continue; }
        bill.total += r;
        if (RESOURCE_CATEGORIES[t->luxury[i]] == RCAT_RAW) bill.raw += r;
        else                                               bill.refined += r;
        if (r > bill.worst_cost) {
            bill.worst_cost = r;
            bill.worst      = t->luxury[i];
        }
    }
    return bill;
}

/* Every tier, in the order a player meets them. */
static const struct { BuildingType house; const char *label; } TIERS[] = {
    { BUILDING_HOUSE,          "Marshfolk" },
    { BUILDING_HOUSE_WORKER,   "Wrights"   },
    { BUILDING_HOUSE_MERCHANT, "Merchants" },
    { BUILDING_HOUSE_ARTISAN,  "Artisans"  },
    { BUILDING_HOUSE_ENGINEER, "Engineers" },
    { BUILDING_HOUSE_INVESTOR, "Investors" },
    { BUILDING_HOUSE_SCHOLAR,  "Scholars"  },
};
#define TIER_COUNT (int)(sizeof(TIERS) / sizeof(TIERS[0]))

/* ---- 1. the arithmetic itself ------------------------------
 * Before believing anything the walk says about a seven-good tier,
 * check it against a number a person can do in their head. */
static void test_the_maths(void)
{
    const BuildingDef *hut = &BUILDING_DEFS[BUILDING_FISHERS_HUT];
    double             r;

    printf("\n=== the arithmetic, against a hand calculation ===\n");

    /* Six residents want six Fish per 30s tick = 0.2/sec. A LONE worker */
    CHECK(hut->produce_amt == 1 && hut->tick_seconds == 6.0f,
          "the Fisher's Hut still lands one Fish every six seconds");
    CHECK(tier_good_amount(RES_FISH, 6) == 6,
          "and six people still want six Fish, not one");
    CHECK(building_work_advance(hut, 5) == 9,
          "a full hut of five advances the clock nine, not five");

    memset(visiting, 0, sizeof(visiting));
    r = chain_workers(RES_FISH, 6.0 / 30.0);
    CHECK(r > 0.66 && r < 0.68, "six mouths of Fish is two thirds of a hand");
    CHECK(good_ratio(RES_FISH) > 0.110 && good_ratio(RES_FISH) < 0.112,
          "which is 0.111 workers per resident");

    /* And a refined good is charged once however full the house is —
     * the property the ratio depends on most, and the one a future
     * change is most likely to break by accident. */
    CHECK(tier_good_amount(RES_OILSKINS, 6) == 1,
          "a household owns one set of Oilskins, not six");
    CHECK(good_ratio(RES_OILSKINS) < good_ratio(RES_FISH),
          "so a two-step luxury still costs less than a one-step staple");
}

/* ---- 2. the walk can price what it is asked about ---------- */
static void test_every_need_is_priceable(void)
{
    int i, g;

    printf("\n=== every need has exactly one source ===\n");

    /* The walk takes the FIRST producer of a good. That is only. */
    for (i = 0; i < TIER_COUNT; i++) {
        const TierDef *t = tier_def_for(TIERS[i].house);
        ResourceType   basic[MAX_TIER_GOODS];
        int            n = tier_basic_needs(t, BUILDING_NONE, basic), k;

        for (k = 0; k < n; k++)
            if (producer_count(basic[k]) != 1) {
                printf("  FAIL: %s's basic %s has %d producers\n",
                       TIERS[i].label, RESOURCE_NAMES[basic[k]],
                       producer_count(basic[k]));
                failures++;
            }
        for (k = 0; k < MAX_TIER_GOODS; k++) {
            if (t->luxury[k] == RES_COUNT) continue;
            if (producer_count(t->luxury[k]) != 1) {
                printf("  FAIL: %s's luxury %s has %d producers\n",
                       TIERS[i].label, RESOURCE_NAMES[t->luxury[k]],
                       producer_count(t->luxury[k]));
                failures++;
            }
        }
    }
    printf("  ok:   every good any tier wants is made in exactly one place\n");

    /* Nothing a building consumes may be a dead end either, or a chain
     * that looks staffable is actually unbuildable. */
    for (g = 0; g < RES_COUNT; g++) {
        int b;
        for (b = 0; b < BUILDING_TYPE_COUNT; b++) {
            const BuildingDef *d = &BUILDING_DEFS[b];
            int k;
            for (k = 0; k < MAX_BUILDING_INPUTS; k++) {
                if (d->consumes[k] != (ResourceType)g) continue;
                if (producer_of((ResourceType)g) < 0) {
                    printf("  FAIL: %s consumes %s, which nothing makes\n",
                           d->name, RESOURCE_NAMES[g]);
                    failures++;
                }
            }
        }
    }
    printf("  ok:   every input a building takes is made by some building\n");
}

/* ---- 3. THE HEADLINE: an island can staff itself ----------- */
static void test_closure(void)
{
    TierBill bills[TIER_COUNT];
    int      i;

    printf("\n=== workers per resident, at capacity %d ===\n", HOUSE_CAPACITY);

    for (i = 0; i < TIER_COUNT; i++) {
        bills[i] = tier_bill(TIERS[i].house, TIERS[i].label);
        printf("  %-10s %s  basics %.2f  total %.2f   dearest: %s (%.2f)\n",
               bills[i].label,
               BUILDING_DEFS[TIERS[i].house].hud_placeable ? "[build]"
                                                           : "[upgrade]",
               bills[i].basics, bills[i].total,
               bills[i].worst == RES_COUNT ? "-"
                                           : RESOURCE_NAMES[bills[i].worst],
               bills[i].worst_cost);
    }
    printf("\n");

    for (i = 0; i < TIER_COUNT; i++) {
        char msg[160];
        const TierBill *b = &bills[i];

        /* Scope taken from the table, not from a list somebody has to
         * remember to update: a tier is guarded if a player can place
         * one without upgrading into it. Add a placeable house tier and
         * it is covered the same day. */
        if (!BUILDING_DEFS[b->house].hud_placeable) continue;

        snprintf(msg, sizeof(msg),
                 "%s can price every good it needs", b->label);
        CHECK(!b->unpriceable, msg);

        snprintf(msg, sizeof(msg),
                 "%s survives on %.2f workers per resident (limit %.2f) "
                 "— dearest basic is %s",
                 b->label, b->basics, SURVIVAL_MAX,
                 b->worst_basic == RES_COUNT
                     ? "-" : RESOURCE_NAMES[b->worst_basic]);
        CHECK(b->basics <= SURVIVAL_MAX, msg);

        snprintf(msg, sizeof(msg),
                 "%s is fully supplied at %.2f, under the wall at %.2f",
                 b->label, b->total, THE_WALL);
        CHECK(b->total < THE_WALL, msg);
    }
}

/* ---- 4. the guard would notice ------------------------------ */
/* Defined with the age-pyramid section below, where the constant it
 * divides by is documented. Forward-declared because the demonstration
 * that the wall is a real line moved onto the projection at Phase 7. */
static double projected(const TierBill *b);

static void test_the_guard_bites(void)
{
    double fish, gin, banquet;

    printf("\n=== the guard can actually fire ===\n");

    fish    = good_ratio(RES_FISH);        /* one step, per resident   */
    gin     = good_ratio(RES_MARSH_GIN);   /* two steps, per household */
    banquet = good_ratio(RES_BANQUET);     /* deep, per household      */

    printf("        fish %.3f  gin %.3f  banquet %.3f\n", fish, gin, banquet);
    CHECK(banquet > gin, "a deeper chain costs more than a shallow one");
    CHECK(fish + gin < THE_WALL,
          "the opening two goods leave most of the island free to work");

    /* The limits are not vacuous, and this is the evidence: real tiers
     * in this same table fall on the far side of both of them. A
     * threshold no data ever exceeds is decoration. */
    {
        TierBill a = tier_bill(BUILDING_HOUSE_ARTISAN, "Artisans");
        char     msg[160];

        snprintf(msg, sizeof(msg),
                 "Artisans sit at %.2f today and %.2f while a generation "
                 "is raised — the limit is a real line",
                 a.total, projected(&a));
        CHECK(a.total < THE_WALL && projected(&a) > THE_WALL, msg);
    }

    /* And the arithmetic is still sensitive: enough luxuries on the
     * base tier clear the wall on TODAY's numbers, which is the shape
     * of the accident this test exists to catch. Nobody would think of
     * adding a luxury as making a tier unlivable. */
    {
        TierBill m = tier_bill(BUILDING_HOUSE, "Marshfolk");
        CHECK(m.total + 6.0 * banquet > THE_WALL,
              "and six such goods on the base tier would break it");
    }

    /* THE SURVIVAL LIMIT IS NOW UNEXERCISED BY REAL DATA, and saying. */
}

/* ---- 5. the economy under an age pyramid ------------------- */
/* 0.24 SINCE LIFE_PLAN Phase 7b, and it is a WORKER fraction. */
#define ADULT_FRACTION    0.24
#define NON_ADULT_RATION  0.50

static double projected(const TierBill *b)
{
    double demand = b->raw * (ADULT_FRACTION
                              + (1.0 - ADULT_FRACTION) * NON_ADULT_RATION)
                  + b->refined;
    return demand / ADULT_FRACTION;
}

static void test_the_headroom_is_for_something(void)
{
    int i;

    printf("\n=== and what an age pyramid would do to it ===\n");

    for (i = 0; i < TIER_COUNT; i++) {
        TierBill b = tier_bill(TIERS[i].house, TIERS[i].label);

        if (!BUILDING_DEFS[b.house].hud_placeable) continue;
        printf("  %-10s %.2f today -> %.2f projected   (raw %.2f, "
               "refined %.2f)%s\n",
               b.label, b.total, projected(&b), b.raw, b.refined,
               projected(&b) >= THE_WALL ? "   <-- OVER THE WALL" : "");
    }

    /* THE BASE TIER FEEDS ITSELF AGAIN, and this assertion is a real
     * guarantee once more rather than a watchdog on a known imbalance. */
    {
        TierBill m = tier_bill(BUILDING_HOUSE, "Marshfolk");
        char     msg[200];

        snprintf(msg, sizeof(msg),
                 "Marshfolk feed themselves through the worst stretch "
                 "(%.2f, wall %.2f)", projected(&m), THE_WALL);
        CHECK(projected(&m) < THE_WALL, msg);
    }

    /* WRIGHTS BECAME IMPORT-DEPENDENT AT PHASE 6b, BY DECISION — the
     * same decision, and for very nearly the same reason, as the one
     * recorded for Merchants below. */
    {
        TierBill w = tier_bill(BUILDING_HOUSE_WORKER, "Wrights");
        char     msg[200];

        snprintf(msg, sizeof(msg),
                 "Wrights eat nothing raw (%.2f of %.2f) and reach %.2f "
                 "while raising children — import-dependent, by decision",
                 w.raw, w.total, projected(&w));
        CHECK(w.raw < 0.001 && projected(&w) >= THE_WALL, msg);
    }

    /* MERCHANTS ARE IMPORT-ONLY, BY DECISION (LIFE_PLAN Phase 5). */
    {
        TierBill m = tier_bill(BUILDING_HOUSE_MERCHANT, "Merchants");
        char     msg[160];

        snprintf(msg, sizeof(msg),
                 "Merchants eat nothing raw (%.2f of %.2f) and reach %.2f "
                 "in a bad decade — import-only, by decision",
                 m.raw, m.total, projected(&m));
        CHECK(m.raw < 0.001 && projected(&m) >= THE_WALL, msg);
    }
}

int main(void)
{
    printf("== closure (NEEDS_PLAN Phase 5) ==\n");

    test_the_maths();
    test_every_need_is_priceable();
    test_closure();
    test_the_guard_bites();
    test_the_headroom_is_for_something();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
