/* test_ghost_faction.c  --  neighbours with no AI
 * (MMO_PLAN later phases) */

#include "ghost_faction.h"
#include "replay.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

#define REC "test_ghost_rec.smlog"

/* Record a session to replay as a neighbour. */
static int make_recording(void)
{
    GameState *gs = game_init();
    int        ok;

    if (!gs) return 0;
    replay_record_demo_session(gs, 4242u);
    ok = game_save(gs, REC);
    game_free(gs);
    return ok;
}

int main(void)
{
    GameState *gs;
    int        seeded, i, npc_buildings = 0;

    printf("== ghost factions ==\n");

    if (!make_recording()) {
        printf("  FAIL: could not record a session to replay\n");
        return 1;
    }

    gs = game_init();
    if (!gs) { printf("  FAIL: game_init\n"); return 1; }
    game_new_seeded(gs, 31337u);

    seeded = ghost_faction_seed(gs, REC, 2, 900u, 10);
    CHECK(seeded > 0, "a recorded session seeds a neighbour");

    /* Nothing has happened yet: the commands are queued, not applied. */
    CHECK(!gs->islands[2].settled,
          "the neighbour has not arrived before its first tick");

    for (i = 0; i < 900; i++) sim_run_one_tick(gs);

    CHECK(gs->islands[2].settled && gs->islands[2].owner == 900u,
          "the neighbour charters its island under its own identity");

    for (i = 0; i < gs->islands[2].building_count; i++)
        if (gs->islands[2].buildings[i].active) npc_buildings++;
    CHECK(npc_buildings > 0, "and builds what the recording built");

    /* The player's own island is untouched: re-addressing means the
     * neighbour acts on ITS island, not on the recorder's. */
    {
        int mine = 0;
        for (i = 0; i < gs->islands[0].building_count; i++)
            if (gs->islands[0].buildings[i].active) mine++;
        CHECK(mine == 0,
              "the recording's island 0 commands did not land on ours");
    }

    /* Ship commands were dropped rather than re-addressed: the
     * recording built a ship, and ship indices are world-scoped, so
     * honouring one would have reached into this world's ships. */
    CHECK(gs->ship_count == 0,
          "ship commands are dropped, not aimed at whatever ship 0 is");

    /* And the whole thing is still a function of the log. */
    CHECK(game_verify_determinism(gs),
          "a world with a ghost neighbour in it replays exactly");

    /* A missing file is a failure, not a crash. */
    CHECK(ghost_faction_seed(gs, "no-such-file.smlog", 3, 901u, 0) == -1,
          "a missing recording is reported rather than assumed empty");

    remove(REC);
    game_free(gs);

    if (failures == 0) { printf("\nPASSED\n"); return 0; }
    printf("\nFAILED (%d)\n", failures);
    return 1;
}
