/*  test_voyage.c  --  headless verification of MMO_PLAN Phase 2
 *
 * A voyage is now defined by its departure tick, not an accumulating
 * float. This checks the three things Phase 2 promises:
 *
 *   1. Arrival is exact: a ship departing at tick D arrives at
 *      D + SHIP_VOYAGE_TICKS.
 *   2. Save mid-voyage, load, and the ship still arrives at the same
 *      tick with the world hashing identically (the record survives a
 *      seed+log round-trip because departure_tick is replayed).
 *   3. VoyageRecord serialises to the expected one-line JSON — the wire
 *      format Phase 4 will publish.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "ship.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define TMP_PATH "test_voyage.tmp"

/* Build a ship on island 0 and send it to island 1. All three commands
 * apply on the first tick, so departure_tick is 0. */
static void launch_voyage(GameState *gs, Uint32 seed)
{
    game_new_seeded(gs, seed);
    game_build_ship(gs);
    game_ship_depart(gs, 0, 1);
    sim_run_one_tick(gs);   /* applies build + depart at tick 0 */
}

int main(void)
{
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }

    /* ---- 1. Exact integer arrival ---- */
    launch_voyage(gs, 4242u);
    CHECK(gs->ships[0].active && gs->ships[0].at_island < 0,
          "ship is at sea after departing");
    uint64_t dep = gs->ships[0].departure_tick;

    uint64_t arrival_tick = 0;
    int      arrived = 0;
    for (int k = 0; k < SHIP_VOYAGE_TICKS + 5 && !arrived; k++) {
        uint64_t t = gs->sim_tick_no;
        sim_run_one_tick(gs);
        if (gs->ships[0].at_island >= 0) { arrival_tick = t; arrived = 1; }
    }
    CHECK(arrived, "ship arrived");
    CHECK(arrival_tick == dep + (uint64_t)SHIP_VOYAGE_TICKS,
          "arrived exactly at departure_tick + SHIP_VOYAGE_TICKS");
    CHECK(gs->ships[0].at_island == 1, "arrived at the destination island");

    /* ---- 2. Save mid-voyage, load, finish, compare ---- */
    GameState *a = game_init();
    GameState *b = game_init();
    if (!a || !b) { printf("game_init failed\n"); return 1; }

    launch_voyage(a, 777u);
    for (int i = 0; i < SHIP_VOYAGE_TICKS / 2; i++)  /* stop mid-crossing */
        sim_run_one_tick(a);

    CHECK(a->ships[0].at_island < 0, "still mid-voyage at save time");
    CHECK(game_save(a, TMP_PATH), "save mid-voyage");
    CHECK(game_load(b, TMP_PATH), "load mid-voyage");

    CHECK(b->sim_tick_no == a->sim_tick_no, "loaded to the same tick");
    CHECK(b->ships[0].departure_tick == a->ships[0].departure_tick,
          "departure_tick survived the seed+log round-trip");
    CHECK(b->ships[0].at_island < 0, "loaded ship is still at sea");
    CHECK(sim_hash(a) == sim_hash(b), "mid-voyage worlds hash identically");

    /* Finish the voyage in both; they must arrive together, hashes equal. */
    for (int i = 0; i < SHIP_VOYAGE_TICKS; i++) {
        sim_run_one_tick(a);
        sim_run_one_tick(b);
    }
    CHECK(a->ships[0].at_island == 1 && b->ships[0].at_island == 1,
          "both arrive after the round-trip");
    CHECK(sim_hash(a) == sim_hash(b), "post-arrival worlds hash identically");

    remove(TMP_PATH);

    /* ---- 3. VoyageRecord wire format ---- */
    Ship sh;
    memset(&sh, 0, sizeof(sh));
    sh.from_island    = 0;
    sh.to_island      = 2;
    sh.departure_tick = 1234;
    for (int i = 0; i < RES_COUNT; i++) sh.cargo[i] = 0;
    sh.cargo[1]         = 5;
    sh.cargo[RES_GOLD]  = 400;

    VoyageRecord v = voyage_record_make(&sh, 1, 0);
    char json[256];
    int  len = voyage_record_to_json(&v, json, sizeof(json));

    /* RES_COUNT is 7; cargo prints all seven slots. */
    const char *expect =
        "{\"player\":0,\"ship\":1,\"from\":0,\"to\":2,"
        "\"departure_tick\":1234,\"cargo\":[0,5,0,0,0,0,400]}";
    CHECK(len == (int)strlen(expect), "serialised length is correct");
    CHECK(strcmp(json, expect) == 0, "VoyageRecord JSON matches the wire format");
    if (strcmp(json, expect) != 0) printf("    got: %s\n", json);

    game_free(gs);
    game_free(a);
    game_free(b);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
