/* test_staffing.c  --  a workplace holds a crew
 * (LIFE_PLAN Phase 1) */

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

/* ---- 1b. what a FULL crew is worth (LIFE_PLAN Phase 2) ----- */
static void test_a_full_crew_is_worth_more(void)
{
    const BuildingDef *hut = &BUILDING_DEFS[BUILDING_FISHERS_HUT];
    int                cap = building_worker_cap(hut);
    int                w;

    printf("\n=== and a full one is worth more than the sum of it ===\n");

    CHECK(building_work_advance(hut, 1) == 1,
          "a lone worker is worth exactly what they always were");
    CHECK(building_work_advance(hut, cap) == 2 * cap - 1,
          "and a full hut of five lands nine, not five");
    CHECK(building_work_advance(hut, 0) == 0, "an empty one lands nothing");

    /* Monotonic, and strictly so: every extra hand is worth having.
     * A curve that flattened would make the top of a crew pointless. */
    for (w = 1; w < cap; w++)
        if (building_work_advance(hut, w + 1)
            <= building_work_advance(hut, w)) {
            printf("  FAIL: worker %d adds nothing\n", w + 1);
            failures++;
        }
    printf("  ok:   every extra hand is worth having\n");

    /* Over-full is clamped, not rewarded. worker_count is retallied
     * every tick from whoever is present, and a demolition mid-
     * reassignment could briefly overfill a building — which must not
     * be a production bonus for knocking a workplace down. */
    CHECK(building_work_advance(hut, cap + 10)
          == building_work_advance(hut, cap),
          "and a crush of bodies is worth no more than a full crew");

    /* THE POINT: one full workplace beats the same people in separate
     * ones, so concentrating labour is a real choice. */
    CHECK(building_work_advance(hut, cap) > cap * building_work_advance(hut, 1),
          "five in one hut beat five huts with one each");
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
            /* A ROW OF PAVEMENT, NOT ONE TILE. The houses used to sit. */
            for (h = 0; h < houses; h++) {
                if (!building_can_place(&isl->map, BUILDING_ROAD,
                                        rr, wc + 1 + h)) ok = 0;
                if (!building_can_place(&isl->map, BUILDING_HOUSE,
                                        fr, wc + 1 + h)) ok = 0;
            }
            if (!ok) continue;

            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            game_place_building(gs, rr, wc, BUILDING_ROAD,      1);
            game_place_building(gs, fr, wc, BUILDING_FISHERS_HUT, 1);
            for (h = 0; h < houses; h++) {
                game_place_building(gs, rr, wc + 1 + h, BUILDING_ROAD,  1);
                game_place_building(gs, fr, wc + 1 + h, BUILDING_HOUSE, 1);
            }
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
 * landed. */
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
 * THE HEADLINE. Several houses' worth of residents against one hut;
 * before this phase exactly one of them would ever have had a job. */
static void test_a_crew_is_hired(void)
{
    GameState *gs = game_init();
    /* Seeded: game_init() takes its seed from the CLOCK, so an
     * unseeded test is a different world every run — which is how
     * test_ageing came to report one number locally and another in
     * CI. */
    if (gs) game_new_seeded(gs, 4242u);
    Island    *isl;
    int        hut, cap, peak;

    printf("\n=== a crew, not a claim ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    hut = build_village(gs, 4);
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

/* ---- 3. what that buys ------------------------------------- */
static void test_production_scales(void)
{
    GameState *gs = game_init();
    /* Seeded: game_init() takes its seed from the CLOCK, so an
     * unseeded test is a different world every run — which is how
     * test_ageing came to report one number locally and another in
     * CI. */
    if (gs) game_new_seeded(gs, 4242u);
    int        hut, landed = 0, workers;

    printf("\n=== five hands, five fish ===\n");
    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }

    hut = build_village(gs, 4);
    if (hut < 0) {
        printf("  FAIL: could not lay a village\n");
        failures++;
        game_free(gs);
        return;
    }
    workers = run_and_peak(gs, hut, 600, &landed);

    CHECK(landed > 0, "the hut lands fish at all");

    /* A minute of a crew of N is worth appreciably more than a minute. */
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
    /* Seeded: game_init() takes its seed from the CLOCK, so an
     * unseeded test is a different world every run — which is how
     * test_ageing came to report one number locally and another in
     * CI. */
    if (gs) game_new_seeded(gs, 4242u);
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
    test_a_full_crew_is_worth_more();
    test_a_crew_is_hired();
    test_production_scales();
    test_nobody_still_means_nothing();

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
