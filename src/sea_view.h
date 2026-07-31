#ifndef SEA_VIEW_H
#define SEA_VIEW_H

/* =========================================================
 * sea_view.h  --  The sea as a place (UI_PLAN N5)
 *
 * `world_ui.c` drew eight nodes and straight lines between them. The sim
 * has had positions, named waypoints, three routes per pair with real
 * geometry, pirate lairs and shipments at known points along their paths
 * since MARITIME_PLAN Phase 1 — and the map showed none of it. Most of
 * what the last twelve phases added is SPATIAL, and the map is both its
 * natural home and the least developed surface in the project.
 *
 * This is the part of that which can be reasoned about: where everything
 * goes, in screen coordinates, as a pure function of (snapshot, sea,
 * island). `world_ui.c` paints what it produces.
 *
 * WHY THE SPLIT, when UI_PLAN puts world_ui explicitly out of scope for
 * the widget kit. Because "out of scope for rows and columns" is not the
 * same as "untestable". A map does not want a layout cursor — but a
 * route drawn off the edge of the screen, a shipment whose marker runs
 * backwards, or a passage plotted that the player has never seen are all
 * things a headless test can catch, and none of them could be caught
 * while the geometry lived inside a function that also called SDL. The
 * projection itself moved here for the same reason: it is arithmetic,
 * and it was the one piece of world_ui.c that everything else was
 * derived from.
 *
 * WHAT IS DRAWN, AND WHY THAT IS SCOPING RATHER THAN HIDING. A passage
 * this player has not learned is NOT plotted. That looks like the
 * mistake this plan warns about twice, and it is worth saying exactly
 * why it is not: a list can print the word "unknown" in a cell, but a
 * line cannot be drawn "unknown" without inventing geometry — and
 * inventing it is the one thing worse than omitting it. The passages
 * screen (N4) still shows the row, still counts down its expiry, and
 * still sells you the map. What conceals is the sim, which will not
 * route a booking down water its seller does not know.
 *
 * SHIPMENTS ARE EVERYONE'S. A booking is the public half of the order
 * book — that is what makes the book the honest channel — so other
 * players' cargo is drawn too, in the muted style the ghost voyages
 * already use. What is NOT drawn is anything the client was not told:
 * a shipment on a private passage arrives here already reported as
 * though it took the lane, because redact_for() did that before the
 * snapshot existed.
 * ========================================================= */

#include <stdint.h>
#include "ui_snapshot.h"
#include "sea.h"

/* Twenty-one is the most routes one island can have out of it at eight
 * islands (7 destinations x 3). The slack is for shipments passing on
 * water that is nobody's business here. */
#define SEA_VIEW_MAX_PATHS   32
#define SEA_VIEW_MAX_POINTS  (SEA_MAX_ROUTE_WAYPOINTS + 2)
#define SEA_VIEW_MAX_MARKS   SEA_MAX_WAYPOINTS

/* The map draws at most this many shipments, for the reason the ghost
 * cap exists: past a couple of dozen markers the sea stops being
 * readable. The remainder is counted and stated rather than dropped. */
#define SEA_VIEW_MAX_CARGO   24

typedef struct {
    float x, y;
} SeaScreen;

typedef struct {
    int32_t   route_id;
    int32_t   from_island, to_island;
    int32_t   variant;           /* SEA_ROUTE_PUBLIC, or 1..2          */
    uint8_t   is_private;
    uint8_t   held;              /* a chart of it is in hand           */
    uint8_t   carrying;          /* one of your shipments is on it now */
    uint32_t  ticks;             /* the crossing                       */
    int32_t   point_count;
    SeaScreen pt[SEA_VIEW_MAX_POINTS];
    char      name[SEA_ROUTE_NAME_LEN];
} SeaPath;

typedef struct {
    int32_t   waypoint;
    SeaScreen at;
    uint8_t   lair;              /* a fleet lies here                  */
    int32_t   guns;
    char      name[SEA_NAME_LEN];
} SeaMark;

typedef struct {
    SeaScreen at;
    int32_t   route_id;
    int32_t   from_island, to_island;
    int32_t   qty;
    uint16_t  kind, what;        /* TradeKind, resource or route id    */
    uint8_t   mine;
    uint8_t   raided;
    uint64_t  arrive_tick;
} SeaCargo;

typedef struct {
    int32_t   island;            /* the harbour this was built around  */
    uint64_t  tick;

    SeaPath   path[SEA_VIEW_MAX_PATHS];
    int32_t   path_count;

    SeaMark   mark[SEA_VIEW_MAX_MARKS];
    int32_t   mark_count;

    SeaCargo  cargo[SEA_VIEW_MAX_CARGO];
    int32_t   cargo_count;
    int32_t   cargo_skipped;     /* over the cap: said, not dropped    */
} SeaView;

/* Where a sea position lands on screen.
 *
 * Moved here from world_ui.c unchanged, including its margin: a node at
 * the very edge of the sea still needs its whole diamond on screen. It
 * is the one function everything spatial is derived from, so it belongs
 * where a test can reach it. */
#define SEA_VIEW_MARGIN_FRAC 0.10f

void sea_to_screen(SeaPos p, float screen_w, float screen_h,
                   float *out_x, float *out_y);

/* Everything the map should draw, from the harbour at `island`. */
void sea_view_build(SeaView *v, const UiSnapshot *snap, const Sea *sea,
                    int island, float screen_w, float screen_h);

/* Where a shipment is along its route right now, as a fraction. Exposed
 * because the tooltip wants it in words and the test wants it in
 * numbers. Clamped to 0..1; a booking already delivered reads 1. */
float sea_cargo_progress(const UiSnapshot *snap, const UiBooking *b,
                         const Sea *sea);

#endif /* SEA_VIEW_H */
