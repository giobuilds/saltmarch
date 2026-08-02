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
#define DEMO_ROAD_LEN 10
    for (wr = 0; wr + 5 < MAP_ROWS && !laid; wr++)
        for (wc = 0; wc + DEMO_ROAD_LEN + 1 < MAP_COLS && !laid; wc++) {
            int rr = wr + 2;                   /* road below the store  */
            int hr = wr + 3;                   /* the row below that    */
            int k, ok = 1;
            int fhut = -1, farm = -1, h1 = -1, h2 = -1;

            if (!building_can_place(&isl->map, BUILDING_WAREHOUSE, wr, wc))
                continue;
            /* A LONG ROW OF PAVEMENT, and then each building is given
             * its own position along it.
             *
             * The first attempt at this demanded a coastal tile and a
             * fertile 2x2 at two fixed offsets, and found a site on none
             * of five seeds — coast and good soil are on opposite sides
             * of an island, which is obvious in hindsight and was not
             * before it was measured. Scanning the row for each in turn
             * asks the same question without insisting they be
             * neighbours. Fixed order, so every machine still lays the
             * identical village. */
            for (k = 0; k < DEMO_ROAD_LEN; k++)
                if (!building_can_place(&isl->map, BUILDING_ROAD, rr, wc + k))
                    ok = 0;
            if (!ok) continue;

            for (k = 0; k < DEMO_ROAD_LEN && fhut < 0; k++)
                if (building_can_place(&isl->map, BUILDING_FISHERS_HUT,
                                       hr, wc + k)) fhut = k;
            /* The farm is 2x2 and sits ABOVE the row, beside the store,
             * because the inland side is where soil is. */
            for (k = 2; k + 1 < DEMO_ROAD_LEN && farm < 0; k++)
                if (building_can_place(&isl->map, BUILDING_FARM, wr, wc + k))
                    farm = k;
            for (k = 0; k < DEMO_ROAD_LEN && h2 < 0; k++) {
                if (k == fhut) continue;
                if (!building_can_place(&isl->map, BUILDING_HOUSE, hr, wc + k))
                    continue;
                if (h1 < 0) h1 = k; else h2 = k;
            }
            if (fhut < 0 || farm < 0 || h1 < 0 || h2 < 0) continue;

            /* Paid in GOLD, not goods. A fresh island holds no Wood at
             * all, so the goods payment this used to request was refused
             * every single time. */
            game_place_building(gs, wr, wc, BUILDING_WAREHOUSE, 1);
            for (k = 0; k < DEMO_ROAD_LEN; k++)
                game_place_building(gs, rr, wc + k, BUILDING_ROAD, 1);
            game_place_building(gs, hr, wc + fhut, BUILDING_FISHERS_HUT, 1);
            game_place_building(gs, wr, wc + farm, BUILDING_FARM,        1);
            /* TWO houses, so there is somebody to marry. One house makes
             * a family, and a family has nobody in it who may marry
             * anybody — which is how the six-month fixture covered no
             * marriage, no move and no reserve. */
            game_place_building(gs, hr, wc + h1, BUILDING_HOUSE, 1);
            game_place_building(gs, hr, wc + h2, BUILDING_HOUSE, 1);
            laid = 1;
        }

    game_buy_resource(gs, RES_FISH,     20);
    game_buy_resource(gs, RES_GRAIN,    40);
    game_buy_resource(gs, RES_OILSKINS, 10);

    game_buy_resource(gs, (ResourceType)0, 8);
    game_build_ship(gs);
    game_ship_transfer(gs, 0, (ResourceType)0, 5);
    game_ship_depart(gs, 0, 1);

    /* FIFTY YEARS. Long enough for the founding couple to raise
     * children, for those children to reach twelve and go to work, to
     * marry across the two houses, to have children of their own, and
     * for the founders to die and the eldest to inherit. See
     * DEMO_SESSION_TICKS in replay.h for what the old six months
     * covered, which was none of that. */
    for (t = 0; t < DEMO_SESSION_TICKS; t++)
        sim_run_one_tick(gs);

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_HOUSE) { house = i; break; }
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_FISHERS_HUT) { hut = i; break; }

    if (house < 0 || hut < 0) return 0;

    /* ---- what this fixture now insists actually happened -----
     * It has silently covered nothing THREE times: paying for its house
     * in goods the island did not have, placing a house on an island
     * with no warehouse, and — until this rewrite — running for six and
     * a half months, which is too short for anybody in it to age out of
     * infancy, marry, conceive, inherit or turn twelve. Four phases of
     * demography went into the cross-platform gate untested.
     *
     * Fifty years, two houses, a hut and a farm. The village feeds
     * itself: it is fed rather than delighted, because nothing here
     * produces Oilskins or Marsh Gin and fifty years of them cannot be
     * bought, so NEUTRAL is the honest bar and above-neutral was the
     * bar that quietly required a luxury chain nobody built. */
    {
        int n, born = 0, grown_here = 0, married = 0;

        for (n = 0; n < isl->resident_count; n++) {
            const Resident *r = &isl->residents[n];
            if (!r->active) continue;
            if (r->birth_house >= 0) {
                born++;
                /* Born here AND old enough to work: the only way this
                 * is true is that a child was carried, delivered, aged
                 * twelve years and passed the labour gate. */
                if (resident_stage(r) == LIFE_TEEN ||
                    resident_stage(r) == LIFE_ADULT) grown_here++;
            }
            if (r->spouse >= 0) married++;
        }

        return isl->buildings[house].connected &&
               isl->pop_data[house].happiness >= HAPPINESS_NEUTRAL &&
               isl->buildings[hut].connected &&
               /* Somebody went to work: more Fish and more Grain on the
                * island than were ever bought, which nothing but the hut
                * and the farm can explain. */
               isl->stockpile.amount[RES_FISH]  > 20 &&
               isl->stockpile.amount[RES_GRAIN] > 40 &&
               /* And the island raised its own people, married them,
                * and put the children to work. */
               born > 0 && grown_here > 0 && married > 0;
    }
}
