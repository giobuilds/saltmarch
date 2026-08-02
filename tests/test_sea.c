/* test_sea.c  --  the water between the islands
 * (MARITIME_PLAN Phase 1: sea geometry) */

#include "sea.h"
#include "island.h"      /* MAX_ISLANDS */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) do {                                          \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); failures++; }    \
        else         { printf("  ok:   %s\n", (msg)); }                \
    } while (0)

static const uint32_t SEEDS[] = {
    1u, 4242u, 777u, 12345u, 99991u, 31337u, 20260727u, 5150u
};
#define SEED_COUNT ((int)(sizeof SEEDS / sizeof SEEDS[0]))

int main(void)
{
    int s;

    printf("=== the sea is a function of its seed ===\n");

    /* Byte-for-byte reproducible. A Sea holds no pointers and no
     * padding that generation leaves untouched (sea_init memsets
     * first), so memcmp is a fair test and a much stricter one than
     * comparing fields by hand. */
    {
        Sea a, b;
        int identical = 1;

        for (s = 0; s < SEED_COUNT; s++) {
            sea_init(&a, SEEDS[s], MAX_ISLANDS);
            sea_init(&b, SEEDS[s], MAX_ISLANDS);
            if (memcmp(&a, &b, sizeof a) != 0) identical = 0;
        }
        CHECK(identical, "the same seed builds the same sea, byte for byte");

        sea_init(&a, 4242u, MAX_ISLANDS);
        sea_init(&b, 4243u, MAX_ISLANDS);
        CHECK(memcmp(&a, &b, sizeof a) != 0,
              "and a different seed builds a different one");
    }

    printf("\n=== every crossing is possible, and takes time ===\n");
    {
        int missing = 0, instant = 0, mismatched = 0, i, j;

        for (s = 0; s < SEED_COUNT; s++) {
            Sea sea;
            sea_init(&sea, SEEDS[s], MAX_ISLANDS);

            for (i = 0; i < MAX_ISLANDS; i++)
                for (j = i + 1; j < MAX_ISLANDS; j++) {
                    const Route *r = sea_route_between(&sea, i, j);
                    uint32_t sum = 0;
                    int leg;

                    if (!r) { missing++; continue; }
                    if (r->total_ticks == 0) instant++;

                    /* The stored total must equal the legs it is made */
                    for (leg = 0; leg < r->waypoint_count + 1; leg++)
                        sum += r->leg_ticks[leg];
                    if (sum != r->total_ticks) mismatched++;
                }
        }
        CHECK(missing == 0, "every island pair has a route");
        CHECK(instant == 0, "and none of them is instantaneous");
        CHECK(mismatched == 0, "a route's legs add up to its total");
    }

    printf("\n=== a route is a path, and it goes where it says ===\n");
    {
        Sea sea;
        int i, j, ends_wrong = 0, starts_wrong = 0, named = 0, total = 0;

        sea_init(&sea, 4242u, MAX_ISLANDS);

        for (i = 0; i < MAX_ISLANDS; i++)
            for (j = i + 1; j < MAX_ISLANDS; j++) {
                const Route *r = sea_route_between(&sea, i, j);
                SeaPos start, end;

                if (!r) continue;
                total++;
                if (r->name[0] != '\0') named++;

                start = sea_route_point(&sea, r, 0);
                end   = sea_route_point(&sea, r, r->total_ticks);

                if (start.x != sea.island[r->from_island].x ||
                    start.y != sea.island[r->from_island].y) starts_wrong++;
                if (end.x != sea.island[r->to_island].x ||
                    end.y != sea.island[r->to_island].y) ends_wrong++;
            }

        CHECK(starts_wrong == 0, "at tick 0 a ship is at the island it left");
        CHECK(ends_wrong == 0, "and at the end it is at the one it sailed to");
        CHECK(named == total && total > 0, "every route has a name");
    }

    printf("\n=== three routes join every pair, and two are secret ===\n");
    {
        int wrong_count = 0, wrong_split = 0, identical = 0;
        int private_not_faster = 0, bad_id = 0, i, j, v;

        for (s = 0; s < SEED_COUNT; s++) {
            Sea sea;
            sea_init(&sea, SEEDS[s], MAX_ISLANDS);

            for (i = 0; i < MAX_ISLANDS; i++)
                for (j = i + 1; j < MAX_ISLANDS; j++) {
                    const Route *r[SEA_ROUTES_PER_PAIR];
                    int pub = 0, priv = 0;

                    if (sea_route_count_between(&sea, i, j) !=
                        SEA_ROUTES_PER_PAIR) { wrong_count++; continue; }

                    for (v = 0; v < SEA_ROUTES_PER_PAIR; v++) {
                        r[v] = sea_route_variant(&sea, i, j, v);
                        if (!r[v]) { wrong_count++; break; }
                        if (r[v]->is_private) priv++; else pub++;
                        if (sea_route_id(&sea, r[v]) < 0) bad_id++;
                    }
                    if (v < SEA_ROUTES_PER_PAIR) continue;

                    /* One public, two private. */
                    if (pub != 1 || priv != 2) wrong_split++;

                    /* The three must be different water. Two routes
                     * that threaded the same waypoint would be one
                     * route sold twice — a chart for the second would
                     * buy nothing. */
                    for (v = 1; v < SEA_ROUTES_PER_PAIR; v++) {
                        int k;
                        for (k = 0; k < v; k++)
                            if (r[v]->waypoint_count == r[k]->waypoint_count &&
                                (r[v]->waypoint_count == 0 ||
                                 r[v]->waypoint[0] == r[k]->waypoint[0]))
                                identical++;
                    }

                    /* The trade-off itself: every private passage beats
                     * the patrolled lane. If this ever stops holding,
                     * charts are a cost with no benefit. */
                    {
                        int pair = sea_pair_index(&sea, i, j);
                        int slot;
                        const Route *lane =
                            &sea.route[pair * SEA_STORED_PER_PAIR];
                        for (slot = 1; slot < SEA_STORED_PER_PAIR; slot++) {
                            const Route *p =
                                &sea.route[pair * SEA_STORED_PER_PAIR + slot];
                            if (!p->is_private) wrong_split++;
                            if (p->total_ticks >= lane->total_ticks)
                                private_not_faster++;
                        }
                    }
                }
        }
        CHECK(wrong_count == 0, "every pair has exactly three routes");
        CHECK(wrong_split == 0, "one of them public, two private");
        CHECK(identical == 0, "and no two of them are the same water");
        CHECK(private_not_faster == 0,
              "every private passage is faster than the public lane");
        CHECK(bad_id == 0, "every route has an id in its own sea");
    }

    printf("\n=== places are places ===\n");
    {
        int too_near = 0, i, j;

        for (s = 0; s < SEED_COUNT; s++) {
            Sea sea;
            sea_init(&sea, SEEDS[s], MAX_ISLANDS);

            for (i = 0; i < MAX_ISLANDS; i++)
                for (j = i + 1; j < MAX_ISLANDS; j++)
                    if (sea_distance(sea.island[i], sea.island[j]) <
                        (uint32_t)SEA_MIN_ISLAND_SEPARATION) too_near++;
        }
        CHECK(too_near == 0,
              "no two islands are closer than the minimum separation");

        /* The separation exists to survive the world map's projection,
         * which test_world checks from the other end. Asserting the
         * number here as well means a change to it fails in the place
         * that explains why it matters. */
        CHECK(SEA_MIN_ISLAND_SEPARATION >= 1226,
              "and that minimum clears the map projection's worst case");
    }

    printf("\n=== the sea has not silently got slower ===\n");
    {
        /* SEA_UNITS_PER_TICK is fitted to the generator so that. */
        double   sum = 0.0;
        int      pairs = 0, i, j;
        double   mean;

        for (s = 0; s < SEED_COUNT; s++) {
            Sea sea;
            sea_init(&sea, SEEDS[s], MAX_ISLANDS);

            for (i = 0; i < MAX_ISLANDS; i++)
                for (j = i + 1; j < MAX_ISLANDS; j++) {
                    const Route *r = sea_route_between(&sea, i, j);
                    if (!r) continue;
                    sum += (double)r->total_ticks;
                    pairs++;
                }
        }
        mean = pairs ? sum / pairs : 0.0;
        printf("       (mean public crossing: %.1f ticks)\n", mean);
        CHECK(mean > 160.0 && mean < 250.0,
              "the average public crossing is still about 200 ticks");
    }

    printf("\n=== the distance function ===\n");
    {
        SeaPos o = { 0, 0 }, x = { 300, 400 }, same = { 0, 0 };

        CHECK(sea_distance(o, x) == 500u, "3-4-5 comes out exactly");
        CHECK(sea_distance(o, same) == 0u, "a place is no distance from itself");
        CHECK(sea_distance(o, x) == sea_distance(x, o), "and it is symmetric");
    }

    printf("\n%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
