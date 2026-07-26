/*  test_world.c  --  the archipelago map at eight islands
 *                    (SUPPLY_CHAIN Phase 5)
 *
 * Phase 5 doubled MAX_ISLANDS and hand-placed eight nodes on the world
 * overview. Hand-placed coordinates are exactly the kind of thing that
 * looks right in a diff and overlaps on screen, and the failure is
 * quiet: an island drawn underneath another is still THERE, still in
 * the array, still ticking — it just cannot be clicked, so a player
 * simply cannot reach one of their colonies.
 *
 * The assertion is therefore made through the real hit-test rather
 * than by eyeballing the table: sample the screen, and require every
 * island to own a region of it, all eight regions to be about the same
 * size (they are identical diamonds, so a small one means a covered
 * one), and no island to answer for a point another already claimed.
 *
 * Built and run by tests/run.sh.
 */

#include "game.h"
#include "building.h"
#include "island.h"
#include "resource.h"
#include "world_ui.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

/* The resolution the game actually runs at (game.h). Testing the
 * layout at any other size would be testing a screen nobody sees. */
#define STEP 4   /* sample every 4th pixel: fine enough for a diamond */

int main(void)
{
    GameState *gs = game_init();
    int        hits[MAX_ISLANDS];
    int        x, y, i, empty = 0, smallest = 0, largest = 0;

    printf("=== the world map at %d islands ===\n", MAX_ISLANDS);

    if (!gs) { printf("game_init failed\n"); return 1; }
    game_new_seeded(gs, 4242u);

    memset(hits, 0, sizeof(hits));

    for (y = 0; y < SCREEN_H; y += STEP) {
        for (x = 0; x < SCREEN_W; x += STEP) {
            int          island = -1, ship = -1;
            ResourceType res;
            WorldHit     h = world_ui_hit_test(SCREEN_W, SCREEN_H,
                                               MAX_ISLANDS,
                                               gs->ships, gs->ship_count, -1,
                                               x, y, &island, &ship, &res);
            if (h != WORLD_HIT_ISLAND) continue;
            if (island < 0 || island >= MAX_ISLANDS) {
                printf("  FAIL: hit-test returned island %d\n", island);
                failures++;
                continue;
            }
            hits[island]++;
        }
    }

    for (i = 0; i < MAX_ISLANDS; i++) {
        if (hits[i] == 0) {
            printf("  FAIL: island %d (%s) cannot be clicked anywhere\n",
                   i, gs->islands[i].name);
            empty++;
        }
        if (largest == 0 || hits[i] > largest) largest = hits[i];
        if (smallest == 0 || hits[i] < smallest) smallest = hits[i];
    }

    printf("  (hit areas, samples per island: ");
    for (i = 0; i < MAX_ISLANDS; i++) printf("%d ", hits[i]);
    printf(")\n");

    CHECK(empty == 0, "every island owns a clickable region of the map");

    /* Every node is the same diamond at the same zoom, so the areas
     * should match. A node materially smaller than its neighbours is
     * one drawn partly underneath another — which is the overlap this
     * test exists to catch, and which "they all have some area" alone
     * would miss. */
    CHECK(smallest * 10 >= largest * 9,
          "and they are all the same size — none is partly covered");

    game_free(gs);

    /* ---- and the south is genuinely reachable ---- */
    printf("\n=== a southern colony, and cotton sailing north ===\n");
    {
        GameState *g = game_init();
        Island    *south;
        int        r, c, placed = 0, t;
        int        southern_idx = -1;

        if (!g) { printf("game_init failed\n"); return 1; }
        game_new_seeded(g, 4242u);

        for (t = 0; t < MAX_ISLANDS; t++)
            if (g->islands[t].map.profile == PROFILE_PLANTATION) {
                southern_idx = t;
                break;
            }
        CHECK(southern_idx >= 0, "the archipelago contains a plantation");
        if (southern_idx < 0) { game_free(g); goto done; }

        /* Charter it the way test_charter does — by hand, because this
         * is about what the south CONTAINS, not about voyages, which
         * test_voyage already proves. */
        g->ship_count            = 1;
        memset(&g->ships[0], 0, sizeof(g->ships[0]));
        g->ships[0].active       = 1;
        g->ships[0].owner        = g->local_player_id;
        g->ships[0].at_island    = southern_idx;
        g->ships[0].cargo[RES_GOLD] = COLONY_FOUNDING_GOLD;
        g->replay_valid          = 0;

        game_colonise(g, 0, southern_idx);
        sim_run_one_tick(g);
        CHECK(g->islands[southern_idx].settled,
              "a southern island can be chartered");

        south = &g->islands[southern_idx];
        south->stockpile.amount[RES_GOLD] = 100000;

        for (r = 0; r < MAP_ROWS && !placed; r++)
            for (c = 0; c < MAP_COLS && !placed; c++)
                if (building_can_place(&south->map, BUILDING_COTTON_FIELD,
                                       r, c)) {
                    placed = (building_place(south->buildings,
                                             &south->building_count,
                                             &south->map,
                                             BUILDING_COTTON_FIELD,
                                             r, c) >= 0);
                }
        CHECK(placed, "and a Cotton Field stands on it");

        /* The point of the whole climate, stated as an assertion: this
         * is the only ground in the world that grows the crop the
         * Artisans line depends on. */
        {
            int north_ok = 0, n;
            for (n = 0; n < MAX_ISLANDS && !north_ok; n++) {
                Island *isl = &g->islands[n];
                if (isl->map.profile == PROFILE_PLANTATION ||
                    isl->map.profile == PROFILE_JUNGLE) continue;
                for (r = 0; r < MAP_ROWS && !north_ok; r++)
                    for (c = 0; c < MAP_COLS && !north_ok; c++)
                        if (building_can_place(&isl->map,
                                               BUILDING_COTTON_FIELD, r, c))
                            north_ok = 1;
            }
            CHECK(!north_ok,
                  "and no northern island in the world could have grown it");
        }

        game_free(g);
    }

done:
    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
