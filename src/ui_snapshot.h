#ifndef UI_SNAPSHOT_H
#define UI_SNAPSHOT_H

/* ui_snapshot.h  --  What the UI is allowed to see
 * (UI_PLAN Phase 0, decision 1) */

#include <stdint.h>
#include "building.h"    /* MAX_BUILDINGS, BuildingType                */
#include "island.h"      /* MAX_ISLANDS, ISLAND_NAME_LEN               */
#include "faction.h"     /* FACTION_HIST_LEN                           */
#include "resource.h"    /* RES_COUNT                                  */
#include "ship.h"        /* MAX_SHIPS                                  */
#include "game.h"        /* ConfirmState (the pending confirmation)    */

struct GameState;        /* the builder's input; UI code never sees it */

/* One building, compacted: what an overlay needs to draw or reason
 * about it, not the simulation's working state. */
typedef struct {
    int16_t type;            /* BuildingType                           */
    int16_t row, col;
    uint8_t active;
    uint8_t connected;       /* road-connected to a warehouse          */
    uint8_t worker_count;
    uint8_t residents;       /* houses only; 0 elsewhere               */
    uint8_t happiness;       /* houses only: 0..HAPPINESS_MAX          */
    int16_t origin_tier;     /* the house type this was upgraded FROM,
                              * or BUILDING_NONE. A Scholar's House's
                              * basics depend on it (NEEDS_PLAN Ph.1)  */
} UiBuilding;

typedef struct {
    char     name[ISLAND_NAME_LEN];
    uint8_t  settled;
    uint8_t  docking_allowed;
    uint32_t owner;              /* player id, PLAYER_NONE if unowned  */

    /* Is this island's detail KNOWN, or merely absent (UI_PLAN N1)? */
    uint8_t  detail_known;

    int32_t  stock[RES_COUNT];
    int32_t  escrow[RES_COUNT];
    uint32_t escrow_nonce;       /* island_escrow_nonce() (UI_PLAN M5) */
    int32_t  capacity;           /* per-resource storage cap           */

    int32_t  residents;          /* island total, housed only          */

    /* ---- the reserve and the treasury (LIFE_PLAN Phase 7) ----
     * What the vitals strip and the tax control read. All derived from
     * the sim each frame; the UI never reaches into GameState for them,
     * which is the whole point of this struct. */
    int32_t  reserve;            /* people with no roof                */
    int32_t  founder_allowance;  /* households still importable        */
    int32_t  homes_empty;        /* roofs with nobody under them       */
    int32_t  left_last_month;    /* people who gave up waiting         */
    int32_t  tax_rate_permille;
    int32_t  compliance_permille;
    int32_t  tax_last_month;

    UiBuilding buildings[MAX_BUILDINGS];
    int32_t    building_count;

    /* What this harbour can currently put to sea, and what it has out
     * (UI_PLAN N1). Capacities are derived from the buildings above,
     * but resolved here so no overlay reproduces the rule. Meaningful
     * only when detail_known. */
    int32_t  merchants_out, merchant_capacity;
    int32_t  hulls_out, hull_capacity;
    int32_t  scholars_out, scholar_capacity;
    int32_t  research_boats;
    uint8_t  insure_shipments;
} UiIsland;

/* The snapshot-side twin of island_has_building() (island.h): is an
 * active, road-connected building of `type` on this island?
 * BUILDING_NONE answers 1. */
int snapshot_has_building(const UiIsland *isl, BuildingType type);

/* The tier-upgrade rule as the UI sees it: reads the house at `idx`,
 * looks up its own prerequisite, and returns tier_upgrade_check()'s
 * verdict. One call so no overlay reassembles the arguments itself. */
RejectReason snapshot_upgrade_check(const UiIsland *isl, int idx,
                                    int branch, BuildingType *out_to);

typedef struct {
    uint8_t  active;
    int32_t  at_island;          /* -1 while at sea                    */
    int32_t  from_island, to_island;
    float    progress;           /* 0..1, derived; cosmetic only       */
    int32_t  cargo[RES_COUNT];

    /* What kind of hull, and how much of it is left (UI_PLAN N6). */
    int32_t  klass;              /* ShipClass                          */
    int32_t  guns, hull, hull_max;
    int32_t  hold;               /* per-resource capacity of this hull */
    int32_t  escorting;          /* ship index it guards, or -1        */
    uint8_t  mine;               /* the local player commands it       */
} UiShip;

/* How the simulation itself is doing. Not world state — these. */
/* ---- the maritime world, in UI terms (UI_PLAN N1) --------- */
#define UI_MAX_ORDERS    256
#define UI_MAX_BOOKINGS   64
#define UI_MAX_ROUTES    512
#define UI_MAX_PAIRS     120
#define UI_MAX_SURVEYS    32
#define UI_MAX_PIRATES     6

typedef struct {
    uint32_t id;                 /* stable identity, never a row index */
    uint32_t owner;
    int32_t  island;
    uint16_t kind;               /* TradeKind                          */
    uint16_t what;               /* resource, or route id for a chart  */
    int32_t  side;               /* OrderSide                          */
    int32_t  qty;                /* units still unfilled               */
    int32_t  limit;
    uint64_t placed_tick;
    uint8_t  mine;               /* the local player posted it         */
} UiOrder;

typedef struct {
    uint16_t kind, what;
    int32_t  qty, price;
    int32_t  from_island, to_island;
    int32_t  route_id;
    uint64_t arrive_tick;
    uint8_t  delivered;
    uint8_t  raided;
    uint8_t  mine;               /* the local player is buyer or seller */
} UiBooking;

typedef struct {
    int32_t  from_island, to_island;
    int32_t  route_id;
    uint64_t finish_tick;
} UiSurvey;

/* A fleet the player has reason to know about. Where they lair is
 * generated from the seed and therefore not a secret; what they are
 * sitting on is only known once you have been there, which is a
 * question for N5 rather than a field here. */
typedef struct {
    int32_t  waypoint;
    int32_t  guns;
    uint8_t  active;
} UiPirate;

typedef struct {
    int32_t  replay_state;       /* GameState.replay_state (0..3)      */
    uint32_t backlog_ticks;      /* ticks the accumulator owes         */
    int32_t  feed_age_s;         /* seconds since the feed changed,
                                  * -1 when there is no feed           */
    int32_t  net_connected;      /* -1 offline, else peers connected   */
    int32_t  feed_malformed;     /* records the feed parser rejected   */
    int32_t  feed_ghosts;        /* voyages currently in the feed      */
} UiHealth;

typedef struct {
    uint64_t tick;               /* the tick this snapshot describes   */
    uint32_t local_player_id;
    UiHealth health;

    UiIsland islands[MAX_ISLANDS];
    int32_t  current_island;

    UiShip   ships[MAX_SHIPS];
    int32_t  ship_count;

    /* The market's live quotes, already resolved through faction_bid/
     * faction_ask so no overlay reproduces the pricing rule. */
    int32_t  bid[RES_COUNT];
    int32_t  ask[RES_COUNT];
    int32_t  counterparty_stock[RES_COUNT];
    int32_t  counterparty_gold;

    /* The market's recent mid-prices, oldest first (UI_PLAN M3). */
    int16_t  price_hist[RES_COUNT][FACTION_HIST_LEN];
    int32_t  price_hist_count[RES_COUNT];

    /* ---- the maritime world (UI_PLAN N1) ------------------
     * Everything the last twelve phases of simulation added, in the
     * form the screens will read it. Bounded and small; the one thing
     * deliberately NOT here is the Sea itself — see ui_snapshot.c. */

    UiOrder    order[UI_MAX_ORDERS];
    int32_t    order_count;
    UiBooking  booking[UI_MAX_BOOKINGS];
    int32_t    booking_count;

    /* What the local player knows of the sea. Charts are indexed by
     * sea route id, which is the id a chart trades under. */
    uint8_t    chart_held[UI_MAX_ROUTES];
    uint8_t    route_known[UI_MAX_ROUTES];

    /* Which two private passages are in play for each island pair —
     * the one mutable byte of the Sea, so the UI can read the rest of
     * it directly and still be looking at the same world. */
    uint8_t    pair_cursor[UI_MAX_PAIRS];

    UiSurvey   survey[UI_MAX_SURVEYS];
    int32_t    survey_count;

    UiPirate   pirate[UI_MAX_PIRATES];
    int32_t    pirate_count;

    /* The pending confirmation, copied whole (UI_PLAN Phase 6). The
     * popup's builder is a pure function of the snapshot like every
     * other, and the command it renders is the one that will be
     * submitted — not a reconstruction of it. */
    ConfirmState confirm;
} UiSnapshot;

/* ---- client-side view state ------------------------------- */
typedef struct {
    int32_t   hud_category;      /* HUD tab (UI_PLAN Phase 3)          */
    int32_t   exchange_page;     /* trade screen page (Phase 1)        */
    int32_t   inventory_page;    /* inventory overlay page (Phase 4)   */

    /* The order book's page, and the draft order being composed on. */
    int32_t   book_page;
    int32_t   book_side;         /* OrderSide: 0 buy, 1 sell           */
    int32_t   book_res;          /* ResourceType being composed        */
    int32_t   book_qty;
    int32_t   book_limit;        /* 0 => follow the market's quote     */

    int32_t   chart_page;        /* the passages overlay (UI_PLAN N4)  */
    int32_t   yard_page;         /* the shipyard overlay (UI_PLAN N6)  */
} UiState;

/* Note: which overlay is OPEN is not here. Phase 0 sketched. */

/* Fill `out` from the live world. The one function in the UI layer that
 * is allowed to see a GameState — everything downstream takes the
 * snapshot. Call once per frame, after the tick loop. */
void ui_snapshot_build(UiSnapshot *out, const struct GameState *gs);

#endif /* UI_SNAPSHOT_H */
