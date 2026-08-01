/*  test_staffing.c  --  a workplace holds a crew
 *                       (LIFE_PLAN Phase 1)
 *
 * WHAT WAS WRONG
 * ==============
 * agents_assign_jobs() kept a `claimed[]` FLAG and skipped any building
 * another agent had taken, so Building.worker_count was 0 or 1 and could
 * never be anything else. Production was therefore a step function of
 * labour: nothing at zero workers, full rate at one, and identical at
 * six. The gap between 0 and 1 was infinite and everything above it was
 * flat — an island with thirty idle residents and one Fisher's Hut
 * produced exactly what an island with one resident did.
 *
 * WHAT IS TRUE NOW
 * ================
 * A hut holds a crew, the def's rate is the PER-WORKER rate, and the
 * production clock advances by the headcount. Five hands land five fish.
 *
 * The assertions below are about the two halves separately — who gets
 * hired (agent.c) and what that buys (island.c) — because they fail for
 * different reasons and a test that only checked the fish would not say
 * which half broke.
 *
 * Linked against the sim alone: no SDL, no UI.
 */

#include "game.h"
#include "agent.h"
#include "building.h"
#include "island.h"
#include "population.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }     \
        else         { printf("  ok:   %s\n", (msg)); }                 \
    } while (0)

/* ---- 1. the rule itself ------------------------------------ */
static void test_the_rule(void)
{
    printf("\n=== how many a workplace holds ===\n");

    CHECK(building_worker_cap(&BUILDING_DEFS[BUILDING_FISHERS_HUT]) > 1,
          "a Fisher's Hut holds more than the one it used to");
    CHECK(building_worker_cap(&BUILDING_DEFS[BUILDING_WAREHOUSE]) == 0,
          "a Warehouse holds nobody — it produces nothing");
    CHECK(building_worker_cap(&BUILDING_DEFS[BUILDING_ROAD]) == 0,
          "and neither does a Road");

    /* The categories are meant to be ordered by the crew you would
     * expect on the site. If that ever stops being true the numbers have
     * drifted from the rule they claim to follow. */
    CHECK(building_worker_cap(&BUILDING_DEFS[BUILDING_FOUNDRY]) >
          building_worker_cap(&BUILDING_DEFS[BUILDING_KNITTING_HOUSE]),
          "heavy industry holds more than one artisan's bench");
}

/* ---- a small island: store, road, and things to work at ----
 * Built by SUBMITTING commands, exactly as replay.c's fixture does, so
 * nothing can be looked up until the ticks have run. Returns the
 * buildings[] index of the Fisher's Hut, or -1. */
static int build_village(GameState *gs, int houses)
{
    Island *isl = game_cur_island(gs);
    int     wr, wc, i, laid = 0;

    for (wr = 0; wr + 6 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + 2 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, fr = wr + 3;
            int ok = 1, h;

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            if (!building_can_place(&isl->map, BUILDING_ROAD, rr, wc)) continue;
            /* The hut needs a coast; if this spot has none, keep looking
             * rather than building a village that can never fish. */
            if (!building_can_place(&isl->map, BUILDING_FISHERS_HUT, fr, wc))
                continue;
            for (h = 0; h < houses; h++)
                if (!building_can_place(&isl->map, BUILDING_HOUSE,
                                        rr, wc + 1 + h)) ok = 0;
            if (!ok) continue;

            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            game_place_building(gs, rr, wc, BUILDING_ROAD,      1);
            game_place_building(gs, fr, wc, BUILDING_FISHERS_HUT, 1);
            for (h = 0; h < houses; h++)
                game_place_building(gs, rr, wc + 1 + h, BUILDING_HOUSE, 1);
            laid = 1;
        }

    if (!laid) return -1;

    /* Long enough for the commands to apply, for connectivity to settle,
     * and for the periodic job pass and the commute to run. */
    for (i = 0; i < 900; i++) sim_run_one_tick(gs);

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_FISHERS_HUT)
            return i;
    return -1;
}

/* Runs `ticks` and reports the BUSIEST the hut ever got, plus what it
 * landed.
 *
 * The peak, not the instant. Building.worker_count is retallied every
 * tick from agents currently AGENT_WORKING, and an agent's cycle is
 * 60s of work, a commute, 15s of rest and a commute back — so reading
 * it on one arbitrary tick can say 0 about a hut that is plainly
 * fishing. That is not a defect this phase introduces; it is what
 * worker_count has always meant, and a test that sampled it once would
 * fail intermittently for reasons having nothing to do with hiring.
 *
 * (It is also why an island's real output is roughly four fifths of its
 * headcount: shifts and rest do not divide into the needs tick. The
 * calendar phase retunes 60+15 to 24+6 for exactly this reason.) */
static int run_and_peak(GameState *gs, int hut, int ticks, int *landed)
{
    Island *isl   = game_cur_island(gs);
    int     peak  = 0, before, i;

    before = isl->stockpile.amount[RES_FISH];
    for (i = 0; i < ticks; i++) {
        sim_run_one_tick(gs);
        if (isl->buildings[hut].worker_count > peak)
            peak = isl->buildings[hut].worker_count;
    }
    if (landed) *landed = isl->stockpile.amount[RES_FISH] - before;
    return peak;
}

/* ---- 2. who gets hired -------------------------------------
 * THE HEADLINE. Two houses is a dozen residents against one hut; before
 * this phase exactly one of them would ever have had a job. */
static void test_a_crew_is_hired(void)
{
    GameState *gs = game_init();
    Island    *isl;
    int        hut, cap, peak;

    printf("\n=== a crew, not a claim ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    hut = build_village(gs, 2);
    if (hut < 0) {
        printf("  FAIL: could not lay a village on this island\n");
        failures++;
        game_free(gs);
        return;
    }
    isl  = game_cur_island(gs);
    cap  = building_worker_cap(&BUILDING_DEFS[BUILDING_FISHERS_HUT]);
    peak = run_and_peak(gs, hut, 900, NULL);

    CHECK(isl->buildings[hut].connected, "the hut is on the road network");
    CHECK(peak > 1,
          "more than one resident works there — the flag is gone");
    CHECK(peak <= cap, "and no more than it holds");

    /* Nobody is assigned to a building that employs nobody. The
     * Warehouse is the one every island has, and it is also the one the
     * road network is seeded from, so an off-by-one in the capacity
     * check would show up here first. */
    {
        int i, at_store = 0;
        for (i = 0; i < isl->building_count; i++)
            if (isl->buildings[i].active &&
                isl->buildings[i].type == BUILDING_WAREHOUSE)
                at_store += isl->buildings[i].worker_count;
        CHECK(at_store == 0, "and nobody is rostered to the Warehouse");
    }

    game_free(gs);
}

/* ---- 3. what that buys -------------------------------------
 * The production clock advances by the headcount, so the same hut over
 * the same interval lands more fish with more hands in it. Measured by
 * running the same world twice rather than asserting a number: the rate
 * is a content decision and this test is about the SHAPE of it. */
static void test_production_scales(void)
{
    GameState *gs = game_init();
    int        hut, landed = 0, workers;

    printf("\n=== five hands, five fish ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    hut = build_village(gs, 2);
    if (hut < 0) {
        printf("  FAIL: could not lay a village\n");
        failures++;
        game_free(gs);
        return;
    }
    workers = run_and_peak(gs, hut, 600, &landed);

    CHECK(landed > 0, "the hut lands fish at all");

    /* A minute of a crew of N is worth appreciably more than a minute of
     * one. The comparison is against what a single worker could have
     * managed over the same 60 seconds at the def's own rate — computed,
     * not a recorded number, so retuning the Fisher's Hut does not make
     * this test wrong. */
    {
        float secs = 600.0f / (float)SIM_TICKS_PER_SEC;
        int   solo = (int)(secs
                     / BUILDING_DEFS[BUILDING_FISHERS_HUT].tick_seconds);
        char  msg[128];

        snprintf(msg, sizeof(msg),
                 "%d hands landed %d fish where one hand lands about %d",
                 workers, landed, solo);
        CHECK(workers > 1 && landed > solo, msg);
    }

    game_free(gs);
}

/* ---- 4. an empty workplace still produces nothing ----------
 * The gate this phase did NOT change, asserted because a rewrite of the
 * production loop is exactly where it would go missing. */
static void test_nobody_still_means_nothing(void)
{
    GameState *gs = game_init();
    int        hut, landed = 0, peak;

    printf("\n=== and an empty hut is still an empty hut ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    /* No houses: nobody on the island to hire. */
    hut = build_village(gs, 0);
    if (hut < 0) {
        printf("  FAIL: could not lay a village\n");
        failures++;
        game_free(gs);
        return;
    }

    peak = run_and_peak(gs, hut, 600, &landed);

    CHECK(peak == 0, "nobody ever works there");
    CHECK(landed == 0,
          "and an unstaffed hut lands nothing, however long it stands");

    game_free(gs);
}

int main(void)
{
    printf("== staffing (LIFE_PLAN Phase 1) ==\n");

    test_the_rule();
    test_a_crew_is_hired();
    test_production_scales();
    test_nobody_still_means_nothing();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
