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
 * wrong about that three times now. Each was found by a phase whose
 * hash did not move when it should have, which is the only symptom this
 * failure has: a gate agreeing with itself about a world where nothing
 * happens looks exactly like a gate that passed.
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
 * THE THIRD TIME — found at LIFE_PLAN Phase 1 — was the other half of
 * the same hole: it had never placed a PRODUCING building. No workplace
 * meant no agent was ever hired, so island_tick_buildings() skipped
 * every building on the island for the fixture's whole life and its
 * production path had never once executed under the cross-platform
 * gate. A phase that changed how production is staffed and how fast it
 * runs moved the recorded hash by exactly zero.
 *
 * Hence the Fisher's Hut, and the choice was not free. A Sawmill was
 * tried first and BROKE THE FIXTURE: at ~240 Gold placed (120 plus 20
 * Wood bought at market) it left 135 of the island's 1000 for food, so
 * the Grain order partially filled, the Oilskins were refused outright,
 * and the house this fixture exists to feed went hungry. A fixture has
 * a budget, and adding to it costs something the earlier assertions
 * were relying on.
 *
 * The hut is 60 Gold, needs no input at all, and produces the very
 * thing the house eats — so it covers hiring, the headcount-driven
 * clock and output while making the fixture's economy partly
 * self-supplying rather than entirely bought. The return value now
 * insists somebody was employed and something was made.
 *
 * A note for whoever changes this next: game_place_building SUBMITS a
 * command, it does not apply one. Nothing exists until a tick runs, so
 * the house cannot be found by index at the moment it is asked for —
 * that was the first thing this rewrite got wrong. */
int replay_record_demo_session(GameState *gs, uint32_t seed)
{
    Island *isl;
    int     t, i, wr = -1, wc = -1, house = -1, hut = -1, laid = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    /* AND THE THING THIS FIXTURE ASSUMED AND NEVER CHECKED: a
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
        for (wc = 0; wc + 4 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2, rc = wc;          /* road below the store  */
            int hr = wr + 3, hc = wc;          /* house below the road  */
            int fr = wr + 2, fc = wc + 1;      /* hut beside the road   */

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            if (hr >= MAP_ROWS) continue;
            if (!building_can_place(&isl->map, BUILDING_ROAD,  rr, rc)) continue;
            if (!building_can_place(&isl->map, BUILDING_HOUSE, hr, hc)) continue;
            /* Coastal, so this is the constraint that actually decides
             * where the village goes. An island always has a shore. */
            if (!building_can_place(&isl->map, BUILDING_FISHERS_HUT, fr, fc))
                continue;

            /* Paid in GOLD, not goods. A fresh island holds 1000 Gold
             * and no Wood at all, so the goods payment this used to
             * request was refused every single time. */
            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            game_place_building(gs, rr, rc, BUILDING_ROAD,      1);
            game_place_building(gs, hr, hc, BUILDING_HOUSE,     1);
            game_place_building(gs, fr, fc, BUILDING_FISHERS_HUT, 1);
            laid = 1;
        }

    /* What marshfolk eat, and DELIBERATELY LESS FISH THAN THEY WILL EAT.
     * Six needs ticks at five residents is thirty Fish; twenty are
     * bought. The rest has to be landed by the hut, which is what makes
     * "more Fish than were ever purchased" below a claim about
     * production rather than about the market.
     *
     * Grain and Oilskins stay bought: the fixture covers ONE producer on
     * purpose, so a failure points at the production path rather than at
     * whichever of three chains happened to stall. */
    game_buy_resource(gs, RES_FISH,     20);
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
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_FISHERS_HUT) { hut = i; break; }

    if (house < 0 || hut < 0) return 0;
    return isl->buildings[house].connected &&
           isl->pop_data[house].happiness > HAPPINESS_NEUTRAL &&
           isl->buildings[hut].connected &&
           /* Somebody went to work: more Fish on the island than were
            * ever bought, which nothing but the hut can explain. */
           isl->stockpile.amount[RES_FISH] > 20;
}
