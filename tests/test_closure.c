/*  test_closure.c  --  can an island staff its own supply?
 *                      (NEEDS_PLAN Phase 5)
 *
 * THE PROPERTY
 * ============
 * Every producing building ticks only while an agent is physically
 * working in it, and every agent is somebody's resident. So the
 * load-bearing number in this economy is WORKERS PER RESIDENT: how many
 * people must be at work to keep one person supplied. At 1.0 the economy
 * eats itself — each new resident brings exactly enough labour to feed
 * themselves and nothing is left over for warehouses, harbours, ships or
 * expeditions. Above 1.0 it diverges, and no amount of play fixes it.
 *
 * That number is not written down anywhere. It EMERGES from
 * BUILDING_DEFS: from tick rates, from how many inputs a good takes, and
 * from how deep its chain runs. Which means it can be moved by a change
 * that looks entirely local — one new luxury good on one tier — and
 * nothing in the game will say so. A player finds out by watching their
 * island fail to grow and having no idea why.
 *
 * So this test computes it. It walks each tier's needs down to raw
 * goods, sums the fractional workers required, and fails if a tier a
 * player can BUILD DIRECTLY cannot be staffed.
 *
 * WHY IT CHARGES WHAT pop_update CHARGES
 * ======================================
 * The per-tick demand comes from tier_good_amount(), the same function
 * the simulation consumes stock with. This test having its own copy of
 * "raw scales with mouths, refined is per household" would be worse than
 * having no test: it would go on certifying the economy it was written
 * against, green, long after the game had changed underneath it.
 *
 * WHAT IT DELIBERATELY DOES NOT GUARD
 * ===================================
 * Upgrade-only tiers. Artisans need ~2.0 workers per resident and that
 * is not a bug — the upper tiers are SUPPOSED to be net importers, which
 * is what the sea is for. An island closes on its mix (roughly one
 * artisan house per nine cottages), not tier by tier. Their ratios are
 * printed, because a reviewer should see them; they are not asserted,
 * because asserting them would be asserting the wrong thing.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

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

/* ---- the two limits ---------------------------------------
 *
 * THE WALL is 1.0 and is arithmetic, not taste: a tier needing more
 * workers than it houses can never reach its own capacity, however well
 * it is played.
 *
 * THE SURVIVAL LIMIT is 0.80 and applies to BASICS ALONE, because
 * basics are the half that kills. A house short of a luxury stops
 * growing; a house short of a basic loses people. Twenty percent of an
 * island's labour left over is what pays for the warehouses, roads,
 * harbours and crews that no production chain accounts for.
 *
 * The split is what makes both numbers defensible. A single 0.80 over
 * the whole need list would have to be argued down every time a tier
 * gains a luxury — and an argument a threshold loses repeatedly is a
 * threshold that ends up deleted. */
#define SURVIVAL_MAX   0.80
#define THE_WALL       1.00

/* ---- the chain walk ---------------------------------------
 *
 * Rates are in units per second throughout. A WORKER making produce_amt
 * every tick_seconds supplies produce_amt/tick_seconds, so sustaining
 * `rate` takes rate*tick_seconds/produce_amt of them — a fractional
 * headcount, which is the right unit: half a shepherd means one working
 * half the time.
 *
 * SINCE LIFE_PLAN PHASE 1 THE DEF'S RATE IS THE PER-WORKER RATE, so the
 * formula below is unchanged and what it counts has changed name: it
 * was fractional BUILDINGS when a workplace held exactly one person,
 * and it is fractional WORKERS now that a Fisher's Hut holds five.
 *
 * That is why Phase 1 could not move these numbers and did not: five
 * workers landing five fish leaves output per worker exactly where it
 * was, so what building_worker_cap() decided was how many BUILDINGS
 * those workers stand in — land and capital, not labour.
 *
 * PHASE 2 MOVES THEM, DELIBERATELY. A full crew is worth more than the
 * sum of its hands: building_work_advance() returns 2w-1, so output per
 * worker at a full workplace is (2c-1)/c — 1.67 at a workshop's three,
 * 1.83 at a factory's six. Every ratio below divides by that.
 *
 * MODELLED AT FULL STAFFING, which is the best case and therefore the
 * right one for a guard: a tier that cannot close with every workplace
 * full cannot close at all. It is also the case the design pushes a
 * player toward, since the bonus exists precisely to make filling a
 * workplace worth doing.
 *
 * Doubles are fine here and nowhere near the sim. This is a property of
 * the def table computed at test time; nothing it produces is hashed,
 * saved or sent. */
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

    /* Units per second one worker sustains, times the full-crew bonus:
     * a crew of `cap` advances the clock 2*cap-1 rather than cap, so
     * each of them is worth (2c-1)/c. Asked of the real function rather
     * than restated here, so a retuned curve cannot leave this test
     * certifying the old economy. */
    {
        int    cap  = building_worker_cap(d);
        double per  = (double)building_work_advance(d, cap) / (double)cap;
        workers = rate * (double)d->tick_seconds / (double)d->produce_amt / per;
    }
    total = workers;

    for (i = 0; i < MAX_BUILDING_INPUTS; i++) {
        double sub;
        if (d->consumes[i] == RES_COUNT) continue;
        /* Making `rate` of the output needs rate*consume/produce of each
         * input, FULL STOP — independent of tick rate, crew size and
         * bonus alike, since all three cancel. Deriving it from `rate`
         * rather than from the worker count is what keeps that true:
         * the old form multiplied the headcount by consume/tick_seconds,
         * which was equivalent only while a worker was a building. */
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
     * MOUTHS or with the number of HOUSES — tier_good_amount's rule.
     * Only the first kind shrinks when infants and the retired eat a
     * half ration, so a projection that applied one factor to the whole
     * bill would flatter every tier with a long luxury list. */
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

    /* BUILDING_NONE origin: for every tier but Scholars this is just
     * basic[]. A Scholar's House gets the base tier's food, which is the
     * same fallback pop_update uses for a house with no recorded
     * history. Scholars are not asserted on, so the choice costs
     * nothing beyond the printed number being the cheapest of their
     * several possible pasts. */
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

    /* Six residents want six Fish per 30s tick = 0.2/sec. A LONE worker
     * lands 1 every 6s = 0.1667/sec — but a full hut of five advances
     * the clock 9 rather than 5, so each of the five is worth 1.8 of
     * that: 0.3/sec each. Sustaining 0.2/sec therefore takes 0.667 of a
     * worker, and 0.667/6 = 0.111 workers per resident.
     *
     * Before Phase 2 the same sum gave 1.2 and 0.2. Both numbers moving
     * by exactly the crew bonus is the whole claim of this phase. */
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

    /* The walk takes the FIRST producer of a good. That is only a
     * meaningful answer while there is one — the day a second Bakehouse
     * variant appears with a different rate, this assertion fires and
     * somebody decides which one the ratio should assume, instead of
     * the test quietly reporting whichever sorts earlier. */
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

/* ---- 4. the guard would notice ------------------------------
 * A threshold nobody has seen fire is a threshold nobody trusts. This
 * builds the failure on purpose — a plausible new luxury with a
 * three-deep chain — and checks the arithmetic moves the way the guard
 * assumes it does. */
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
     * threshold no data ever exceeds is decoration.
     *
     * BOTH OF THESE MOVED AT PHASE 7 and the reason is worth recording,
     * because it is the one genuinely good thing the household change
     * did to the economy. Refined goods are charged PER HOUSE, so
     * raising HOUSE_CAPACITY from six to ten spread every per-household
     * bill over two-thirds more people. Artisans fell from over the
     * wall to 0.76, and four Banquets on the base tier stopped breaking
     * it.
     *
     * So the demonstration moved to where the guard now actually lives:
     * the PROJECTION. Today's numbers are comfortable and the projected
     * ones are not, which is precisely the gap this phase opened. */
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

    /* THE SURVIVAL LIMIT IS NOW UNEXERCISED BY REAL DATA, and saying so
     * is better than quietly dropping the assertion that used to prove
     * it was not. Before Phase 2, Engineers needed 1.09 workers per
     * resident on basics alone; the crew bonus took them to 0.61 and no
     * tier in the game exceeds 0.80 any more.
     *
     * That limit becomes live again at Phase 5, which is the point of
     * the headroom this phase just bought — see below. */
}

/* ---- 5. the economy under an age pyramid -------------------
 * Both levers are BUILT now (LIFE_PLAN Phase 5): only adults are given
 * agents, and rations are age-weighted for the goods charged per
 * resident. Goods charged per house do not move, which is why the bill
 * is split raw/refined rather than scaled whole.
 *
 * ADULT_FRACTION IS MEASURED, NOT GUESSED. The first version of this
 * section used 0.55, a number invented while writing LIFE_PLAN, and
 * five sections of that document were arithmetic on top of it.
 * tests/test_ageing.c now runs a real island for sixty years and
 * reports the worst year, and this is that floor.
 *
 * THE WORST SUSTAINED STRETCH, not the worst year and not the mean.
 * Across ten seeds: 80% typical, 41% in the single worst year, and 48%
 * averaged over the worst five consecutive years. A single bad year is
 * absorbed by the happiness ladder — ten months of buffer, which exists
 * for exactly this — so the year is the wrong statistic and the
 * five-year mean is the right one. Closure has to hold through a bad
 * decade, because a bad decade is when an island actually fails. */
/* 0.24 SINCE LIFE_PLAN Phase 7b, and it is a WORKER fraction rather
 * than an adult one — the two stopped being the same number when the
 * reserve arrived. tests/test_ageing.c counts only people who are of
 * working age, housed and not carrying a child, because only those
 * three things together staff a supply chain.
 *
 * IT WAS 0.13 AT PHASE 7 AND NOTHING CLOSED. Two things doubled it:
 * work begins at twelve rather than eighteen, so a household's youths
 * are hands instead of dependants; and grown children are no longer
 * evicted from an overfull house, so a family home keeps its unmarried
 * adults instead of pushing them into the reserve to idle.
 *
 * The base tier closes again at this number — barely, at 0.98 against a
 * wall of 1.00, which is where a founding village ought to sit. The two
 * tiers above it do not, and are import-dependent by decision. */
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
     * guarantee once more rather than a watchdog on a known imbalance.
     *
     * It was 0.89 through Phase 6b, 1.69 at Phase 7 — over the wall,
     * with the test reduced to measuring the size of the gap — and 0.98
     * now. Lowering the working age to twelve and letting a family keep
     * its grown children put the opening tier back inside its own
     * means.
     *
     * 0.98 IS CLOSE AND IS MEANT TO BE. A founding village should spend
     * almost everything it has on staying alive; the margin is what
     * pays for warehouses, roads and the first boat, and there should
     * not be much of it. But it is under the line, which means an
     * island that builds its chains need not import food at all — and
     * that is the difference between a hard game and an impossible
     * one. */
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
     * recorded for Merchants below.
     *
     * Their bill is entirely REFINED (0.47, no raw at all), and a
     * refined good is charged once per household however many live
     * there. That was affordable while a household was six grown
     * lodgers: six workers between them paid one household's bill.
     * Since a house is founded by a COUPLE and fills with children, the
     * same bill falls on two workers for the eighteen years it takes
     * the eldest to grow up — three times the burden, and 0.47 becomes
     * 1.44.
     *
     * Note what does NOT fix it: HOUSE_CAPACITY. The bill is per
     * household either way, and the household has two adults in it
     * whether the ceiling is four or six, so the ratio does not move.
     * The only levers are the tier's own needs list and the demography.
     *
     * So a Wrights town buys its comforts in, like a merchant town, and
     * the sea is what makes that possible. Asserted rather than
     * exempted, so the day somebody rebalances the tier the test says
     * the policy changed instead of quietly passing. */
    {
        TierBill w = tier_bill(BUILDING_HOUSE_WORKER, "Wrights");
        char     msg[200];

        snprintf(msg, sizeof(msg),
                 "Wrights eat nothing raw (%.2f of %.2f) and reach %.2f "
                 "while raising children — import-dependent, by decision",
                 w.raw, w.total, projected(&w));
        CHECK(w.raw < 0.001 && projected(&w) >= THE_WALL, msg);
    }

    /* MERCHANTS ARE IMPORT-ONLY, BY DECISION (LIFE_PLAN Phase 5).
     *
     * They eat nothing raw — Coffee, Flatbread, Rum, Marsh Hats, Wool
     * Cloaks and Plantain Fry are all REFINED, charged per household,
     * so the number of mouths never entered their cost and the half
     * ration cannot touch them. Through a bad demographic decade they
     * clear the wall, and a tier at the wall cannot grow.
     *
     * Three fixes were measured and rejected: charging provisions per
     * person multiplies their bill sixfold (0.55 -> 3.31), adding a raw
     * basic adds to it rather than subtracting (0.55 -> 0.66), and
     * shortening their luxury list changes what the tier is.
     *
     * So the decision is that a merchant town does not feed itself. The
     * sea already supports that; it is arguably what a merchant town
     * IS. This is asserted rather than merely exempted, so that the day
     * somebody makes Merchants self-sufficient the test says the policy
     * has changed rather than silently passing. */
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
