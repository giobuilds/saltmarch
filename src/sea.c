/* sea.c  --  Generating the water between the islands
 * (MARITIME_PLAN Phase 1: sea geometry) */

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

/* ---- names ------------------------------------------------ */
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

/* ---- placement -------------------------------------------- */
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

/* ---- routes ----------------------------------------------- */
#define ROUTE_DETOUR_LIMIT 3   /* max detour, as a fraction: dist/N */

/* The public lane is slower than the water alone requires: it is. */
#define ROUTE_CONVOY_NUM   9
#define ROUTE_CONVOY_DEN   8   /* the public lane takes 9/8 the time */

/* Waypoints, ranked by how far off the direct line they sit. Fills
 * `out` with waypoint indices, nearest detour first, and returns how
 * many were within `limit_div` (a detour of at most direct/limit_div). */
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

/* The three routes joining one pair. */
static void build_routes(Sea *sea, int a, int b)
{
    enum { RANK_MAX = SEA_PRIVATE_POOL + 2 };
    int rank[RANK_MAX];
    int n, lane_wp = -1;
    int i;

    /* Unlimited: every waypoint is ranked, and how far a detour may go
     * is decided below per route rather than by excluding candidates
     * that the fallbacks would then need to invent replacements for. */
    n = rank_waypoints(sea, sea->island[a], sea->island[b], 0,
                       rank, RANK_MAX);

    /* The lane threads the FURTHEST waypoint any of this pair's routes */
    if (n > SEA_PRIVATE_POOL - 1) lane_wp = rank[SEA_PRIVATE_POOL - 1];
    else if (n > 0)               lane_wp = rank[n - 1];

    add_route(sea, a, b, 0, lane_wp, ROUTE_CONVOY_NUM, ROUTE_CONVOY_DEN);

    /* Then the pool of private passages. The first is the open reach — */
    for (i = 0; i < SEA_PRIVATE_POOL; i++) {
        int wp = -1;

        if (i > 0 && i - 1 < n) {
            int k, taken = 0;
            for (k = 0; k < n; k++) {
                /* Never the lane's own water: two routes through one
                 * waypoint would be one route sold twice. */
                if (rank[k] == lane_wp) continue;
                if (taken == i - 1) { wp = rank[k]; break; }
                taken++;
            }
        }
        add_route(sea, a, b, i + 1, wp, 1, 1);
    }
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

int sea_pair_index(const Sea *sea, int island_a, int island_b)
{
    int lo = island_a < island_b ? island_a : island_b;
    int hi = island_a < island_b ? island_b : island_a;
    int i, n = 0;

    if (lo < 0 || hi >= sea->island_count || lo == hi) return -1;

    /* Pairs in generation order: (0,1) (0,2) ... (1,2) ... */
    for (i = 0; i < lo; i++) n += sea->island_count - 1 - i;
    return n + (hi - lo - 1);
}

/* The stored slot for one of a pair's routes: 0 is the lane, 1..pool
 * are the private passages in generation order. */
static const Route *pair_slot(const Sea *sea, int pair, int slot)
{
    int idx = pair * SEA_STORED_PER_PAIR + slot;

    if (pair < 0 || idx < 0 || idx >= sea->route_count) return NULL;
    return &sea->route[idx];
}

const Route *sea_route_variant(const Sea *sea, int island_a, int island_b,
                               int variant)
{
    int pair = sea_pair_index(sea, island_a, island_b);
    int cursor;

    if (pair < 0 || variant < 0 || variant >= SEA_ROUTES_PER_PAIR)
        return NULL;
    if (variant == SEA_ROUTE_PUBLIC) return pair_slot(sea, pair, 0);

    /* The live private passages are the two the cursor points at. The
     * rest of the pool exists and is generated, but is not in use —
     * a chart for one of them is a map of water nobody sails. */
    cursor = sea->pair_cursor[pair] % SEA_PRIVATE_POOL;
    return pair_slot(sea, pair,
                     1 + (cursor + variant - 1) % SEA_PRIVATE_POOL);
}

const Route *sea_route_between(const Sea *sea, int island_a, int island_b)
{
    return sea_route_variant(sea, island_a, island_b, SEA_ROUTE_PUBLIC);
}

int sea_route_count_between(const Sea *sea, int island_a, int island_b)
{
    return sea_pair_index(sea, island_a, island_b) < 0
         ? 0 : SEA_ROUTES_PER_PAIR;
}

int sea_route_id(const Sea *sea, const Route *route)
{
    if (!route || route < sea->route ||
        route >= sea->route + sea->route_count) return -1;
    return (int)(route - sea->route);
}

int sea_rotate_pair(Sea *sea, int pair)
{
    const Route *going;
    int          cursor;

    if (pair < 0 || pair >= SEA_MAX_PAIRS) return -1;

    /* The one leaving play is the passage the cursor currently points
     * at; advancing brings the far end of the pool in behind it. */
    cursor = sea->pair_cursor[pair] % SEA_PRIVATE_POOL;
    going  = pair_slot(sea, pair, 1 + cursor);
    sea->pair_cursor[pair] =
        (uint8_t)((cursor + 1) % SEA_PRIVATE_POOL);

    return going ? sea_route_id(sea, going) : -1;
}

/* Pairs do NOT all turn over together: each is staggered by its own
 * index across one lifetime, so the sea changes shape continuously
 * rather than voiding every chart in the world on the same tick. */
uint64_t sea_pair_next_rotation(int island_count, int pair, uint64_t now)
{
    uint64_t pairs = (island_count > 1)
                   ? (uint64_t)island_count * (uint64_t)(island_count - 1) / 2u
                   : 1u;
    uint64_t offset, first, since;

    if (pair < 0 || pairs == 0u) return 0u;

    offset = (uint64_t)pair * SEA_ROUTE_LIFETIME_TICKS / pairs;
    first  = offset + SEA_ROUTE_LIFETIME_TICKS;

    if (now <= first) return first;

    since = (now - first) % SEA_ROUTE_LIFETIME_TICKS;
    return since == 0u ? now : now + (SEA_ROUTE_LIFETIME_TICKS - since);
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

    /* Seeded with the crossing's ends before the walk, so the tail */
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
