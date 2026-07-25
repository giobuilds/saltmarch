/*  test_charter.c  --  port charters (MMO_PLAN later phases)
 *
 * An island is held, not owned outright: a charter bought from the
 * faction and kept current by upkeep. Miss enough payments and it
 * lapses — the island relists itself, which is how a persistent world
 * hands islands to new players with nobody administering it.
 *
 * The properties worth pinning:
 *   - the bid is a real gold sink into the faction, and gold is still
 *     conserved between the two sides;
 *   - a working colony pays upkeep without noticing;
 *   - an idle one lapses, and only after its grace period;
 *   - a lapsed island keeps its buildings and can be chartered again;
 *   - all of it replays, because all of it is hashed.
 */

#include "game.h"
#include "island.h"
#include "building.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* Put a ship at `island` with the founding gold aboard.
 *
 * Deliberately hand-placed rather than sailed: these tests are about
 * charters, not voyages. It writes ship state directly, which means a
 * world built this way is NOT a function of its command log — see
 * test_replays(), which sails the ship properly for exactly that
 * reason. */
static void ship_at(GameState *gs, int island, int gold)
{
    Ship *sh;
    gs->ship_count       = 1;
    sh                   = &gs->ships[0];
    memset(sh, 0, sizeof(*sh));
    sh->active           = 1;
    sh->owner            = gs->local_player_id;
    sh->at_island        = island;
    sh->cargo[RES_GOLD]  = gold;
    gs->replay_valid     = 0;   /* this world is no longer replayable */
}

static void test_bid_is_a_sink(void)
{
    GameState *gs = game_init();
    int32_t    faction_before, ship_gold;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 1u);

    ship_at(gs, 1, COLONY_FOUNDING_GOLD);
    faction_before = gs->faction.gold;

    game_colonise(gs, 0, 1);
    sim_run_one_tick(gs);

    CHECK(gs->islands[1].settled && gs->islands[1].owner == 1u,
          "the charter is granted");
    CHECK(gs->faction.gold == faction_before + CHARTER_BID_GOLD,
          "the bid lands in the faction's purse — a real gold sink");
    CHECK(gs->islands[1].stockpile.amount[RES_GOLD] ==
          COLONY_FOUNDING_GOLD - CHARTER_BID_GOLD,
          "the colony keeps the rest as its treasury");

    ship_gold = gs->ships[0].cargo[RES_GOLD];
    CHECK(ship_gold == 0, "and the hold paid for all of it");

    game_free(gs);
}

static void test_upkeep_and_lapse(void)
{
    GameState *gs = game_init();
    int        i, ticks;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 2u);

    ship_at(gs, 1, COLONY_FOUNDING_GOLD);
    game_colonise(gs, 0, 1);
    sim_run_one_tick(gs);

    /* A solvent colony pays without drama. */
    {
        int gold_before = gs->islands[1].stockpile.amount[RES_GOLD];
        for (i = 0; i < CHARTER_UPKEEP_TICKS + 1; i++) sim_run_one_tick(gs);
        CHECK(gs->islands[1].stockpile.amount[RES_GOLD] ==
              gold_before - CHARTER_UPKEEP_GOLD,
              "a solvent colony pays its upkeep");
        CHECK(gs->islands[1].settled, "...and keeps its charter");
    }

    /* Empty the treasury and let it run dry. */
    gs->islands[1].stockpile.amount[RES_GOLD] = 0;

    ticks = CHARTER_UPKEEP_TICKS * (CHARTER_GRACE_PAYMENTS - 1) + 2;
    for (i = 0; i < ticks; i++) sim_run_one_tick(gs);
    CHECK(gs->islands[1].settled,
          "a broke colony is not evicted on the first missed payment");
    CHECK(gs->islands[1].charter_arrears > 0, "but the arrears are counted");

    for (i = 0; i < CHARTER_UPKEEP_TICKS + 2; i++) sim_run_one_tick(gs);
    CHECK(!gs->islands[1].settled && gs->islands[1].owner == PLAYER_NONE,
          "past the grace period the charter lapses and the island relists");

    game_free(gs);
}

static void test_relisted_island_keeps_its_ruins(void)
{
    GameState *gs = game_init();
    int        i, before_count;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 3u);

    ship_at(gs, 1, COLONY_FOUNDING_GOLD);
    game_colonise(gs, 0, 1);
    sim_run_one_tick(gs);

    /* Build something, then let the charter lapse. */
    game_set_current_island(gs, 1);
    {
        Island *isl = &gs->islands[1];
        int r, c, placed = 0;
        for (r = 0; r < MAP_ROWS && !placed; r++)
            for (c = 0; c < MAP_COLS && !placed; c++)
                if (building_can_place(&isl->map, BUILDING_WAREHOUSE, r, c)) {
                    /* Paid in Gold: a fresh colony has a treasury and
                     * no Wood at all. */
                    game_place_building(gs, r, c, BUILDING_WAREHOUSE, 1);
                    placed = 1;
                }
        sim_run_one_tick(gs);
    }
    before_count = gs->islands[1].building_count;
    CHECK(before_count > 0, "the colony built something");

    gs->islands[1].stockpile.amount[RES_GOLD] = 0;
    for (i = 0; i < CHARTER_UPKEEP_TICKS * (CHARTER_GRACE_PAYMENTS + 1); i++)
        sim_run_one_tick(gs);

    CHECK(!gs->islands[1].settled, "the charter lapsed");
    CHECK(gs->islands[1].building_count == before_count,
          "the buildings are still standing — a ruin, not bare ground");

    /* And it can be chartered again. */
    ship_at(gs, 1, COLONY_FOUNDING_GOLD);
    game_colonise(gs, 0, 1);
    sim_run_one_tick(gs);
    CHECK(gs->islands[1].settled && gs->islands[1].owner == 1u,
          "a relisted island can be chartered again");

    game_free(gs);
}

/* All of it is sim state, so all of it must replay. */
static void test_replays(void)
{
    GameState *gs = game_init();
    int        i;

    if (!gs) { printf("  FAIL: game_init\n"); failures++; return; }
    game_new_seeded(gs, 4u);

    /* Everything through the funnel this time: build a ship, load the
     * founding gold, sail, charter. A world assembled any other way is
     * not a function of its log, and F9 says so — which is how the
     * first draft of this test failed, correctly. */
    game_build_ship(gs);
    sim_run_one_tick(gs);
    game_ship_transfer(gs, 0, RES_GOLD, COLONY_FOUNDING_GOLD);
    sim_run_one_tick(gs);
    game_ship_depart(gs, 0, 1);
    for (i = 0; i < 400; i++) sim_run_one_tick(gs);

    CHECK(gs->ships[0].at_island == 1, "the ship arrived under its own sail");

    game_colonise(gs, 0, 1);
    for (i = 0; i < CHARTER_UPKEEP_TICKS * 2 + 5; i++) sim_run_one_tick(gs);

    CHECK(gs->islands[1].settled, "and chartered the island");
    CHECK(game_verify_determinism(gs),
          "a world with charters in it replays to the same hash");

    game_free(gs);
}

int main(void)
{
    printf("== port charters ==\n");
    test_bid_is_a_sink();
    test_upkeep_and_lapse();
    test_relisted_island_keeps_its_ruins();
    test_replays();

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
