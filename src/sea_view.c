/*  sea_view.c  --  The sea as a place (UI_PLAN N5)
 *
 *  Pure geometry over the snapshot and the (generated) Sea. No SDL, no
 *  drawing — see sea_view.h for why a map surface has a headless half at
 *  all, and why an unlearned passage is not plotted.
 */

#include "sea_view.h"
#include "island.h"
#include "orderbook.h"
#include <string.h>

/* The paths one island can have out of it, all three variants of every
 * pair, must fit — and so must the shipments passing on other water. */
typedef char sea_view_bounds_check[
    (SEA_VIEW_MAX_PATHS >= (MAX_ISLANDS - 1) * SEA_ROUTES_PER_PAIR) ? 1 : -1];

void sea_to_screen(SeaPos p, float screen_w, float screen_h,
                   float *out_x, float *out_y)
{
    float mx = screen_w * SEA_VIEW_MARGIN_FRAC;
    float my = screen_h * SEA_VIEW_MARGIN_FRAC;

    *out_x = mx + ((float)p.x / (float)SEA_WIDTH)  * (screen_w - 2.0f * mx);
    *out_y = my + ((float)p.y / (float)SEA_HEIGHT) * (screen_h - 2.0f * my);
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n;
    if (cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

float sea_cargo_progress(const UiSnapshot *snap, const UiBooking *b,
                         const Sea *sea)
{
    const Route *r;
    uint64_t     total, started, elapsed;

    if (!snap || !b || !sea) return 0.0f;
    if (b->delivered) return 1.0f;
    if (b->route_id < 0 || b->route_id >= sea->route_count) return 0.0f;

    r     = &sea->route[b->route_id];
    total = r->total_ticks;
    if (total == 0u || b->arrive_tick < total) return 0.0f;

    /* Derived from the arrival, exactly as the sim derives it: a
     * booking stores when it lands, not when it left, so the departure
     * is the arrival less the crossing. Recomputing it any other way is
     * how a marker comes to disagree with the tick a cargo appears. */
    started = b->arrive_tick - total;
    if (snap->tick <= started) return 0.0f;

    elapsed = snap->tick - started;
    if (elapsed >= total) return 1.0f;
    return (float)elapsed / (float)total;
}

/* One route as a polyline: harbour, waypoints in order, harbour. */
static void path_points(SeaPath *out, const Sea *sea, const Route *r,
                        float screen_w, float screen_h)
{
    int n = 0, i;

    sea_to_screen(sea->island[r->from_island], screen_w, screen_h,
                  &out->pt[n].x, &out->pt[n].y);
    n++;

    for (i = 0; i < r->waypoint_count && n < SEA_VIEW_MAX_POINTS - 1; i++) {
        int w = r->waypoint[i];
        if (w < 0 || w >= sea->waypoint_count) continue;
        sea_to_screen(sea->waypoint[w].pos, screen_w, screen_h,
                      &out->pt[n].x, &out->pt[n].y);
        n++;
    }

    sea_to_screen(sea->island[r->to_island], screen_w, screen_h,
                  &out->pt[n].x, &out->pt[n].y);
    n++;

    out->point_count = n;
}

/* Is one of the local player's shipments on this route right now? The
 * lane a cargo is on is the one thing that makes a line on this map
 * urgent rather than decorative. */
static int carrying_on(const UiSnapshot *snap, int route_id)
{
    int i;
    for (i = 0; i < snap->booking_count; i++)
        if (snap->booking[i].mine && !snap->booking[i].delivered &&
            snap->booking[i].route_id == route_id) return 1;
    return 0;
}

static void build_paths(SeaView *v, const UiSnapshot *snap, const Sea *sea,
                        int island, float screen_w, float screen_h)
{
    int d, variant;

    for (d = 0; d < sea->island_count && d < MAX_ISLANDS; d++) {
        if (d == island) continue;
        if (sea_pair_index(sea, island, d) < 0) continue;

        for (variant = 0; variant < SEA_ROUTES_PER_PAIR; variant++) {
            const Route *r = sea_route_variant(sea, island, d, variant);
            SeaPath     *p;
            int          rid;

            if (!r) continue;
            rid = sea_route_id(sea, r);
            if (rid < 0 || rid >= UI_MAX_ROUTES) continue;

            /* Water you have never seen is not drawn. A cell can say
             * "unknown"; a line cannot be drawn unknown without
             * inventing where it goes — see sea_view.h. */
            if (!snap->route_known[rid]) continue;
            if (v->path_count >= SEA_VIEW_MAX_PATHS) break;

            p = &v->path[v->path_count++];
            memset(p, 0, sizeof(*p));
            p->route_id    = rid;
            p->from_island = r->from_island;
            p->to_island   = r->to_island;
            p->variant     = variant;
            p->is_private  = (uint8_t)(r->is_private ? 1 : 0);
            p->held        = (uint8_t)(snap->chart_held[rid] > 0 ? 1 : 0);
            p->carrying    = (uint8_t)carrying_on(snap, rid);
            p->ticks       = r->total_ticks;
            copy_str(p->name, sizeof(p->name), r->name);
            path_points(p, sea, r, screen_w, screen_h);
        }
    }
}

/* Every waypoint, named, and the fleets that lie at some of them.
 *
 * Where a fleet lairs is generated from the world seed and therefore no
 * secret (ui_snapshot.c says so where it copies them) — what they are
 * sitting on is not copied, and hunting them is N6's question. A lane
 * that runs through one is the point: it is the only way "this passage
 * is fast but unsafe" is ever visible before the cargo is lost. */
static void build_marks(SeaView *v, const UiSnapshot *snap, const Sea *sea,
                        float screen_w, float screen_h)
{
    int w, i;

    for (w = 0; w < sea->waypoint_count && w < SEA_VIEW_MAX_MARKS; w++) {
        SeaMark *m = &v->mark[v->mark_count++];

        memset(m, 0, sizeof(*m));
        m->waypoint = w;
        copy_str(m->name, sizeof(m->name), sea->waypoint[w].name);
        sea_to_screen(sea->waypoint[w].pos, screen_w, screen_h,
                      &m->at.x, &m->at.y);

        for (i = 0; i < snap->pirate_count; i++) {
            if (!snap->pirate[i].active) continue;
            if (snap->pirate[i].waypoint != w) continue;
            m->lair = 1;
            m->guns = snap->pirate[i].guns;
        }
    }
}

static void build_cargo(SeaView *v, const UiSnapshot *snap, const Sea *sea,
                        float screen_w, float screen_h)
{
    int i;

    for (i = 0; i < snap->booking_count; i++) {
        const UiBooking *b = &snap->booking[i];
        const Route     *r;
        SeaCargo        *c;
        float            t;

        if (b->delivered) continue;
        if (b->route_id < 0 || b->route_id >= sea->route_count) continue;

        if (v->cargo_count >= SEA_VIEW_MAX_CARGO) { v->cargo_skipped++; continue; }

        r = &sea->route[b->route_id];
        t = sea_cargo_progress(snap, b, sea);

        c = &v->cargo[v->cargo_count++];
        memset(c, 0, sizeof(*c));
        c->route_id    = b->route_id;
        c->from_island = b->from_island;
        c->to_island   = b->to_island;
        c->qty         = b->qty;
        c->kind        = b->kind;
        c->what        = b->what;
        c->mine        = b->mine;
        c->raided      = b->raided;
        c->arrive_tick = b->arrive_tick;

        /* Walked along the real path rather than lerped between two
         * nodes: a cargo on a passage that threads the Coffin Race
         * should be seen going round by the Coffin Race, or the map is
         * back to being a diagram of the world. */
        {
            SeaPos at = sea_route_point(sea, r,
                            (uint32_t)(t * (float)r->total_ticks));
            sea_to_screen(at, screen_w, screen_h, &c->at.x, &c->at.y);
        }
    }
}

void sea_view_build(SeaView *v, const UiSnapshot *snap, const Sea *sea,
                    int island, float screen_w, float screen_h)
{
    memset(v, 0, sizeof(*v));
    if (!snap || !sea) return;

    v->island = island;
    v->tick   = snap->tick;

    if (island >= 0 && island < MAX_ISLANDS)
        build_paths(v, snap, sea, island, screen_w, screen_h);
    build_marks(v, snap, sea, screen_w, screen_h);
    build_cargo(v, snap, sea, screen_w, screen_h);
}
