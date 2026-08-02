#ifndef SEA_VIEW_H
#define SEA_VIEW_H

/* sea_view.h  --  The sea as a place (UI_PLAN N5) */

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

/* Where a sea position lands on screen. */
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
