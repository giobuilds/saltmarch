/*  test_ownership.c  --  headless verification of MMO_PLAN Phase 5's
 *                        sim half: ownership, grants, harbor escrow,
 *                        docking — privacy by validation.
 *
 * Two players share one GameState here, the way they will share one
 * deterministic world in co-op: the test flips local_player_id between
 * submissions to play both sides, everything flows through the command
 * funnel, and at the end game_verify_determinism() must still pass —
 * proving the whole ownership era replays from the log.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "building.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static void run_ticks(GameState *gs, int n)
{
    while (n-- > 0) sim_run_one_tick(gs);
}

/* Submit as a given player: everything through the real funnel. */
#define AS(gs, player, call) do {                    \
        (gs)->local_player_id = (player);            \
        call;                                        \
        (gs)->local_player_id = 1u;                  \
    } while (0)

/* Place `type` on `island` as `player`, paying gold, at the first legal
 * spot. Returns 1 if a placement command was submitted. */
static int place_first_fit(GameState *gs, int island, uint32_t player,
                           BuildingType type)
{
    Island *isl = &gs->islands[island];
    int r, c;

    for (r = 0; r < MAP_ROWS; r++)
        for (c = 0; c < MAP_COLS; c++)
            if (building_can_place(&isl->map, type, r, c, NULL, 0)) {
                game_set_current_island(gs, island);
                gs->selected_building = type;
                gs->build_confirm_row = r;
                gs->build_confirm_col = c;
                AS(gs, player, game_place_building_confirmed(gs, 1));
                gs->selected_building = BUILDING_NONE;
                return 1;
            }
    return 0;
}

int main(void)
{
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }
    game_new_seeded(gs, 2024u);

    /* ---- The world starts owned by player 1 ---- */
    CHECK(gs->islands[0].owner == 1u, "island 0 starts owned by player 1");
    CHECK(gs->islands[1].owner == PLAYER_NONE, "island 1 starts unowned");

    /* ---- Privacy by validation ---- */
    int gold0 = gs->islands[0].stockpile.amount[RES_GOLD];
    game_set_current_island(gs, 0);
    AS(gs, 2u, game_buy_resource(gs, RES_FISH, 5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].stockpile.amount[RES_GOLD] == gold0,
          "player 2 cannot trade with player 1's stockpile");

    int b0 = gs->islands[0].building_count;
    AS(gs, 2u, game_try_place_road(gs, 30, 30));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].building_count == b0,
          "player 2 cannot build on player 1's island");

    /* ---- The join bootstrap: GRANT_START ---- */
    AS(gs, 2u, game_grant_start(gs, 1));
    run_ticks(gs, 1);
    CHECK(gs->islands[1].settled && gs->islands[1].owner == 2u,
          "grant settles island 1 for player 2");
    CHECK(gs->islands[1].stockpile.amount[RES_GOLD] == STARTING_GOLD,
          "granted island gets the standard treasury");

    AS(gs, 2u, game_grant_start(gs, 2));
    run_ticks(gs, 1);
    CHECK(!gs->islands[2].settled,
          "a player who owns an island cannot grant itself another");

    /* ---- Ships carry their owner ---- */
    game_set_current_island(gs, 1);
    AS(gs, 2u, game_build_ship(gs));
    run_ticks(gs, 1);
    CHECK(gs->ship_count == 1 && gs->ships[0].active &&
          gs->ships[0].owner == 2u, "player 2's ship is owned by player 2");

    AS(gs, 1u, game_ship_depart(gs, 0, 0));
    run_ticks(gs, 1);
    CHECK(gs->ships[0].at_island == 1,
          "player 1 cannot order player 2's ship to sail");

    /* Player 2 stocks the hold and sails for player 1's island. */
    AS(gs, 2u, game_buy_resource(gs, RES_FISH, 10));
    run_ticks(gs, 1);
    AS(gs, 2u, game_ship_transfer(gs, 0, RES_FISH, 10));   /* own island */
    run_ticks(gs, 1);
    CHECK(gs->ships[0].cargo[RES_FISH] == 10,
          "owner loads own hold at own island (stockpile path)");

    AS(gs, 2u, game_ship_depart(gs, 0, 0));
    run_ticks(gs, SHIP_VOYAGE_TICKS + 2);
    CHECK(gs->ships[0].at_island == 0, "ship arrived at player 1's island");

    /* ---- Foreign transfers need a Harbor ---- */
    game_set_current_island(gs, 0);
    AS(gs, 2u, game_ship_transfer(gs, 0, RES_FISH, -5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].escrow[RES_FISH] == 0 &&
          gs->ships[0].cargo[RES_FISH] == 10,
          "no harbor: a foreign ship cannot transfer at all");

    CHECK(place_first_fit(gs, 0, 1u, BUILDING_HARBOR),
          "player 1 places a Harbor on island 0");
    run_ticks(gs, 1);

    AS(gs, 2u, game_ship_transfer(gs, 0, RES_FISH, -5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].escrow[RES_FISH] == 5 &&
          gs->ships[0].cargo[RES_FISH] == 5,
          "with a harbor: foreign unload goes into ESCROW, not stockpile");

    /* ---- Only the owner works the escrow ---- */
    int fish0 = gs->islands[0].stockpile.amount[RES_FISH];
    AS(gs, 2u, game_escrow_take(gs, 0, RES_FISH, 5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].escrow[RES_FISH] == 5,
          "the visitor cannot take from the owner's escrow");

    AS(gs, 1u, game_escrow_take(gs, 0, RES_FISH, 5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].escrow[RES_FISH] == 0 &&
          gs->islands[0].stockpile.amount[RES_FISH] == fish0 + 5,
          "the owner accepts the delivery: escrow -> stockpile");

    /* Owner leaves payment in escrow; visitor collects it. */
    AS(gs, 1u, game_escrow_put(gs, 0, RES_GOLD, 100));
    run_ticks(gs, 1);
    AS(gs, 2u, game_ship_transfer(gs, 0, RES_GOLD, 100));
    run_ticks(gs, 1);
    CHECK(gs->ships[0].cargo[RES_GOLD] == 100 &&
          gs->islands[0].escrow[RES_GOLD] == 0,
          "the visitor collects payment from escrow into the hold");

    /* ---- Blockade: docking permission ---- */
    AS(gs, 1u, game_set_docking(gs, 0, 0));
    run_ticks(gs, 1);
    AS(gs, 2u, game_ship_transfer(gs, 0, RES_FISH, -5));
    run_ticks(gs, 1);
    CHECK(gs->islands[0].escrow[RES_FISH] == 0 &&
          gs->ships[0].cargo[RES_FISH] == 5,
          "docking forbidden: the foreign ship can no longer deliver");

    /* ---- The whole ownership era replays from the log ---- */
    CHECK(game_verify_determinism(gs) == 1,
          "F9 passes: grants, escrow and docking all replay identically");

    game_free(gs);
    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
