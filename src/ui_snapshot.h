#ifndef UI_SNAPSHOT_H
#define UI_SNAPSHOT_H

/* =========================================================
 * ui_snapshot.h  --  What the UI is allowed to see
 *                    (UI_PLAN Phase 0, decision 1)
 *
 * A UiSnapshot is a copy of the world taken once per frame, AFTER the
 * tick accumulator has finished — so every overlay in a frame sees the
 * same tick, and none of them can observe the world mid-tick.
 *
 * The point is what it makes impossible. An overlay builder takes
 *
 *     *_build(UiList *, const UiSnapshot *, const UiState *)
 *
 * and never a GameState*, so "the UI mutated the stockpile" and "the UI
 * stepped the RNG" stop being review comments and become compile
 * errors. MMO_PLAN's risk register puts "a mutation path escapes the
 * funnel" at number one, and the UI is where those paths live.
 *
 * The second payoff is that a snapshot does not say where it came from.
 * Live sim, a replayed past tick, a remote server's state — the UI
 * cannot tell, which is what makes a time-travel scrubber or a
 * spectator view a matter of choosing a source rather than rewriting
 * overlays.
 *
 * Cost: ~20 KB memcpy'd per frame at MAX_BUILDINGS=600 across four
 * islands. Negligible beside a single frame of rendering, and the
 * alternative (handing out live pointers "just for reading") is how the
 * discipline erodes.
 *
 * SDL-free, like everything the UI kit touches.
 * ========================================================= */

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
    uint8_t happy;           /* houses only: needs met last tick       */
} UiBuilding;

typedef struct {
    char     name[ISLAND_NAME_LEN];
    uint8_t  settled;
    uint8_t  docking_allowed;
    uint32_t owner;              /* player id, PLAYER_NONE if unowned  */

    /* Is this island's detail KNOWN, or merely absent (UI_PLAN N1)?
     *
     * The most important byte in this struct, and the one it is easiest
     * to do without and be wrong. Since SERVER_AUTHORITY Phase 3 a
     * foreign island arrives with an empty building list and a zeroed
     * stockpile — and those mean "you were not told", not "there is
     * nothing there". A screen that renders the second reading is
     * telling the player something false about a rival, and they will
     * act on it: 0 Planks reads as a market to sell into rather than as
     * an island you know nothing about.
     *
     * It is one flag for the whole island rather than one per field,
     * because redaction is all-or-nothing per island and a per-field
     * version would invite a UI that guesses which zeros are real. */
    uint8_t  detail_known;

    int32_t  stock[RES_COUNT];
    int32_t  escrow[RES_COUNT];
    uint32_t escrow_nonce;       /* island_escrow_nonce() (UI_PLAN M5) */
    int32_t  capacity;           /* per-resource storage cap           */

    int32_t  residents;          /* island total                       */

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
 * BUILDING_NONE answers 1.
 *
 * Two copies of a six-line predicate, deliberately — the sim reads
 * Island and the UI reads UiIsland, and threading one function through
 * both would mean handing UI code a GameState, which is the thing this
 * whole header exists to prevent. tests/test_confirm.c asserts the two
 * agree over a real world, which is the safeguard that matters. */
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

    /* What kind of hull, and how much of it is left (UI_PLAN N6). The
     * guns-versus-hold trade is made at the yard and then invisible
     * for the rest of the ship's life, which is most of the reason
     * nobody ever built anything but a merchantman. `hull_max` rides
     * along so a condition can be shown as a fraction without the UI
     * holding a copy of the class table's numbers. */
    int32_t  klass;              /* ShipClass                          */
    int32_t  guns, hull, hull_max;
    int32_t  hold;               /* per-resource capacity of this hull */
    int32_t  escorting;          /* ship index it guards, or -1        */
    uint8_t  mine;               /* the local player commands it       */
} UiShip;

/* How the simulation itself is doing. Not world state — these are
 * facts about THIS CLIENT's run: whether the last determinism check
 * passed, whether the tick loop is keeping up, how stale the shared
 * feed is. UI_PLAN Phase 4 renders them through the same alert
 * machinery as "this farm has no worker", on the principle that the
 * player is the monitoring system: a stall should be visible seconds
 * after it starts rather than at the next desync. */
/* ---- the maritime world, in UI terms (UI_PLAN N1) ---------
 * Bounds mirror the sim's so a full world always fits; they are named
 * separately so a UI file never has to include the sim's headers to
 * know how big an array is. Static asserts in ui_snapshot.c keep them
 * honest. */
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

/* ---- client-side view state -------------------------------
 * Everything about how the world is being LOOKED at, as opposed to what
 * it is. Never hashed, never saved, never sent: two clients disagreeing
 * about which page of the trade screen is open is not a desync.
 *
 * It is also a pure fold over the input stream, which is what lets a
 * recorded session replay through the real UI in CI (UI_PLAN M1). */
typedef struct {
    int32_t   hud_category;      /* HUD tab (UI_PLAN Phase 3)          */
    int32_t   exchange_page;     /* trade screen page (Phase 1)        */
    int32_t   inventory_page;    /* inventory overlay page (Phase 4)   */

    /* The order book's page, and the draft order being composed on it
     * (UI_PLAN N3). Everything above is a page index; this is the first
     * COMPOSED state the UI has ever held, and it is here rather than
     * on GameState for the same reason the pages are: two clients
     * disagreeing about a half-written order is not a desync, and a
     * draft that never becomes a Command never touches the world.
     *
     * It stays a pure fold over the input stream — book_hit() returns
     * the value each click produces and the caller assigns it — which
     * is what lets a recorded session replay the composer click by
     * click rather than replaying its result. */
    int32_t   book_page;
    int32_t   book_side;         /* OrderSide: 0 buy, 1 sell           */
    int32_t   book_res;          /* ResourceType being composed        */
    int32_t   book_qty;
    int32_t   book_limit;        /* 0 => follow the market's quote     */

    int32_t   chart_page;        /* the passages overlay (UI_PLAN N4)  */
    int32_t   yard_page;         /* the shipyard overlay (UI_PLAN N6)  */
} UiState;

/* Note: which overlay is OPEN is not here. Phase 0 sketched a
 * UiOverlay enum on this struct; Phase 4 gave the question a single
 * real owner instead — game_topmost_overlay() in game.h, reading the
 * flags that already exist on GameState. Two enums naming the same
 * thing is how they drift. */

/* Fill `out` from the live world. The one function in the UI layer that
 * is allowed to see a GameState — everything downstream takes the
 * snapshot. Call once per frame, after the tick loop. */
void ui_snapshot_build(UiSnapshot *out, const struct GameState *gs);

#endif /* UI_SNAPSHOT_H */
