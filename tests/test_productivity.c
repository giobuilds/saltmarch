/* test_productivity.c  --  what a worker is worth
 * (LIFE_PLAN Phase 8) */

#include "game.h"
#include "island.h"
#include "resident.h"
#include "calendar.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* An ordinary worker: grown, unmarried, new to the job. */
static void worker(Resident *r, int years)
{
    memset(r, 0, sizeof(*r));
    r->active      = 1;
    r->id          = 1;
    r->age_months  = (int32_t)(years * MONTHS_PER_YEAR);
    r->home_idx    = 0;
    r->spouse      = -1;
    r->birth_house = -1;
}

/* ---- 1. the band ------------------------------------------ */
static void test_the_band(void)
{
    Resident r;

    printf("\n=== nobody leaves the band ===\n");

    /* The worst case the model can build: a retired, unmarried,
     * inexperienced resident in a starving house. */
    worker(&r, 70);
    CHECK(resident_productivity(&r, 0) >= PRODUCTIVITY_MIN,
          "the most miserable worker still clears the floor");

    /* And the best: a prime adult, married, veteran, well fed. */
    worker(&r, 35);
    r.spouse        = 1;
    r.tenure_months = PROD_TENURE_FULL * 2;   /* past the ramp */
    CHECK(resident_productivity(&r, HAPPINESS_MAX) <= PRODUCTIVITY_MAX,
          "and the happiest does not clear the ceiling");
    CHECK(resident_productivity(&r, HAPPINESS_MAX) > PRODUCTIVITY_BASE,
          "though they are worth more than an ordinary one");
}

/* ---- 2. FOUR INPUTS, MOVED ONE AT A TIME ------------------- */
static void test_the_inputs_are_independent(void)
{
    Resident base, r;
    int      b;

    printf("\n=== four inputs, and none of them is the others ===\n");

    worker(&base, 30);
    b = resident_productivity(&base, HAPPINESS_NEUTRAL);

    /* PRIME: a youth of twelve works at the baseline, an adult above
     * it. Stated as a bonus for being grown rather than a penalty for
     * being young, so a young island is not charged twice for a fact it
     * already pays for in dependants. */
    r = base; r.age_months = 13 * MONTHS_PER_YEAR;
    CHECK(resident_productivity(&r, HAPPINESS_NEUTRAL) < b,
          "a youth is worth less than a grown worker");
    CHECK(resident_productivity(&r, HAPPINESS_NEUTRAL) >= PRODUCTIVITY_BASE,
          "but not less than an ordinary one — youth is not a penalty");

    /* MARRIED */
    r = base; r.spouse = 1;
    CHECK(resident_productivity(&r, HAPPINESS_NEUTRAL) > b,
          "somebody to come home to is worth something");

    /* FED — and this is the ONLY one a bad harvest can touch. */
    CHECK(resident_productivity(&base, HAPPINESS_MAX) > b,
          "a house with luxuries in it works better");
    CHECK(resident_productivity(&base, 0) < b,
          "and a starving one works worse");

    /* TENURE, read here for the first time since Phase 3 tracked it. */
    r = base; r.tenure_months = PROD_TENURE_FULL;
    CHECK(resident_productivity(&r, HAPPINESS_NEUTRAL) > b,
          "and knowing the work is worth something too");
    {
        Resident half = base;
        half.tenure_months = PROD_TENURE_FULL / 2;
        CHECK(resident_productivity(&half, HAPPINESS_NEUTRAL) > b &&
              resident_productivity(&half, HAPPINESS_NEUTRAL) <
              resident_productivity(&r, HAPPINESS_NEUTRAL),
              "and it ramps rather than arriving all at once");
    }

    /* THE POINT OF ALL OF THAT: no single input can span the band, so
     * one bad harvest cannot move a worker from the ceiling to the
     * floor. This is section 6's rule stated as arithmetic. */
    {
        int fed_swing = resident_productivity(&base, HAPPINESS_MAX)
                      - resident_productivity(&base, 0);
        CHECK(fed_swing < PRODUCTIVITY_MAX - PRODUCTIVITY_MIN,
              "no one input spans the whole band — a famine cannot move "
              "them all");
    }
}

/* ---- 3. the household average ----------------------------- */
static void test_the_household_average(void)
{
    Resident r[3];

    printf("\n=== and a workplace is paid the household's average ===\n");

    worker(&r[0], 30);
    worker(&r[1], 30); r[1].id = 2;
    r[1].tenure_months = PROD_TENURE_FULL;      /* a veteran */
    worker(&r[2], 4);  r[2].id = 3;             /* a small child */

    {
        int one = resident_productivity(&r[0], HAPPINESS_NEUTRAL);
        int two = resident_productivity(&r[1], HAPPINESS_NEUTRAL);
        int avg = residents_house_productivity(r, 3, 0, HAPPINESS_NEUTRAL);

        CHECK(avg > one && avg < two,
              "the average sits between the newcomer and the veteran");
        CHECK(avg == (one + two) / 2,
              "and the child is not in it — they hold no job to be "
              "worth anything at");
    }

    /* A woman carrying a child holds no job either, so she is not
     * averaged in. */
    r[1].pregnancy = PREGNANCY_MONTHS;
    CHECK(residents_house_productivity(r, 3, 0, HAPPINESS_NEUTRAL) ==
          resident_productivity(&r[0], HAPPINESS_NEUTRAL),
          "nor is somebody who is carrying");

    /* An empty house answers with the baseline rather than zero — a
     * building with nobody in it is gated on worker_count elsewhere,
     * and a zero here would multiply its production away. */
    CHECK(residents_house_productivity(r, 3, 9, HAPPINESS_NEUTRAL) ==
          PRODUCTIVITY_BASE,
          "and a house nobody lives in answers with the baseline");
}

/* ---- 4. THE STALL THAT NEARLY SHIPPED --------------------- */
/* `advance * percent / 100` is 0 for a lone worker at the floor, which
 * is a building that never produces rather than one that produces
 * slowly. Driven through the REAL sim, at one worker, in a starving
 * house — the exact case a fully-staffed fixture cannot see. */
static void test_a_lone_miserable_worker_still_works(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        i, hut = -1, before, t;

    printf("\n=== one hungry worker still lands something ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4242u);
    isl = game_cur_island(gs);

    /* A warehouse, a road, a house and a coastal hut. */
    {
        int wr, wc, laid = 0;
        for (wr = 0; wr + 4 < MAP_ROWS && !laid; wr++)
            for (wc = 0; wc + 2 < MAP_COLS && !laid; wc++) {
                if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                    continue;
                if (!building_can_place(&isl->map, BUILDING_ROAD, wr+2, wc))
                    continue;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, wr+3, wc))
                    continue;
                /* BESIDE the road, not below the house. Connectivity. */
                if (!building_can_place(&isl->map, BUILDING_FISHERS_HUT,
                                        wr+2, wc+1))
                    continue;
                game_place_building(gs, wr,   wc,   BUILDING_WAREHOUSE, 1);
                game_place_building(gs, wr+2, wc,   BUILDING_ROAD,      1);
                game_place_building(gs, wr+3, wc,   BUILDING_HOUSE,     1);
                game_place_building(gs, wr+2, wc+1, BUILDING_FISHERS_HUT, 1);
                laid = 1;
            }
        if (!laid) { printf("  FAIL: nowhere to build\n"); failures++;
                     game_free(gs); return; }
    }

    for (t = 0; t < 400; t++) sim_run_one_tick(gs);
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_FISHERS_HUT) hut = i;
    if (hut < 0) { printf("  FAIL: no hut\n"); failures++;
                   game_free(gs); return; }

    /* Held JUST above the bottom of the ladder: miserable enough for
     * the hunger penalty, never so miserable that the house empties. */
    before = isl->stockpile.amount[RES_FISH];
    for (t = 0; t < 6000; t++) {
        sim_run_one_tick(gs);
        for (i = 0; i < isl->building_count; i++)
            if (isl->pop_data[i].active) isl->pop_data[i].happiness = 2;
    }

    CHECK(isl->stockpile.amount[RES_FISH] > before,
          "a miserable crew still lands fish — a penalty is a slowdown, "
          "never a stall");
    game_free(gs);
}

int main(void)
{
    printf("== what a worker is worth (LIFE_PLAN Phase 8) ==\n");

    test_the_band();
    test_the_inputs_are_independent();
    test_the_household_average();
    test_a_lone_miserable_worker_still_works();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
