/* test_voyage.c  --  headless verification of MMO_PLAN Phase 2 */

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

    uint32_t crossing = sea_crossing_ticks(&gs->sea,
                                           gs->ships[0].from_island,
                                           gs->ships[0].to_island);
    uint64_t arrival_tick = 0;
    int      arrived = 0;
    for (int k = 0; k < (int)crossing + 5 && !arrived; k++) {
        uint64_t t = gs->sim_tick_no;
        sim_run_one_tick(gs);
        if (gs->ships[0].at_island >= 0) { arrival_tick = t; arrived = 1; }
    }
    CHECK(arrived, "ship arrived");
    CHECK(arrival_tick == dep + (uint64_t)crossing,
          "arrived exactly at departure_tick + the route's length");
    CHECK(gs->ships[0].at_island == 1, "arrived at the destination island");

    /* ---- 2. Save mid-voyage, load, finish, compare ---- */
    GameState *a = game_init();
    GameState *b = game_init();
    if (!a || !b) { printf("game_init failed\n"); return 1; }

    launch_voyage(a, 777u);
    {
        uint32_t half = sea_crossing_ticks(&a->sea,
                                           a->ships[0].from_island,
                                           a->ships[0].to_island) / 2u;
        for (uint32_t i = 0; i < half; i++)   /* stop mid-crossing */
            sim_run_one_tick(a);
    }

    CHECK(a->ships[0].at_island < 0, "still mid-voyage at save time");
    CHECK(game_save(a, TMP_PATH), "save mid-voyage");
    CHECK(game_load(b, TMP_PATH), "load mid-voyage");

    CHECK(b->sim_tick_no == a->sim_tick_no, "loaded to the same tick");
    CHECK(b->ships[0].departure_tick == a->ships[0].departure_tick,
          "departure_tick survived the seed+log round-trip");
    CHECK(b->ships[0].at_island < 0, "loaded ship is still at sea");
    CHECK(sim_hash(a) == sim_hash(b), "mid-voyage worlds hash identically");

    /* Finish the voyage in both; they must arrive together, hashes equal. */
    {
        uint32_t full = sea_crossing_ticks(&a->sea,
                                           a->ships[0].from_island,
                                           a->ships[0].to_island);
        for (uint32_t i = 0; i < full; i++) {
            sim_run_one_tick(a);
            sim_run_one_tick(b);
        }
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

    /* cargo prints every slot, so the expected string is built from
     * RES_COUNT rather than written out — it was a literal until
     * SUPPLY_CHAIN Phase 3 added thirteen goods, at which point the
     * test was asserting the width of the enum as much as the format. */
    char expect[256];
    {
        int  n = snprintf(expect, sizeof(expect),
                          "{\"player\":0,\"ship\":1,\"from\":0,\"to\":2,"
                          "\"departure_tick\":1234,\"cargo\":[0,5");
        for (int i = 2; i < RES_COUNT; i++)
            n += snprintf(expect + n, sizeof(expect) - (size_t)n, ",%d",
                          i == (int)RES_GOLD ? 400 : 0);
        snprintf(expect + n, sizeof(expect) - (size_t)n, "]}");
    }
    CHECK(len == (int)strlen(expect), "serialised length is correct");
    CHECK(strcmp(json, expect) == 0, "VoyageRecord JSON matches the wire format");
    if (strcmp(json, expect) != 0) printf("    got: %s\n", json);

    game_free(gs);
    game_free(a);
    game_free(b);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
