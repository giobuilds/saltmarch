/*  test_command_funnel.c  --  headless verification of MMO_PLAN
 *                             Phase 1a (funnel) + 1b (fixed timestep)
 *
 * The funnel's defining invariant: the world is a pure function of its
 * initial state plus the ordered command log, advanced in fixed ticks.
 * This test proves it the only way that actually catches an escaped
 * mutation — by replaying, which is exactly the Phase 1c F9 mechanism
 * in miniature:
 *
 *   1. Build a world, snapshot its initial islands[]/ships[] and clock
 *      (INITIAL).
 *   2. Play: submit a scripted command sequence and run the fixed-tick
 *      loop forward, so commands apply at their tick boundaries and the
 *      simulation advances. Copy the resulting world (PLAYED).
 *   3. Restore islands/ships and the sim clock to INITIAL (keeping the
 *      command log), then run the identical number of ticks again.
 *   4. Assert the replayed world is byte-identical to PLAYED.
 *
 * If any mutation had bypassed command_submit(), the log would not
 * carry it, replay would not reproduce it, and step 4 would fail. A
 * pass means every mutation the run performed went through the funnel,
 * and that ticking the sim from the same state and log is deterministic.
 *
 * Islands and Ships are pure value types (Map embeds its tile grid by
 * value; no heap pointers), so memcpy is a true deep copy and memcmp is
 * a total equality check including camera, map, agents and pop state.
 *
 * Built and run by tests/run.sh, linking the game's own .o files.
 */

#include "game.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

int main(void)
{
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }

    /* island 0 (Saltford) starts settled with STARTING_GOLD. */
    Island   initial_islands[MAX_ISLANDS];
    Ship     initial_ships[MAX_SHIPS];
    int      initial_ship_count = gs->ship_count;
    uint64_t initial_tick       = gs->sim_tick_no;
    int      initial_applied    = gs->cmd_applied;
    uint64_t initial_acc        = gs->sim_acc_ns;
    memcpy(initial_islands, gs->islands, sizeof(initial_islands));
    memcpy(initial_ships,   gs->ships,   sizeof(initial_ships));

    int gold0_before = gs->islands[0].stockpile.amount[RES_GOLD];

    const int TICKS = 400;   /* > NEEDS_INTERVAL and a full voyage      */

    /* ---- Play: submit commands, then run the fixed-tick loop --------
     * Chosen so that at least some commands mutate regardless of the
     * random map: trades and ship-building do not depend on tile
     * layout. Placement commands may be rejected on a given seed;
     * that is fine — rejection is deterministic and replay reproduces
     * it identically, which is exactly what the invariant claims.
     * Commands submitted here are stamped for tick 0 and applied on the
     * first sim_run_one_tick below. */
    ResourceType tradable = (RES_GOLD == 0) ? (ResourceType)1 : (ResourceType)0;

    game_buy_resource(gs, tradable, 5);   /* gold -> goods            */
    game_build_ship(gs);                  /* gold -> a ship in slot 0 */
    game_ship_transfer(gs, 0, tradable, 3); /* load some cargo        */
    game_sell_resource(gs, tradable, 1);  /* sell one back            */
    game_ship_depart(gs, 0, 1);           /* sail toward island 1     */
    game_colonise(gs, 0, 1);              /* rejected: still at sea   */
    game_try_place_road(gs, 30, 30);
    game_try_place_road(gs, 30, 31);
    game_try_place_road(gs, 31, 30);

    CHECK(gs->cmd_count == 9, "every submitted command was logged");
    CHECK(gs->cmd_applied == 0,
          "commands are deferred, not applied on submit (1b)");

    for (int i = 0; i < TICKS; i++)
        sim_run_one_tick(gs);

    CHECK(gs->cmd_applied == gs->cmd_count,
          "every queued command was applied by the tick loop");
    CHECK(gs->sim_tick_no == initial_tick + TICKS, "the world clock advanced");
    CHECK(gs->islands[0].stockpile.amount[RES_GOLD] != gold0_before,
          "the run actually moved gold (real mutation occurred)");

    /* Snapshot the played-out world. */
    Island played_islands[MAX_ISLANDS];
    Ship   played_ships[MAX_SHIPS];
    int    played_ship_count = gs->ship_count;
    memcpy(played_islands, gs->islands, sizeof(played_islands));
    memcpy(played_ships,   gs->ships,   sizeof(played_ships));

    CHECK(played_ship_count > initial_ship_count,
          "a ship was created during play");

    /* ---- Restore to INITIAL and replay the same ticks ------ */
    memcpy(gs->islands, initial_islands, sizeof(initial_islands));
    memcpy(gs->ships,   initial_ships,   sizeof(initial_ships));
    gs->ship_count  = initial_ship_count;
    gs->sim_tick_no = initial_tick;
    gs->cmd_applied = initial_applied;
    gs->sim_acc_ns  = initial_acc;

    for (int i = 0; i < TICKS; i++)
        sim_run_one_tick(gs);

    /* ---- Assert replay reproduced the played world exactly */
    int islands_match = memcmp(gs->islands, played_islands,
                               sizeof(played_islands)) == 0;
    int ships_match   = gs->ship_count == played_ship_count &&
                        memcmp(gs->ships, played_ships,
                               sizeof(played_ships)) == 0;

    if (!islands_match) {
        for (int i = 0; i < MAX_ISLANDS; i++)
            if (memcmp(&gs->islands[i], &played_islands[i],
                       sizeof(Island)) != 0)
                printf("    island %d diverged on replay\n", i);
    }

    CHECK(islands_match, "replayed islands are byte-identical to played");
    CHECK(ships_match,   "replayed ships are byte-identical to played");

    game_free(gs);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
