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
 * This measures it — and the first version of this test measured it
 * WRONG in a way worth recording, because the same trap is everywhere
 * in this suite:
 *
 *   game_init() seeds from the CLOCK. The test ran one randomly
 *   generated world, reported 50%, and CI ran a different world and
 *   reported 45%. A measurement that changes per run is not a
 *   measurement; it is a sample being quoted as a constant.
 *
 * So: fixed seeds, several of them, and the statistic that matters.
 *
 * THE STATISTIC IS THE WORST SUSTAINED STRETCH, not the worst year.
 * A single bad year is absorbed by the happiness ladder, which is ten
 * months of buffer and exists for exactly this. Five bad years in a row
 * is structural. Across ten seeds the worst year is 41% and the worst
 * five-year mean is 48%, which is why the closure projection uses the
 * latter. */
#define SEEDS_TESTED  4
#define YEARS_RUN     60
#define SAMPLE_CAP    64

static int adult_fraction_for(uint32_t seed, int *sustained, int *mean)
{
    GameState *gs = game_init();
    Island    *isl;
    int        t, hist[SAMPLE_CAP], n = 0, k, lo = 100;
    long       sum = 0;

    if (!gs) return 0;
    game_new_seeded(gs, seed);
    if (!build_village(gs, 8)) { game_free(gs); return 0; }
    isl = game_cur_island(gs);

    for (t = 0; t < YEARS_RUN * 12 * (int)CALENDAR_MONTH_TICKS; t++) {
        /* The larder kept full BY HAND. Storage is capped at a
         * warehouse's worth, so a one-off purchase starves the village
         * in six months and would make this a test of logistics — which
         * is what an earlier version of it accidentally measured. */
        isl->stockpile.amount[RES_FISH]      = 900;
        isl->stockpile.amount[RES_GRAIN]     = 900;
        isl->stockpile.amount[RES_OILSKINS]  = 900;
        isl->stockpile.amount[RES_MARSH_GIN] = 900;
        sim_run_one_tick(gs);

        /* After the first decade, so the founding cohort has stopped
         * dominating the answer. */
        if (t < 10 * 12 * (int)CALENDAR_MONTH_TICKS) continue;
        if (t % (12 * (int)CALENDAR_MONTH_TICKS) != 0) continue;
        {
            int i, pop = 0, ad = 0;
            for (i = 0; i < isl->resident_count; i++) {
                const Resident *r = &isl->residents[i];
                if (!r->active) continue;
                pop++;
                /* WORKERS, NOT ADULTS (LIFE_PLAN Phase 7). The two
                 * stopped being the same number when the reserve
                 * arrived: somebody with no roof is grown and idle, and
                 * a woman carrying a child is grown and not at work.
                 * Neither staffs a supply chain, and this measurement
                 * exists to tell test_closure how many hands an island
                 * really has. */
                if (r->home_idx == RESIDENT_HOMELESS) continue;
                if (r->pregnancy > 0) continue;
                if (resident_stage(r) == LIFE_ADULT) ad++;
            }
            if (pop < 4 || n >= SAMPLE_CAP) continue;
            hist[n] = ad * 100 / pop;
            sum += hist[n];
            n++;
        }
    }
    game_free(gs);
    if (n < 10) return 0;

    for (k = 0; k + 5 <= n; k++) {
        int j, acc = 0;
        for (j = 0; j < 5; j++) acc += hist[k + j];
        if (acc / 5 < lo) lo = acc / 5;
    }
    *sustained = lo;
    *mean      = (int)(sum / n);
    return 1;
}

static void test_the_adult_fraction(void)
{
    static const uint32_t SEEDS[SEEDS_TESTED] = { 1u, 7u, 12345u, 777u };
    int worst = 100, mean_sum = 0, measured = 0, i;

    printf("\n=== and how many of them can work ===\n");

    for (i = 0; i < SEEDS_TESTED; i++) {
        int sustained = 0, mean = 0;
        if (!adult_fraction_for(SEEDS[i], &sustained, &mean)) continue;
        printf("        seed %-6u  worst 5-year mean %d%%,  overall %d%%\n",
               SEEDS[i], sustained, mean);
        if (sustained < worst) worst = sustained;
        mean_sum += mean;
        measured++;
    }

    CHECK(measured >= 2, "at least two worlds ran long enough to measure");
    if (measured == 0) return;

    /* THE NUMBER LIFE_PLAN GUESSED AT 55%, and which Phase 5 measured
     * at 48% when every resident arrived grown. Phase 6b makes an
     * island raise its own people, and the floor fell to 33% — where it
     * sits on every seed tried, because it is not really a statistic at
     * all. It is the shape of a household: two parents and four
     * children under one roof is a third of the house able to work, and
     * every house that fills does it the same way.
     *
     * PHASE 7 TOOK IT FURTHER, to 12-14%, and the number changed
     * MEANING as well as value: it counts WORKERS now, not adults.
     * Somebody with no roof is grown and idle, and a woman carrying a
     * child is grown and not at work — neither staffs a chain, and
     * test_closure needs to know how many hands an island really has.
     *
     * Where it went: a household is two parents and about eight minors,
     * she is pregnant a third of her fertile life, and the reserve eats
     * without working.
     *
     * THAT TROUGH IS NOT A TROUGH ANY MORE — it is the steady state, and
     * no tier closes at it. That is a design position (the marketplace
     * is meant to cover the gap) and a known-unfunded one: see
     * test_closure.c, which now measures the SIZE of the gap rather
     * than asserting it away.
     *
     * Asserted a little below the measured value, not at it, so an
     * unluckier seed does not fail a number that has not moved. */
    CHECK(worst >= 10,
          "through the years it is raising children, roughly an eighth "
          "of the island is at work");
    CHECK(mean_sum / measured >= 15,
          "and a sixth of it is, taken over a lifetime");
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
