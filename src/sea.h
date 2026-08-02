#ifndef SEA_H
#define SEA_H

/* sea.h  --  The water between the islands
 * (MARITIME_PLAN Phase 1: sea geometry) */

#include <stdint.h>

/* The sea's extent in its own units. Arbitrary, but fixed: island
 * positions are generated inside it and everything else is relative. */
#define SEA_WIDTH   10000
#define SEA_HEIGHT  10000

/* Sea units a ship covers in one sim tick. */
#define SEA_UNITS_PER_TICK  34

/* How far apart things are kept. Islands and waypoints have different
 * requirements and used to share one number, which was too small. */
#define SEA_MIN_ISLAND_SEPARATION    1500
#define SEA_MIN_WAYPOINT_SEPARATION   600

#define SEA_MAX_WAYPOINTS   24
#define SEA_NAME_LEN        24

/* A route names itself after the waypoint it threads ("by the Weeping
 * Stones"), so it needs room for a waypoint name and a preposition. */
#define SEA_ROUTE_NAME_LEN  (SEA_NAME_LEN + 8)

/* Waypoints one route may thread. Public routes take the scenic way
 * about; two is enough to make a path read as a path. */
#define SEA_MAX_ROUTE_WAYPOINTS 2
#define SEA_MAX_ROUTE_LEGS      (SEA_MAX_ROUTE_WAYPOINTS + 1)

/* Three routes are IN PLAY between any island pair (MARITIME_PLAN
 * Phase 3): one public lane and two private passages. */
#define SEA_ROUTES_PER_PAIR 3    /* live: one public, two private     */
#define SEA_PRIVATE_POOL    6    /* generated private passages        */
#define SEA_STORED_PER_PAIR (1 + SEA_PRIVATE_POOL)
#define SEA_MAX_PAIRS       (16 * 15 / 2)
#define SEA_MAX_ROUTES      512

/* How long a private passage stays in play. A "year" in a game with. */
#define SEA_ROUTE_LIFETIME_TICKS 18000

/* Which of a pair's three this is. Variant 0 is the lane everybody
 * knows; 1 and 2 are the passages a chart buys you. */
#define SEA_ROUTE_PUBLIC    0

typedef struct {
    int32_t x, y;
} SeaPos;

typedef struct {
    SeaPos pos;
    char   name[SEA_NAME_LEN];
} Waypoint;

/* A route is a path: island -> waypoint(s) -> island, with each leg's
 * duration stored as whole ticks rather than recomputed from a
 * distance every time somebody asks. */
typedef struct {
    int      from_island;
    int      to_island;
    int      variant;           /* 0 public, 1..2 private            */
    int      is_private;        /* concealed until charted           */
    int      waypoint[SEA_MAX_ROUTE_WAYPOINTS];
    int      waypoint_count;
    uint32_t leg_ticks[SEA_MAX_ROUTE_LEGS];
    uint32_t total_ticks;
    char     name[SEA_ROUTE_NAME_LEN];
} Route;

typedef struct {
    SeaPos   island[16];        /* indexed by island; 16 >= MAX_ISLANDS */
    int      island_count;
    Waypoint waypoint[SEA_MAX_WAYPOINTS];
    int      waypoint_count;
    Route    route[SEA_MAX_ROUTES];
    int      route_count;

    /* Where each pair is in its rotation: the two live private
     * passages are pool entries [cursor] and [cursor+1], modulo the
     * pool. World state — the one field in here a checkpoint carries
     * and the one a replay must reproduce. */
    uint8_t  pair_cursor[SEA_MAX_PAIRS];
} Sea;

/* The pair index for two islands, order-independent, or -1. This is
 * what indexes pair_cursor[]. */
int sea_pair_index(const Sea *sea, int island_a, int island_b);

/* Retire the oldest live passage on `pair` and bring the next one in.
 * Returns the route id that just went out of play, or -1. The caller
 * is what makes charts for it worthless — the sea only decides which
 * water is in use. */
int sea_rotate_pair(Sea *sea, int pair);

/* The next tick at or after `now` on which `pair` rotates, in a world of
 * `island_count` islands. Answers `now` itself on a tick that rotates. */
uint64_t sea_pair_next_rotation(int island_count, int pair, uint64_t now);

/* Generate the whole sea from `seed` and `island_count`. Deterministic:
 * the same arguments always produce the same positions, names, paths
 * and durations, on every platform. */
void sea_init(Sea *sea, uint32_t seed, int island_count);

/* The PUBLIC route joining two islands, or NULL. Order-independent — a
 * route is a piece of water, not a direction of travel. */
const Route *sea_route_between(const Sea *sea, int island_a, int island_b);

/* One of a pair's three routes by variant, or NULL. Variant 0 is
 * public; 1 and 2 are private. */
const Route *sea_route_variant(const Sea *sea, int island_a, int island_b,
                               int variant);

/* How many routes join the pair — SEA_ROUTES_PER_PAIR for any real
 * pair, 0 for a pair that has none (an island with itself). */
int sea_route_count_between(const Sea *sea, int island_a, int island_b);

/* The index of `route` within `sea->route[]`. This is the id a chart
 * names and the id a route trades under, so it has to be stable for a
 * given seed — which it is, because generation order is. Returns -1 if
 * the route is not part of this sea. */
int sea_route_id(const Sea *sea, const Route *route);

/* How long the PUBLIC crossing takes, in whole ticks. Falls back to a
 * sensible constant if the pair has no route, so a caller that has not
 * been taught about routes yet cannot divide by zero. */
uint32_t sea_crossing_ticks(const Sea *sea, int island_a, int island_b);

/* Where a ship is at `elapsed` ticks into `route`, as a position in
 * sea units, walking the legs in order. Used for drawing and, later,
 * for deciding who can see whom. */
SeaPos sea_route_point(const Sea *sea, const Route *route, uint32_t elapsed);

/* Integer distance between two sea positions. Exposed because the
 * tests assert on it and because sight radius will want it. */
uint32_t sea_distance(SeaPos a, SeaPos b);

#endif /* SEA_H */
