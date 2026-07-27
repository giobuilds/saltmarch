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
 * Measured rather than guessed: across eight seeds the mean generated
 * path is ~4288 units, so 21 units per tick puts the AVERAGE crossing
 * at the 200 ticks SHIP_VOYAGE_TICKS used to impose on every crossing
 * alike. This phase is meant to change the SHAPE of the world, not its
 * pace — voyages now differ from each other (roughly 70 to 400 ticks)
 * around the same centre, so a route being long is information rather
 * than a slower game.
 *
 * The first value here was 15, which quietly made the average voyage
 * half again as long and made this comment untrue. If the generator's
 * scale changes, re-measure. */
#define SEA_UNITS_PER_TICK  21

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

/* One route per island pair today: N*(N-1)/2, which is 28 at eight
 * islands. Sized with room for the private routes MARITIME_PLAN Phase
 * 3 adds and for more islands than eight. */
#define SEA_MAX_ROUTES      256

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
} Sea;

/* Generate the whole sea from `seed` and `island_count`. Deterministic:
 * the same arguments always produce the same positions, names, paths
 * and durations, on every platform. */
void sea_init(Sea *sea, uint32_t seed, int island_count);

/* The route joining two islands, or NULL. Order-independent — a route
 * is a piece of water, not a direction of travel. */
const Route *sea_route_between(const Sea *sea, int island_a, int island_b);

/* How long that crossing takes, in whole ticks. Falls back to a
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
