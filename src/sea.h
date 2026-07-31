#ifndef SEA_H
#define SEA_H

/* =========================================================
 * sea.h  --  The water between the islands
 *            (MARITIME_PLAN Phase 1: sea geometry)
 *
 * Until now the sea was not a place. Islands had no position anywhere
 * in the tree — the only coordinates were `NODE_POS` in world_ui.c,
 * hand-placed screen fractions for drawing — and every crossing took
 * the same time, `SHIP_VOYAGE_TICKS`, whichever islands it joined.
 * Distance did not exist as a gameplay quantity, so nothing could
 * depend on it: not variable durations, not sight, not
 * distance-priced risk, and not more islands than somebody had
 * hand-placed dots for.
 *
 * A Sea is islands at positions, named waypoints between them, and
 * routes that are PATHS through those waypoints rather than a line and
 * a constant.
 *
 * REGENERATED, NOT SAVED. The whole thing is a pure function of the
 * world seed, exactly like a Map, so it is rebuilt by sea_init() on
 * load and appears in no save file and no snapshot. That is why this
 * phase needs neither a SAVE_VERSION bump nor a snapshot change: there
 * is nothing here a checkpoint could disagree about.
 *
 * INTEGER THROUGHOUT. Positions, distances and leg durations are
 * integers, and the distance function is an integer square root. Not
 * fastidiousness: MARITIME_PLAN's determinism note is that a float
 * accumulated into hashed state fails as two machines disagreeing
 * rather than as a wrong answer on one, and leg durations are exactly
 * the kind of derived quantity that invites a float. Drawing may use
 * floats freely — it is downstream of everything.
 * ========================================================= */

#include <stdint.h>

/* The sea's extent in its own units. Arbitrary, but fixed: island
 * positions are generated inside it and everything else is relative. */
#define SEA_WIDTH   10000
#define SEA_HEIGHT  10000

/* Sea units a ship covers in one sim tick.
 *
 * Measured rather than guessed, and RE-measured whenever the generator
 * changes shape. Across eight seeds the mean PUBLIC crossing comes out
 * at 196.7 ticks — the same centre SHIP_VOYAGE_TICKS used to impose on
 * every crossing alike, so voyages differ from each other (roughly 100
 * to 350 ticks) without the game as a whole getting slower.
 *
 * The value has moved twice, both times because something upstream
 * changed the average path and the constant had to absorb it:
 *
 *   15 -> 21  Phase 1. The first value quietly made the average voyage
 *             half again as long and made this comment untrue.
 *   21 -> 26  Phase 3a. Three routes per pair: the public lane now
 *             threads a slightly wider waypoint and carries a convoy
 *             penalty, which together took the mean to 246 before this
 *             was re-fitted.
 *   26 -> 34  Phase 3e. The private pool: the lane now threads the
 *             furthest waypoint the pool uses, so that every passage
 *             in it beats the lane, which pushed the mean to 258.
 *
 * If the generator's scale changes again, re-measure. The failure mode
 * is not a wrong number, it is every voyage in the game silently
 * getting slower while nothing says so. */
#define SEA_UNITS_PER_TICK  34

/* How far apart things are kept. Islands and waypoints have different
 * requirements and used to share one number, which was too small.
 *
 * Islands must not merely be distinct: they must not OVERLAP once the
 * world map projects them, or one is drawn underneath another and
 * cannot be clicked at all. The projection is anisotropic — the sea is
 * square and the screen is not — so a pair separated diagonally can
 * clear in neither axis. At 1920x1080 with the map's margins, a node
 * is 141x70 px and the worst diagonal case needs roughly 1226 sea
 * units to clear it; 1500 leaves room for the margins to be tuned
 * without silently reintroducing the bug. test_world asserts the
 * relationship across seeds rather than trusting this comment.
 *
 * Waypoints only need to be distinct places. */
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
 * Phase 3): one public lane and two private passages.
 *
 * But more than that are GENERATED. Charts expire, and when one does
 * the passage it mapped goes out of use and a fresh one comes into
 * play — so the sea keeps changing shape and the map can never be
 * finished (Phase 3e). Each pair therefore has a POOL of private
 * routes, of which two are live at a time, and a cursor says which
 * two.
 *
 * THE CURSOR IS THE ONLY PART OF A SEA THAT IS WORLD STATE. Everything
 * else here is still a pure function of the world seed, which is the
 * property the rest of this header is written around: the geometry,
 * the names, the durations and the pool all regenerate identically on
 * every machine, and a checkpoint carries one small number per pair
 * saying where in the rotation the world has got to. Making the whole
 * Sea mutable would have been simpler to write and much worse to
 * reason about — a desync in generated data is a bug in a generator,
 * a desync in saved data is a bug anywhere.
 *
 * (1 + pool) * N*(N-1)/2 = 196 at eight islands and a pool of six. */
#define SEA_ROUTES_PER_PAIR 3    /* live: one public, two private     */
#define SEA_PRIVATE_POOL    6    /* generated private passages        */
#define SEA_STORED_PER_PAIR (1 + SEA_PRIVATE_POOL)
#define SEA_MAX_PAIRS       (16 * 15 / 2)
#define SEA_MAX_ROUTES      512

/* How long a private passage stays in play. A "year" in a game with no
 * calendar: long enough that charting one is worth doing and short
 * enough that the map is never finished. Charter upkeep falls every
 * 1200 ticks, so this is fifteen of those.
 *
 * The pairs do NOT all turn over together. Each rotates on its own
 * offset, so the sea changes shape continuously rather than lurching
 * once every half hour and invalidating everybody's charts at the same
 * instant. */
#define SEA_ROUTE_LIFETIME_TICKS 18000

/* Which of a pair's three this is. Variant 0 is the lane everybody
 * knows; 1 and 2 are the passages a chart buys you.
 *
 * PUBLIC IS ALWAYS THE SLOWEST OF THE THREE, and that is a guarantee
 * the generator has to make rather than hope for, because the whole
 * trade-off — "public are slow but protected, private are faster but
 * unsafe" — collapses if a pair ever generates a private route that is
 * the long way round. See the convoy penalty in sea.c. */
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
 * `island_count` islands. Answers `now` itself on a tick that rotates.
 *
 * The schedule is arithmetic over the pair index and the tick — it reads
 * no state and allocates nothing — but it exists as a function because
 * two callers need the same answer and must not compute it twice. The
 * sim asks "is it now?" every tick; the charts screen asks "how long
 * has this passage left?" every frame (UI_PLAN N4). A UI that reproduced
 * the stagger would be one edit away from telling a player their chart
 * had a thousand ticks left on the tick it became waste paper. */
uint64_t sea_pair_next_rotation(int island_count, int pair, uint64_t now);

/* Generate the whole sea from `seed` and `island_count`. Deterministic:
 * the same arguments always produce the same positions, names, paths
 * and durations, on every platform. */
void sea_init(Sea *sea, uint32_t seed, int island_count);

/* The PUBLIC route joining two islands, or NULL. Order-independent — a
 * route is a piece of water, not a direction of travel.
 *
 * This is deliberately the public one rather than the best one: a
 * caller that has not been taught about charts must never accidentally
 * route a cargo down a passage the player has not discovered. Code that
 * means "the fastest route this player may use" asks for it by name. */
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
