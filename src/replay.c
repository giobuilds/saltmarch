/* replay.c  --  The scripted session (MMO_PLAN Phase 1d) */

#include "replay.h"
#include "building.h"
#include "island.h"
#include "map.h"
#include "resource.h"
#include "simlog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE FIXTURE HAS TO EXERCISE WHAT IT CLAIMS TO, and this file has. */
int replay_record_demo_session(GameState *gs, uint32_t seed)
{
    Island *isl;
    int     t, i, wr = -1, wc = -1, house = -1, hut = -1, laid = 0;

    game_new_seeded(gs, seed);
    isl = game_cur_island(gs);

    /* AND THE THING THIS FIXTURE ASSUMED AND NEVER CHECKED:. */
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
             * its own position along it. */
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

    /* FIFTY YEARS. Long enough for the founding couple to raise */
    for (t = 0; t < DEMO_SESSION_TICKS; t++)
        sim_run_one_tick(gs);

    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_HOUSE) { house = i; break; }
    for (i = 0; i < isl->building_count; i++)
        if (isl->buildings[i].active &&
            isl->buildings[i].type == BUILDING_FISHERS_HUT) { hut = i; break; }

    if (house < 0 || hut < 0) return 0;

    /* ---- what this fixture now insists actually happened ----- */
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
