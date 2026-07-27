/*  sea.c  --  Generating the water between the islands
 *             (MARITIME_PLAN Phase 1: sea geometry)
 *
 *  See sea.h for what a Sea is and why it is regenerated rather than
 *  saved. This file is the generator, and it is integer-only on
 *  purpose — every number it produces is one the simulation may later
 *  depend on, and a float that rounds differently on two machines
 *  would surface as a desync rather than as a wrong answer.
 *
 *  It draws its randomness from a hash of (seed, purpose, index)
 *  rather than from a running generator, the same technique map.c uses
 *  for its deposit and crop passes. That means generating waypoint 7
 *  never depends on how many times anything else was generated first,
 *  so adding a pass here cannot silently move everything downstream of
 *  it.
 */

#include "sea.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- deterministic integer noise --------------------------
 * FNV-1a over the inputs. Well-mixed, integer-only, identical
 * everywhere — the same reasoning voyage_is_raided sets out. */
static uint32_t sea_hash(uint32_t seed, uint32_t purpose,
                         uint32_t a, uint32_t b)
{
    uint32_t h = 2166136261u;
    uint32_t part[4];
    int      i;

    part[0] = seed;
    part[1] = purpose;
    part[2] = a;
    part[3] = b;

    for (i = 0; i < 4; i++) {
        h ^= part[i];
        h *= 16777619u;
    }
    /* One more round: the low bits of a single FNV pass are weak, and
     * these values are used modulo small numbers. */
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

/* Integer square root, Newton's method on integers. No <math.h>, no
 * float, no platform variance. */
static uint32_t isqrt32(uint32_t n)
{
    uint32_t x, y;

    if (n == 0) return 0;
    x = n;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

uint32_t sea_distance(SeaPos a, SeaPos b)
{
    int64_t dx = (int64_t)a.x - (int64_t)b.x;
    int64_t dy = (int64_t)a.y - (int64_t)b.y;
    uint64_t sq = (uint64_t)(dx * dx + dy * dy);

    /* The sea is 10000 units square, so the longest possible distance
     * squared is 2e8 — comfortably inside 32 bits once the 64-bit
     * multiply is done. */
    if (sq > 0xFFFFFFFFull) sq = 0xFFFFFFFFull;
    return isqrt32((uint32_t)sq);
}

/* ---- names ------------------------------------------------
 * Waypoints are places, so they have names rather than coordinates —
 * a lane described as "by the Gullet" reads as somewhere a person has
 * been. Authored content, like the island names: PRIVACY.md's rule is
 * about text a PLAYER supplies reaching world state, and this is not
 * that. */
static const char *const WAYPOINT_NAMES[] = {
    "the Gullet",      "Widow's Reach",   "Harrow Shoal",   "the Kelp Gate",
    "Drownman's Bar",  "the Bellows",     "Silt Narrows",   "Cormorant Rock",
    "the Sunken Mole", "Gannet Spit",     "the Blind Sound","Mirebank",
    "the Teeth",       "Saltwrack",       "Lantern Skerry", "the Slack",
    "Rimefast Bar",    "the Coffin Race", "Hookline Deep",  "the Weeping Stones",
    "Fathom Gate",     "the Ossuary",     "Petrel Narrows", "the Long Shallow"
};
#define WAYPOINT_NAME_COUNT \
    ((int)(sizeof WAYPOINT_NAMES / sizeof WAYPOINT_NAMES[0]))

/* ---- placement --------------------------------------------
 * Rejection sampling with a minimum separation, bounded so generation
 * always terminates. If the attempts run out the last candidate is
 * taken: a slightly crowded sea is a far better failure than a hang,
 * and the bound is generous enough that it does not happen at the
 * counts this uses. */
#define PLACE_ATTEMPTS 64

static int too_close(SeaPos p, const SeaPos *taken, int count, int32_t min_d)
{
    int i;
    for (i = 0; i < count; i++)
        if (sea_distance(p, taken[i]) < (uint32_t)min_d) return 1;
    return 0;
}

static SeaPos place_one(uint32_t seed, uint32_t purpose, uint32_t index,
                        const SeaPos *taken, int count, int32_t margin,
                        int32_t min_sep)
{
    SeaPos p;
    int    attempt;

    p.x = 0;
    p.y = 0;

    for (attempt = 0; attempt < PLACE_ATTEMPTS; attempt++) {
        uint32_t hx = sea_hash(seed, purpose, index, (uint32_t)attempt * 2u);
        uint32_t hy = sea_hash(seed, purpose, index, (uint32_t)attempt * 2u + 1u);
        int32_t  span_x = SEA_WIDTH - 2 * margin;
        int32_t  span_y = SEA_HEIGHT - 2 * margin;

        p.x = margin + (int32_t)(hx % (uint32_t)span_x);
        p.y = margin + (int32_t)(hy % (uint32_t)span_y);

        if (!too_close(p, taken, count, min_sep)) break;
    }
    return p;
}

/* ---- routes -----------------------------------------------
 * A route from A to B threads the waypoint nearest the midpoint of the
 * crossing, so a path bends towards somewhere real rather than being a
 * straight line with a name. A waypoint too far off the line is
 * ignored — a detour past the far corner of the sea is not a route,
 * it is a mistake. */
#define ROUTE_DETOUR_LIMIT 3   /* max detour, as a fraction: dist/N */

/* The public lane is slower than the water alone requires: it is a
 * patrolled convoy route with mandated calls, not a straight run. This
 * is what MAKES public slow and private fast, and it is applied as a
 * duration penalty rather than left to the geometry because geometry
 * cannot promise it. A pair whose waypoints all happened to sit almost
 * on the direct line would otherwise generate three routes of nearly
 * identical length, and the entire risk/speed trade-off of Phase 3
 * would quietly not exist for that pair.
 *
 * Expressed as a numerator/denominator so it stays integer: hashed
 * state, so no floats (see sea.h). */
#define ROUTE_CONVOY_NUM   9
#define ROUTE_CONVOY_DEN   8   /* the public lane takes 9/8 the time */

/* Waypoints, ranked by how far off the direct line they sit. Fills
 * `out` with waypoint indices, nearest detour first, and returns how
 * many were within `limit_div` (a detour of at most direct/limit_div).
 * Ties break on the lower index so the ranking is total — two clients
 * that ordered a tie differently would generate different routes from
 * the same seed, which is the failure this whole file is written to
 * avoid. */
static int rank_waypoints(const Sea *sea, SeaPos a, SeaPos b,
                          int limit_div, int *out, int max_out)
{
    uint32_t direct = sea_distance(a, b);
    int      n = 0;
    int      i, j;

    for (i = 0; i < sea->waypoint_count; i++) {
        SeaPos   w   = sea->waypoint[i].pos;
        uint32_t via = sea_distance(a, w) + sea_distance(w, b);
        uint32_t extra;

        if (via <= direct) continue;          /* impossible, but be safe */
        extra = via - direct;
        if (limit_div > 0 && extra > direct / (uint32_t)limit_div) continue;

        /* Insertion into the ranked list. */
        for (j = 0; j < n; j++) {
            SeaPos   w2    = sea->waypoint[out[j]].pos;
            uint32_t via2  = sea_distance(a, w2) + sea_distance(w2, b);
            uint32_t extra2 = via2 - direct;
            if (extra < extra2 || (extra == extra2 && i < out[j])) break;
        }
        if (j >= max_out) continue;
        if (n < max_out) n++;
        {
            int k;
            for (k = n - 1; k > j; k--) out[k] = out[k - 1];
        }
        out[j] = i;
    }
    return n;
}


static uint32_t leg_ticks(SeaPos a, SeaPos b)
{
    uint32_t t = sea_distance(a, b) / SEA_UNITS_PER_TICK;
    return t < 1u ? 1u : t;        /* no instantaneous legs */
}

/* Lay one route down: `wp` is the waypoint it threads, or -1 for the
 * open crossing. `slow_num/slow_den` scales the duration. */
static void add_route(Sea *sea, int a, int b, int variant, int wp,
                      int slow_num, int slow_den)
{
    Route *r;
    SeaPos pa, pb;

    if (sea->route_count >= SEA_MAX_ROUTES) return;

    r  = &sea->route[sea->route_count];
    pa = sea->island[a];
    pb = sea->island[b];

    memset(r, 0, sizeof(*r));
    r->from_island = a;
    r->to_island   = b;
    r->variant     = variant;
    r->is_private  = (variant != SEA_ROUTE_PUBLIC);

    if (wp >= 0) {
        r->waypoint[0]    = wp;
        r->waypoint_count = 1;
        r->leg_ticks[0]   = leg_ticks(pa, sea->waypoint[wp].pos);
        r->leg_ticks[1]   = leg_ticks(sea->waypoint[wp].pos, pb);
        snprintf(r->name, sizeof(r->name), "%s %s",
                 r->is_private ? "past" : "by", sea->waypoint[wp].name);
    } else {
        r->waypoint_count = 0;
        r->leg_ticks[0]   = leg_ticks(pa, pb);
        snprintf(r->name, sizeof(r->name), "%s",
                 r->is_private ? "the open reach" : "the open crossing");
    }

    /* The penalty goes on the legs, not only on the total, or a ship
     * drawn along the path would arrive at the far island and then keep
     * sailing while the clock caught up. */
    {
        int legs = r->waypoint_count + 1, i;
        r->total_ticks = 0;
        for (i = 0; i < legs; i++) {
            uint32_t t = r->leg_ticks[i] * (uint32_t)slow_num
                       / (uint32_t)slow_den;
            r->leg_ticks[i] = t < 1u ? 1u : t;
            r->total_ticks += r->leg_ticks[i];
        }
    }

    sea->route_count++;
}

/* The three routes joining one pair.
 *
 *   variant 0, public   -- the patrolled convoy lane: a wider detour,
 *                          and slowed on top of it.
 *   variant 1, private  -- the open reach. Straight water, fastest.
 *   variant 2, private  -- a shortcut past the nearest waypoint.
 *
 * Two properties have to hold for EVERY pair, not merely for most, and
 * both are arranged by construction rather than hoped for:
 *
 *   The three are different water. Variant 1 threads no waypoint at
 *   all, and variants 0 and 2 are given different ones. Two routes
 *   through the same waypoint would be one route sold twice, and a
 *   chart for the second would buy nothing.
 *
 *   Every private passage beats the lane. The waypoints are ranked by
 *   how far off the direct line they sit and the lane always takes one
 *   ranked no nearer than the shortcut's, so the lane's path is at
 *   least as long BEFORE the convoy penalty is applied. If this ever
 *   stops holding, charts become a cost with no benefit.
 *
 * Both are asserted across seeds in test_sea, which is what caught the
 * first version of this function: it drew the two waypoints from
 * differently-limited rankings, and pairs with no waypoint near enough
 * to qualify fell back to open water for the lane as well — three
 * routes, two of them the same crossing, the "private" one slower. */
static void build_routes(Sea *sea, int a, int b)
{
    enum { RANK_MAX = 8 };
    int rank[RANK_MAX];
    int n, lane_wp = -1, short_wp = -1;

    /* Unlimited: every waypoint is ranked, and how far a detour may go
     * is decided below per route rather than by excluding candidates
     * that the fallbacks would then need to invent replacements for. */
    n = rank_waypoints(sea, sea->island[a], sea->island[b], 0,
                       rank, RANK_MAX);

    if (n > 0) short_wp = rank[0];

    /* The lane threads the NEXT waypoint out from the shortcut's. Not
     * the furthest available: an early version took that, and the
     * average public crossing went from the ~200 ticks sea.h calibrates
     * SEA_UNITS_PER_TICK against to 429 — every voyage in the game
     * silently slowed by half, in an increment that ships no charts to
     * compensate. The lane is a slightly wider berth, not a tour. */
    if (n > 1) lane_wp = rank[1];

    add_route(sea, a, b, 0, lane_wp,  ROUTE_CONVOY_NUM, ROUTE_CONVOY_DEN);
    add_route(sea, a, b, 1, -1,       1, 1);
    add_route(sea, a, b, 2, short_wp, 1, 1);
}

/* ---- the generator ---------------------------------------- */

void sea_init(Sea *sea, uint32_t seed, int island_count)
{
    SeaPos taken[16 + SEA_MAX_WAYPOINTS];
    int    n_taken = 0;
    int    i, j;

    memset(sea, 0, sizeof(*sea));

    if (island_count < 0) island_count = 0;
    if (island_count > (int)(sizeof sea->island / sizeof sea->island[0]))
        island_count = (int)(sizeof sea->island / sizeof sea->island[0]);
    sea->island_count = island_count;

    /* Islands first: they are what everything else is placed around,
     * and they are kept off the very edge so a route never has to hug
     * the boundary. */
    for (i = 0; i < island_count; i++) {
        SeaPos p = place_one(seed, 0x1515Au, (uint32_t)i, taken, n_taken,
                             SEA_MIN_ISLAND_SEPARATION,
                             SEA_MIN_ISLAND_SEPARATION);
        sea->island[i]   = p;
        taken[n_taken++] = p;
    }

    /* Then waypoints, in the water the islands left. */
    sea->waypoint_count = SEA_MAX_WAYPOINTS < WAYPOINT_NAME_COUNT
                        ? SEA_MAX_WAYPOINTS : WAYPOINT_NAME_COUNT;
    for (i = 0; i < sea->waypoint_count; i++) {
        SeaPos p = place_one(seed, 0x5741Fu, (uint32_t)i, taken, n_taken,
                             SEA_MIN_WAYPOINT_SEPARATION,
                             SEA_MIN_WAYPOINT_SEPARATION);
        sea->waypoint[i].pos = p;
        snprintf(sea->waypoint[i].name, SEA_NAME_LEN, "%s",
                 WAYPOINT_NAMES[i]);
        taken[n_taken++] = p;
    }

    /* Three routes per island pair (MARITIME_PLAN Phase 3): the lane
     * everybody knows, and two passages a chart buys you. All of them
     * are generated with the world; concealment is a property of what
     * a PLAYER knows, not of what exists. */
    for (i = 0; i < island_count; i++)
        for (j = i + 1; j < island_count; j++)
            build_routes(sea, i, j);
}

const Route *sea_route_variant(const Sea *sea, int island_a, int island_b,
                               int variant)
{
    int i;

    for (i = 0; i < sea->route_count; i++) {
        const Route *r = &sea->route[i];
        if (r->variant != variant) continue;
        if ((r->from_island == island_a && r->to_island == island_b) ||
            (r->from_island == island_b && r->to_island == island_a))
            return r;
    }
    return NULL;
}

const Route *sea_route_between(const Sea *sea, int island_a, int island_b)
{
    return sea_route_variant(sea, island_a, island_b, SEA_ROUTE_PUBLIC);
}

int sea_route_count_between(const Sea *sea, int island_a, int island_b)
{
    int i, n = 0;

    for (i = 0; i < sea->route_count; i++) {
        const Route *r = &sea->route[i];
        if ((r->from_island == island_a && r->to_island == island_b) ||
            (r->from_island == island_b && r->to_island == island_a))
            n++;
    }
    return n;
}

int sea_route_id(const Sea *sea, const Route *route)
{
    if (!route || route < sea->route ||
        route >= sea->route + sea->route_count) return -1;
    return (int)(route - sea->route);
}

uint32_t sea_crossing_ticks(const Sea *sea, int island_a, int island_b)
{
    const Route *r = sea_route_between(sea, island_a, island_b);
    return r ? r->total_ticks : 1u;
}

SeaPos sea_route_point(const Sea *sea, const Route *route, uint32_t elapsed)
{
    SeaPos from, to;
    int    leg, legs;

    if (!route) { from.x = 0; from.y = 0; return from; }

    /* Seeded with the crossing's ends before the walk, so the tail
     * `return to` is right by construction rather than because the
     * loop is guaranteed to have run at least once. It is guaranteed —
     * legs is waypoint_count + 1, so never zero — but MSVC cannot see
     * that and warned, and it was right to: a function whose result
     * depends on a loop having executed is one refactor away from
     * returning a stack value. */
    from = sea->island[route->from_island];
    to   = sea->island[route->to_island];

    legs = route->waypoint_count + 1;

    for (leg = 0; leg < legs; leg++) {
        uint32_t span = route->leg_ticks[leg];

        from = (leg == 0)
             ? sea->island[route->from_island]
             : sea->waypoint[route->waypoint[leg - 1]].pos;
        to   = (leg == legs - 1)
             ? sea->island[route->to_island]
             : sea->waypoint[route->waypoint[leg]].pos;

        if (elapsed < span) {
            /* Linear along this leg. 64-bit intermediate so a long leg
             * across a wide sea cannot overflow the multiply. */
            SeaPos p;
            p.x = (int32_t)(from.x +
                  (int64_t)(to.x - from.x) * (int64_t)elapsed / (int64_t)span);
            p.y = (int32_t)(from.y +
                  (int64_t)(to.y - from.y) * (int64_t)elapsed / (int64_t)span);
            return p;
        }
        elapsed -= span;
    }
    return to;   /* past the end: at the far island */
}
