/*  test_determinism.c  --  headless verification of MMO_PLAN Phase 1c
 *
 * Exercises the F9 machinery the way the key does, but without a window:
 *
 *   1. Play a scripted session (commands + ticks) on a live world.
 *   2. Call game_verify_determinism(): it rebuilds the world from
 *      world_seed, replays the log, and compares sim_hash(). Assert it
 *      reports PASS (state 1) and that the two hashes are equal.
 *   3. Corrupt the live world by ONE unit of one resource — a mutation
 *      that no command in the log accounts for — and verify again.
 *      Assert it now reports DESYNC (state 2): proof the check actually
 *      catches an escaped mutation rather than always passing.
 *   4. Assert sim_hash ignores cosmetic state: moving a camera leaves
 *      the hash unchanged.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "resource.h"
#include <SDL3/SDL.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

int main(void)
{
    GameState *gs = game_init();
    if (!gs) { printf("game_init failed\n"); return 1; }

    ResourceType tradable = (RES_GOLD == 0) ? (ResourceType)1 : (ResourceType)0;

    /* A short, varied session. */
    game_buy_resource(gs, tradable, 6);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, tradable, 4);
    game_ship_depart(gs, 0, 1);
    game_try_place_road(gs, 28, 28);

    for (int i = 0; i < 350; i++)
        sim_run_one_tick(gs);

    /* ---- PASS case ---- */
    int ok = game_verify_determinism(gs);
    CHECK(ok == 1, "verify reports deterministic (PASS) after honest play");
    CHECK(gs->replay_state == 1, "replay_state is PASS");
    CHECK(gs->replay_live_hash == gs->replay_replay_hash,
          "live and replayed hashes are equal");

    /* ---- Cosmetic state is excluded ---- */
    uint64_t before = sim_hash(gs);
    gs->islands[0].camera.offset_x += 123.0f;
    gs->islands[0].camera.zoom     *= 1.5f;
    CHECK(sim_hash(gs) == before, "moving the camera does not change sim_hash");

    /* ---- DESYNC case: an escaped mutation ----
     * Add one unit of a resource directly, bypassing the command log.
     * The replay (which only knows the log) cannot reproduce it. */
    gs->islands[0].stockpile.amount[tradable] += 1;
    int ok2 = game_verify_determinism(gs);
    CHECK(ok2 == 0, "verify catches an off-log mutation (DESYNC)");
    CHECK(gs->replay_state == 2, "replay_state is DESYNC");
    CHECK(gs->replay_live_hash != gs->replay_replay_hash,
          "live and replayed hashes differ after the escaped mutation");

    game_free(gs);

    printf(failures ? "\nFAILED (%d)\n" : "\nPASSED\n", failures);
    return failures ? 1 : 0;
}
