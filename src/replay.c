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

/* THE FIXTURE HAS TO EXERCISE WHAT IT CLAIMS TO, and this file has been
 * wrong about that twice.
 *
 * The first time, it paid for its house in goods a fresh island does
 * not have, so the placement was refused every run and the house it
 * said it placed never existed.
 *
 * The second time — found at NEEDS_PLAN Phase 3 — the house existed but
 * was never connected to a warehouse and the island held nothing anyone
 * eats, so pop_update took its "no road to Warehouse" branch for the
 * fixture's whole life. Three phases of change to the needs economy
 * moved the recorded hash not at all, because the cross-platform
 * determinism gate had never once run a needs tick that did anything.
 *
 * So this lays a road to the warehouse, stocks what marshfolk eat, runs
 * long enough for the needs tick to fire repeatedly, and RETURNS
 * WHETHER THE HOUSE ENDED UP FED. tests/test_determinism.c asserts on
 * that return, so a third time is a failing test rather than a hash
 * quietly agreeing with itself.
 *
 * A note for whoever changes this next: game_place_building SUBMITS a
 * command, it does not apply one. Nothing exists until a tick runs, so
 * the house cannot be found by index at the moment it is asked for —
 * that was the first thing this rewrite got wrong. */
int replay_record_demo_session(GameState *gs, uint32_t seed)
{
    Island *isl;
    int     t, i, wr = -1, wc = -1, house = -1, laid = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    /* AND THE THIRD THING THIS FIXTURE ASSUMED AND NEVER CHECKED: a
     * fresh island has NO BUILDINGS AT ALL. It has no warehouse, and
     * connectivity is seeded FROM warehouses — so the house the old
     * fixture placed could not have been connected under any road,
     * because there was nothing for a road to reach. That is the whole
     * reason pop_update never did anything here.
     *
     * So: a warehouse, a road against it, and a house against the road.
     * One tile of pavement is the entire requirement — a building is
     * connected when a road 4-adjacent to its footprint reaches a
     * warehouse, and a road laid against the warehouse already has.
     *
     * Candidates are walked in a fixed order and the first legal set
     * wins, so every machine builds the same village. Placement
     * SUBMITS commands rather than applying them, so nothing here can
     * be looked up until the ticks below have run — which is what the
     * second version of this got wrong. */
    for (wr = 0; wr + 2 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + 2 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, rc = wc;          /* road below the store */
            int hr = wr + 3, hc = wc;          /* house below the road */

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            if (hr >= MAP_ROWS) continue;
            if (!building_can_place(&isl->map, BUILDING_ROAD,  rr, rc)) continue;
            if (!building_can_place(&isl->map, BUILDING_HOUSE, hr, hc)) continue;

            /* Paid in GOLD, not goods. A fresh island holds 1000 Gold
             * and no Wood at all, so the goods payment this used to
             * request was refused every single time. */
            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            game_place_building(gs, rr, rc, BUILDING_ROAD,      1);
            game_place_building(gs, hr, hc, BUILDING_HOUSE,     1);
            laid = 1;
        }

    /* What marshfolk eat. Bought rather than produced: this fixture
     * exercises the needs tick, and a farm that has to be staffed first
     * would make that coverage depend on the agent scheduler too. */
    game_buy_resource(gs, RES_FISH,     40);
    game_buy_resource(gs, RES_GRAIN,    40);
    game_buy_resource(gs, RES_OILSKINS, 10);

    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    /* Long enough for the needs tick (every 300) to fire several times,
     * so growth and consumption are in the recording rather than one
     * lonely boundary. */
    for (t = 0; t < 2000; t++)
        sim_run_one_tick(gs);

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_HOUSE) { house = i; break; }

    if (house < 0) return 0;
    return isl->buildings[house].connected &&
           isl->pop_data[house].happiness > HAPPINESS_NEUTRAL &&
           isl->stockpile.amount[RES_FISH] < 40;
}
