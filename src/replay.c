/*  replay.c  --  The scripted session (MMO_PLAN Phase 1d)
 *
 *  Just the fixture generator now: a deterministic session that
 *  exercises the float-sensitive paths, used by --record and by the
 *  headless tests.
 *
 *  The CLI and the UI harness that used to live here moved to
 *  replay_ui.c when the harness started driving the real overlay
 *  builders — those live in libsaltmarch_ui, which links this library,
 *  so a harness calling them cannot also sit inside it.
 */

#include "replay.h"
#include "building.h"
#include "island.h"
#include "map.h"
#include "resource.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void replay_record_demo_session(GameState *gs, uint32_t seed)
{
    Island *isl;
    int     r, c, t, placed = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    for (r = 0; r < MAP_ROWS && !placed; r++)
        for (c = 0; c < MAP_COLS && !placed; c++)
            if (building_can_place(&isl->map, BUILDING_HOUSE, r, c)) {
                /* Paid in GOLD, not goods. A fresh island holds 1000
                 * Gold and no Wood at all, so the goods payment this
                 * used to request was refused every single time — the
                 * fixture has been claiming since Phase 1d that it
                 * "touches the float-sensitive paths on purpose (a
                 * house, so population and agents run)" while never
                 * actually placing the house. Now it does. */
                game_place_building(gs, r, c, BUILDING_HOUSE, 1);
                placed = 1;
            }
    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    for (t = 0; t < 500; t++)
        sim_run_one_tick(gs);
}
