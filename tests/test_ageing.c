/*  test_ageing.c  --  people get older, and some of it kills them
 *                     (LIFE_PLAN Phase 5)
 *
 * Phase 3 gave residents ages and then ignored them. This is where the
 * ages start to matter: they advance, they gate who may work, they
 * decide how big a ration a house eats, and eventually they end.
 *
 * THE ASSERTION THAT MATTERS MOST is the last one. LIFE_PLAN's whole
 * Phase 5 blocker rested on an ADULT FRACTION of 0.55 — a number I made
 * up. Everything downstream of it (which tiers close, what the
 * productivity floor can be, whether Merchants are viable) was
 * arithmetic on a guess. So this test MEASURES it, from a real island
 * run for decades, and the closure projection uses what it finds.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "island.h"
#include "resident.h"
#include "calendar.h"
#include "building.h"
#include "map.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- 1. a month is a month -------------------------------- */
static void test_ageing_rate(void)
{
    Resident r[4];
    PopData  pop[4];
    int      before;

    printf("\n=== a month older, once a month ===\n");
    memset(r, 0, sizeof(r));
    memset(pop, 0, sizeof(pop));
    r[0].active = 1; r[0].id = 1; r[0].age_months = 30 * MONTHS_PER_YEAR;
    pop[0].active = 1; pop[0].residents = 1;

    before = r[0].age_months;
    residents_age(r, 1, pop, 12345u, 300);
    CHECK(r[0].age_months == before + 1, "one call, one month");

    residents_age(r, 1, pop, 12345u, 600);
    residents_age(r, 1, pop, 12345u, 900);
    CHECK(r[0].age_months == before + 3, "and it accumulates");
    CHECK(r[0].tenure_months == 3, "tenure keeps pace with it");

    /* An inactive slot is not a person and does not age. */
    r[1].active = 0; r[1].age_months = 100;
    residents_age(r, 2, pop, 12345u, 1200);
    CHECK(r[1].age_months == 100, "an empty slot does not get older");
}

/* ---- 2. THE LABOUR GATE ----------------------------------- */
static void test_only_adults_work(void)
{
    Resident r[8];
    int      i;

    printf("\n=== children do not go to work ===\n");
    memset(r, 0, sizeof(r));

    for (i = 0; i < 6; i++) { r[i].active = 1; r[i].home_idx = 0; }
    r[0].age_months = 5  * MONTHS_PER_YEAR;   /* child  */
    r[1].age_months = 15 * MONTHS_PER_YEAR;   /* youth  */
    r[2].age_months = 30 * MONTHS_PER_YEAR;   /* adult  */
    r[3].age_months = 44 * MONTHS_PER_YEAR;   /* adult  */
    r[4].age_months = 70 * MONTHS_PER_YEAR;   /* elder  */
    r[5].age_months = 19 * MONTHS_PER_YEAR;   /* adult  */

    CHECK(residents_adults_at(r, 6, 0) == 3,
          "three of six are of working age");
    CHECK(residents_adults_at(r, 6, 1) == 0,
          "and none of them live next door");

    /* Rations: an adult a whole one, everybody else a half, rounded up
     * so a house of children is never fed for free. */
    CHECK(residents_mouths_at(r, 6, 0) == 3 + 2,
          "six people eat five rations — halves round up");

    {
        Resident one[1];
        memset(one, 0, sizeof(one));
        one[0].active = 1; one[0].age_months = 4 * MONTHS_PER_YEAR;
        CHECK(residents_mouths_at(one, 1, 0) == 1,
              "and a lone child still eats something");
    }
}

/* ---- 3. death ---------------------------------------------- */
static void test_death(void)
{
    Resident r[1];
    PopData  pop[1];
    int      i, alive_at_69 = 1;

    printf("\n=== and eventually it ends ===\n");

    /* NOBODY DIES BEFORE THE GUARANTEE. A game where a thirty-year-old
     * can drop dead is a game where a chain fails for no visible
     * reason, which is the whole class of thing this plan exists to
     * stop. */
    for (i = 0; i < 200 && alive_at_69; i++) {
        memset(r, 0, sizeof(r));
        memset(pop, 0, sizeof(pop));
        r[0].active = 1; r[0].id = (uint32_t)i;
        r[0].age_months = (LIFE_GUARANTEED_YEARS - 1) * MONTHS_PER_YEAR;
        pop[0].active = 1; pop[0].residents = 1;
        residents_age(r, 1, pop, 12345u, (uint64_t)i * 300u);
        if (!r[0].active) alive_at_69 = 0;
    }
    CHECK(alive_at_69, "two hundred sixty-nine-year-olds all survive");

    /* But a long-lived one does eventually go, and takes the house's
     * headcount down with them — deaths drive the count, growth
     * follows it. */
    {
        int died = 0, months = 0;
        memset(r, 0, sizeof(r));
        memset(pop, 0, sizeof(pop));
        r[0].active = 1; r[0].id = 77;
        r[0].age_months = LIFE_GUARANTEED_YEARS * MONTHS_PER_YEAR;
        pop[0].active = 1; pop[0].residents = 1;

        while (!died && months < 1200) {
            residents_age(r, 1, pop, 12345u, (uint64_t)(months + 1) * 300u);
            months++;
            if (!r[0].active) died = 1;
        }
        CHECK(died, "and somebody past seventy does not live forever");
        CHECK(pop[0].residents == 0,
              "the house is one smaller for it");
        printf("        (this one lasted %d months past the guarantee)\n",
               months);
    }
}

/* ---- a village whose houses are actually on the road -------
 * Houses in a row below a row of road tiles, so EVERY house is
 * 4-adjacent to pavement. A row of houses beside a single road tile
 * connects exactly one of them, which is how the first version of this
 * measured a five-house village and got one. */
static int build_village(GameState *gs, int houses)
{
    Island *isl = game_cur_island(gs);
    int     wr, wc, laid = 0;

    for (wr = 0; wr + 5 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + houses + 2 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, hr = wr + 3, ok = 1, h;

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            for (h = 0; h < houses; h++) {
                if (!building_can_place(&isl->map, BUILDING_ROAD,  rr, wc + h))
                    ok = 0;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, hr, wc + h))
                    ok = 0;
            }
            if (!ok) continue;

            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            for (h = 0; h < houses; h++) {
                game_place_building(gs, rr, wc + h, BUILDING_ROAD,  1);
                game_place_building(gs, hr, wc + h, BUILDING_HOUSE, 1);
            }
            laid = 1;
        }
    return laid;
}

/* ---- 4. THE HEADLINE: what the adult fraction really is ----
 * LIFE_PLAN assumed 0.55 and built five sections of arithmetic on it.
 * This measures it instead. */
static void test_the_adult_fraction(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        t, samples = 0, worst = 100, best = 0;
    long       sum = 0;

    printf("\n=== and how many of them can work ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    if (!build_village(gs, 8)) {
        printf("  FAIL: could not lay a village\n");
        failures++;
        game_free(gs);
        return;
    }
    isl = game_cur_island(gs);

    /* Sixty years, with the larder kept full BY HAND. Storage is capped
     * at a warehouse's worth, so a one-off purchase starves the village
     * in six months and would make this a test of logistics rather than
     * of demographics — which is exactly what the first version of it
     * measured. */
    for (t = 0; t < 60 * 12 * (int)CALENDAR_MONTH_TICKS; t++) {
        isl->stockpile.amount[RES_FISH]      = 400;
        isl->stockpile.amount[RES_GRAIN]     = 400;
        isl->stockpile.amount[RES_OILSKINS]  = 400;
        isl->stockpile.amount[RES_MARSH_GIN] = 400;
        sim_run_one_tick(gs);

        /* Sample yearly, after the first decade so the founding cohort
         * has stopped dominating. */
        if (t < 10 * 12 * (int)CALENDAR_MONTH_TICKS) continue;
        if (t % (12 * (int)CALENDAR_MONTH_TICKS) != 0) continue;

        {
            int i, pop = 0, adults = 0, pct;
            for (i = 0; i < isl->resident_count; i++) {
                if (!isl->residents[i].active) continue;
                pop++;
                if (resident_stage(&isl->residents[i]) == LIFE_ADULT) adults++;
            }
            if (pop < 4) continue;   /* too few to say anything about */
            pct = adults * 100 / pop;
            sum += pct; samples++;
            if (pct < worst) worst = pct;
            if (pct > best)  best  = pct;
        }
    }

    CHECK(samples > 20, "the island survived long enough to be measured");
    if (samples == 0) { game_free(gs); return; }

    printf("        (%d samples: worst %d%%, mean %ld%%, best %d%%)\n",
           samples, worst, sum / samples, best);

    /* THE NUMBER LIFE_PLAN GUESSED AT 55%. An island peopled by adult
     * immigrants who then age in place sits far above that — which is
     * why nothing in the economy needed rebalancing for Phase 5, and
     * why the projection in test_closure now uses a measured floor
     * rather than an invented one.
     *
     * The floor is asserted, not the mean: closure has to hold in the
     * bad decade, not on average. */
    CHECK(worst >= 50,
          "at its worst, half the island is still of working age");
    CHECK(sum / samples >= 60,
          "and typically far more than that");

    game_free(gs);
}

int main(void)
{
    printf("== ageing (LIFE_PLAN Phase 5) ==\n");

    test_ageing_rate();
    test_only_adults_work();
    test_death();
    test_the_adult_fraction();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
