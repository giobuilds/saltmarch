#ifndef FACTION_H
#define FACTION_H

/* faction.h  --  The NPC market as a real counterparty
 * (MMO_PLAN Phase 3) */

#include "resource.h"
#include "sea.h"
#include <stdint.h>

/* island.h includes this header, so the island count cannot come from
 * there without a cycle. Kept in step by a compile-time assert in
 * faction.c. */
#define MAX_ISLANDS_FOR_LANES 8

/* Baseline inventory the faction holds of each tradeable good, and the
 * elasticity scale (kept equal, which makes the quote curve exactly
 * "2x price when empty, base price at baseline, 0 at twice baseline"). */
#define FACTION_BASE_INVENTORY  200

/* Starting (and only) gold reserve. Finite: heavy net-selling can drain
 * it, at which point the faction stops buying until trade flows gold
 * back. It does NOT mean-revert — that keeps player+faction gold exactly
 * conserved across every trade (see the conservation assert in game.c). */
#define FACTION_START_GOLD      20000

/* Inventory nudges one unit toward baseline every this many ticks —
 * "slow mean reversion", the thing that makes prices recover over time. */
#define FACTION_REVERT_INTERVAL_TICKS  4

/* ---- price history (UI_PLAN M3) ---------------------------
 * A short ring of past mid-prices per good, sampled on a fixed tick
 * interval. It exists to answer a question the numbers alone cannot: */
#define FACTION_HIST_LEN            24
#define FACTION_HIST_INTERVAL_TICKS 50   /* one sample per 5 seconds */

/* ---- home ports and market making (MARITIME_PLAN Phase 2) ---- */
#define FACTION_PORT_COUNT 2

/* It cannot quote everything: two sides times every good times every */
#define FACTION_QUOTE_GOODS   6
#define FACTION_QUOTE_LOT    20   /* units per standing order          */
#define FACTION_QUOTE_INTERVAL_TICKS 100  /* re-quote every 10 seconds */

/* ---- charts (MARITIME_PLAN Phase 3b) ---------------------- */
#define FACTION_CHART_ROUTES   4    /* routes quoted at once           */
#define FACTION_CHART_LOT      1    /* charts per standing order       */
#define FACTION_CHART_GOLD_PER_TICK_SAVED 3
#define FACTION_CHART_MIN_PRICE 40

/* Whether island `idx` is one of the faction's home ports. A pure
 * function of the index, so it needs no state and every client agrees
 * without being told. */
int  faction_is_home_port(int idx);

typedef struct {
    int32_t  gold;
    int32_t  inventory[RES_COUNT];   /* GOLD slot unused                 */
    uint32_t revert_timer;           /* ticks toward the next nudge      */

    /* Per-ROUTE insurance premium, in tenths of a percent of declared
     * cargo value. Moves as an EMA on every insured shipment's outcome,
     * so the table is a map of where cargo has been lost. */
    int16_t  route_premium[SEA_MAX_ROUTES];

        /* Where the faction's standing orders are in the book */
    uint32_t quote_order[FACTION_PORT_COUNT][FACTION_QUOTE_GOODS][2];
    uint32_t quote_timer;

    /* The chart offers it currently has standing, one id each, and
     * which route each is for. Same reason as quote_order: a refresh
     * has to withdraw its own stale offers rather than stack new ones
     * beside them. */
    uint32_t chart_order[FACTION_CHART_ROUTES];
    uint32_t chart_cursor;   /* which routes it is offering this cycle */

/* Ring of mid-prices ((bid+ask)/2), oldest-to-newest by index once
     * hist_count reaches FACTION_HIST_LEN. hist_head is where the next
     * sample goes. */
    int16_t  hist[RES_COUNT][FACTION_HIST_LEN];
    uint16_t hist_head;
    uint16_t hist_count;
    uint32_t hist_timer;
} Faction;

/* Baseline: full gold reserve, every tradeable good at baseline stock,
 * every lane at the starting premium. */
void faction_init(Faction *f);

/* The premium this route currently charges, in tenths of a percent.
 * Out-of-range ids get the starting premium rather than nothing, so a
 * caller holding a stale id is overcharged rather than insured free. */
int  faction_route_premium(const Faction *f, int route_id);

/* Fold one insured shipment's outcome into the route's premium:
 * `raided` pushes it up, a safe arrival pulls it down. An EMA rather
 * than a counter, so a passage that was dangerous last month stops
 * being priced as though it still is. */
void faction_route_experience(Faction *f, int route_id, int raided);

/* Set every route's starting premium from what kind of water it is:
 * the patrolled lane at the base rate, a private passage dearer,
 * because it is dearer. Called once when a world is built, after the
 * sea exists — faction_init runs before it and cannot know. */
void faction_init_routes(Faction *f, const Sea *sea);

/* Price the faction will PAY the player for one unit of `r` (bid), and
 * price it CHARGES the player for one unit (ask). ask > bid always (the
 * spread is BUY_PRICE/SELL_PRICE, preserved proportionally). Both are 0
 * for RES_GOLD or an out-of-range resource. */
int  faction_bid(const Faction *f, ResourceType r);
int  faction_ask(const Faction *f, ResourceType r);

/* One tick of slow mean reversion of inventory toward baseline, plus
 * the periodic price-history sample. Called once per sim tick from
 * sim_run_one_tick. Gold is deliberately left alone (see
 * FACTION_START_GOLD). */
void faction_tick(Faction *f);

/* Read the history for `r` oldest-first into `out` (at most
 * FACTION_HIST_LEN entries). Returns how many were written — 0 before
 * the first sample. The ring's internal order is not something callers
 * should have to think about. */
int  faction_history(const Faction *f, ResourceType r, int16_t *out, int max);

#endif /* FACTION_H */
